#ifndef LOG_MANAGER_H
#define LOG_MANAGER_H

#include <stdarg.h>

/* Initializes the logger output file
    to 'path'. STDOUT is used if 'path'
    is NULL */
void init_log_manager(const char* path);


/* Used for logging basic server info */
void log_info(const char* fmt, ...);


/* Used for logging server errors */
void log_err(const char* fmt, ...);


/* Used for logging critical server errors
    (This will send sigterm to terminate the program) */
void log_crit(const char* fmt, ...);


#endif
