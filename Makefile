CFLAGS=-Wall -Wextra -Wpedantic -g
CXXFLAGS=-std=gnu++23  $(CFLAGS) $(shell pkg-config --cflags fmt) -fopenmp -g -O3
LDFLAGS=$(shell pkg-config --libs fmt )
all: huffman_code
check: huffman_code
	./huffman_code
clean:
	$(RM) huffman_code
