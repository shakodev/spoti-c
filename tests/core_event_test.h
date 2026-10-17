#ifndef SPOTIC_TEST_CORE_EVENT_H
#define SPOTIC_TEST_CORE_EVENT_H

#include "core/worker.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

void test_heap_watch(bool enabled);
void test_workers(void);

#endif
