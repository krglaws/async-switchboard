#ifndef MAP_H
#define MAP_H

#include <kylestructs.h>

/* Create a new hashmap from a string given two delimiters.
 * const char *s - the string to convert into a hashmap
 * const char *delim1 - the delimiter between pairs
 * const char *delim2 - the delimiter between keys and values
 */
ks_hashmap* new_map_from_str(const char* s, const char* delim1,
                             const char* delim2);

/* Print a hashmap. Useful for debugging.
 */
void print_map(const ks_hashmap* hm);

#endif
