
CC       := gcc
CXX      := g++
CFLAGS   := -Wall -Wextra -pedantic -MMD -MP -std=c99
CXXFLAGS := -Wall -Wextra -pedantic -MMD -MP

all: readfat

clean:
	rm -f fat.d readfat.d fat.o readfat.o readfat

fat.o: CFLAGS += -D_FILE_OFFSET_BITS=64 -D_POSIX_C_SOURCE=200808L

readfat: fat.o readfat.o
	$(CXX) $^ -o $@

-include fat.d readfat.d