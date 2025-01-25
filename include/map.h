#ifndef MAP_H
#define MAP_H

#include <kylestructs.h>

/* */
int init_host_map(const char *map_str);

const char *get_host_ip(const char *host_name);

#endif
