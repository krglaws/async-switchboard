#ifndef LOG_MANAGER_H
#define LOG_MANAGER_H

#include <stdarg.h>
#include <string.h>

int init_logger();

void emit_log(const char* level, const char* filename, int lineno,
              const char* fmt, ...);

int flush_logs();

int log_queue_size();

#define LOG_BUFFER_SIZE (1024 * 8)

#define INFOLVL "\e[92m[INFO]:\e[0m "
#define WARNLVL "\e[93m[WARN]:\e[0m "
#define ERRLVL "\e[91m[ERROR]:\e[0m "

/* Used for logging server info at various log levels */
#define LOG_INFO(fmt, ...) \
    emit_log(INFOLVL, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...) \
    emit_log(WARNLVL, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) \
    emit_log(ERRLVL, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#endif
