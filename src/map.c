#include "map.h"

#include <ctype.h>
#include <errno.h>
#include <kylestructs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "logger.h"

#define MAX_IP_LEN (64)

struct host_info {
    char ip[MAX_IP_LEN];
    int port;
};

ks_hashmap* host_map = NULL;

void print_list_charp_dc(const ks_datacont* dc) {
    ks_iterator* ls_iter = ks_iterator_new(dc->ls, KS_LIST);
    const ks_datacont* val;
    while ((val = ks_iterator_next(ls_iter)) != NULL) {
        printf(" %s", val->cp);
    }
    ks_iterator_delete(ls_iter);
}

void print_host_info_dc(const ks_datacont* dc) {
    struct host_info* hi = ((struct host_info*)dc->vp);
    printf("(%s %d)", hi->ip, hi->port);
}

void print_map(const ks_hashmap* hm, void(print_val)(const ks_datacont*)) {
    ks_iterator* hm_iter = ks_iterator_new(hm, KS_HASHMAP);
    const ks_datacont* key;
    while ((key = ks_iterator_next(hm_iter)) != NULL) {
        printf("%s:", key->cp);
        const ks_datacont* val = ks_hashmap_get(hm, key);
        print_val(val);
        printf("\n");
    }
    ks_iterator_delete(hm_iter);
}

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

        int keylen = mid - next;
        if (keylen < 1) {
            goto ERROR;
        }
        char keybuff[keylen];
        for (int i = 0; i < keylen; i++) {
            keybuff[i] = tolower(*(next + i));
        }

        ks_datacont* key = ks_datacont_new(keybuff, KS_CHARP, keylen);
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

static void free_host_map() {
    ks_iterator* iter = ks_iterator_new(host_map, KS_HASHMAP);
    ks_datacont* dc;
    while ((dc = (ks_datacont*)ks_iterator_next(iter)) != NULL) {
        free(dc->vp);
        ks_datacont_delete(dc);
    }
    ks_iterator_delete(iter);
    ks_hashmap_delete(host_map);
}

static struct host_info* parse_host_info(const char* s, size_t len) {
    struct host_info* hi = malloc(sizeof(struct host_info));
    size_t i = 0;
    while (i < len && s[i] != ' ') {
        i++;
    }
    size_t ip_len = i;
    if (ip_len == 0 || ip_len + 1 >= len || ip_len > MAX_IP_LEN) {
        goto ERROR;
    }
    memcpy(hi->ip, s, i);
    hi->ip[ip_len] = '\0';

    i++;

    int port_len = len - ip_len - 1;
    hi->port = atoi(s + i);
    if (port_len < 1 || hi->port < 1) {
        goto ERROR;
    }
    return hi;

ERROR:
    free(hi);
    return NULL;
}

int init_host_map(const char* host_config) {
    ks_hashmap* tmp = new_map_from_str(host_config, "\n", " ");
    if (tmp == NULL) {
        LOG_ERROR("failed to parse host config");
        return -1;
    }

    host_map = ks_hashmap_new(KS_CHARP, tmp->num_buckets);
    ks_iterator* iter = ks_iterator_new(tmp, KS_HASHMAP);
    const ks_datacont* key;
    while ((key = ks_iterator_next(iter)) != NULL) {
        const ks_datacont* val = ks_hashmap_get(tmp, key);
        const ks_datacont* dc_end = ks_list_get(val->ls, -1);
        struct host_info* hi = parse_host_info(dc_end->cp, dc_end->size);
        if (hi == NULL) {
            LOG_ERROR("failed to parse host info for key='%s' value='%s'");
            goto ERROR;
        }
        ks_datacont* key_copy = ks_datacont_copy(key);
        ks_datacont* new_val =
            ks_datacont_new(hi, KS_VOIDP, sizeof(struct host_info));
        ks_hashmap_add(host_map, key_copy, new_val);
    }
    if (atexit(free_host_map) != 0) {
        LOG_ERROR("failed on call to atext(): %s", strerror(errno));
        goto ERROR;
    }

    return 0;

ERROR:
    ks_iterator_delete(iter);
    ks_hashmap_delete(tmp);
    ks_hashmap_delete(host_map);
    return -1;
}

const struct host_info* get_host_info(const char* hostname) {
    ks_datacont* key = malloc(sizeof(ks_datacont));
    key->cp = (char*)hostname;
    key->size = strlen(hostname);
    key->type = KS_CHARP;
    const ks_datacont* val = ks_hashmap_get(host_map, key);
    free(key);
    if (val == NULL) {
        return NULL;
    }

    return val->vp;
}
