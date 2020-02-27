
CC       := gcc
CXX      := g++
CFLAGS   := -Wall -Wextra -pedantic -MMD -MP -std=c99
CXXFLAGS := -Wall -Wextra -pedantic -MMD -MP

all: readfat seekfile

clean:
	rm -f fat.d readfat.d seekfile.d fat.o \
	readfat.o seekfile.o readfat seekfile

fat.o: CFLAGS += -D_FILE_OFFSET_BITS=64 -D_POSIX_C_SOURCE=200808L
readfat.o: CXXFLAGS += -D_FILE_OFFSET_BITS=64 -D_POSIX_C_SOURCE=200808L
seekfile.o: CXXFLAGS += -D_FILE_OFFSET_BITS=64 -D_POSIX_C_SOURCE=200808L

readfat: fat.o readfat.o
	$(CXX) $^ -o $@

seekfile: fat.o seekfile.o
	$(CXX) $^ -o $@

-include fat.d readfat.d seekfile.d
