ifeq ($(origin CC), default)
CC = gcc
endif
CPPFLAGS ?=
CFLAGS  ?=
LDFLAGS ?=
LDLIBS  ?=

CPPFLAGS += -I../include
CFLAGS   += -std=c99 -Wall -Wextra -Wpedantic -Werror

SRC     := ../src/mboot.c
TESTS   := test_all.c
RM      ?= rm -f

.PHONY: all clean

all: test_all
	@./test_all

test_all: $(TESTS) $(SRC) ../include/mboot.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(SRC) $(TESTS) $(LDLIBS) -o $@

clean:
	$(RM) test_all
