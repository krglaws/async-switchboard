#ifndef MAP_H
#define MAP_H

#include <kylestructs.h>

int init_host_map(const char* host_config);

const struct host_info* get_host_info(const char* host_name);

ks_hashmap* new_map_from_str(const char* s, const char* delim1,
                             const char* delim2);

/* for debugging */
void print_list_charp_dc(const ks_datacont* dc);
void print_host_info_dc(const ks_datacont* dc);
void print_map(const ks_hashmap* hm, void(print_val)(const ks_datacont*));

#endif
