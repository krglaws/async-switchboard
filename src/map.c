#include "map.h"

#include <kylestructs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Really just meant to count occurences of the delimiter
 * between key/value pairs to get an approximate map size.
 * Does not check whether there are actual valid
 * key/value pairs.
 */
static int count_pairs(const char* s, const char* delim) {
    if (s == NULL || delim == NULL) {
        return 0;
    }
    int count = 1;
    char* finder = (char*)s;
    while ((finder = strstr(finder, delim)) != NULL) {
        finder += strlen(delim);
        if (*finder != '\0') {
            count++;
        }
    }

    return count;
}

void print_map(const ks_hashmap* hm) {
    ks_iterator* hm_iter = ks_iterator_new(hm, KS_HASHMAP);
    const ks_datacont* key;
    while ((key = ks_iterator_next(hm_iter)) != NULL) {
        printf("%s:", key->cp);
        const ks_datacont* ls = ks_hashmap_get(hm, key);
        ks_iterator* ls_iter = ks_iterator_new(ls->ls, KS_LIST);
        const ks_datacont* val;
        while ((val = ks_iterator_next(ls_iter)) != NULL) {
            printf(" %s", val->cp);
        }
        ks_iterator_delete(ls_iter);
        printf("\n");
    }
    ks_iterator_delete(hm_iter);
}

ks_hashmap* new_map_from_str(const char* s, const char* delim1,
                             const char* delim2) {
    if (s == NULL || delim1 == NULL || delim2 == NULL) {
        return NULL;
    }
    int pairs = count_pairs(s, delim1);
    if (pairs == 0) {
        return NULL;
    }
    int num_buckets = (int)((pairs)*1.5);
    if (num_buckets < 16) {
        num_buckets = 16;
    }
    ks_hashmap* hm = ks_hashmap_new(KS_CHARP, num_buckets);

    char* next = (char*)s;
    while (*next != '\0') {
        char* mid = strstr(next, delim2);
        if (mid == NULL) {
            goto ERROR;
        }
        char* end = strstr(mid, delim1);
        if (end == NULL) {
            end = mid + strlen(mid);
        }

        ks_datacont* key = ks_datacont_new(next, KS_CHARP, mid - next);
        ks_datacont* val = ks_datacont_new(mid + strlen(delim2), KS_CHARP,
                                           end - (mid + strlen(delim2)));
        const ks_datacont* exists = ks_hashmap_get(hm, key);
        if (exists != NULL) {
            ks_list_add(exists->ls, val);
            ks_datacont_delete(key);
        } else {
            ks_list* ls = ks_list_new();
            ks_list_add(ls, val);
            ks_datacont* dc = ks_datacont_new(ls, KS_LIST, 1);
            ks_hashmap_add(hm, key, dc);
        }

        next = end;
        if (*next != '\0') {
            next += strlen(delim1);
        }
    }

    return hm;

ERROR:
    ks_hashmap_delete(hm);
    return NULL;
}
