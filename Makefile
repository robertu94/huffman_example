CFLAGS=-Wall -Wextra -Wpedantic -g
CXXFLAGS=-std=gnu++23  $(CFLAGS) $(shell pkg-config --cflags fmt) -fopenmp -g -O3
LDFLAGS=$(shell pkg-config --libs fmt )
all: huffman_code memory_efficent_huffman
check: all
	./memory_efficent_huffman
clean:
	$(RM) huffman_code
