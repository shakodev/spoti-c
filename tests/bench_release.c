#include "bench_process.h"
#include <inttypes.h>
#include <string.h>

static bool sibling(wchar_t *out,size_t capacity,const wchar_t *app,const wchar_t *name)
{
    if (wcscpy_s(out,capacity,app)) { return false; }
    wchar_t *slash=wcsrchr(out,L'\\'),*forward=wcsrchr(out,L'/');
    if (!slash || (forward && forward>slash)) { slash=forward; }
    if (!slash) { return wcscpy_s(out,capacity,name)==0; }
    slash[1]=0; return wcscat_s(out,capacity,name)==0;
}
static bool workload(FILE *raw,BenchProcess *process,const char *name,BenchCommand command,unsigned repeats)
{
    ProcessMetrics first,last; CpuMetrics cpu; BenchSnapshot ui;
    if (!metrics_process(process->process.hProcess,&first)) { return false; }
    bench_sample(raw,name,&first,NULL);
    bool ok=true;
    for (unsigned i=0;i<repeats;++i) {
        if (command==BENCH_SCROLL) {
            for (unsigned frame=0;frame<60;++frame) {
                ok=bench_command(process,command)&&ok;
                if (WaitForSingleObject(process->process.hProcess,16)!=WAIT_TIMEOUT) { return false; }
            }
            if (!metrics_process(process->process.hProcess,&last) || !metrics_cpu(&first,&last,&cpu)) { return false; }
            bench_sample(raw,name,&last,&cpu);
        } else {
            ok=bench_command(process,command)&&ok;
            if (!bench_interval(raw,process,name,1,&last,&cpu)) { return false; }
        }
    }
    if (!bench_snapshot(process,&ui) || !metrics_cpu(&first,&last,&cpu)) { return false; }
    bench_ui(raw,name,&ui);
    fprintf(raw,"{\"type\":\"workload\",\"phase\":\"%s\",\"actions\":%u,\"elapsed_s\":%.9f,"
        "\"machine_percent\":%.9f,\"healthy\":%s}\n",name,repeats,cpu.elapsed_seconds,cpu.machine_percent,
        ok&&ui.ready&&ui.player_state!=PLAYER_ERROR ? "true" : "false");
    return ok&&ui.ready&&ui.player_state!=PLAYER_ERROR&&last.peak_resident<20000000;
}
int wmain(int argc,wchar_t **argv)
{
    if (argc!=2) { return 1; }
    FILE *raw=NULL; if (fopen_s(&raw,"bench-release.jsonl","w")) { return 1; }
    bool ok=bench_identity(raw); BenchProcess process={0}; BenchSnapshot ui={0};
    ProcessMetrics memory={0}; CpuMetrics cpu={0}; bool valid=false,measured=false;
    uint64_t executable=bench_file_size(argv[1]);
    if (bench_launch(&process,argv[1],true)) {
        Sleep(3000);
        if (bench_snapshot(&process,&ui)) {
            bench_ui(raw,"startup",&ui);
            fprintf(raw,"{\"type\":\"startup\",\"condition\":\"uncontrolled OS cache; offline snapshot\","
                "\"cold_verified\":false,\"useful_ms\":%.6f,\"exe_bytes\":%" PRIu64 "}\n",
                startup_ms(&ui.startup,STARTUP_PRESENT),executable);
        }
        bool loaded=bench_command(&process,BENCH_LOAD); Sleep(3000);
        bool play=loaded&&bench_command(&process,BENCH_PLAY); Sleep(3000);
        BenchSnapshot before;
        bool started=bench_snapshot(&process,&before)&&before.player_state==PLAYER_PLAYING;
        measured=bench_interval(raw,&process,"playing_gapless",30,&memory,&cpu);
        valid=bench_snapshot(&process,&ui)&&started&&ui.player_state==PLAYER_PLAYING&&
            ui.audio.frames>before.audio.frames+UINT64_C(48000)*29&&ui.audio.underruns==before.audio.underruns;
        bench_ui(raw,"playing_gapless",&ui);
        /* Do not stop collecting when a threshold has already failed. */
        bool seek=workload(raw,&process,"seek",BENCH_SEEK,6);
        bool scroll=workload(raw,&process,"scroll",BENCH_SCROLL,10);
        bool device=workload(raw,&process,"device_loss_injected",BENCH_DEVICE_LOSS,1);
        ok=bench_modules(raw,process.process.dwProcessId)&&seek&&scroll&&device&&play&&ok;
        valid=metrics_process(process.process.hProcess,&memory)&&ui.gpu_valid&&valid;
        if (valid) { bench_sample(raw,"after_workloads",&memory,NULL); }
    } else { fputs("{\"type\":\"failure\",\"reason\":\"visible app launch failed\"}\n",raw); ok=false; }
    ReleaseMeasurement m={.peak_resident=memory.peak_resident,.resident_limit=20000000,
        .executable_bytes=executable,.startup_ms=startup_ms(&ui.startup,STARTUP_PRESENT),
        .machine_cpu_percent=cpu.machine_percent,.process_valid=valid,.cpu_valid=measured,
        .cold_verified=false,.live_verified=false};
    uint32_t failures=metrics_release_failures(&m);
    bool closed=bench_close(&process);
    fprintf(raw,"{\"type\":\"gate\",\"failures\":%u,\"resident_limit\":20000000,"
        "\"exe_limit\":2000000,\"startup_limit_ms\":30,\"cpu_limit_percent\":0.05,"
        "\"cold_start\":\"unverified\",\"account_WASAPI_Connect\":\"unverified\","
        "\"physical_device_loss\":\"unverified\",\"balanced\":%s}\n",failures,closed ? "true" : "false");
    ok=closed&&!failures&&ok; fflush(raw);
    /* Separate processes retain their own complete residency, no subtraction.
     * These fixture regressions do not establish live/hardware compatibility. */
    const wchar_t *names[]={L"test_gapless.exe",L"test_player.exe",L"test_artwork.exe",L"bench_headless.exe"};
    for (unsigned i=0;i<4;++i) {
        wchar_t path[1024]; int result=1;
        if (sibling(path,1024,argv[1],names[i])) { result=bench_child(path,L"",i==3 ? 660000 : 90000); }
        fprintf(raw,"{\"type\":\"fixture_regression\",\"name\":\"%ls\",\"exit\":%d}\n",names[i],result);
        fflush(raw); ok=result==0&&ok;
    }
    ok=fclose(raw)==0&&ok;
    puts("bench_release: raw results in bench-release.jsonl and headless-metrics.jsonl");
    puts("Cold boot and account/WASAPI/Connect acceptance require separate evidence; this run cannot certify them.");
    return ok ? 0 : 1;
}
