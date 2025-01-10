#include "map.c"

#include <assert.h>
#include <stdlib.h>


void test_count_pairs() {
    struct test_case {
        const char* str;
        const char* delim;
        int expected;
    };
    struct test_case tests[6] = {
        {.str = NULL, .delim = NULL, .expected = 0},
        {.str = NULL, .delim = "\r\n", .expected = 0},
        {.str = "Content-Length: 256\r\nContent-Type: text/html\r\n",
         .delim = NULL,
         .expected = 0},
        {.str = "Content-Length: 256\r\nContent-Type: text/html\r\n",
         .delim = "\r\n",
         .expected = 2},
        {.str = "Content-Length: 256\r\nContent-Type: text/html",
         .delim = "\r\n",
         .expected = 2},
        {.str = "Content-Length: 256", .delim = "\r\n", .expected = 1},
    };

    for (int i = 0; i < 6; i++) {
        struct test_case tc = tests[i];
        assert(count_pairs(tc.str, tc.delim) == tc.expected);
    }
}

void test_new_map_from_str_fails() {
    struct test_case {
        const char* str;
        const char* delim1;
        const char* delim2;
    };
    struct test_case tests[4] = {
        {.str = NULL, .delim1 = NULL, .delim2 = NULL},
        {.str = NULL, .delim1 = "\r\n", .delim2 = NULL},
        {.str = NULL, .delim1 = "\r\n", .delim2 = ":"},
        {.str = "Content-Length: 256\r\nContent-Type: text/html\r\n",
         .delim1 = NULL,
         .delim2 = NULL},
    };
    for (int i = 0; i < 4; i++) {
        struct test_case tc = tests[i];
        assert(new_map_from_str(tc.str, tc.delim1, tc.delim2) == NULL);
    }
}

void test_new_map_from_str_succeeds_headers() {
    const char* headers = "Content-Type: text/html\r\n"
        "Content-Length: 1024\r\n"
        "Host: wwww.google.com\r\n";
    const char* delim1 = "\r\n";
    const char* delim2 = ":";

    ks_hashmap* hm = new_map_from_str(headers, delim1, delim2);

    assert(hm != NULL);
    assert(ks_hashmap_count(hm) == 3);

    struct test_case {
        char *key;
        char *val;
    };
    struct test_case tests[3] = {
        {.key="Content-Type", .val=" text/html"},
        {.key="Content-Length", .val=" 1024"},
        {.key="Host", .val=" www.google.com"},
    };

    for (int i = 0; i < 3; i++) {
        ks_datacont* key = ks_datacont_new(tests[i].key, KS_CHARP, strlen(tests[i].key));

        const ks_datacont* value = ks_hashmap_get(hm, key);
        assert(value != NULL);
        assert(strcmp(value->cp, tests[i].val));

        ks_datacont_delete(key);
    }

    ks_hashmap_delete(hm);
}

int main() {
    test_count_pairs();
    test_new_map_from_str_fails();
    test_new_map_from_str_succeeds_headers();
}
