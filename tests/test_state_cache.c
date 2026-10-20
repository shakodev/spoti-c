#include "ui/startup.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while (0)
#include "support/ui_heap_watch.h"
static StateCache source,decoded;
static uint8_t wire[STATE_CACHE_WIRE_MAX],mutated[STATE_CACHE_WIRE_MAX];
static size_t bytes;
static wchar_t path[160],ready_name[100];
static void put(uint8_t *p,uint32_t n) { for (unsigned i=0;i<4;++i) { p[i]=(uint8_t)(n>>(8*i)); } }
static void repair(uint8_t *p,size_t n)
{
    memset(p+16,0,4); uint32_t crc=UINT32_MAX;
    for (size_t i=0;i<n;++i) {
        crc^=p[i]; for (unsigned j=0;j<8;++j) { crc=(crc>>1)^(0xedb88320u & (0u-(crc&1))); }
    }
    put(p+16,~crc);
}
static void defaults(void)
{ CHECK(decoded.epoch==42 && decoded.width==1100 && decoded.volume==1000 && !decoded.count && !decoded.route_id[0]); }
static void roundtrip(void)
{
    state_cache_defaults(&source,42); source.x=-1200; source.y=50; source.route=APP_LIBRARY;
    source.count=2; source.volume=237; source.repeat=2; source.shuffle=1; source.detail=1;
    source.track_id[0]=12; source.route_id[0]=42; source.scroll=48;
    source.entities[0]=(StateCacheEntity){.kind=COLLECTION_ALBUM,.id={7},.label="Synthetic album"};
    source.entities[1]=(StateCacheEntity){.kind=COLLECTION_PLAYLIST_ITEM,.id={9},.label="Saved playlist"};
    CHECK(state_cache_encode(&source,wire,sizeof(wire),&bytes)==STATE_CACHE_OK);
    CHECK(bytes==48+96+2*116 && bytes<STATE_CACHE_MAX_BYTES && wire[0]=='S' && wire[4]==3);
    CHECK(state_cache_decode(wire,bytes,42,&decoded)==STATE_CACHE_OK);
    CHECK(!memcmp(&source,&decoded,sizeof(source)));
    size_t written=123; CHECK(state_cache_encode(&source,mutated,bytes-1,&written)==STATE_CACHE_FULL && !written);
    CHECK(state_cache_decode(wire,bytes,43,&decoded)==STATE_CACHE_STALE);
    CHECK(decoded.volume==237 && decoded.repeat==2 && !decoded.count && !decoded.route && !decoded.track_id[0]);
    memcpy(mutated,wire,bytes); put(mutated+4,1); repair(mutated,bytes);
    CHECK(state_cache_decode(mutated,bytes,42,&decoded)==STATE_CACHE_OK && !decoded.repeat && !decoded.shuffle);
    CHECK(decoded.volume_nonzero==237);
    put(mutated+4,2); repair(mutated,bytes);
    CHECK(state_cache_decode(mutated,bytes,42,&decoded)==STATE_CACHE_OK && decoded.repeat==2 && decoded.volume_nonzero==237);
    put(mutated+48+24,0); repair(mutated,bytes);
    CHECK(state_cache_decode(mutated,bytes,42,&decoded)==STATE_CACHE_OK && !decoded.volume && decoded.volume_nonzero==1000);
    put(mutated+4,99); repair(mutated,bytes);
    CHECK(state_cache_decode(mutated,bytes,42,&decoded)==STATE_CACHE_VERSION); defaults();
    for (size_t n=0;n<bytes;++n) { CHECK(state_cache_decode(wire,n,42,&decoded)==STATE_CACHE_CORRUPT); defaults(); }
    CHECK(state_cache_decode(NULL,0,42,&decoded)==STATE_CACHE_CORRUPT); defaults();
    CHECK(state_cache_decode(wire,STATE_CACHE_MAX_BYTES+1u,42,&decoded)==STATE_CACHE_CORRUPT);
    CHECK(state_cache_decode(wire,bytes,42,NULL)==STATE_CACHE_INVALID);
    StateCache maximum=source; maximum.count=STATE_CACHE_ENTITIES;
    for (unsigned i=0;i<STATE_CACHE_ENTITIES;++i) { maximum.entities[i]=source.entities[i%2]; }
    size_t maximum_bytes;
    CHECK(state_cache_encode(&maximum,mutated,sizeof(mutated),&maximum_bytes)==STATE_CACHE_OK && maximum_bytes==STATE_CACHE_WIRE_MAX);
    CHECK(state_cache_decode(mutated,maximum_bytes,42,&decoded)==STATE_CACHE_OK && decoded.count==STATE_CACHE_ENTITIES);
}
static void malformed(void)
{
    const unsigned fields[]={0,8,12,20,32,36,40,44,48+8,48+12,48+16,48+20,48+24,48+28,48+32,48+36,48+40,48+76,144};
    for (unsigned i=0;i<sizeof(fields)/sizeof(fields[0]);++i) {
        memcpy(mutated,wire,bytes); put(mutated+fields[i],UINT32_MAX); repair(mutated,bytes);
        CHECK(state_cache_decode(mutated,bytes,42,&decoded)==STATE_CACHE_CORRUPT); defaults();
    }
    memcpy(mutated,wire,bytes); memset(mutated+164,'a',96); repair(mutated,bytes);
    CHECK(state_cache_decode(mutated,bytes,42,&decoded)==STATE_CACHE_CORRUPT); defaults();
    mutated[164]=0xff; mutated[165]=0; repair(mutated,bytes);
    CHECK(state_cache_decode(mutated,bytes,42,&decoded)==STATE_CACHE_CORRUPT);
    uint32_t rng=7729;
    for (unsigned i=0;i<10000;++i) {
        memcpy(mutated,wire,bytes); rng=rng*1664525+1013904223;
        size_t at=rng%bytes; mutated[at]^=(uint8_t)(1+(rng>>24)%255);
        CHECK(state_cache_decode(mutated,bytes,42,&decoded)!=STATE_CACHE_OK); defaults();
        if (i%2) { repair(mutated,bytes); }
        StateCacheResult result=state_cache_decode(mutated,bytes,42,&decoded);
        CHECK(result==STATE_CACHE_OK || result==STATE_CACHE_CORRUPT || result==STATE_CACHE_VERSION || result==STATE_CACHE_STALE);
        if (result!=STATE_CACHE_OK && result!=STATE_CACHE_STALE) { defaults(); }
    }
    StateCache invalid=source; invalid.count=129;
    CHECK(state_cache_encode(&invalid,mutated,sizeof(mutated),&bytes)==STATE_CACHE_INVALID);
    CHECK(state_cache_encode(&source,wire,sizeof(wire),&bytes)==STATE_CACHE_OK);
    invalid=source; invalid.volume_nonzero=0;
    CHECK(state_cache_encode(&invalid,mutated,sizeof(mutated),&bytes)==STATE_CACHE_INVALID);
}
typedef struct SaveGate { HANDLE ready,release; bool commit; StateCache state; StateCacheResult result; } SaveGate;
static bool gate(void *context)
{
    SaveGate *g=context; CHECK(SetEvent(g->ready));
    CHECK(WaitForSingleObject(g->release,10000)==WAIT_OBJECT_0); return g->commit;
}
static DWORD WINAPI save_thread(void *context)
{
    SaveGate *g=context; g->result=state_cache_save(path,&g->state,gate,g); return 0;
}
static bool never_commit(void *context) { (void)context; return false; }
static bool interrupted(void *context)
{
    (void)context; HANDLE event=OpenEventW(EVENT_MODIFY_STATE,FALSE,ready_name); CHECK(event!=NULL);
    CHECK(SetEvent(event)); CHECK(CloseHandle(event));
    HANDLE stop=CreateEventW(NULL,TRUE,FALSE,NULL); CHECK(stop!=NULL);
    (void)WaitForSingleObject(stop,INFINITE); CHECK(CloseHandle(stop)); return false;
}
static void disk(void)
{
    CHECK(state_cache_load(path,42,&decoded)==STATE_CACHE_MISSING); defaults();
    CHECK(state_cache_save(path,&source,NULL,NULL)==STATE_CACHE_OK);
    HANDLE locked=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL); CHECK(locked!=INVALID_HANDLE_VALUE);
    CHECK(state_cache_save(path,&source,NULL,NULL)==STATE_CACHE_IO); CHECK(CloseHandle(locked));
    CHECK(state_cache_load(path,42,&decoded)==STATE_CACHE_OK && decoded.volume==237);
    StateCache newer=source; newer.volume=998;
    CHECK(state_cache_save(path,&newer,never_commit,NULL)==STATE_CACHE_CANCELLED);
    CHECK(state_cache_load(path,42,&decoded)==STATE_CACHE_OK && decoded.volume==237);
    SaveGate g={.ready=CreateEventW(NULL,TRUE,FALSE,NULL),.release=CreateEventW(NULL,TRUE,FALSE,NULL),.commit=true,.state=newer};
    CHECK(g.ready && g.release); HANDLE thread=CreateThread(NULL,0,save_thread,&g,0,NULL); CHECK(thread);
    CHECK(WaitForSingleObject(g.ready,10000)==WAIT_OBJECT_0);
    CHECK(state_cache_load(path,42,&decoded)==STATE_CACHE_OK && decoded.volume==237);
    CHECK(state_cache_save(path,&source,NULL,NULL)==STATE_CACHE_OK);
    CHECK(SetEvent(g.release)); CHECK(WaitForSingleObject(thread,10000)==WAIT_OBJECT_0 && g.result==STATE_CACHE_OK);
    CHECK(CloseHandle(thread) && CloseHandle(g.ready) && CloseHandle(g.release));
    CHECK(state_cache_load(path,42,&decoded)==STATE_CACHE_OK && decoded.volume==998);
    /* Kill a separate writer after its flushed temp exists but before rename. */
    wchar_t exe[260],command[400]; CHECK(GetModuleFileNameW(NULL,exe,260)>0);
    HANDLE ready=CreateEventW(NULL,TRUE,FALSE,ready_name); CHECK(ready);
    CHECK(swprintf(command,400,L"\"%ls\" --interrupt %lu",exe,GetCurrentProcessId())>0);
    STARTUPINFOW si={.cb=sizeof(si)}; PROCESS_INFORMATION pi={0};
    CHECK(CreateProcessW(NULL,command,NULL,NULL,FALSE,CREATE_NO_WINDOW,NULL,NULL,&si,&pi));
    CHECK(WaitForSingleObject(ready,10000)==WAIT_OBJECT_0);
    CHECK(TerminateProcess(pi.hProcess,0)); CHECK(WaitForSingleObject(pi.hProcess,10000)==WAIT_OBJECT_0);
    wchar_t orphan[240]; CHECK(swprintf(orphan,240,L"%ls.%lu.1.tmp",path,pi.dwProcessId)>0);
    CHECK(CloseHandle(pi.hThread) && CloseHandle(pi.hProcess) && CloseHandle(ready));
    CHECK(state_cache_load(path,42,&decoded)==STATE_CACHE_OK && decoded.volume==998);
    CHECK(DeleteFileW(orphan));
    HANDLE f=CreateFileW(path,GENERIC_WRITE,0,NULL,TRUNCATE_EXISTING,0,NULL); CHECK(f!=INVALID_HANDLE_VALUE);
    DWORD n; CHECK(WriteFile(f,"bad",3,&n,NULL) && CloseHandle(f));
    CHECK(state_cache_load(path,42,&decoded)==STATE_CACHE_CORRUPT); defaults();
    CHECK(state_cache_save(path,&source,NULL,NULL)==STATE_CACHE_OK);
    CHECK(DeleteFileW(path));
}
static App app;
static _Alignas(64) uint8_t clay[UI_CLAY_BYTES];
static uint8_t scratch[UI_SCRATCH_BYTES],core[MEMORY_BUDGET_CORE];
static Startup start;
static DWORD WINAPI runtime_warmup(void *context) { (void)context; return 0; }
static void blocked_job(const Event *event,const CancelToken *cancel,void *context)
{ (void)event; (void)cancel; (void)context; CHECK(false); }
static void blocked_done(const Event *event,WorkerOutcome outcome,void *context)
{ (void)event; (void)context; CHECK(outcome==WORKER_CANCELLED); }
static unsigned calls;
static AppResult network(void *context,const AppRequest *request) { (void)context; (void)request; ++calls; return APP_PENDING; }
static void shell(void)
{
    AppServices services={.request=network}; UiScheduler scheduler; ui_scheduler_init(&scheduler);
    CHECK(app_init(&app,&services,&scheduler,clay,sizeof(clay),scratch,sizeof(scratch))==APP_OK);
    startup_restore(&app,&source);
    CHECK(!app.model.logged_in && !app.model.online && app.model.status==APP_OFFLINE && !calls);
    CHECK(app.model.location.screen==APP_LIBRARY && app.model.player.state==PLAYER_STOPPED);
    ui_heap_begin();
    Clay_RenderCommandArray commands=app_layout(&app,1100,720,96,(UiWidgetInput){0});
    ui_heap_end(); CHECK(commands.length>0 && !calls);
    bool saw=false;
    for (int32_t i=0;i<commands.length;++i) {
        Clay_RenderCommand *c=&commands.internalArray[i];
        if (c->commandType==CLAY_RENDER_COMMAND_TYPE_TEXT && c->renderData.text.stringContents.length==14 &&
            !memcmp(c->renderData.text.stringContents.chars,"Saved playlist",14)) { saw=true; }
    }
    CHECK(saw); CHECK(app_enqueue(&app,(AppAction){.type=APP_TOGGLE_PLAY})==APP_OK);
    (void)app_update(&app,0); CHECK(!calls && !app.model.logged_in);
    startup_capture(&decoded,&app,NULL); CHECK(decoded.count==2 && decoded.volume==237);
    startup_begin(&start,path); CHECK(start.times.ticks[STARTUP_MAIN] && !start.times.ticks[STARTUP_PRESENT]);
    Arena arena; CHECK(arena_init(&arena,core,sizeof(core)));
    CHECK(startup_writer_start(&start,&arena)==STATE_CACHE_OK);
    WorkerJob blocker={.run=blocked_job,.complete=blocked_done,.not_before=CORE_CLOCK_NEVER,.deadline=CORE_CLOCK_NEVER}; uint64_t ticket;
    CHECK(worker_submit(start.worker,&blocker,&ticket)==CORE_OK);
    CHECK(startup_save(&start,&source)==STATE_CACHE_FULL);
    CHECK(worker_cancel(start.worker,ticket));
    CHECK(WaitForSingleObject(worker_idle_handle(start.worker),10000)==WAIT_OBJECT_0);
    CHECK(startup_save(&start,&source)==STATE_CACHE_OK);
    CHECK(WaitForSingleObject(worker_idle_handle(start.worker),10000)==WAIT_OBJECT_0);
    bool busy=true; CHECK(startup_save_status(&start,&busy)==STATE_CACHE_OK && !busy);
    CHECK(startup_writer_close(&start,10000)==STATE_CACHE_OK && !start.worker);
    arena_dispose(&arena); CHECK(state_cache_load(path,42,&decoded)==STATE_CACHE_OK && decoded.count==2);
    CHECK(startup_ms(&start.times,(StartupMilestone)-1)==-1 && startup_ms(&start.times,STARTUP_ONLINE)==-1);
    startup_mark(&start.times,STARTUP_WINDOW); uint64_t stamp=start.times.ticks[STARTUP_WINDOW];
    startup_mark(&start.times,STARTUP_WINDOW); CHECK(stamp==start.times.ticks[STARTUP_WINDOW]);
    CHECK(startup_ms(&start.times,STARTUP_WINDOW)>=0);
    (void)app_session(&app,true,true,1); CHECK(calls==1);
    (void)app_session(&app,false,false,2); CHECK(!app.cached);
    startup_capture(&decoded,&app,NULL); CHECK(!decoded.count && !decoded.track_id[0]);
    CHECK(DeleteFileW(path)); app_dispose(&app);
}
int wmain(int argc,wchar_t **argv)
{
    if (argc==2 && !wcscmp(argv[1],L"--warmup")) { return 0; }
    unsigned long id=GetCurrentProcessId();
    if (argc==3 && !wcscmp(argv[1],L"--interrupt")) { id=wcstoul(argv[2],NULL,10); }
    CHECK(swprintf(path,160,L"state-cache-test-%lu.bin",id)>0);
    CHECK(swprintf(ready_name,100,L"Local\\SpoticStateCacheTest%lu",id)>0);
    if (argc==3) { state_cache_defaults(&source,42); return (int)state_cache_save(path,&source,interrupted,NULL); }
    /* ASan/CRT initialize process-lifetime thread bookkeeping on first use. */
    HANDLE warm=CreateThread(NULL,0,runtime_warmup,NULL,0,NULL); CHECK(warm);
    CHECK(WaitForSingleObject(warm,10000)==WAIT_OBJECT_0 && CloseHandle(warm));
    /* First CreateProcess also initializes two stable OS/runtime handles. */
    wchar_t exe[260],command[300]; CHECK(GetModuleFileNameW(NULL,exe,260));
    CHECK(swprintf(command,300,L"\"%ls\" --warmup",exe)>0);
    STARTUPINFOW si={.cb=sizeof(si)}; PROCESS_INFORMATION pi={0};
    CHECK(CreateProcessW(NULL,command,NULL,NULL,FALSE,CREATE_NO_WINDOW,NULL,NULL,&si,&pi));
    CHECK(WaitForSingleObject(pi.hProcess,10000)==WAIT_OBJECT_0);
    CHECK(CloseHandle(pi.hThread) && CloseHandle(pi.hProcess));
    DWORD before,after; CHECK(GetProcessHandleCount(GetCurrentProcess(),&before));
    roundtrip(); malformed(); disk(); shell();
    CHECK(GetProcessHandleCount(GetCurrentProcess(),&after) && before==after);
    puts("state cache: codec fuzz, bounds, migration, atomic interruption, concurrent saves, offline shell and worker lifetime passed"); return 0;
}
