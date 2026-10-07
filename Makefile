CC = gcc
CFLAGS = -Wall -Wextra -g -std=c99 -Iinclude -O2
AR = ar
ARFLAGS = rcs

SRCS = $(wildcard src/*.c)

OBJS = $(patsubst src/%.c, build/%.o, $(SRCS))

TARGET = libhttp.a

all: $(TARGET)

$(TARGET): $(OBJS) 
	$(AR) $(ARFLAGS)  $@ $^

build/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf build $(TARGET)

.PHONY: all clean
