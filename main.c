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
	char   *source;
	size_t size;
	size_t offset;
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
} TokenType;

typedef struct {
	TokenType type;
	char      *value;
} Token;

void token_repr(Token *tok, char *buf){
	int repr_size = 0;
	switch(tok->type){
		case TOK_KEYWORD:  { repr_size = sprintf(buf, "TOK_KEYWORD [%s]", tok->value); break;}
		case TOK_ID:       { repr_size = sprintf(buf, "TOK_ID [%s]", tok->value); break;}
		case TOK_NUMBER:   { repr_size = sprintf(buf, "TOK_NUMBER [%s]", tok->value); break;}
		case TOK_LPAREN:   { repr_size = sprintf(buf, "TOK_LPAREN"); break;}
		case TOK_RPAREN:   { repr_size = sprintf(buf, "TOK_RPAREN"); break;}
		case TOK_LBRACKET: { repr_size = sprintf(buf, "TOK_LBRACKET"); break;}
		case TOK_RBRACKET: { repr_size = sprintf(buf, "TOK_RBRACKET"); break;}
		case TOK_COMMA:    { repr_size = sprintf(buf, "TOK_COMMA"); break;}
	}
	buf[repr_size] = '\0';
}

void tokenize_line(char *line, size_t n){

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

	while(1){

		printf("tiny-ctrl> ");
		nread = getline(&buffer, &size, stdin);
		if(nread == -1){
			printf("\nExiting...\n");
			break;
		}

		// Trimming the endline characters
		last_good = strcspn(buffer, "\r\n");
		buffer[last_good] = '\0';

		printf("Read (%zu): %s", last_good, buffer);
		parse_code = parse_line(buffer, last_good);
		if(parse_code != 0){
			printf("Exiting with code %i...\n", parse_code);
			break;
		}

	}

	free(buffer);
	arena_destroy(&a);
	if(parse_code != 0){
		exit(parse_code);
	}

	return 0;
}
