/**
 * @file :  logs.c
 * Description:
 * Author:  
 * Date:
 * @copyright: free
*/
#include "logs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <assert.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/syscall.h>

#define RBLEN1M   1048576

/* round buffer */
/* 真的能够无锁访问吗？ */
typedef struct round_data {
    int total_size;
    int data_size;
    uint8_t *data;
} RBdata;

typedef struct round_buffer {
    int length;
    int head, tail;
    pthread_mutex_t lock;
    RBdata buffer[0]; // 连续内存好？还是分段内存好？
} RoundBuffer, RBuffer;

/**
 * 设计：
 * 1、内存只申请一次，能够降低申请释放的风险
 * 2、没有锁，尾指针追头指针，int类型基本保证原子？
 *    猜：尾指针能越位头指针吗？造成输出混乱
 * 3、必须多线程访问的buffer
*/
/** total 100M ？
 * @param buf_len   :   10k
 * @param buf_size  :   10k
 * return:  buf, transform it to rb_read & rb_write
*/
RoundBuffer *rb_alloc(int buf_len, int buf_size) {
    if (buf_len <= 0 || buf_size <= 0) {
        return NULL;
    }

    RoundBuffer *rb = (RoundBuffer *)malloc(sizeof(RoundBuffer) + sizeof(RBdata) * (buf_len + 1));
    if (rb) {
        int i = 0;
        memset(rb, 0, sizeof(RoundBuffer) + sizeof(RBdata) * (buf_len + 1));
        rb->buffer[buf_len].data = NULL;
        rb->buffer[buf_len].data_size = 0;
        rb->buffer[buf_len].total_size = -1;
        for (i = 0; i < buf_len; i++) {
            rb->buffer[i].data = (uint8_t *)malloc(buf_size * sizeof(uint8_t) + 1);
            if (rb->buffer[i].data == NULL)
                perror("RBSIZE OOM"), assert(0); // exit
            memset(rb->buffer[i].data, 0, buf_size * sizeof(uint8_t) + 1);
            rb->buffer[i].total_size = buf_size;
            rb->buffer[i].data_size = 0;
        }
        if (i != buf_len)
            perror("RBLEN OOM"), assert(0); // exit
        rb->length = buf_len;
        rb->head = rb->tail = 0;
        pthread_mutex_init(&rb->lock, NULL);
    }
    return rb;
}

/**
 * @param 
 * @param buf   :  round_buffer
 * @param len   :  out put len
 * @param TODO  : read function set by user !
 * @return read data_size
*/
int rb_push_data(RBuffer *buf, char *data, int data_size) {
    int rsize = 0, nsize = 0;
    RBuffer *rb = (RBuffer *)buf;
    if (rb == NULL || data == NULL || data_size <= 0) {
        return -1;
    }
    assert(!(rb->head > rb->length));
    assert(!(rb->tail > rb->length));
    // push
    // 可以相等，任何时候相等都是空的！
    // 写满时head比tail小1，只此一种
    // 
    pthread_mutex_lock(&rb->lock);
    if (rb->head == rb->tail) {
        // 空
        rsize = data_size < rb->buffer[rb->head].total_size ? data_size : rb->buffer[rb->head].total_size - 1;
        memcpy(rb->buffer[rb->head].data, data, rsize);
        rb->buffer[rb->head].data[rsize] = 0;
        rb->buffer[rb->head].data_size = rsize;
        rb->head++;
        // printf("push head %d tail %d\n",  rb->head, rb->tail);
        if (rb->head >= rb->length) {
            rb->head = 0;
        }
    } else if ( (rb->head + 1 == rb->tail) || (rb->head == rb->length && rb->tail == 0) ) {
        // 满
        // return 0;
        rsize = 0;
    } else {
        // 余
        rsize = data_size < rb->buffer[rb->head].total_size ? data_size : rb->buffer[rb->head].total_size;
        memcpy(rb->buffer[rb->head].data, data, rsize);
        rb->buffer[rb->head].data[rsize] = 0;
        rb->buffer[rb->head].data_size = rsize;
        rb->head++;
        // printf("push head %d tail %d\n",  rb->head, rb->tail);
        if (rb->head >= rb->length) {
            rb->head = 0;
        }
    }
    pthread_mutex_unlock(&rb->lock);
    return rsize;
}

/**
 * @param buf   :   
 * @param fd    :   不能用fd，系统调用阻塞，还是得buf和size
*/
int rb_pop_data(RBuffer *buf, char *data, int data_size) {
    int wsize = 0;
    if (buf == NULL || data == NULL || data_size < 0) {
        return -1;
    }
    RBuffer *rb = (RBuffer *)buf;
    assert(!(rb->head > rb->length));
    assert(!(rb->tail > rb->length));
    pthread_mutex_lock(&rb->lock);
    if (rb->head == rb->tail) {
        // 空
        wsize = 0;
    } else {
        wsize = data_size <= rb->buffer[rb->tail].data_size ? data_size - 1 : rb->buffer[rb->tail].data_size;
        memcpy(data, rb->buffer[rb->tail].data, wsize);
        data[wsize] = 0;
        rb->tail++;
        // printf("pop tail %d head %d, size %d\n",  rb->tail, rb->head, wsize);
        if (rb->tail >= rb->length) {
            rb->tail = 0;
        }
    }
    pthread_mutex_unlock(&rb->lock);
    return wsize;
}

int rb_buf_size(RBuffer *buf) {
    RBuffer *rb = (RBuffer *)buf;
    if (rb == NULL) {
        return -1;
    }
    assert(!(rb->head > rb->length));
    assert(!(rb->tail > rb->length));
    pthread_mutex_lock(&rb->lock);
    if ( (rb->head + 1 == rb->tail) || ((rb->head == rb->length - 1) && rb->tail == 0) ) {
        pthread_mutex_unlock(&rb->lock);
        return 0;
    }
    pthread_mutex_unlock(&rb->lock);
    return 1;
}

int rb_data_size(RBuffer *buf) {
    RBuffer *rb = (RBuffer *)buf;
    if (rb == NULL) {
        return -1;
    }
    assert(!(rb->head > rb->length));
    assert(!(rb->tail > rb->length));
    pthread_mutex_lock(&rb->lock);
    if (rb->head != rb->tail) {
        pthread_mutex_unlock(&rb->lock);
        return 1;
    }
    pthread_mutex_unlock(&rb->lock);
    return 0;
}

/**
 * log interface start
*/

typedef struct logs_context {
    const char *log_path;
    int log_level;
    RBuffer *buffers;
    int drop_cnt;
    int stoped;
    int inited;
    pthread_t thread_handle;
} LogsContext;

#define LOGS_GLOBAL_PATH            "/tmp/logs/global.log"
static LogsContext logs_ctx_global = {
    .log_path = NULL,
    .log_level = 0,
    .buffers = NULL,
    .drop_cnt = 0,
    .stoped = 0,
    .inited = 0
};

static LogsHandle logs_handle_global = {
    .ctx = &logs_ctx_global
};
#define     SINGLE_FILESIXE_MAX         52428800     // 1048576=1M 52428800=50M
#define     TOTAL_FILE_MAX              20   // 20*50M = 1000M ≈ 1G
#define     CACHE_BUF_SIZE              5120
void *logs_output_thread(void *arg) {
    LogsContext *ctx = (LogsContext *)arg;
    if (arg == NULL) {
        system("/bin/bash echo \"init log but arg is null!\" >> /tmp/logs/global.log");
        return NULL;
    }
    char buffer[5120] = {0};
    int data_size = 0;
    int poplen = 0;
    long curlen = 0;
    ctx->stoped = 0;
    FILE *fp = fopen(ctx->log_path, "a+");
    printf("[%d]logs func start %p %s\n", (int)syscall(SYS_gettid), fp, ctx->log_path);
    if (fp == NULL) {
        assert(fp);
        return NULL;
    }
    #define LOGRECORDSTART_STR       "logrecord start..."
    fwrite(LOGRECORDSTART_STR, strlen(LOGRECORDSTART_STR), 1, fp);
    while(ctx && ctx->stoped == 0) {
        int datasize = rb_data_size(ctx->buffers);
        
        while((poplen = rb_pop_data(ctx->buffers, buffer, sizeof(buffer))) > 0) {
            // printf("%s", buffer);
            fwrite(buffer, poplen, 1, fp);
            fflush(fp);

            curlen = ftell(fp);
            if (curlen >= SINGLE_FILESIXE_MAX) {
                int i = 0;
                char tmpfilename_0[2000] = {0};
                char tmpfilename_1[2000] = {0};
                fclose(fp);
                for (i = TOTAL_FILE_MAX - 1; i > 1; i--) {
                    snprintf(tmpfilename_0, sizeof(tmpfilename_0), "%s.%d", ctx->log_path, i);
                    snprintf(tmpfilename_1, sizeof(tmpfilename_1), "%s.%d", ctx->log_path, i-1);
                    rename(tmpfilename_1, tmpfilename_0);
                }
                snprintf(tmpfilename_0, sizeof(tmpfilename_0), "%s.1", ctx->log_path);
                rename(ctx->log_path, tmpfilename_0);

                fp = fopen(ctx->log_path, "a+");
            }
            if (fp == NULL) {
                assert(0);
            }

            // rollback file
        }
    }

    #define LOGRECORDEXIT_STR       "logrecord exit!"
    fwrite(LOGRECORDEXIT_STR, strlen(LOGRECORDEXIT_STR), 1, fp);
    return NULL;
    // printf("logs func exit!\n");
}

int logs_init(const char *path, int level, LogsHandle *handle)
{
    LogsContext *ctx = NULL;
    if (handle == NULL) {
        ctx = &logs_ctx_global;
        if (ctx->inited++) {
            return 0;
        }
    } else {
        ctx = malloc(sizeof(LogsContext));
        if (ctx == NULL) {
            return errno;
        }
        memset(ctx, 0, sizeof(LogsContext));
        ctx->inited++;
    }
    char *name_at = NULL;
    char *path_at = NULL;
    char *log_path = NULL;
    char filepath[512] = {0};
    char filename[512] = {0};

    if (path && strlen(path) > 0) {
        log_path = strdup(path);
    } else {
        log_path = strdup(LOGS_GLOBAL_PATH);
    }
    ctx->log_path = log_path;
    name_at = strrchr(log_path, '/');
    if (name_at == NULL) {
        // curdir that only content filename
    } else {
        strncpy(filepath, log_path, name_at - log_path);
        strncpy(filename, name_at+1, strlen(name_at+1));
    }
    // printf("Got filepath %s\n", filepath);
    // printf("Got filename %s\n", filename);
    if(NULL == opendir(filepath)) {
        mkdir(filepath,0775);
    }
    if (level > 0 && level <= 9) {
        ctx->log_level = level;
    } else {
        ctx->log_level = 7;
    }
    ctx->buffers = rb_alloc(CACHE_BUF_SIZE, CACHE_BUF_SIZE);// 5k*5k = 25M
    pthread_t lth;
    pthread_create(&ctx->thread_handle, NULL, logs_output_thread, (void *)ctx);

    if (handle) {
        handle->ctx = ctx;
    }
    
    return 0;
}

int logs_print(void *log_ctx, int level, const char *fmt, ...) {
    LogsContext *ctx = (LogsContext *)log_ctx;
    if (log_ctx == NULL) {
        ctx = &logs_ctx_global;
    }
    if (ctx == NULL || level > ctx->log_level) {
        return 0;
    }
    if (ctx->inited <= 0) {
        return 0;
    }
    char buffer[5120] = {0};
    va_list ap;
    va_start(ap, fmt);
    int len = vsnprintf(buffer, sizeof(buffer), fmt, ap);
    va_end(ap);
    if (len > sizeof(buffer) - 6) {
        buffer[5119] = 0;
        buffer[5118] = '\n';
        buffer[5117] = '.';
        buffer[5116] = '.';
        buffer[5115] = '.';
    } else if (ctx->drop_cnt > 0) {
        char droped[512] = {0};
        snprintf(droped, sizeof(droped), " droped %d\n", ctx->drop_cnt);
        strcat(buffer, droped);
    }
    if (rb_buf_size(ctx->buffers) > 0) {
        rb_push_data(ctx->buffers, buffer, len);
    } else {
        ctx->drop_cnt++;
    }
    
    return 0;
}


#define TEST_CASE_MAIN          1
#if TEST_CASE_MAIN
#endif