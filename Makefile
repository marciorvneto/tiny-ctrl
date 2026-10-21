.PHONY: tctl stdlib clean

tctl: build/tctl stdlib

build/tctl: main.c | build
	gcc -ggdb main.c -lm -o build/tctl

build:
	mkdir -p build

stdlib: | build
	rm -rf build/libs
	cp -r libs build/

clean:
	rm -rf build
