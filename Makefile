main: main.c build
	gcc -ggdb main.c -o build/main

build:
	mkdir -p build
