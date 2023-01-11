/**
 * logs.h
*/
#include <pthread.h>

typedef struct logs_handler {
    void *ctx;
} LogsHandle;

int logs_init(const char *path, int level, LogsHandle *handle);
int logs_print(void *log_ctx, int level, const char *fmt, ...);