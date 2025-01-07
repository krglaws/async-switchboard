#include "map.c"

#include <assert.h>
#include <stdlib.h>

void test_count_pairs_fails() {
    struct test_case {
        const char* str;
        const char* delim;
        int expected;
    };
    struct test_case tests[3] = {
        {.str = NULL, .delim = NULL, .expected = -1},
        {.str = NULL, .delim = "\r\n", .expected = -1},
        {.str = "Content-Length: 256\r\nContent-Type: text/html\r\n",
         .delim = NULL,
         .expected = 0},
    };

    for (int i = 0; i < 3; i++) {
        struct test_case tc = tests[i];
        assert(count_pairs(tc.str, tc.delim) == tc.expected);
    }
}

void test_count_pairs_succeeds() {
    struct test_case {
        const char* str;
        const char* delim;
        int expected;
    };
    struct test_case tests[3] = {
        {.str = "Content-Length: 256\r\nContent-Type: text/html\r\n",
         .delim = "\r\n",
         .expected = 2},
        {.str = "Content-Length: 256\r\nContent-Type: text/html",
         .delim = "\r\n",
         .expected = 2},
        {.str = "Content-Length: 256", .delim = "\r\n", .expected = 2},
    };

    for (int i = 0; i < 2; i++) {
        struct test_case tc = tests[i];
        assert(count_pairs(tc.str, tc.delim) == tc.expected);
    }
}

void test_new_map_from_str_fails() {
    ks_hashmap* hm = new_map_from_str(NULL, NULL, NULL);
    assert(hm == NULL);
}

int main() {
    test_count_pairs_fails();
    test_count_pairs_succeeds();
}
