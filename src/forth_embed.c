#include "forth_embed.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include "iso646.h"

// ------------------------- TOKENIZER FORTH -------------------------

//EBNF grammar :
// <digit> = 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9
// <number> = { <digit> }
// <stack_operators> = <number>| -| +| *| /| dup| drop| swap| over| rot| .| ."| emit| cr| <| >| =| invert| or| and
// <expression> = : { <stack_operators> } ;	

#define array_size(x) sizeof(x)/sizeof(x[0])

#define FOREACH_TOKENS(TOKENS) \
	TOKENS(tt_dup, "dup") \
	TOKENS(tt_drop, "drop") \
	TOKENS(tt_swap, "swap") \
	TOKENS(tt_over, "over") \
	TOKENS(tt_rot, "rot") \
	TOKENS(tt_dot, ".") \
	TOKENS(tt_dotstring, ".\"") \
	TOKENS(tt_emit, "emit") \
	TOKENS(tt_cr, "cr") \
	\
	TOKENS(tt_equal, "=") \
	TOKENS(tt_great, "<") \
	TOKENS(tt_less, ">") \
	TOKENS(tt_invert, "invert") \
	TOKENS(tt_and, "and") \
	TOKENS(tt_or, "or") \
	\
	TOKENS(tt_plus, "+") \
	TOKENS(tt_minus, "-") \
	TOKENS(tt_multip, "*") \
	TOKENS(tt_div, "/") \
	\
	TOKENS(tt_if, "if") \
	TOKENS(tt_else, "else") \
	TOKENS(tt_then, "then") \
	TOKENS(tt_do, "do") \
	TOKENS(tt_loop, "loop") \
	TOKENS(tt_begin, "begin") \
	TOKENS(tt_until, "until") \
	\
	TOKENS(tt_allot, "allot") \
	TOKENS(tt_cells, "cells") \
	TOKENS(tt_constant, "constant") \
	TOKENS(tt_variable, "variable") \
	TOKENS(tt_at, "@") \
	TOKENS(tt_setvalue, "!") \
	TOKENS(tt_function, ":") \
	TOKENS(tt_semicolon, ";") \

#define GENERATE_ENUM(ENUM) ENUM,

enum token_type {
	FOREACH_TOKENS(GENERATE_ENUM)
	tt_ident,
	tt_string,
	tt_value,

	tt_none,
	tt_endof,
};

struct token {
	enum token_type type;
	union {
		int integer;
		char* string;
		char* name;
	} data;
};

struct token key_word_by_name(const char* word, const char* token_name, enum token_type type) {
	if (strcmp(token_name, word) == 0)
		return (struct token) { .type = type };
	return (struct token) { .type = tt_none };
}

#define key_word_func_by(fname) key_word_##fname
#define make_key_word_func(token_name, token_type) struct token key_word_##token_type(const char* word){ \
return key_word_by_name(word, token_name, token_type);\
}\

#define GENERATE_FUNCTIONS(tokenr, token_string) make_key_word_func(token_string, tokenr);
#define GENERATE_TOKEN_PAIRS(tokenr, token_string) {tokenr, key_word_func_by(tokenr)},

FOREACH_TOKENS(GENERATE_FUNCTIONS);

struct token_type_pair {
	enum token_type type;
	struct token(*func_cmp)(const char*);
};

struct token key_word_func_by(identifier) (const char* word) {
	if (strchr(word, '"') or strchr(word, '\\')) {
		return (struct token) { .type = tt_none };
	}
	return (struct token) { .type = tt_ident, .data.name = strdup(word) };
}

struct token key_word_func_by(string) (const char* word) {
	int word_lenght = strlen(word);
	if (word[word_lenght - 1] != '"')
		return (struct token) { .type = tt_none };

	return (struct token) { .type = tt_string, .data.string = strdup(word) };
}

struct token key_word_func_by(integer) (const char* word) {
	char* enp_pos;
	int value = strtol(word, &enp_pos, 10);
	uintptr_t integer_lenght = enp_pos - word;
	if (integer_lenght == strlen(word))
		return (struct token) { .type = tt_value, .data.integer = value };
	return (struct token) { .type = tt_none };
}


struct token_type_pair key_words[] = {
	FOREACH_TOKENS(GENERATE_TOKEN_PAIRS)
	
	/// Must be last
	{tt_value, key_word_func_by(integer)},
	{tt_string, key_word_func_by(string)},
	{tt_ident, key_word_func_by(identifier)},
};

struct token* tokenizer(const char* stream) {
	char* stream_copy = strdup(stream);

	const char* delimiter = " ,\n";
	char* word = strtok(stream_copy, delimiter);

	struct token* tokens = calloc(100, sizeof(struct token));
	int token_count = 0;

	while (word != NULL) {

		for (int key = 0; key < array_size(key_words); key++) {
			const struct token_type_pair key_word = key_words[key];
			struct token new_token = key_word.func_cmp(word);

			if (new_token.type == tt_none)
				continue;
			
			tokens[token_count] = new_token;
			break;
		}
		token_count++;
		word = strtok(NULL, " ");
	}
	tokens[token_count] = (struct token){ .type = tt_endof };
	return tokens;
}

// ------------------------- VARIABLES OPERATIONS -------------------------

static int stack_data[100];
static int stack_top = 0;


void stack_push(int value) {
	stack_data[stack_top] = value;
	stack_top++;
}

int stack_pop() {
	stack_top--;
	return stack_data[stack_top];
}


static int return_stack[10];
static int return_stack_top = 0;


void return_stack_push(int value) {
	return_stack[return_stack_top] = value;
	return_stack_top++;
}

int return_stack_pop() {
	return_stack_top--;
	return return_stack[return_stack_top];
}

enum named_type {
	nt_constant,
	nt_variable,
	nt_function,
	nt_function_native
};

struct named_any {
	enum named_type type;
	const char* name;
	int data; // pointer from variable, jump position from function, value from constant
};

static struct named_any dictionary[100];
static int dictionary_count = 0;

static int integer_memory[1000];
static int integer_memory_pointer_top = 0;

// ------------------------- STACK OPERATION -------------------------

#define ftrue -1 // forth true
#define ffalse 0 // forth false

void dup_op() {
	int value = stack_pop();
	stack_push(value);
	stack_push(value);
}

void drop_op() {
	stack_pop();
}

void swap_op() {
	int value1 = stack_pop();
	int value2 = stack_pop();
	stack_push(value1);
	stack_push(value2);
}

void over_op() {
	int value1 = stack_pop();
	int value2 = stack_pop();
	stack_push(value2);
	stack_push(value1);
	stack_push(value2);
}

void rot_op() {
	int value1 = stack_pop();
	int value2 = stack_pop();
	int value3 = stack_pop();
	stack_push(value2);
	stack_push(value1);
	stack_push(value3);
}

void dot_op() {
	int value = stack_pop();
	printf("%d", value);
}

void emit_op() {
	char value = (char)stack_pop();
	printf("%c", value);
}

void cr_op() {
	printf("\n");
}

// ------------------------- BOOLEAN OPERATION -------------------------
void equal_op() {
	int value1 = stack_pop();
	int value2 = stack_pop();

	int result = value2 == value1 ? ftrue : ffalse;
	stack_push(result);
}

void great_op() {
	int value1 = stack_pop();
	int value2 = stack_pop();

	int result = value2 > value1 ? ftrue : ffalse;
	stack_push(result);
}

void less_op() {
	int value1 = stack_pop();
	int value2 = stack_pop();

	int result = value2 < value1 ? ftrue : ffalse;
	stack_push(result);
}

void invert_op() {
	int value = stack_pop();
	stack_push(~value);
}

void and_op() {
	int value1 = stack_pop();
	int value2 = stack_pop();

	if (value1 == ftrue && value2 == ftrue) {
		stack_push(ftrue);
	} else {
		stack_push(ffalse);
	}
}

void or_op() {
	int value1 = stack_pop();
	int value2 = stack_pop();

	if (value1 == ftrue || value2 == ftrue) {
		stack_push(ftrue);
	} else {
		stack_push(false);
	}
}

// ------------------------- MATH OPERATION -------------------------

void plus_op () {
	int value1 = stack_pop();
	int value2 = stack_pop();
	stack_push(value1 + value2);
}

void minus_op() {
	int value1 = stack_pop();
	int value2 = stack_pop();
	stack_push(value2 - value1);
}

void multiplication_op() {
	int value1 = stack_pop();
	int value2 = stack_pop();
	stack_push(value2 * value1);
}

void dividing_op() {
	int value1 = stack_pop();
	int value2 = stack_pop();
	stack_push(value2 / value1);
}


// ------------------------- CONTROLL FLOW OPERATIONS -------------------------

int find_controll_flow_token(const struct token* stream, int position, const enum token_type incriment, const enum token_type find) {
	int temp_position = position + 1;
	int if_stack = 0;
	while (true) {
		enum token_type type = stream[temp_position].type;
		if (type == incriment) {
			if_stack++;
		}

		if (type == find) {
			if (if_stack == 0) {
				return temp_position;
			} else {
				if_stack--;
			}
		}

		if (type == tt_endof or type == tt_semicolon) {
			return -1;
		}

		temp_position++;
	}
}


int if_op(const struct token* stream, int position) {
	int cmp = stack_pop();
	
	int find_else = find_controll_flow_token(stream, position, tt_if, tt_else);
	int find_then = find_controll_flow_token(stream, position, tt_if, tt_then);

	if (cmp == ftrue) {
		return_stack_push(find_then);
		return position;
	}

	if (find_else > 0) {
		return find_else;
	} else {
		return find_then;
	}
}

int dictionary_add(const struct token* stream, int position, enum named_type type, int data) {
	const struct token name_token = stream[position + 1];
	dictionary[dictionary_count] = (struct named_any){ .name = name_token.data.name, .data = data, .type = type };
	dictionary_count++;
	if (type == nt_function) {
		return find_controll_flow_token(stream, position, tt_none, tt_semicolon); // skip function body
	} else {
		return position + 1;
	}
}

bool dictionary_get_push(const char* name) {
	for (int i = 0; i < dictionary_count; i++) {
		const struct named_any current = dictionary[i];
		if (strcmp(current.name, name) == 0) {
			stack_push(current.data); 
			stack_push(current.type);
			return true;
		}
	}
	return false;
}


void allot_op() {
	int offset = stack_pop();
	integer_memory_pointer_top += offset;
}

void eval(const struct token* stream, int current_pos);

int do_loop(const struct token* stream, int position) { // TODO rewrite danger with push stack and next loop
	int start_index = stack_pop();
	int end_index = stack_pop();

	if (start_index < end_index) {
		return_stack_push(position -1);
		stack_push(end_index);
		stack_push(start_index + 1);
		return position;
	} else {
		return find_controll_flow_token(stream, position, tt_do, tt_loop);
	}
}

void eval(const struct token* stream, int current_pos) {
	for (; ; current_pos++) {
		const struct token current_token = stream[current_pos];
		enum token_type current_token_type = current_token.type;

		if (current_token_type == tt_value) {
			stack_push(current_token.data.integer);
		}

		if (current_token_type == tt_equal) {
			equal_op();
		}

		if (current_token_type == tt_less) {
			less_op();
		}

		if (current_token_type == tt_great) {
			great_op();
		}

		if (current_token_type == tt_and) {
			and_op();
		}

		if (current_token_type == tt_or) {
			or_op();
		}

		if (current_token_type == tt_invert) {
			invert_op();
		}

		if (current_token_type == tt_dup) {
			dup_op();
		}

		if (current_token_type == tt_plus) {
			plus_op();
		}

		if (current_token_type == tt_minus) {
			minus_op();
		}

		if (current_token_type == tt_div) {
			dividing_op();
		}

		if (current_token_type == tt_multip) {
			multiplication_op();
		}

		if (current_token_type == tt_dot) {
			dot_op();
		}

		if (current_token_type == tt_cr) {
			cr_op();
		}

		if (current_token_type == tt_ident) {
			if (not dictionary_get_push(current_token.data.name)) { // push data and type to stack
				printf("Error name constant/variable/function nor found, what is: %s ?", current_token.data.name);
				return;
			}

			// is type function jump to func body
			int ident_type = stack_pop();
			if (ident_type == (int)nt_function) {
				return_stack_push(current_pos);
				current_pos = stack_pop();
			}
		}

		if (current_token_type == tt_if) {
			current_pos = if_op(stream, current_pos);
		}

		if (current_token_type == tt_variable) {
			current_pos = dictionary_add(stream, current_pos, nt_variable, integer_memory_pointer_top);
		}

		if (current_token_type == tt_constant) {
			current_pos = dictionary_add(stream, current_pos, nt_constant, stack_pop());
		}

		if (current_token_type == tt_function) {
			current_pos = dictionary_add(stream, current_pos, nt_function, current_pos + 1);// +1 skip token name
		}

		if (current_token_type == tt_dotstring) {
			printf("%s", stream[current_pos+1].data.string);
			current_pos++;
		}

		if (current_token_type == tt_do) {
			current_pos = do_loop(stream, current_pos);
		}

		if (current_token_type == tt_begin) {
			return_stack_push(current_pos -1);
		}

		if (current_token_type == tt_until) {
			int value = stack_pop();
			if (value == -1) {
				current_pos = return_stack_pop();
			} else {
				return_stack_pop();
			}
		}

		if (current_token_type == tt_else) {
			current_pos = return_stack_pop(); // jump to then token
		}

		if (current_token_type == tt_loop) {
			current_pos = return_stack_pop(); // jump to do token
		}

		if (current_token_type == tt_semicolon) { // jump to call function
			current_pos = return_stack_pop();
		}

		if (current_token_type == tt_setvalue) {
			int pointer = stack_pop();
			integer_memory[pointer] = stack_pop();
		}

		if (current_token_type == tt_at) {
			int pointer = stack_pop();
			stack_push(integer_memory[pointer]);
		}

		if (current_token_type == tt_allot) {
			allot_op();
		}

		if (current_token_type == tt_endof) {
			break;
		}
	}
}

struct forth_byte_code {
	struct token* stream;
};

const struct forth_byte_code* forth_compile(const char* script) {
	struct forth_byte_code* byte_code = (struct forth_byte_code*)malloc(sizeof(struct forth_byte_code));
	byte_code->stream = tokenizer(script);
	return byte_code;
}


void forth_run(const struct forth_byte_code* script) {
	eval(script->stream, 0);
}