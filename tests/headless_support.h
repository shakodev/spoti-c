#ifndef SPOTIC_HEADLESS_TEST_SUPPORT_H
#define SPOTIC_HEADLESS_TEST_SUPPORT_H
#include "headless/fixture_server.h"
#include "headless/headless.h"
#include "support/test_check.h"
typedef struct HeadlessFixture {
    HeadlessServer server;
    wchar_t cache[240];
    char url[128];
} HeadlessFixture;
void hf_open(HeadlessFixture *);
void hf_close(HeadlessFixture *);
void hf_load(Headless *, bool play);
void hf_wait(Headless *, PlayerState);
#endif
