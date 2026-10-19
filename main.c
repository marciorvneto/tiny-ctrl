#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

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
	while(isalpha(pos[i])){
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
	      X(AST_BINARY_OP)        \
	      X(AST_FN_CALL)        \
	      X(AST_ARGS_LIST)         \
	      X(AST_NODE_TYPE_COUNT)
				

typedef enum {
#define X(name) name,
	AST_NODES_LIST
#undef X
} ASTNodeType;

// Expression value types

typedef enum {
	VALUE_NUMBER,
	VALUE_STRING,
	VALUE_POLYNOMIAL,
	VALUE_TF,

	// Things to think about
	VALUE_ARRAY,
	VALUE_MATRIX,
	VALUE_TYPE_COUNT
} ValueType;

typedef enum {
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
			char   *fn_name;
			size_t num_arguments;
			AST    *first_arg;
		} fn_call;

		struct {
			TokenType op;
			AST       *left;
			AST       *right;
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


AST *ast_make_fn_call(Arena *a, const char *fn_name){
	AST *fn_call = ast_alloc(a, AST_FN_CALL);
	fn_call->as.fn_call.num_arguments = 0;
	fn_call->as.fn_call.fn_name       = arena_alloc(a, strlen(fn_name) + 1);
	fn_call->as.fn_call.first_arg     = NULL;
	strcpy(fn_call->as.fn_call.fn_name, fn_name);
	return fn_call;
}

AST *ast_make_binop(Arena *a, TokenType op){
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
		AST   *binop          = ast_make_binop(a, op_tok->type);
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
		AST   *binop          = ast_make_binop(a, op_tok->type);
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
		AST   *binop          = ast_make_binop(a, op_tok->type);
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
		AST *stmt     = ast_make_statement(a, STATEMENT_ASSIGN);
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
				token_type_repr(ast->as.binop.op, opBuf);
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


int parse_line(Arena *a, const char *line, AST *ast){
	// Null-terminated

  // --------- Basic commands -----------

	if(strcmp(line, "q") == 0){
		printf("\nExiting...\n");
		exit(0);
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
//    Environment
//
//============================== 

typedef struct {
	Value  *vars;
	size_t num_vars;
} Enviromnent;

//============================== 
//
//    Interpreting
//
//============================== 

Value make_num(double num){
	Value v = {
		.valueType    = VALUE_NUMBER,
		.value.number = num
	};
	return v;
}

Value eval_expr(Arena *a, Enviromnent *e, AST *expr);

Value eval_binop(Arena *a, Enviromnent *e, AST *op, Value *left, Value *right){
	// Number op Number

	if(left->valueType == VALUE_NUMBER && left->valueType == VALUE_NUMBER){

		switch(op->as.binop.op){
			case TOK_PLUS:
				{
					return make_num(left->value.number + right->value.number);
				}
			case TOK_MINUS:
				{
					return make_num(left->value.number - right->value.number);
				}
			case TOK_MUL:
				{
					return make_num(left->value.number * right->value.number);
				}
			case TOK_DIV:
				{
					return make_num(left->value.number / right->value.number);
				}
			case TOK_POW:
				{
					return make_num(powf(left->value.number, right->value.number));
				}
			default:
				{
					fprintf(stderr, "Unreachable\n");
					exit(1);
				}
		}
	}

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

	}
	
	return make_num(0);

}

void print_value(Value *value){
	switch(value->valueType){
		case VALUE_NUMBER:
			{
				printf("%.4f\n", value->value.number);
				break;
			}
		case VALUE_STRING:
			{
				printf("%s\n", value->value.str);
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
				value.valueType = expr->as.literal.valueType;
				value.value = expr->as.literal.value;
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
				fprintf(stderr, "Unreachable\n");
				exit(1);
			}
	}

	return value;
}

int eval_stmt(Arena *a, AST *ast, Enviromnent *e, AST *stmt){

	if(stmt->as.statement.type == STATEMENT_EXPR){
		Value result = eval_expr(a, e, stmt->as.statement.expr);
		print_value(&result);
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

	Arena temp_arena = arena_create(1024 * 1024);      // 1 MB for now
	Arena a          = arena_create(32 * 1024 * 1024); // 32 MB for now
	Enviromnent e = {0};

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
	 	last_good = strcspn(buffer, "\r\n");
	 	buffer[last_good] = '\0';

		SCRATCH_SCOPE(&temp_arena){
			AST *ast = arena_alloc(&temp_arena, sizeof(AST));
			parse_code = parse_line(&temp_arena, buffer, ast);
			run_ast(&a, ast, &e);
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
