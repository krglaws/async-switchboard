# Variables
CC = clang
CFLAGS = -Wall -Wextra -Iinclude -lkylestructs
DEBUG_CFLAGS = -Wall -Wextra -g -Iinclude -I. -g -O0 -fsanitize=address,undefined -lkylestructs
RELEASE_CFLAGS = -Wall -Wextra -O2 -Iinclude
SRC_DIR = src
TEST_DIR = tests
BUILD_DIR = build
TARGET = $(BUILD_DIR)/main

SRC_FILES = $(wildcard $(SRC_DIR)/*.c)
TEST_FILES = $(wildcard $(TEST_DIR)/*.c)
OBJ_FILES = $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/release_%.o, $(SRC_FILES))
DEBUG_OBJ_FILES = $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/debug_%.o, $(SRC_FILES))
TEST_OBJ_FILES = $(patsubst $(TEST_DIR)/%.c, $(BUILD_DIR)/test_%.o, $(TEST_FILES))

# Rules
.PHONY: all clean tests

all: $(TARGET)

$(BUILD_DIR):
	@mkdir -p $@

# Build release objects
$(BUILD_DIR)/release_%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(RELEASE_CFLAGS) -c $< -o $@

# Build debug objects
$(BUILD_DIR)/debug_%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(DEBUG_CFLAGS) -c $< -o $@

# Test target uses debug objects and test objects
$(BUILD_DIR)/test_%.o: $(TEST_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(DEBUG_CFLAGS) $< -o $@

# Main target uses release objects
$(TARGET): $(OBJ_FILES) | $(BUILD_DIR)
	$(CC) $(RELEASE_CFLAGS) $^ -o $@

test: $(TEST_OBJ_FILES)
	./runtests.sh

clean:
	rm -rf $(BUILD_DIR)

