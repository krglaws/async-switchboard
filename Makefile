CC := clang
CFLAGS := -lkylestructs -Iinclude -Wall -Wextra -O2
TESTFLAGS := -lkylestructs -Iinclude -Isrc -g -O0 -fsanitize=address,undefined

SRC := src
INC := include
SBIN := bin

TEST := tests
TBIN := testbin

default: all

dir_guard=@mkdir -p $(SBIN) $(TBIN)

# compile sources

MAPDEPS := $(SRC)/map.c 
MAPTARG := $(SBIN)/map.o
$(MAPTARG): $(MAPDEPS)
	$(dir_guard)
	$(CC) -c $< -o $@ $(CFLAGS)

# compile tests

MAPTESTTARG := $(TBIN)/map_test
$(MAPTESTTARG): $(TEST)/map_test.c
	$(dir_guard)
	$(CC) $^ -o $@ $(TESTFLAGS)

.PHONY: tests
tests: $(MAPTESTTARG) $(MAPDEPS)
	@echo Finished building tests.

.PHONY: run_tests
run_tests: tests
	./runtests.sh

.PHONY: clean
clean:
	rm -f $(SBIN)/*
	rm -f $(TBIN)/*

.PHONY: all
all: run_tests
	@echo "All tests passed."
	@echo "Building executable..."
	#$(MAKE) $(EXE)
	@echo "Done."
