#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include <src/logger.c>

void test_filter_out_specials() {
    struct test_case {
        char src[16];
        size_t src_size;
        char *expected_dst;
        int expected_ret;
    };
    struct test_case tests[3] = {
        {.src = "helloworld",
         .src_size = 10,
         .expected_dst = "helloworld",
         .expected_ret = 0},
        {.src = "hello\r\0\nworld",
         .src_size = 13,
         .expected_dst = "hello\\r<?>\\nworld",
         .expected_ret = 0},
        {.src = "way\0too\0long\0\0\0",
         .src_size = 15,
         .expected_dst = "",
         .expected_ret = -1},
    };

    for (int i = 0; i < 3; i++) {
        struct test_case test = tests[i];
        char dst[20];
        assert(filter_out_specials(test.src, test.src_size, dst, 20) ==
               test.expected_ret);
        if (test.expected_ret == 0) {
            assert(memcmp(test.expected_dst, dst, strlen(test.expected_dst)) ==
                   0);
        }
    }
}

int main() { test_filter_out_specials(); }
