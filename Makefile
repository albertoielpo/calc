# Makefile
# Automatic variables
# ----------------------
# $@	The target name	calc
# $^	All prerequisites, space-separated	calc.c
# $<	The first prerequisite only
# $*	The stem (filename without extension, used in pattern rules)
#

CC      = gcc
# CFLAGS  = -Wextra -Wall -Wpedantic -O2 -g -std=c99
CFLAGS  = -static -Wextra -Wall -Wpedantic -O2 -g -std=c99
TARGET  = calc
SRCS    = calc.c

.PHONY: all run clean test

all: clean $(TARGET) test

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o bin/$@ $^ -lm

run: $(TARGET)
	./bin/$(TARGET)

clean:
	rm -f $(TARGET) *.o

test: $(TARGET)
	./calc_test
