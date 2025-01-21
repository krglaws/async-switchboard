#define _GNU_SOURCE
#include "logger.h"

#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <kylestructs.h>


typedef struct {
    int in_fd;
    int out_fd;
} logger_args_t;


static ks_list *log_queue = NULL;


static void* logger_main(void* args) {
    int in_fd = ((logger_args_t*)args)->in_fd;
    int out_fd = ((logger_args_t*)args)->out_fd;
    char buffer[LOG_BUFFER_SIZE];

    while (true) {
        int read_bytes;
        if ((read_bytes = read(in_fd, buffer, LOG_BUFFER_SIZE)) < 0) {
            perror("read()");
            break;
        }

        int written_bytes;
        if ((written_bytes = write(out_fd, buffer, read_bytes)) < 0) {
            perror("write()");
            break;
        }
    }

    exit(EXIT_FAILURE);
}

static void end_logger() {
    ks_list_delete(log_queue);
}

int init_logger() {
    log_queue = ks_list_new();

    logger_args_t args;
    int rw[2];
    if (pipe(rw) == -1) {
        perror("pipe()");
        return -1;
    }

    args.in_fd = rw[0];
    args.out_fd = 1;
    int write_pipe = rw[1];

    int flags = fcntl(rw[0], F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl(F_GETFL)");
        return -1;
    }
    flags |= O_NONBLOCK;
    if (fcntl(write_pipe, F_SETFL, flags) == -1) {
        perror("fcntl(F_SETFL)");
        return -1;
    }

    int err;
    pthread_t thread;
    if ((err = pthread_create(&thread, NULL, logger_main, &args)) != 0) {
        fprintf(stderr, "pthread_create(): %s\n", strerror(err));
        return -1;
    }

    if (atexit(end_logger) != 0) {
        perror("atexit()");
        return -1;
    }

    return 0;
}


void submit_log(const char* level, const char* filename, int lineno, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);

    char msgbuff[LOG_BUFFER_SIZE];
    if (vsnprintf(msgbuff, LOG_BUFFER_SIZE, fmt, ap) >= LOG_BUFFER_SIZE) {
        sprintf(msgbuff, "Omitting log statement at %s:%d -- way too big!",
                filename, lineno);
    }
    va_end(ap);

    char timebuff[32];
    time_t rawtime = time(NULL);
    struct tm* ptm = localtime(&rawtime);
    strftime(timebuff, sizeof(timebuff), "%x %T", ptm);

    int len;
    char* buffptr = msgbuff;
    char* nlloc;
    char logbuff[LOG_BUFFER_SIZE];

    // for each '\n' char in message,
    // print preceding 'logtype' string
    do
    {
      if ((nlloc = strstr(buffptr, "\n")) != NULL)
      {
        len = (int) (nlloc - buffptr);
      }
      else
      {
        len = strlen(buffptr);
      }

      //fprintf(logfile, "(%s)%s%.*s\n", timebuff, logtype, len, buffptr);
      if (snprintf(logbuff, "(%s)%s%.*s\n", timebuff, level, len, buffptr) >= LOG_BUFFER_SIZE) {
          sprintf(logbuff, "Omitting log statement at %s:%d -- way too big!", filename, lineno);
      }
      if (vsnprintf(logbuff, LOG_BUFFER_SIZE, fmt, ap) >= LOG_BUFFER_SIZE) {
      }

      buffptr += (len + 1);

    } while (*buffptr);
}
