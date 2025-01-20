#include <src/logger.c>

#include <assert.h>
#include <stdlib.h>

void test_get_available_capacity() {
    struct test_case {
        size_t left;
        size_t right;
        size_t expected;
    };
    struct test_case tests[3] = {
        {.left = 0, .right = 0,.expected = 10},
        {.left = 1, .right = 8,.expected = 3},
        {.left = 8, .right = 1,.expected = 7},
    };

    for (int i = 0; i < 3; i++) {
        struct test_case test = tests[i];
        assert(get_available_capacity(10, test.left, test.right) == test.expected);
    }
}


void test_get_current_read_size() {
    struct test_case {
        size_t left;
        size_t right;
        size_t expected;
    };
    struct test_case tests[3] = {
        {.left = 0, .right = 0,.expected = 10},
        {.left = 1, .right = 8,.expected = 2},
        {.left = 8, .right = 1,.expected = 7},
    };

    for (int i = 0; i < 3; i++) {
        struct test_case test = tests[i];
        assert(get_current_read_size(10, test.left, test.right) == test.expected);
    }
}


int main() {
    test_get_available_capacity();
    test_get_current_read_size();
}
