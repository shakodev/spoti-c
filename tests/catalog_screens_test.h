#ifndef CATALOG_SCREENS_TEST_H
#define CATALOG_SCREENS_TEST_H
#include "ui/app_fixture.h"
#include "support/test_check.h"
#include <string.h>
extern App cs_app;
extern AppFixture cs_host;
extern UiScheduler cs_scheduler;
extern PlayerSnapshot cs_player;
extern PlayerCommand cs_command;
extern AppRequest cs_latest;
extern unsigned cs_requests, cs_cancels, cs_commands;
extern uint64_t cs_now;
void cs_open(bool asynchronous);
void cs_close(void);
void cs_close_visual(void); /* GPU/DirectWrite process caches are outside handle baseline. */
void cs_tick(uint64_t);
void cs_settle(void);
void cs_action(AppActionType, double);
void cs_query(const char *);
Clay_RenderCommandArray cs_layout(float, float, float);
void cs_response(const char *, unsigned);
void cs_fixture(const char *);
void cs_faults(void);
int cs_visual(void);
#endif
