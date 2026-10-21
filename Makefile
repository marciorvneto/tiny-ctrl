tctl: main.c build
	gcc -ggdb main.c -lm -o build/tctl

build:
	mkdir -p build
	cp -r libs build
