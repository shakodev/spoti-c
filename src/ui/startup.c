#include "startup.h"
#include <string.h>

static uint64_t filetime_ticks(FILETIME t) { return (uint64_t)t.dwLowDateTime|(uint64_t)t.dwHighDateTime<<32; }
void startup_mark(StartupTimes *t,StartupMilestone m)
{
    if (!t || m<STARTUP_PROCESS || m>=STARTUP_MILESTONES || t->ticks[m]) { return; }
    LARGE_INTEGER q; QueryPerformanceCounter(&q); t->ticks[m]=(uint64_t)q.QuadPart;
}
double startup_ms(const StartupTimes *t,StartupMilestone m)
{
    if (!t || !t->frequency || m<STARTUP_PROCESS || m>=STARTUP_MILESTONES || !t->ticks[m] || !t->ticks[0] || t->ticks[m]<t->ticks[0]) { return -1; }
    return (double)(t->ticks[m]-t->ticks[0])*1000/(double)t->frequency;
}
void startup_begin(Startup *s,const wchar_t *path)
{
    if (!s) { return; }
    memset(s,0,sizeof(*s)); LARGE_INTEGER frequency; QueryPerformanceFrequency(&frequency);
    s->times.frequency=(uint64_t)frequency.QuadPart; startup_mark(&s->times,STARTUP_MAIN);
    FILETIME created,exit_time,kernel,user,now; GetSystemTimePreciseAsFileTime(&now);
    LARGE_INTEGER q; QueryPerformanceCounter(&q);
    if (GetProcessTimes(GetCurrentProcess(),&created,&exit_time,&kernel,&user)) {
        uint64_t age=filetime_ticks(now)>=filetime_ticks(created) ? filetime_ticks(now)-filetime_ticks(created) : 0;
        uint64_t delta=age/10000000*s->times.frequency+(age%10000000)*s->times.frequency/10000000;
        if (delta<(uint64_t)q.QuadPart) { s->times.ticks[STARTUP_PROCESS]=(uint64_t)q.QuadPart-delta; }
    }
    s->save_result=STATE_CACHE_MISSING;
    if (path && wcslen(path)<240) { wcscpy_s(s->path,240,path); }
    s->times.cache_result=(uint32_t)state_cache_load(s->path,STARTUP_EPOCH,&s->restored);
}
static bool allow_commit(void *context) { return !worker_cancelled(*(const CancelToken **)context); }
static void save_run(const Event *event,const CancelToken *cancel,void *context)
{
    (void)event; Startup *s=context;
    StateCacheResult result=worker_cancelled(cancel) ? STATE_CACHE_CANCELLED : state_cache_save(s->path,&s->pending,allow_commit,&cancel);
    AcquireSRWLockExclusive(&s->lock); s->save_result=result; ReleaseSRWLockExclusive(&s->lock);
}
static void save_done(const Event *event,WorkerOutcome outcome,void *context)
{
    (void)event; Startup *s=context; AcquireSRWLockExclusive(&s->lock);
    if (outcome!=WORKER_DONE) { s->save_result=STATE_CACHE_CANCELLED; }
    s->busy=false; ReleaseSRWLockExclusive(&s->lock);
}
StateCacheResult startup_writer_start(Startup *s,Arena *arena)
{
    if (!s || s->worker || !s->path[0]) { return STATE_CACHE_INVALID; }
    return worker_start(arena,1,(CoreClock){0},&s->worker)==CORE_OK ? STATE_CACHE_OK : STATE_CACHE_IO;
}
StateCacheResult startup_save(Startup *s,const StateCache *snapshot)
{
    if (!s || !snapshot || !s->worker) { return STATE_CACHE_INVALID; }
    AcquireSRWLockExclusive(&s->lock);
    if (s->busy) { ReleaseSRWLockExclusive(&s->lock); return STATE_CACHE_FULL; }
    s->pending=*snapshot; s->busy=true;
    WorkerJob job={.run=save_run,.complete=save_done,.context=s,.deadline=CORE_CLOCK_NEVER,.priority=WORKER_DISK};
    uint64_t ticket; CoreResult result=worker_submit(s->worker,&job,&ticket);
    if (result!=CORE_OK) { s->busy=false; s->save_result=result==CORE_FULL ? STATE_CACHE_FULL : STATE_CACHE_IO; }
    ReleaseSRWLockExclusive(&s->lock); return result==CORE_OK ? STATE_CACHE_OK : result==CORE_FULL ? STATE_CACHE_FULL : STATE_CACHE_IO;
}
StateCacheResult startup_save_status(Startup *s,bool *busy)
{
    if (!s || !busy) { return STATE_CACHE_INVALID; }
    AcquireSRWLockShared(&s->lock); *busy=s->busy; StateCacheResult r=s->save_result; ReleaseSRWLockShared(&s->lock); return r;
}
StateCacheResult startup_writer_close(Startup *s,DWORD timeout)
{
    if (!s || !s->worker) { return STATE_CACHE_OK; }
    if (WaitForSingleObject(worker_idle_handle(s->worker),timeout)!=WAIT_OBJECT_0) { return STATE_CACHE_FULL; }
    if (worker_stop(s->worker)!=CORE_OK || worker_join(s->worker,timeout)!=CORE_OK) { return STATE_CACHE_IO; }
    s->worker=NULL; return s->save_result;
}
