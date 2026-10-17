#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ALIGNMENT 16 	// Might change that in the future
#define ALIGN_ALLOC(offset, align) ((offset + ((align) - 1)) & (~((align) - 1))) // Alignment trick


//============================== 
//
//    Arena
//
//============================== 

typedef struct {
	char *base;
	size_t offset;
	size_t capacity;
} Arena;

Arena arena_create(size_t capacity){
	Arena a;
	a.base     = malloc(capacity);
	a.offset   = 0;
	a.capacity = capacity;
	return a;
}

void arena_destroy(Arena *a){
	free(a->base);
}

void *arena_alloc(Arena *a, size_t size){
	size_t aligned_offset = ALIGN_ALLOC(a->offset, ALIGNMENT);
	if(aligned_offset + size > a->capacity){
		fprintf(stderr, "Arena capacity exceeded. Currently at %zu, requested %zu", a->offset, size);
		return NULL;
	}
	a->offset = aligned_offset + size;
	return a->base + aligned_offset;
}

//============================== 
//
//    Data structures
//
//============================== 


// Example: 3x2 - 5x + 1:
// coeffs     = [1, -5, 3]
// num_coeffs = 3
// deg        = 2
typedef struct {
	double* coeffs;
	size_t  num_coeffs;
	size_t  deg;
} Polynomial;

Polynomial poly_create(Arena *a, size_t deg, int *err){
	*err = 0;
	size_t num_coeffs = deg + 1;
	double *coeffs = arena_alloc(a, num_coeffs * sizeof(double));
	if(coeffs == NULL){
		*err = 1;
	}
	Polynomial p = {
		.coeffs     = coeffs,
		.deg        = deg,
		.num_coeffs = num_coeffs
	};
	return p;
}

void poly_repr(Arena *a, Polynomial *p, char* buf){
	int printed = 0;
	for(size_t i = 0; i < p->num_coeffs; i++){
		double coef = p->coeffs[i];
		if(coef == 0) continue;
		if(i > 1){
			printed += sprintf(buf + printed, "%.2e x^%zu", coef, i);
		}else if(i == 1){
			printed += sprintf(buf + printed, "%.2e x", coef);
		}else{
			printed += sprintf(buf + printed, "%.2e", coef);
		}
		if(i < p->num_coeffs - 1) printed += sprintf(buf + printed, " + ");
	}
	buf[printed] = '\0';
}

// Example: 3x2 - 5x + 1:
// coeffs     = [1, -5, 3]
// num_coeffs = 3
// deg        = 2
typedef struct {
	Polynomial num;
	Polynomial den;
} TF;

//============================== 
//
//    Environment
//
//============================== 

typedef enum {
	VAR_POLYNOMIAL,
	VAR_TF,
	VAR_NUM,
} VarType;

typedef struct {
	char    *name;
	VarType type;
	union {
		Polynomial poly;
		TF         tf;
	} as;
} Variable;

typedef struct {
	Variable *vars;
	size_t   num_vars;
} Enviromnent;

//============================== 
//
//    Parsing
//
//============================== 

typedef struct {
	const char   *source;
	size_t       size;
	size_t       offset;
} Tokenizer;

typedef enum {
	TOK_KEYWORD,
	TOK_ID,
	TOK_NUMBER,
	TOK_LPAREN,
	TOK_RPAREN,
	TOK_LBRACKET,
	TOK_RBRACKET,
	TOK_COMMA,
	TOK_EQ,
} TokenType;

typedef struct {
	TokenType type;
	char      *value;
} Token;

void token_repr(Token *tok, char *buf){
	int repr_size = 0;
	switch(tok->type){
		case TOK_KEYWORD:  { repr_size = sprintf(buf, "TOK_KEYWORD [%s]", tok->value); break;}
		case TOK_ID:       { repr_size = sprintf(buf, "TOK_ID      [%s]", tok->value); break;}
		case TOK_NUMBER:   { repr_size = sprintf(buf, "TOK_NUMBER  [%s]", tok->value); break;}
		case TOK_LPAREN:   { repr_size = sprintf(buf, "TOK_LPAREN"); break;}
		case TOK_RPAREN:   { repr_size = sprintf(buf, "TOK_RPAREN"); break;}
		case TOK_LBRACKET: { repr_size = sprintf(buf, "TOK_LBRACKET"); break;}
		case TOK_RBRACKET: { repr_size = sprintf(buf, "TOK_RBRACKET"); break;}
		case TOK_COMMA:    { repr_size = sprintf(buf, "TOK_COMMA"); break;}
		case TOK_EQ:       { repr_size = sprintf(buf, "TOK_EQ"); break;}
	}
	buf[repr_size] = '\0';
}

void token_print(Token *tok){
	char buf[256];
	token_repr(tok, buf);
	printf("%s\n", buf);
}

size_t tokenizer_is_id(const char *pos){
	if(!isalpha(pos[0])) return 0;
	size_t i = 1;
	while(isalpha(pos[i])){
		i += 1;
	}
	return i;
}

size_t tokenizer_is_number(const char *pos){
	int scientific = 0;
	if(!isdigit(pos[0])) return 0;
	size_t i = 1;
	while(isdigit(pos[i])){
		i += 1;
	}
	if(pos[i] == '.' && isdigit(pos[i + 1])){
		// scientific notation
		i += 1;
	}
	while(isdigit(pos[i])){
		i += 1;
	}
	return i;
}


int tokenizer_is_symbol(const char *pos, const char *symbol){
	if(strncmp(pos, symbol, 1) == 0) return 1;
	return 0;
}


int tokenize_line(Arena *a, const char *line, size_t n, Token *tokens , size_t *num_tokens){
	Tokenizer tokenizer;
	tokenizer.source = line;
	tokenizer.offset = 0;
	tokenizer.size   = n;

	*num_tokens      = 0;
	printf("Tokenizing: %s\n", line);

	while(tokenizer.offset < tokenizer.size){
		const char *pos = tokenizer.source + tokenizer.offset;

		if(isspace(pos[0])){
			tokenizer.offset++;
			continue;
		}

		// Check for keywords
		if(
				(strncmp(pos, "let", 3) == 0)
				// && !isalpha(*(pos + 3))
			)
		{
			char *value = arena_alloc(a, 3 + 1);
			strncpy(value, pos, 3);
			value[3] = '\0';
			Token tok = {
				tok.type  = TOK_KEYWORD,
				tok.value = value,
			};
			tokens[*num_tokens] = tok;
			(*num_tokens)++;
			tokenizer.offset += 3;
			token_print(&tok);
			continue;

		}

		// Check for identifiers
		int id_size = tokenizer_is_id(pos);
		if(id_size > 0){
			char *value = arena_alloc(a, id_size + 1);
			strncpy(value, pos, id_size);
			value[id_size] = '\0';
			Token tok = {
				tok.type  = TOK_ID,
				tok.value = value,
			};
			tokens[*num_tokens] = tok;
			(*num_tokens)++;
			tokenizer.offset += id_size;
			token_print(&tok);
			continue;
		}
		//
		// Check for identifiers
		int num_size = tokenizer_is_number(pos);
		if(num_size > 0){
			char *value = arena_alloc(a, num_size + 1);
			strncpy(value, pos, num_size);
			value[num_size] = '\0';
			Token tok = {
				tok.type  = TOK_NUMBER,
				tok.value = value,
			};
			tokens[*num_tokens] = tok;
			(*num_tokens)++;
			tokenizer.offset += num_size;
			token_print(&tok);
			continue;
		}

		// Check for special symbols
		if(tokenizer_is_symbol(pos, "=")) {
			Token tok = {
				tok.type  = TOK_EQ,
				tok.value = NULL,
			};
			tokens[*num_tokens] = tok;
			(*num_tokens)++;
			tokenizer.offset += 1;
			token_print(&tok);
			continue;
		};
		if(tokenizer_is_symbol(pos, "(")) {
			Token tok = {
				tok.type  = TOK_LPAREN,
			};
			tokens[*num_tokens] = tok;
			(*num_tokens)++;
			tokenizer.offset += 1;
			token_print(&tok);
			continue;
		};
		if(tokenizer_is_symbol(pos, ")")) {
			Token tok = {
				tok.type  = TOK_RPAREN,
			};
			tokens[*num_tokens] = tok;
			(*num_tokens)++;
			tokenizer.offset += 1;
			token_print(&tok);
			continue;
		};
		if(tokenizer_is_symbol(pos, "[")) {
			Token tok = {
				tok.type  = TOK_LBRACKET,
			};
			tokens[*num_tokens] = tok;
			(*num_tokens)++;
			tokenizer.offset += 1;
			token_print(&tok);
			continue;
		};
		if(tokenizer_is_symbol(pos, "]")) {
			Token tok = {
				tok.type  = TOK_RBRACKET,
			};
			tokens[*num_tokens] = tok;
			(*num_tokens)++;
			tokenizer.offset += 1;
			token_print(&tok);
			continue;
		};
		if(tokenizer_is_symbol(pos, ",")) {
			Token tok = {
				tok.type  = TOK_COMMA,
			};
			tokens[*num_tokens] = tok;
			(*num_tokens)++;
			tokenizer.offset += 1;
			token_print(&tok);
			continue;
		};

		fprintf(stderr,"Unexpected character at offset %zu: %c\n", tokenizer.offset, pos[0]);
		return 1;

	}

	return 0;

}

int parse_line(char *line, size_t n){
	// Includes newlines

  // --------- Basic commands -----------

	if(strcmp(line, "q") == 0){
		printf("\nExiting...\n");
		exit(0);
	}

	return 0;

}

int main(){
	char *buffer      = NULL; // Null-terminated
	size_t size       = 0;
	ssize_t nread     = 0;
	ssize_t last_good = 0;
	int parse_code    = 0;

	Arena a = arena_create(1024 * 1024); // 1 MB for now
	Enviromnent e = {0};

	const char *line = "let p = poly(1.23,2,3)";
	Token tokens[1024];
	size_t num_tokens = 0;
	tokenize_line(&a, line, strlen(line), tokens, &num_tokens);

	// while(1){

	// 	printf("tiny-ctrl> ");
	// 	nread = getline(&buffer, &size, stdin);
	// 	if(nread == -1){
	// 		printf("\nExiting...\n");
	// 		break;
	// 	}

	// 	// Trimming the endline characters
	// 	last_good = strcspn(buffer, "\r\n");
	// 	buffer[last_good] = '\0';

	// 	printf("Read (%zu): %s", last_good, buffer);
	// 	parse_code = parse_line(buffer, last_good);
	// 	if(parse_code != 0){
	// 		printf("Exiting with code %i...\n", parse_code);
	// 		break;
	// 	}

	// }

	free(buffer);
	arena_destroy(&a);
	if(parse_code != 0){
		exit(parse_code);
	}

	return 0;
}
