/**
 * debug.h
*/
#include "logs.h"

#define LELVE_FATAL          1
#define LELVE_ERR            2
#define LELVE_WARN           3
#define LELVE_INFO           4
#define LELVE_VERB           5
#define LELVE_HUGE           6
#define LELVE_DBG            7
#define LELVE_DBGLV          8
#define LELVE_DBGLLV         9
#define LELVE_ALWAYS         0

#define __GNU_SOURCE
#include <unistd.h>
#include <sys/syscall.h>

#define LOGRECORD(x, format, ...) \
{ \
    int levelx = x; \
    logs_print(NULL, x, format, ##__VA_ARGS__); \
}

#define LogRecord       LOGRECORD

// not avilable for c99, __VA_ARGS__ -> ##__VA_ARGS__
#define LOGERR(format, ...)     LOGRECORD(LELVE_ERR, format, ##__VA_ARGS__)
#define LOGDBG(format, ...)      LOGRECORD(LELVE_DBG, format, ##__VA_ARGS__)
