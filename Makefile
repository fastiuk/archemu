all: build test
build:
	gcc -g arm-emu.c -o emu

test:
	./emu
