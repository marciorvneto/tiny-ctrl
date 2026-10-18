main: main.c build
	gcc -ggdb main.c -lm -o build/main

build:
	mkdir -p build
