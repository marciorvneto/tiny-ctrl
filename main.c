#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define ALIGNMENT 16 	// Might change that in the future
#define ALIGN_ALLOC(offset, align) ((offset + ((align) - 1)) & (~((align) - 1))) // Alignment trick

#define UNREACHABLE(x) do { fprintf(stderr, "Unreachable: " x); exit(1); } while(0);
#define MAX(x,y) ((x) > (y) ? (x) : (y))

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

size_t arena_save(Arena *a){
	return a->offset;
}

void arena_restore(Arena *a, size_t offset){
	a->offset = offset;
}

#define CONCAT_INTERNAL(x,y) x##y
#define CONCAT(x, y) CONCAT_INTERNAL(x, y)
#define SCRATCH_SCOPE(arena) for(size_t CONCAT(scratch_, __LINE__) = arena_save(arena), _once = 1; _once == 1; arena_restore(arena, CONCAT(scratch_, __LINE__)), _once = 0)

//============================== 
//
//    Data structures
//
//============================== 

typedef struct {
	double* nums;
	size_t  size;
} Array;

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
//    Tokenization
//
//============================== 

// X-Macros for defining token types.
// I wanted more granular control over how those
// tokens were printed, so instead of defining
// a single X macro, I added a V macro for tokens
// that carry value information

#define TOK_TYPE_LIST \
	V(TOK_KEYWORD) \
	V(TOK_ID) \
	V(TOK_NUMBER) \
	X(TOK_LPAREN) \
	X(TOK_RPAREN) \
	X(TOK_LBRACKET) \
	X(TOK_RBRACKET) \
	X(TOK_COMMA) \
	X(TOK_EQ) \
	V(TOK_PLUS) \
	V(TOK_MINUS) \
	V(TOK_MUL) \
	V(TOK_DIV) \
	V(TOK_POW) \
	V(TOK_AT) \
	X(TOK_TYPE_COUNT)

typedef enum {
#define X(name) name,
#define V(name) name,
	TOK_TYPE_LIST
#undef X
#undef V
} TokenType;

typedef struct {
	TokenType type;
	char      *value;
} Token;

void token_type_repr(TokenType type, char *buf){
	int repr_size = 0;
	switch(type){
#define X(name) case name: { repr_size = sprintf(buf, #name); break;}
#define V(name) case name: { repr_size = sprintf(buf, #name); break;}
		TOK_TYPE_LIST
#undef X
#undef V
	}
	buf[repr_size] = '\0';
}

void token_repr(Token *tok, char *buf){
	int repr_size = 0;
	switch(tok->type){
#define X(name) case name: { repr_size = sprintf(buf, #name); break;}
#define V(name) case name: { repr_size = sprintf(buf, "%-15s [%s]", #name, tok->value); break;}
		TOK_TYPE_LIST
#undef X
#undef V
	}
	buf[repr_size] = '\0';
}

void token_print(Token *tok){
	char buf[256];
	token_repr(tok, buf);
	printf("%s\n", buf);
}

typedef struct {
	const char   *source;
	size_t       size;
	size_t       offset;
} Tokenizer;


size_t tokenizer_is_id(const char *pos){
	if(!isalpha(pos[0])) return 0;
	size_t i = 1;
	while(isalnum(pos[i])){
		i += 1;
	}
	return i;
}

size_t tokenizer_is_number(const char *pos){
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

int tokenizer_is_str(const char *pos, const char *str, size_t n){
	if(strncmp(pos, str, n) == 0) return 1;
	return 0;
}

Token tokenizer_make_symbol(Arena *a, TokenType type, Tokenizer *tokenizer){
	Token tok = {
		tok.type  = type,
		tok.value = NULL,
	};
	size_t num_chars = 1;
	tokenizer->offset += num_chars;
	return tok;
}


int tokenize_line(Arena *a, const char *line, size_t n, Token *tokens , size_t *num_tokens){
	Tokenizer tokenizer;
	tokenizer.source = line;
	tokenizer.offset = 0;
	tokenizer.size   = n;

	*num_tokens      = 0;
	// printf("Tokenizing: %s\n", line);

	while(tokenizer.offset < tokenizer.size){
		const char *pos = tokenizer.source + tokenizer.offset;

		if(isspace(pos[0])){
			tokenizer.offset++;
			continue;
		}

		// Check for keywords
		if(
				tokenizer_is_str(pos, "let", 3)
				&& !isalpha(*(pos + 3))
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
			// token_print(&tok);
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
			// token_print(&tok);
			continue;
		}

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
			// token_print(&tok);
			continue;
		}
		// Check for binary operators
			if(tokenizer_is_symbol(pos, "+")) {
				Token tok = tokenizer_make_symbol(a, TOK_PLUS, &tokenizer);
				tokens[(*num_tokens)++] = tok;
				continue;
			};
			if(tokenizer_is_symbol(pos, "-")) {
				Token tok = tokenizer_make_symbol(a, TOK_MINUS, &tokenizer);
				tokens[(*num_tokens)++] = tok;
				continue;
			};
			if(tokenizer_is_symbol(pos, "*")) {
				Token tok = tokenizer_make_symbol(a, TOK_MUL, &tokenizer);
				tokens[(*num_tokens)++] = tok;
				continue;
			};
			if(tokenizer_is_symbol(pos, "/")) {
				Token tok = tokenizer_make_symbol(a, TOK_DIV, &tokenizer);
				tokens[(*num_tokens)++] = tok;
				continue;
			};
			if(tokenizer_is_symbol(pos, "@")) {
				Token tok = tokenizer_make_symbol(a, TOK_AT, &tokenizer);
				tokens[(*num_tokens)++] = tok;
				continue;
			};

		// Check for special symbols
		if(tokenizer_is_symbol(pos, "=")) {
			Token tok = tokenizer_make_symbol(a, TOK_EQ, &tokenizer);
			tokens[(*num_tokens)++] = tok;
			continue;
		};
		if(tokenizer_is_symbol(pos, "(")) {
			Token tok = tokenizer_make_symbol(a, TOK_LPAREN, &tokenizer);
			tokens[(*num_tokens)++] = tok;
			continue;
		};
		if(tokenizer_is_symbol(pos, ")")) {
			Token tok = tokenizer_make_symbol(a, TOK_RPAREN, &tokenizer);
			tokens[(*num_tokens)++] = tok;
			continue;
		};
		if(tokenizer_is_symbol(pos, "[")) {
			Token tok = tokenizer_make_symbol(a, TOK_LBRACKET, &tokenizer);
			tokens[(*num_tokens)++] = tok;
			continue;
		};
		if(tokenizer_is_symbol(pos, "]")) {
			Token tok = tokenizer_make_symbol(a, TOK_RBRACKET, &tokenizer);
			tokens[(*num_tokens)++] = tok;
			continue;
		};
		if(tokenizer_is_symbol(pos, ",")) {
			Token tok = tokenizer_make_symbol(a, TOK_COMMA, &tokenizer);
			tokens[(*num_tokens)++] = tok;
			continue;
		};

		fprintf(stderr,"Unexpected character at offset %zu: %c\n", tokenizer.offset, pos[0]);
		return 1;

	}

	return 0;

}

//============================== 
//
//    Parsing
//
//============================== 

// X-Macros are convenient here :)

#define AST_NODES_LIST           \
	      X(AST_PROGRAM)           \
	      X(AST_STATEMENT_LIST)    \
	      X(AST_STATEMENT)         \
	      X(AST_LITERAL)        \
	      X(AST_ARRAY)        \
	      X(AST_BINARY_OP)        \
	      X(AST_FN_CALL)        \
	      X(AST_ARGS_LIST)         \
	      X(AST_NODE_TYPE_COUNT)
				
typedef enum {
#define X(name) name,
	AST_NODES_LIST
#undef X
} ASTNodeType;


#define OP_TYPE_LIST           \
	      X(OP_PLUS)           \
	      X(OP_MINUS)           \
	      X(OP_MUL)           \
	      X(OP_DIV)           \
	      X(OP_POW)           \
	      X(OP_MMUL)           \
	      X(OP_TYPE_LIST_COUNT)

typedef enum {
#define X(name) name,
	OP_TYPE_LIST
#undef X
} OpType;

void op_type_repr(OpType type, char *buf){
	int repr_size = 0;
	switch(type){
#define X(name) case name: { repr_size = sprintf(buf, #name); break;}
#define V(name) case name: { repr_size = sprintf(buf, #name); break;}
		OP_TYPE_LIST
#undef X
#undef V
	}
	buf[repr_size] = '\0';
}

OpType op_from_tok(TokenType type){
	switch (type) {
		case TOK_PLUS:  return OP_PLUS;
	  case TOK_MINUS: return OP_MINUS;
	  case TOK_MUL:   return OP_MUL;
	  case TOK_DIV:   return OP_DIV;
	  case TOK_POW:   return OP_POW;
	  case TOK_AT:    return OP_MMUL;
		default:        UNREACHABLE("op_from_tok");
	}
}

// Expression value types

typedef enum {
	VALUE_ID,
	VALUE_NUMBER,
	VALUE_STRING,
	VALUE_POLYNOMIAL,
	VALUE_TF,
	VALUE_NAN,

	// Things to think about
	VALUE_ARRAY,
	VALUE_MATRIX,
	VALUE_TYPE_COUNT
} ValueType;

typedef enum {
	STATEMENT_DECL_ASSIGN,
	STATEMENT_ASSIGN,
	STATEMENT_EXPR,
} StatementType;

// Forward declarations
typedef struct AST AST;

typedef struct {
	ValueType valueType;
	union {
		Polynomial poly;
		TF         tf;
		double     number;
		char       *str;
		Array      array;
	} value;
} Value;


struct AST {
	ASTNodeType type;
	AST*        parent;
	AST*        next; // Optional, useful for linked lists

	union {
		struct {
			size_t num_statements;
			AST    *statements_list;
		} program;

		struct {
			StatementType type;
			char          *dst;
			AST           *expr;
		} statement;

		Value literal;

		struct   {
			size_t size;
			AST    *first;
		} array;


		struct   {
			char   *fn_name;
			size_t num_arguments;
			AST    *first_arg;
		} fn_call;

		struct {
			OpType op;
			AST    *left;
			AST    *right;
		} binop;
	} as;
};

void ast_node_repr(AST *ast, char *buf){
	int repr_size = 0;
	switch(ast->type){
#define X(name) case name:   { repr_size = sprintf(buf, #name); break;}
		AST_NODES_LIST
#undef X
	}
	buf[repr_size] = '\0';
}

void ast_node_print(AST *ast){
	char buf[256];
	ast_node_repr(ast, buf);
	printf("%s\n", buf);
}

typedef struct {
	Token  *tokens;
	size_t num_tokens;
	size_t offset;
} Parser;

int parser_done(Parser *parser){
	return !(parser->offset < parser->num_tokens);
}

Token *parser_peek(Parser *parser){
	if(parser->offset < parser->num_tokens) return &parser->tokens[parser->offset];
	return NULL;
}

AST *ast_alloc(Arena *a, ASTNodeType type){
	AST *node = arena_alloc(a, sizeof(AST));
	memset(node, 0, sizeof(AST));
	node->type = type;
	return node;
}

Token *parser_eat(TokenType type, Parser *parser){
	Token *next = parser_peek(parser);
	if(next->type == type){
		parser->offset++;
		return next;
	}
	char buf[256];
	token_type_repr(type, buf);
	fprintf(stderr, "Expected a token of type %s at position %zu\n", buf, parser->offset);
	return NULL;
}

Token *parser_lookahed(Parser *parser){
	if(parser->offset + 1 >= parser->num_tokens) return NULL;
	Token *la = &parser->tokens[parser->offset + 1];
	return la;
}


AST *ast_make_number(Arena *a, double number){
	AST *expr = ast_alloc(a, AST_LITERAL);
	expr->as.literal.value.number = number;
	expr->as.literal.valueType = VALUE_NUMBER;
	return expr;
}

AST *ast_make_id(Arena *a, char *id){
	AST *expr = ast_alloc(a, AST_LITERAL);
	expr->as.literal.value.str = id;
	expr->as.literal.valueType = VALUE_STRING;
	return expr;
}

AST *ast_make_array(Arena *a){
	AST *array = ast_alloc(a, AST_ARRAY);
	array->as.array.first = NULL;
	array->as.array.size  = 0;
	return array;
}

AST *ast_make_fn_call(Arena *a, const char *fn_name){
	AST *fn_call = ast_alloc(a, AST_FN_CALL);
	fn_call->as.fn_call.num_arguments = 0;
	fn_call->as.fn_call.fn_name       = arena_alloc(a, strlen(fn_name) + 1);
	fn_call->as.fn_call.first_arg     = NULL;
	strcpy(fn_call->as.fn_call.fn_name, fn_name);
	return fn_call;
}

AST *ast_make_binop(Arena *a, OpType op){
	AST *binop = ast_alloc(a, AST_BINARY_OP); 
	binop->as.binop.left = NULL;
	binop->as.binop.right = NULL;
	binop->as.binop.op = op;
	return binop;
}

AST *ast_make_statement(Arena *a, StatementType type){
	AST *stmt               = ast_alloc(a, AST_STATEMENT);
	stmt->as.statement.type = type;
	return stmt;
}


AST *parse_expression(Arena *a, AST *parent, Parser *parser); // Forward declaration
AST *parse_term(Arena *a, AST *parent, Parser *parser);
AST *parse_expo(Arena *a, AST *parent, Parser *parser);
AST *parse_primary(Arena *a, AST *parent, Parser *parser);
AST *parse_array(Arena *a, AST *parent, Parser *parser);


AST *parse_array(Arena *a, AST *parent, Parser *parser){
		if(parser_eat(TOK_LBRACKET, parser) == NULL) return NULL;
		AST *array = ast_make_array(a);
		if(!parser_done(parser) && parser_peek(parser)->type == TOK_RBRACKET){
			if(parser_eat(TOK_RBRACKET, parser) == NULL) return NULL;
			return array;
		}

		AST *head = parse_expression(a, parent, parser);
		if(parser_peek(parser)->type == TOK_COMMA) parser_eat(TOK_COMMA, parser);

		AST *tail = NULL;
		if(head == NULL){
			if(parser_eat(TOK_RBRACKET, parser) == NULL) return NULL;
			return array;
		}
		array->as.array.first = head;
		tail = head;

		size_t size = 1;

		while(!parser_done(parser) && parser_peek(parser)->type != TOK_RBRACKET){
			AST *expr = parse_expression(a, parent, parser);
			if(parser_peek(parser)->type == TOK_COMMA) parser_eat(TOK_COMMA, parser);
			tail->next = expr;
			tail       = expr;
			size       += 1;
		}

		if(parser_eat(TOK_RBRACKET, parser) == NULL) return NULL;
		array->as.array.size = size;
		return array;
}

AST *parse_fn_call(Arena *a, AST *parent, Parser *parser, char* fn_name){
		if(parser_eat(TOK_LPAREN, parser) == NULL) return NULL;
		AST *fn_call = ast_make_fn_call(a, fn_name);
		if(!parser_done(parser) && parser_peek(parser)->type == TOK_RPAREN){
			if(parser_eat(TOK_RPAREN, parser) == NULL) return NULL;
			return fn_call;
		}

		AST *head = parse_expression(a, parent, parser);
		if(parser_peek(parser)->type == TOK_COMMA) parser_eat(TOK_COMMA, parser);
		AST *tail = NULL;
		if(head == NULL){
			if(parser_eat(TOK_RPAREN, parser) == NULL) return NULL;
			return fn_call;
		}
		fn_call->as.fn_call.first_arg = head;
		tail = head;

		while(!parser_done(parser) && parser_peek(parser)->type != TOK_RPAREN){
			AST *expr = parse_expression(a, parent, parser);
			if(parser_peek(parser)->type == TOK_COMMA) parser_eat(TOK_COMMA, parser);
			tail->next = expr;
			tail       = expr;
		}

		if(parser_eat(TOK_RPAREN, parser) == NULL) return NULL;
		return fn_call;
}

AST *parse_primary(Arena *a, AST *parent, Parser *parser) {
	Token *current = parser_peek(parser);

	if(current->type == TOK_NUMBER)
	{
		if(parser_eat(TOK_NUMBER, parser) == NULL) return NULL;
		AST *expr = ast_make_number(a, atof(current->value));
		expr->parent = parent;
		return expr;

	} else if(current->type == TOK_LBRACKET)
	{
			AST *array = parse_array(a, parent, parser);
			return array;
	} else if(current->type == TOK_ID)
	{
		Token *id_tok = parser_eat(TOK_ID, parser);

		if(!parser_done(parser) && parser_peek(parser)->type == TOK_LPAREN){
			// Calling a function
			AST *fn_call = parse_fn_call(a, parent, parser, id_tok->value);
			return fn_call;
		}

		AST *src = ast_make_id(a, current->value);
		src->parent = parent;
		return src;
	} else
	{
		if(parser_eat(TOK_LPAREN, parser) == NULL) return NULL;
		AST *expr = parse_expression(a, parent, parser);
		if(parser_eat(TOK_RPAREN, parser) == NULL) return NULL;
		return expr;
	}
	return NULL;
}

AST *parse_term(Arena *a, AST *parent, Parser *parser){
	AST *left = parse_expo(a, parent, parser);
	while(!parser_done(parser) &&
			(parser_peek(parser)->type == TOK_MUL ||
			 parser_peek(parser)->type == TOK_AT  ||
			 parser_peek(parser)->type == TOK_DIV 
			 )
		)
	{
		Token *op_tok         = parser_eat(parser_peek(parser)->type, parser);
		AST   *binop          = ast_make_binop(a, op_from_tok(op_tok->type));
		AST   *right          = parse_expo(a, parent, parser);
		binop->as.binop.left  = left;
		binop->as.binop.right = right;
		left = binop;
	}
	return left;
}

AST *parse_expo(Arena *a, AST *parent, Parser *parser){
	AST *primary = parse_primary(a, parent, parser);
	if(!parser_done(parser) && parser_peek(parser)->type == TOK_POW){
		Token *op_tok         = parser_eat(parser_peek(parser)->type, parser);
		AST   *binop          = ast_make_binop(a, op_from_tok(op_tok->type));
		AST   *right          = parse_expo(a, parent, parser);
		binop->as.binop.left  = primary;
		binop->as.binop.right = right;
		return binop;
	}
	return primary;
}

AST *parse_expression(Arena *a, AST *parent, Parser *parser){
	AST *left = parse_term(a, parent, parser);
	while(!parser_done(parser) && (
			parser_peek(parser)->type == TOK_PLUS ||
			parser_peek(parser)->type == TOK_MINUS)
		)
	{
		Token *op_tok         = parser_eat(parser_peek(parser)->type, parser);
		AST   *binop          = ast_make_binop(a, op_from_tok(op_tok->type));
		AST   *right          = parse_term(a, parent, parser);
		binop->as.binop.left  = left;
		binop->as.binop.right = right;
		left = binop;
	}
	return left;
}



AST *parse_statement(Arena *a, AST *parent, Parser *parser){
	Token *current = parser_peek(parser);

	if(current->type == TOK_KEYWORD && (strcmp(current->value, "let") == 0)){
		AST *stmt     = ast_make_statement(a, STATEMENT_DECL_ASSIGN);
		stmt->parent  = parent;

		if(parser_eat(TOK_KEYWORD, parser) == NULL) return NULL;
		Token *id_tok = parser_eat(TOK_ID, parser);

		if(id_tok == NULL){
			return NULL;
		}

		if(parser_eat(TOK_EQ, parser) == NULL) return NULL;

		stmt->as.statement.dst  = arena_alloc(a, strlen(id_tok->value) + 1);
		strcpy(stmt->as.statement.dst, id_tok->value);

		AST *expr               = parse_expression(a, stmt, parser);
		stmt->as.statement.expr = expr;

		return stmt;
	}else if
		(
			current->type == TOK_ID  &&  
			parser_lookahed(parser)  != NULL  &&
			parser_lookahed(parser)->type  == TOK_EQ
		)
	{
		AST *stmt    = ast_make_statement(a, STATEMENT_ASSIGN);
		stmt->parent = parent;

		Token *id_tok = parser_eat(TOK_ID, parser);
		if(id_tok == NULL){
			return NULL;
		}
		if(parser_eat(TOK_EQ, parser) == NULL) return NULL;

		stmt->as.statement.dst  = arena_alloc(a, strlen(id_tok->value) + 1);
		strcpy(stmt->as.statement.dst, id_tok->value);

		AST *expr               = parse_expression(a, stmt, parser);
		stmt->as.statement.expr = expr;

		return stmt;
	}else{
		AST *stmt               = ast_make_statement(a, STATEMENT_EXPR);
		AST *expr               = parse_expression(a, stmt, parser);
		stmt->as.statement.expr = expr;
		return stmt;
	}
}

AST *parse_statements_list(Arena *a, AST *program_node, Parser *parser){
	if(parser_done(parser)) return NULL;

	size_t num_statements = 0;

	AST *head = NULL;
	AST *tail = NULL;
	head = parse_statement(a, program_node, parser);
	head->parent = program_node;
	if(head == NULL){
		fprintf(stderr, "Parsing error at position %zu\n", parser->offset);
		return NULL;
	}

	num_statements++;
	tail = head;

	while(!parser_done(parser)){
		AST *stmt = parse_statement(a, program_node, parser);
		if(stmt == NULL){
			break;
		}
		stmt->parent = program_node;
		tail->next   = stmt;
		tail         = stmt;
		num_statements++;
	}

	program_node->as.program.num_statements = num_statements;
	return head;
}



int parse_tokens(Arena *a, AST *ast, Token *tokens, size_t num_tokens){
	ast->parent = NULL;
	ast->type = AST_PROGRAM;

	Parser parser;
	parser.num_tokens = num_tokens;
	parser.tokens     = tokens;
	parser.offset     = 0;

	if(num_tokens == 0) return 0;

	AST *list = parse_statements_list(a, ast, &parser);
	ast->as.program.statements_list = list;

	return list == NULL ? 1 : 0;
}

void fill_indent(size_t level, char *buf){
	size_t indent_size = 2;
	size_t total_spaces = indent_size * level;
	for(size_t i = 0; i < total_spaces; i++){
		buf[i] = ' ';
	}
	buf[total_spaces] = '\0';
}

void ast_print(AST *ast, int level){
	char buf[256];
	fill_indent(level, buf);
	switch(ast->type){
		case AST_PROGRAM:
			{
				printf("%sPROGRAM:\n", buf);
				AST *stmt = ast->as.program.statements_list;
				while(stmt != NULL){
					ast_print(stmt, level + 1);
					stmt = stmt->next;
				}
				break;
			}
		case AST_STATEMENT:
			{
				printf("%sSTMT:\n", buf);
				fill_indent(level + 1, buf);
				printf("%sSRC:  %s\n", buf, ast->as.statement.dst);
				printf("%sEXP:\n", buf);
				ast_print(ast->as.statement.expr, level + 2);
				break;
			}
		case AST_LITERAL:
			{
				if(ast->as.literal.valueType == VALUE_STRING){
					printf("%sLIT: %s\n", buf, ast->as.literal.value.str);
				} else if(ast->as.literal.valueType == VALUE_NUMBER){
					printf("%sLIT: %.2f\n", buf, ast->as.literal.value.number);
				}
				break;
			}
		case AST_FN_CALL:
			{
				printf("%sFN[%s]:\n", buf, ast->as.fn_call.fn_name);
				AST *arg = ast->as.fn_call.first_arg;
				while(arg != NULL){
					ast_print(arg, level + 1);
					arg = arg->next;
				}
				break;
			}
		case AST_BINARY_OP:
			{
				char opBuf[128];
				op_type_repr(ast->as.binop.op, opBuf);
				printf("%sBINOP[%s]:\n", buf, opBuf);
				fill_indent(level + 1, buf);
				printf("%sLEFT:\n", buf);
				ast_print(ast->as.binop.left, level + 2);
				fill_indent(level + 1, buf);
				printf("%sRIGHT:\n", buf);
				ast_print(ast->as.binop.right, level + 2);
				break;
			}
	}
}

//============================== 
//
//    Environment
//
//============================== 

typedef struct {
	char  *name;
	Value *value;
} Variable;

typedef struct {
	Variable *vars;
	size_t   num_vars;
} Enviromnent;

Enviromnent env_create(Arena *a){
	Enviromnent e;
	e.vars     = arena_alloc(a, 32 * sizeof(Variable));
	e.num_vars = 0;
	return e;
}

int env_variable_idx(Arena *a, Enviromnent *e, const char* name){
	for(int i = 0; i < e->num_vars; i++){
		if(strcmp(name, e->vars[i].name) == 0) return i;
	}
	return -1;
}

Variable *env_get_variable_from_idx(Arena *a, Enviromnent *e, int idx){
	if(idx < 0) return NULL;
	if(idx >= e->num_vars) return NULL;
	return &e->vars[idx];
}


void env_alloc_variable(Arena *a, Enviromnent *e, const char* name, Value *value){
	Variable *var       = &e->vars[e->num_vars];

	size_t name_len     = strlen(name);
	var->name           = arena_alloc(a, name_len + 1);
	strcpy(var->name, name);

	var->value          = arena_alloc(a, sizeof(Value));
	*var->value         = *value;

	e->num_vars++;

	// TODO: Implement reallocation
}

void env_set_variable(Arena *a, Enviromnent *e, const char* name, Value *value){
	int idx =  env_variable_idx(a, e, name);
	if(idx < 0){
		env_alloc_variable(a, e, name, value);
		return;
	}

	Variable *var       = &e->vars[idx];
	*var->value         = *value;
}

void  env_list_variables(Arena* a, Enviromnent  *e){
	for(size_t i = 0; i < e->num_vars; i++){
		printf("%s\n", e->vars[i].name);
	}
}

//============================== 
//
//    Interpreting
//
//============================== 

typedef Value (*BinaryFn)(Arena *a, Value *left, Value *right);

Value make_num(double num){
	Value v = {
		.valueType    = VALUE_NUMBER,
		.value.number = num
	};
	return v;
}
Value make_array(Array arr){
	Value v = {
		.valueType   = VALUE_ARRAY,
		.value.array = arr
	};
	return v;
}
Value make_poly(Polynomial poly){
	Value v = {
		.valueType  = VALUE_POLYNOMIAL,
		.value.poly = poly
	};
	return v;
}

Value make_nan(){
	Value v = {
		.valueType    = VALUE_NAN,
		.value.number = 0
	};
	return v;
}

Value op_with_nan(Arena *a, Value *left, Value *right){
	return make_nan();
}
Value add_num_num(Arena *a, Value *left, Value *right){
		return make_num(left->value.number + right->value.number);
}
Value sub_num_num(Arena *a, Value *left, Value *right){
		return make_num(left->value.number - right->value.number);
}
Value mul_num_num(Arena *a, Value *left, Value *right){
		return make_num(left->value.number * right->value.number);
}
Value div_num_num(Arena *a, Value *left, Value *right){
		return make_num(left->value.number / right->value.number);
}
Value pow_num_num(Arena *a, Value *left, Value *right){
		return make_num(pow(left->value.number, right->value.number));
}

Value add_num_array(Arena *a, Value *left, Value *right){
		double num = left->valueType == VALUE_NUMBER ? left->value.number : right->value.number;
		Array  arr = left->valueType == VALUE_ARRAY ? left->value.array : right->value.array;
		for(size_t i = 0; i < arr.size; i++){
			arr.nums[i] += num;
		}
		return make_array(arr);
}
Value add_array_array(Arena *a, Value *left, Value *right){
		Array arr1 = left->value.array;
		Array arr2 = right->value.array;
		if(arr1.size != arr2.size){
			printf("Incompatible array sizes: %zu and %zu\n", arr1.size, arr2.size);
			return make_nan();
		}
		for(size_t i = 0; i < arr1.size; i++){
			arr1.nums[i] += arr2.nums[i];
		}
		return make_array(arr1);
}
Value sub_num_array(Arena *a, Value *left, Value *right){
		double num         = left->valueType == VALUE_NUMBER ? left->value.number : right->value.number;
		Array  arr         = left->valueType == VALUE_ARRAY ? left->value.array : right->value.array;
		double left_is_num = left->valueType == VALUE_NUMBER;
		for(size_t i = 0; i < arr.size; i++){
			if(left_is_num){
				arr.nums[i] = num - arr.nums[i];
			}else{
				arr.nums[i] = arr.nums[i] - num;
			}
		}
		return make_array(arr);
}
Value sub_array_array(Arena *a, Value *left, Value *right){
		Array arr1 = left->value.array;
		Array arr2 = right->value.array;
		if(arr1.size != arr2.size){
			printf("Incompatible array sizes: %zu and %zu\n", arr1.size, arr2.size);
			return make_nan();
		}
		for(size_t i = 0; i < arr1.size; i++){
			arr1.nums[i] -= arr2.nums[i];
		}
		return make_array(arr1);
}

// ------------- Polynomials --------------------

Value add_num_poly(Arena *a, Value *left, Value *right){
		double num       = left->valueType == VALUE_NUMBER ? left->value.number : right->value.number;
		Polynomial  poly = left->valueType == VALUE_POLYNOMIAL ? left->value.poly : right->value.poly;
		poly.coeffs[0] += num;
		return make_poly(poly);
}

Value add_poly_poly(Arena *a, Value *left, Value *right){
		Polynomial p1    = left->value.poly;
		Polynomial p2    = right->value.poly;
		size_t max_size  = MAX(p1.num_coeffs, p2.num_coeffs);

		Polynomial pnew;
		pnew.num_coeffs = max_size;
		pnew.deg        = max_size - 1;
		pnew.coeffs     = arena_alloc(a, max_size * sizeof(double));

		for(size_t i = 0; i < max_size; i++){
			double v1 = 0;
			double v2 = 0;
			if(i < p1.num_coeffs) v1 = p1.coeffs[i];
			if(i < p2.num_coeffs) v2 = p2.coeffs[i];
			pnew.coeffs[i] = v1 + v2;
		}

		return make_poly(pnew);
}

Value sub_num_poly(Arena *a, Value *left, Value *right){
		double num         = left->valueType == VALUE_NUMBER ? left->value.number : right->value.number;
		Polynomial  poly   = left->valueType == VALUE_POLYNOMIAL ? left->value.poly : right->value.poly;
		double left_is_num = left->valueType == VALUE_NUMBER;
		if(left_is_num) poly.coeffs[0] = num - poly.coeffs[0];
		else poly.coeffs[0] -= num;
		return make_poly(poly);
}


Value sub_poly_poly(Arena *a, Value *left, Value *right){
		Polynomial p1    = left->value.poly;
		Polynomial p2    = right->value.poly;
		size_t max_size  = MAX(p1.num_coeffs, p2.num_coeffs);

		Polynomial pnew;
		pnew.num_coeffs = max_size;
		pnew.deg        = max_size - 1;
		pnew.coeffs     = arena_alloc(a, max_size * sizeof(double));

		for(size_t i = 0; i < max_size; i++){
			double v1 = 0;
			double v2 = 0;
			if(i < p1.num_coeffs) v1 = p1.coeffs[i];
			if(i < p2.num_coeffs) v2 = p2.coeffs[i];
			pnew.coeffs[i] = v1 - v2;
		}

		return make_poly(pnew);
}




static const BinaryFn binop_dispatch[OP_TYPE_LIST_COUNT][VALUE_TYPE_COUNT][VALUE_TYPE_COUNT] = {
	[OP_PLUS] = {
		[VALUE_NUMBER] = {
			[VALUE_NUMBER]     = add_num_num,
			[VALUE_ARRAY]      = add_num_array,
			[VALUE_POLYNOMIAL] = add_num_poly,
			[VALUE_NAN]        = op_with_nan,
		},
		[VALUE_ARRAY] = {
			[VALUE_NUMBER]     = add_num_array,
			[VALUE_ARRAY]      = add_array_array,
			[VALUE_POLYNOMIAL] = op_with_nan,
			[VALUE_NAN]        = op_with_nan,
		},
		[VALUE_POLYNOMIAL] = {
			[VALUE_NUMBER]     = add_num_poly,
			[VALUE_ARRAY]      = op_with_nan,
			[VALUE_POLYNOMIAL] = add_poly_poly,
			[VALUE_NAN]        = op_with_nan,
		},
		[VALUE_NAN] = {
			[VALUE_NUMBER]     = op_with_nan,
			[VALUE_ARRAY]      = op_with_nan,
			[VALUE_POLYNOMIAL] = op_with_nan,
			[VALUE_NAN]        = op_with_nan,
		}
	},

	[OP_MINUS] = {
		[VALUE_NUMBER] = {
			[VALUE_NUMBER] = sub_num_num,
			[VALUE_ARRAY]  = sub_num_array,
			[VALUE_NAN]    = op_with_nan,
		},
		[VALUE_ARRAY] = {
			[VALUE_NUMBER] = sub_num_array,
			[VALUE_ARRAY]  = sub_array_array,
			[VALUE_NAN]    = op_with_nan,
		},
		[VALUE_POLYNOMIAL] = {
			[VALUE_NUMBER]     = sub_num_poly,
			[VALUE_ARRAY]      = op_with_nan,
			[VALUE_POLYNOMIAL] = sub_poly_poly,
			[VALUE_NAN]        = op_with_nan,
		},
		[VALUE_NAN] = {
			[VALUE_NUMBER] = op_with_nan,
			[VALUE_ARRAY]  = op_with_nan,
			[VALUE_NAN]    = op_with_nan,
		}
	},

	[OP_MUL] = {
		[VALUE_NUMBER] = {
			[VALUE_NUMBER] = mul_num_num,
			[VALUE_NAN]    = op_with_nan,
		},
		[VALUE_NAN] = {
			[VALUE_NUMBER] = op_with_nan,
			[VALUE_ARRAY]  = op_with_nan,
			[VALUE_NAN]    = op_with_nan,
		}
	},

	[OP_DIV] = {
		[VALUE_NUMBER] = {
			[VALUE_NUMBER] = div_num_num,
			[VALUE_NAN]    = op_with_nan,
		},
		[VALUE_NAN] = {
			[VALUE_NUMBER] = op_with_nan,
			[VALUE_ARRAY]  = op_with_nan,
			[VALUE_NAN]    = op_with_nan,
		} 
	},

	[OP_POW] = {
		[VALUE_NUMBER] = {
			[VALUE_NUMBER] = pow_num_num,
			[VALUE_ARRAY]  = op_with_nan,
			[VALUE_NAN]    = op_with_nan,
		},
		[VALUE_NAN] = {
			[VALUE_NUMBER] = op_with_nan,
			[VALUE_ARRAY]  = op_with_nan,
			[VALUE_NAN]    = op_with_nan,
		} 
	},
};



Value resolve_variable(Arena *a, Enviromnent *e, Value *literal){
		int idx = -1;
		if((idx  = env_variable_idx(a, e, literal->value.str)) < 0){
			// Variable  not  found
			printf("Undefined variable %s\n", literal->value.str);
			return make_nan();
		}
		Variable *var = env_get_variable_from_idx(a, e, idx);
		return *var->value;
}

Value eval_expr(Arena *a, Enviromnent *e, AST *expr);

Value eval_binop(Arena *a, Enviromnent *e, AST *op, Value *raw_left, Value *raw_right){

	Value *left  = raw_left;
	Value *right = raw_right;

	// Resolve variables
	// TODO: Check if it begins  in quotation  marks?  For  now, assume
	//       all strings  are  variable  names
	if(left->valueType == VALUE_STRING){
		*left = resolve_variable(a, e, left);
	}
	if(right->valueType == VALUE_STRING){
		*right = resolve_variable(a, e, right);
	}

	if( op->as.binop.op >= OP_TYPE_LIST_COUNT ||
			left->valueType >= VALUE_TYPE_COUNT  ||
			right->valueType >= VALUE_TYPE_COUNT
			)
	{
		return make_nan();
	}

	BinaryFn fn = binop_dispatch[op->as.binop.op][left->valueType][right->valueType];

	if(!fn){
		printf("Unsupported operation!\n");
		return make_nan();
	}

	return fn(a, left, right);


	fprintf(stderr, "Expected 2 numbers, got something else!\n");
	exit(1);

}

Value eval_func_call(Arena *a, Enviromnent *e, AST *fn){

	if(fn->as.fn_call.num_arguments == 1 || 1){
		// Basic math on numbers
		Value val = eval_expr(a, e, fn->as.fn_call.first_arg);

		if(strcmp(fn->as.fn_call.fn_name, "log") == 0)  { return make_num(logf(val.value.number));}
		if(strcmp(fn->as.fn_call.fn_name, "exp") == 0)  { return make_num(expf(val.value.number));}
		if(strcmp(fn->as.fn_call.fn_name, "sin") == 0)  { return make_num(sinf(val.value.number));}
		if(strcmp(fn->as.fn_call.fn_name, "cos") == 0)  { return make_num(cosf(val.value.number));}
		if(strcmp(fn->as.fn_call.fn_name, "sqr") == 0)  { return make_num(val.value.number * val.value.number);}
		if(strcmp(fn->as.fn_call.fn_name, "sqrt") == 0) { return make_num(sqrtf(val.value.number));}


		if(strcmp(fn->as.fn_call.fn_name, "poly") == 0) {
			if(val.valueType == VALUE_ARRAY){
				return make_poly(val.value.poly);
			}
			printf("Unsupported operation!\n");
			return make_nan();

		}

	}
	return make_nan();
}

void print_value(Arena *a, Enviromnent *e, Value *value){
	switch(value->valueType){
		case VALUE_NUMBER:
			{
				printf("%.4f\n", value->value.number);
				break;
			}
		case VALUE_STRING:
			{
				Value resolved = resolve_variable(a, e, value);
				if(resolved.valueType == VALUE_NAN){
					printf("%s\n", value->value.str);
				}else{
					print_value(a, e, &resolved);
				}
				break;
			}
		case VALUE_ARRAY:
			{
				printf("[");
				for(size_t i = 0; i < value->value.poly.num_coeffs; i++){
					printf("%.2f", value->value.poly.coeffs[i]);
					if(i < value->value.poly.num_coeffs - 1){
						printf(", ");
					}
				}
				printf("]\n");
				break;
			}
		case VALUE_POLYNOMIAL:
			{
				printf("poly([");
				for(size_t i = 0; i < value->value.poly.num_coeffs; i++){
					printf("%.2f", value->value.poly.coeffs[i]);
					if(i < value->value.poly.num_coeffs - 1){
						printf(", ");
					}
				}
				printf("])\n");
				break;
			}
		default:
			{
				printf("<UNK>\n");
				break;
			}
	}
}

Value eval_expr(Arena *a, Enviromnent *e, AST *expr){
	Value value = {0};

	switch(expr->type){
		case AST_LITERAL:
			{
				Value *literal = &expr->as.literal;
				if(literal->valueType == VALUE_STRING){
					*literal = resolve_variable(a, e, literal);
				}
				value = *literal;
				break;
			}
		case AST_ARRAY:
			{
				value.valueType        = VALUE_ARRAY;
				value.value.array.size = expr->as.array.size;
				value.value.array.nums = arena_alloc(a, expr->as.array.size * sizeof(double));
				AST *item              = expr->as.array.first;
				size_t i               = 0;
				while(item != NULL){
					Value item_value = eval_expr(a, e, item);
					// TODO: check if it really is a number
					value.value.array.nums[i++] = item_value.value.number;
					item                        = item->next;
				}
				break;
			}
		case AST_BINARY_OP:
			{
				Value left  = eval_expr(a,  e, expr->as.binop.left);
				Value right = eval_expr(a,  e, expr->as.binop.right);
				value       = eval_binop(a, e, expr, &left, &right);
				break;
			}
		case AST_FN_CALL:
			{
				value       = eval_func_call(a, e, expr);
				break;
			}
		default:
			{
				fprintf(stderr, "Unreachable [eval_expr]\n");
				exit(1);
			}
	}

	return value;
}

int eval_stmt(Arena *a, AST *ast, Enviromnent *e, AST *stmt){

	if(stmt->as.statement.type == STATEMENT_EXPR){
		Value result = eval_expr(a, e, stmt->as.statement.expr);
		print_value(a, e, &result);
		return 0;
	}

	if(stmt->as.statement.type == STATEMENT_DECL_ASSIGN){
		char *dst = stmt->as.statement.dst;
		// Check if it hasn't been defined before
		if(env_variable_idx(a, e, dst)  >= 0){
			printf("Variable %s has already been defined\n", dst);
			return 1;
		}

		//  TODO: Check if it's not null
		Value value = eval_expr(a, e, stmt->as.statement.expr);
		env_set_variable(a, e, dst, &value);
		return 0;
	}

	if(stmt->as.statement.type == STATEMENT_ASSIGN){
		char *dst = stmt->as.statement.dst;
		// Check if it has been defined before
		if(env_variable_idx(a, e, dst)  < 0){
			printf("Undefined variable %s\n", dst);
			return 1;
		}

		//  TODO: Check if it's not null
		Value value = eval_expr(a, e, stmt->as.statement.expr);
		env_set_variable(a, e, dst, &value);
		return 0;
	}

	return 0;
}

int run_ast(Arena *a, AST *ast, Enviromnent *e){

	AST *stmt = ast->as.program.statements_list;

	while(stmt != NULL){
		int status = eval_stmt(a, ast, e, stmt);
		if(status != 0) return status;
		stmt = stmt->next;
	}

	return 0;
}


int parse_line(Arena *a, Enviromnent *e,  const char *line, AST *ast, int *done){
	// Null-terminated

  // --------- Basic commands -----------

	if(strcmp(line, "") == 0){
		// Just a blank line
		*done = 1;
		return 0;
	}

	if(strcmp(line, "q") == 0){
		printf("\nExiting...\n");
		*done = 1;
		exit(0);
	}

	if(strcmp(line, "ls") == 0){
		env_list_variables(a, e);
		*done = 1;
		return 0;
	}

  // --------- Commands that require parsing -----------

	Token  tokens[1024];
	size_t num_tokens = 0;
	int    err        = 0;
	err = tokenize_line(a, line, strlen(line), tokens, &num_tokens);
	if(err != 0){
		fprintf(stderr, "Tokenization error\n");
		return err;
	}

	err = parse_tokens(a, ast, tokens, num_tokens);
	if(err != 0){
		return err;
	}

	// if(err == 0) ast_print(ast, 0);
	return 0;
}

//============================== 
//
//    Helpers
//
//============================== 

size_t trim_end(char *str){
	// Ex:       this is a string      \0
	// Produces: this is a string\0
	// Assumes string to be null-terminated
	// Mutates the string in-place

	size_t size = strlen(str);
	for(int i = size - 1; i >= 0; i--){
		if(isspace(str[i])){
			str[i] = '\0';
		}else{
			break;
		}
	}

	return strlen(str);
}

//============================== 
//
//    Main
//
//============================== 

int main(){
	char *buffer      = NULL; // Null-terminated
	size_t size       = 0;
	ssize_t nread     = 0;
	ssize_t last_good = 0;
	int parse_code    = 0;

	Arena temp_arena  = arena_create(1024 * 1024);      // 1 MB for now
	Arena a           = arena_create(32 * 1024 * 1024); // 32 MB for now
	Enviromnent e     = env_create(&a);

	int skip_ast_run  = 0;

	// const char *line = "let p = (((poly() + sqr(3) + (5))))";
	// parse_line(&a, line);

	 while(1){

	 	printf("tiny-ctrl> ");
	 	nread = getline(&buffer, &size, stdin);
	 	if(nread == -1){
	 		printf("\nExiting...\n");
	 		break;
	 	}

	 	// Trimming the endline characters
	 	last_good         = strcspn(buffer, "\r\n");
	 	buffer[last_good] = '\0';

		trim_end(buffer);

		skip_ast_run      = 0;

		SCRATCH_SCOPE(&temp_arena){
			AST *ast   = arena_alloc(&temp_arena, sizeof(AST));
			parse_code = parse_line(&temp_arena, &e,  buffer, ast, &skip_ast_run);
			if(!skip_ast_run) run_ast(&a, ast, &e);
		}
	 	if(parse_code != 0){
	 		printf("Exiting with code %i...\n", parse_code);
	 		break;
	 	}

	 }
  
	 free(buffer);
	arena_destroy(&temp_arena);
	arena_destroy(&a);
	if(parse_code != 0){
		exit(parse_code);
	}

	return 0;
}
