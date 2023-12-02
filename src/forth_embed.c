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
	TOKENS(tt_index, "i") \
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
};

struct token {
	enum token_type type;
	union {
		int integer;
		char* string;
		char* name;
	} data;
};

static struct token key_word_by_name(const char* word, const char* token_name, enum token_type type) {
	if (strcmp(token_name, word) == 0)
		return (struct token) { .type = type };
	return (struct token) { .type = tt_none };
}

#define key_word_func_by(fname) key_word_##fname
#define make_key_word_func(token_name, token_type) static struct token key_word_##token_type(const char* word){ \
	return key_word_by_name(word, token_name, token_type);\
}\

#define GENERATE_FUNCTIONS(tokenr, token_string) make_key_word_func(token_string, tokenr);
#define GENERATE_TOKEN_PAIRS(tokenr, token_string) { tokenr, key_word_func_by(tokenr) },

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

static struct token key_word_func_by(string) (const char* word) {
	int word_lenght = strlen(word);
	if (word[word_lenght - 1] != '"')
		return (struct token) { .type = tt_none };

	char* word_dup = strdup(word);
	word_dup[word_lenght-1] = '\0'; // set zero char to " position.
	return (struct token) { .type = tt_string, .data.string = word_dup };
}

static struct token key_word_func_by(integer) (const char* word) {
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

struct forth_byte_code {
	struct token* stream;
	int count;
};

typedef void(*token_iterator)(char* word, void* arg);

static void tokens_iterator(const char* stream, token_iterator func, void* arg) {
	char* stream_copy = strdup(stream);
	const char* delimiter = " \n\t";// space, new line, tab
	char* word = strtok(stream_copy, delimiter);
	
	while (word != NULL) {
		func(word, arg);
		word = strtok(NULL, " ");
	}
	free(stream_copy);
}

static void tokens_count(char* word, void* arg) {
	int* token_count = (int*)arg;
	(*token_count)++;
}

static void tokens_to_lexem(char* word, void* arg) {
	struct forth_byte_code* lexem_container = (struct forth_byte_code*)arg;
	for (int key = 0; key < array_size(key_words); key++) {
		const struct token_type_pair key_word = key_words[key];
		struct token new_token = key_word.func_cmp(word);

		if (new_token.type == tt_none)
			continue;

		lexem_container->stream[lexem_container->count] = new_token;
		lexem_container->count++;
		break;
	}
}

static struct forth_byte_code* tokenizer(const char* stream) {
	int token_count = 0;
	tokens_iterator(stream, tokens_count, &token_count);

	struct token* tokens = calloc(token_count, sizeof(struct token));

	struct forth_byte_code* lexem_container = (struct forth_byte_code*)malloc(sizeof(struct forth_byte_code));
	if (lexem_container == NULL or tokens == NULL) {
		return NULL;
	}

	lexem_container->stream = tokens;
	lexem_container->count = 0;
	tokens_iterator(stream, tokens_to_lexem, lexem_container);

	return lexem_container;
}

// ------------------------- VARIABLES OPERATIONS -------------------------

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

struct forth_state {
	// data segment
	int* data_stack;
	int data_stack_top;

	// return segment
	int* return_stack;
	int return_stack_top;

	// dictionary segment
	struct named_any* dictionary;
	int dictionary_count;

	// memory segment
	int* integer_memory;
	int integer_memory_pointer_top;

	// native functions
	forth_native_function* native_functions;
	int native_function_count;
};

struct forth_state* forth_make_state(int data_size, int integer_memory_size, int return_stack_size, int dictionary_size, int native_functions_size) {
	struct forth_state* state = malloc(sizeof(struct forth_state));
	if (state == NULL) {
		return NULL;
	}

	state->data_stack = calloc(data_size, sizeof(int));
	state->data_stack_top = 0;

	state->integer_memory = calloc(integer_memory_size, sizeof(int));
	state->integer_memory_pointer_top = 0;

	state->return_stack = calloc(return_stack_size, sizeof(int));
	state->return_stack_top = 0;
	
	state->dictionary = calloc(dictionary_size, sizeof(struct named_any));
	state->dictionary_count = 0;
	
	state->native_functions = calloc(native_functions_size, sizeof(forth_native_function));
	state->native_function_count = 0;
	return state;
}

struct forth_state* forth_make_default_state() {
	return forth_make_state(50, 1000, 40, 10, 10);
}

void release_state(struct forth_state* fs) {
	free(fs->data_stack);
	free(fs->integer_memory);
	free(fs->return_stack);
	free(fs->dictionary);
}

void release_forth_byte_code(struct forth_byte_code* fbc) {
	for (int index = 0; index < fbc->count; index++) {
		const struct token current_token = fbc->stream[index];
		enum token_type current_token_type = current_token.type;

		if (current_token_type == tt_string or current_token_type == tt_ident) {
			free(current_token.data.name);
		}
	}
	free(fbc->stream);
	free(fbc);
}

static void stack_push(struct forth_state* fs, int value) {
	int index = fs->data_stack_top;
	fs->data_stack[index] = value;
	fs->data_stack_top++;
}

static int stack_pop(struct forth_state* fs) {
	fs->data_stack_top--;
	return fs->data_stack[fs->data_stack_top];
}

static void return_stack_push(struct forth_state* fs, int value) {
	fs->return_stack[fs->return_stack_top] = value;
	fs->return_stack_top++;
}

static int return_stack_pop(struct forth_state* fs) {
	fs->return_stack_top--;
	return fs->return_stack[fs->return_stack_top];
}

// ------------------------- STACK OPERATION -------------------------

#define ftrue -1 // forth true
#define ffalse 0 // forth false

static void dup_op(struct forth_state* fs) {
	int value = stack_pop(fs);
	stack_push(fs, value);
	stack_push(fs, value);
}

static void drop_op(struct forth_state* fs) {
	stack_pop(fs);
}

static void swap_op(struct forth_state* fs) {
	int value1 = stack_pop(fs);
	int value2 = stack_pop(fs);
	stack_push(fs, value1);
	stack_push(fs, value2);
}

static void over_op(struct forth_state* fs) {
	int value1 = stack_pop(fs);
	int value2 = stack_pop(fs);
	stack_push(fs, value2);
	stack_push(fs, value1);
	stack_push(fs, value2);
}

static void rot_op(struct forth_state* fs) {
	int value1 = stack_pop(fs);
	int value2 = stack_pop(fs);
	int value3 = stack_pop(fs);
	stack_push(fs, value2);
	stack_push(fs, value1);
	stack_push(fs, value3);
}
// print value 
static void dot_op(struct forth_state* fs) {
	int value = stack_pop(fs);
	printf("%d ", value); // dot operator make space
}

// print char
static void emit_op(struct forth_state* fs) {
	char value = (char)stack_pop(fs);
	printf("%c", value);
}

// print new line
static void cr_op() {
	printf("\n");
}

// ------------------------- BOOLEAN OPERATION -------------------------
static void equal_op(struct forth_state* fs) {
	int value1 = stack_pop(fs);
	int value2 = stack_pop(fs);

	int result = value2 == value1 ? ftrue : ffalse;
	stack_push(fs, result);
}

static void great_op(struct forth_state* fs) {
	int value1 = stack_pop(fs);
	int value2 = stack_pop(fs);

	int result = value2 > value1 ? ftrue : ffalse;
	stack_push(fs, result);
}

static void less_op(struct forth_state* fs) {
	int value1 = stack_pop(fs);
	int value2 = stack_pop(fs);

	int result = value2 < value1 ? ftrue : ffalse;
	stack_push(fs, result);
}

static void invert_op(struct forth_state* fs) {
	int value = stack_pop(fs);
	stack_push(fs, ~value);
}

static void and_op(struct forth_state* fs) {
	int value1 = stack_pop(fs);
	int value2 = stack_pop(fs);

	if (value1 == ftrue && value2 == ftrue) {
		stack_push(fs, ftrue);
	} else {
		stack_push(fs, ffalse);
	}
}

static void or_op(struct forth_state* fs) {
	int value1 = stack_pop(fs);
	int value2 = stack_pop(fs);

	if (value1 == ftrue || value2 == ftrue) {
		stack_push(fs, ftrue);
	} else {
		stack_push(fs, false);
	}
}

// ------------------------- MATH OPERATION -------------------------

static void plus_op(struct forth_state* fs) {
	int value1 = stack_pop(fs);
	int value2 = stack_pop(fs);
	stack_push(fs, value1 + value2);
}

static void minus_op(struct forth_state* fs) {
	int value1 = stack_pop(fs);
	int value2 = stack_pop(fs);
	stack_push(fs, value2 - value1);
}

static void multiplication_op(struct forth_state* fs) {
	int value1 = stack_pop(fs);
	int value2 = stack_pop(fs);
	stack_push(fs, value2 * value1);
}

static void dividing_op(struct forth_state* fs) {
	int value1 = stack_pop(fs);
	int value2 = stack_pop(fs);
	stack_push(fs, value2 / value1);
}

// ------------------------- CONTROLL FLOW OPERATIONS -------------------------

static int find_controll_flow_token(const struct token* stream, int position, enum token_type incriment, enum token_type find) {
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

		if (type == tt_semicolon) {
			return -1;
		}

		temp_position++;
	}
}


static int if_op(struct forth_state* fs, const struct token* stream, int position) {
	int cmp = stack_pop(fs);

	int find_else = find_controll_flow_token(stream, position, tt_if, tt_else);
	int find_then = find_controll_flow_token(stream, position, tt_if, tt_then);

	if (cmp == ftrue) {
		return_stack_push(fs, find_then);
		return position;
	}

	if (find_else > 0) {
		return find_else;
	} else {
		return find_then;
	}
}

static void dictionary_add_from_name(struct forth_state* fs, const char* name, enum named_type type, int data) {
	fs->dictionary[fs->dictionary_count] = (struct named_any){ .name = name, .data = data, .type = type };
	fs->dictionary_count++;
}

static int dictionary_add_from_token(struct forth_state* fs, const struct token* stream, int position, enum named_type type, int data) {
	const struct token name_token = stream[position + 1];
	dictionary_add_from_name(fs, name_token.data.name, type, data);
	if (type == nt_function) {
		return find_controll_flow_token(stream, position, tt_none, tt_semicolon); // skip function body
	} else {
		return position + 1;
	}
}

static bool dictionary_get_push(struct forth_state* fs, const char* name) {
	for (int i = 0; i < fs->dictionary_count; i++) {
		const struct named_any current = fs->dictionary[i];
		if (strcmp(current.name, name) == 0) {
			stack_push(fs, current.data);
			stack_push(fs, current.type);
			return true;
		}
	}
	return false;
}

static void allot_op(struct forth_state* fs) {
	int offset = stack_pop(fs);
	fs->integer_memory_pointer_top += offset;
}

static int do_loop_start(struct forth_state* fs, const struct token* stream, int position) { // TODO rewrite danger with push stack and next loop
	int start_index = stack_pop(fs);
	int end_index = stack_pop(fs);

	if (start_index < end_index) {
		return_stack_push(fs, position);
		return_stack_push(fs, end_index);
		return_stack_push(fs, start_index);
		return position;
	} else {
		return find_controll_flow_token(stream, position, tt_do, tt_loop);
	}
}

static int do_loop_end(struct forth_state* fs, const struct token* stream, int position) {
	int start_index = return_stack_pop(fs);
	int end_index = return_stack_pop(fs);
	int do_position = return_stack_pop(fs);

	start_index++;

	if (start_index < end_index) {
		return_stack_push(fs, do_position);
		return_stack_push(fs, end_index);
		return_stack_push(fs, start_index);
		return do_position;
	} else {
		return position;
	}
}

static void loop_index_push(struct forth_state* fs) {
	int i = return_stack_pop(fs);
	stack_push(fs, i);
	return_stack_push(fs, i);
}

static void set_value(struct forth_state* fs) {
	int pointer = stack_pop(fs);
	fs->integer_memory[pointer] = stack_pop(fs);
}

static void get_value_of_variable(struct forth_state* fs) { // at @
	int pointer = stack_pop(fs);
	stack_push(fs, fs->integer_memory[pointer]);
}

static int until_op(struct forth_state* fs) { // return jump position
	int value = stack_pop(fs);
	if (value == ftrue) {
		return return_stack_pop(fs);
	} else {
		return_stack_pop(fs);
	}
}

static int ident_op(struct forth_state* fs, const char* name, int position) {
	if (not dictionary_get_push(fs, name)) { // push data and type to stack
		printf("Error name constant/variable/function nor found, what is: %s ?", name);
		return;
	}

	// is type function jump to func body
	int ident_type = stack_pop(fs);
	if (ident_type == (int)nt_function) {
		return_stack_push(fs, position);
		return stack_pop(fs);
	}

	if (ident_type == (int)nt_function_native) {
		int native_function_index = stack_pop(fs);
		fs->native_functions[native_function_index](fs);
	}
	return position;
}

static int do_string_op(struct forth_state* fs, const struct token* stream, int position) { // print string
	printf("%s", stream[position + 1].data.string);
	position++; // skip string token
	return position;
}

static void eval(struct forth_state* fs, const struct token* stream, int start_position, int end_poition) {
	for (int current_pos = start_position; current_pos < end_poition; current_pos++) {
		const struct token current_token = stream[current_pos];
		enum token_type current_token_type = current_token.type;

		switch (current_token_type) {
		case tt_dup:
			dup_op(fs);
			break;

		case tt_drop:
			drop_op(fs);
			break;

		case tt_swap:
			swap_op(fs);
			break;

		case tt_over:
			over_op(fs);
			break;

		case tt_rot:
			rot_op(fs);
			break;

		case tt_dot:
			dot_op(fs);
			break;

		case tt_emit:
			break;

		case tt_cr:
			cr_op(fs);
			break;

		case tt_equal:
			equal_op(fs);
			break;

		case tt_great:
			great_op(fs);
			break;

		case tt_less:
			less_op(fs);
			break;

		case tt_invert:
			invert_op(fs);
			break;

		case tt_and:
			and_op(fs);
			break;

		case tt_or:
			or_op(fs);
			break;

		case tt_plus:
			plus_op(fs);
			break;

		case tt_minus:
			minus_op(fs);
			break;

		case tt_multip:
			multiplication_op(fs);
			break;

		case tt_div:
			dividing_op(fs);
			break;

		case tt_at:
			get_value_of_variable(fs);
			break;

		case tt_setvalue:
			set_value(fs);
			break;

		case tt_index:
			loop_index_push(fs);
			break;

		case tt_allot:
			allot_op(fs);
			break;

		case tt_begin:
			return_stack_push(fs, current_pos - 1);
			break;

		case tt_value:
			stack_push(fs, current_token.data.integer);
			break;

		case tt_dotstring:
			current_pos = do_string_op(fs, stream, current_pos);
			break;

		case tt_if:
			current_pos = if_op(fs, stream, current_pos);
			break;

		case tt_do:
			current_pos = do_loop_start(fs, stream, current_pos);
			break;

		case tt_loop:
			current_pos = do_loop_end(fs, stream, current_pos); // jump to do token
			break;

		case tt_until:
			current_pos = until_op(fs);
			break;

		case tt_constant:
			current_pos = dictionary_add_from_token(fs, stream, current_pos, nt_constant, stack_pop(fs));
			break;

		case tt_variable:
			current_pos = dictionary_add_from_token(fs, stream, current_pos, nt_variable, fs->integer_memory_pointer_top);
			fs->integer_memory_pointer_top++;
			break;

		case tt_function:
			current_pos = dictionary_add_from_token(fs, stream, current_pos, nt_function, current_pos + 1);// +1 skip token name
			break;
		case tt_ident:
			current_pos = ident_op(fs, current_token.data.name, current_pos);
			break;

		case tt_else: // jump to then
		case tt_semicolon: // jump to call function position 
			current_pos = return_stack_pop(fs);
			break;

			// not used
		case tt_then:
		case tt_cells:
		case tt_string:
		case tt_none:
			break;

		default:
			printf("Undefine operator from token: %s", current_token.data.name);
			return;
		}
	}
}

// ------------------------- PUBLIC API -------------------------

const struct forth_byte_code* forth_compile(const char* script) {
	return tokenizer(script);
}

bool forth_run_function(struct forth_state* fs, const struct forth_byte_code* script, const char* func_name) {
	if (not dictionary_get_push(fs, func_name)) {
		return false;
	}
	drop_op(fs); // skip any type (type nt_function)
	int func_start_position = stack_pop(fs);
	int func_end_position = find_controll_flow_token(script->stream, func_start_position, tt_none, tt_semicolon); // skip function body
	eval(fs, script->stream, func_start_position, func_end_position);
	return true;
}

void forth_run(struct forth_state* fs, const struct forth_byte_code* script) {
	eval(fs, script->stream, 0, script->count);
}

void forth_data_stack_push(struct forth_state* fs, int value) {
	stack_push(fs, value);
}

int forth_data_stack_pop(struct forth_state* fs) {
	return stack_pop(fs);
}

void forth_set_constant(struct forth_state* fs, const char* name, int value) {
	dictionary_add_from_name(fs, name, nt_constant, value);
}

void forth_set_function(struct forth_state* fs, const char* name, forth_native_function func) {
	dictionary_add_from_name(fs, name, nt_function_native, fs->native_function_count);
	fs->native_functions[fs->native_function_count] = func;
	fs->native_function_count += 1;
}