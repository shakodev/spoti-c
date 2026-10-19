#include "bench_process.h"
#include <inttypes.h>

static bool interval(FILE *raw,BenchProcess *process,const char *name)
{
    Sleep(3000); /* Harness warmup, never the application event loop. */
    BenchSnapshot first,last; ProcessMetrics memory; CpuMetrics cpu;
    if (!bench_snapshot(process,&first)) { return false; }
    bench_ui(raw,name,&first);
    if (!bench_interval(raw,process,name,10,&memory,&cpu) || !bench_snapshot(process,&last)) { return false; }
    bench_ui(raw,name,&last);
    /* Ending diagnostic is a single explicitly induced wake. */
    uint64_t wakes=last.window.wakes-first.window.wakes;
    bool quiet=last.renderer.presents==first.renderer.presents && wakes==1 &&
        last.window.timeouts==first.window.timeouts && last.window.last_wait_ms==INFINITE &&
        last.accounting.render_count==first.accounting.render_count &&
        last.player_state==first.player_state && last.player_error==0;
    bool budget=memory.peak_resident<20000000 && cpu.machine_percent<0.05;
    fprintf(raw,"{\"type\":\"idle_gate\",\"phase\":\"%s\",\"diagnostic_wakes\":1,"
        "\"observed_wakes\":%" PRIu64 ",\"input_contaminated\":%s,\"quiet\":%s,\"machine_percent\":%.9f,\"budget\":%s}\n",
        name,wakes,last.input_messages!=first.input_messages ? "true" : "false",
        quiet ? "true" : "false",cpu.machine_percent,budget ? "true" : "false");
    return quiet&&budget;
}
int wmain(int argc,wchar_t **argv)
{
    if (argc!=2) { return 1; }
    FILE *raw=NULL; if (fopen_s(&raw,"bench-idle.jsonl","w")) { return 1; }
    BenchProcess process={0}; bool ok=bench_identity(raw)&&bench_launch(&process,argv[1],true);
    if (ok) {
        bool idle=interval(raw,&process,"idle");
        bool loaded=bench_command(&process,BENCH_LOAD);
        Sleep(3000);
        BenchSnapshot s; loaded=bench_snapshot(&process,&s)&&s.player_state==PLAYER_PAUSED&&loaded;
        bool paused=loaded&&interval(raw,&process,"paused");
        ok=bench_modules(raw,process.process.dwProcessId)&&idle&&paused;
    }
    bool closed=bench_close(&process); fprintf(raw,"{\"type\":\"shutdown\",\"balanced\":%s}\n",closed ? "true" : "false");
    ok=closed&&ok; ok=fclose(raw)==0&&ok;
    printf("bench_idle: %s (bench-idle.jsonl)\n",ok ? "PASS" : "FAIL"); return ok ? 0 : 1;
}
