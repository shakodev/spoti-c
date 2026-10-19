#include "ui/startup.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int compare(const void *a,const void *b)
{ double x=*(const double *)a,y=*(const double *)b; return (x>y)-(x<y); }
int wmain(int argc,wchar_t **argv)
{
    if (argc!=2) { fputs("usage: bench_startup path-to-spotic.exe\n",stderr); return 1; }
    (void)CreateDirectoryW(L".cache",NULL);
    static StateCache cache; state_cache_defaults(&cache,STARTUP_EPOCH); cache.route=APP_LIBRARY; cache.count=2;
    cache.entities[0]=(StateCacheEntity){.kind=COLLECTION_ALBUM,.id={7},.label="Offline benchmark album"};
    cache.entities[1]=(StateCacheEntity){.kind=COLLECTION_PLAYLIST_ITEM,.id={9},.label="Offline benchmark playlist"};
    if (state_cache_save(L".cache/startup-bench.state",&cache,NULL,NULL)!=STATE_CACHE_OK) { return 1; }
    puts("condition=warm-file-cache; cache=valid; offline; actual DX11 app; 1 uncounted warmup + 20 samples");
    puts("OS-cold=unmeasured; requires separate controlled boot/cache conditions; no cache eviction or prefetch service used");
    double samples[20]; unsigned misses=0;
    for (unsigned i=0;i<21;++i) {
        wchar_t name[100],command[1024];
        (void)swprintf(name,100,L"Local\\SpoticStartup%lu_%u",GetCurrentProcessId(),i);
        HANDLE mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,NULL,PAGE_READWRITE,0,sizeof(StartupTimes),name);
        if (!mapping) { return 1; }
        StartupTimes *times=MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(*times));
        if (!times) { CloseHandle(mapping); return 1; } memset(times,0,sizeof(*times));
        (void)swprintf(command,1024,L"\"%ls\" --startup-probe=%ls",argv[1],name);
        STARTUPINFOW si={.cb=sizeof(si)}; PROCESS_INFORMATION pi={0};
        if (!CreateProcessW(NULL,command,NULL,NULL,FALSE,0,NULL,NULL,&si,&pi)) { UnmapViewOfFile(times); CloseHandle(mapping); return 1; }
        DWORD wait=WaitForSingleObject(pi.hProcess,15000),exit_code=1;
        if (wait!=WAIT_OBJECT_0) { (void)TerminateProcess(pi.hProcess,1); (void)WaitForSingleObject(pi.hProcess,5000); }
        else { (void)GetExitCodeProcess(pi.hProcess,&exit_code); }
        StartupTimes result=*times;
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess); UnmapViewOfFile(times); CloseHandle(mapping);
        if (exit_code || !result.useful || !result.frequency || !result.ticks[STARTUP_PROCESS] ||
            result.ticks[STARTUP_MAIN]<result.ticks[STARTUP_PROCESS] ||
            result.ticks[STARTUP_WINDOW]<result.ticks[STARTUP_MAIN] ||
            result.ticks[STARTUP_PRESENT]<result.ticks[STARTUP_WINDOW] || result.cache_result!=STATE_CACHE_OK) {
            fprintf(stderr,"sample %u failed: exit=%lu useful=%u cache=%u\n",i,exit_code,result.useful,result.cache_result); return 1;
        }
        double present=startup_ms(&result,STARTUP_PRESENT);
        printf("sample=%u warmup=%u creation_qpc=%llu frequency=%llu WinMain_ms=%.3f window_ms=%.3f first_present_ms=%.3f services_ms=%.3f online_ready_ms=%.3f target=%s\n",
            i,i==0,(unsigned long long)result.ticks[0],(unsigned long long)result.frequency,
            startup_ms(&result,STARTUP_MAIN),startup_ms(&result,STARTUP_WINDOW),present,
            startup_ms(&result,STARTUP_SERVICES),startup_ms(&result,STARTUP_ONLINE),present<30 ? "PASS" : "MISS");
        if (i) { samples[i-1]=present; if (present>=30) { ++misses; } }
    }
    qsort(samples,20,sizeof(samples[0]),compare);
    printf("process creation -> useful first present: p50=%.3f ms p95=%.3f ms target_misses=%u/20; online-ready=unavailable (offline)\n",
        samples[9],samples[18],misses);
    if (!DeleteFileW(L".cache/startup-bench.state")) { return 1; } return 0;
}
