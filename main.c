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
//    Parsing
//
//============================== 


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
