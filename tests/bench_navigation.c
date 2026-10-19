#include "bench_process.h"
int bench_tracklist_workload(void);
int main(void)
{
    FILE *raw=NULL; if (fopen_s(&raw,"bench-navigation.jsonl","w")) { return 1; }
    ProcessMetrics first,last; CpuMetrics cpu;
    bool ok=bench_identity(raw)&&metrics_process(GetCurrentProcess(),&first);
    if (ok) { bench_sample(raw,"10k_filter_scroll",&first,NULL); }
    int result=bench_tracklist_workload();
    if (ok && metrics_process(GetCurrentProcess(),&last) && metrics_cpu(&first,&last,&cpu)) {
        bench_sample(raw,"10k_filter_scroll",&last,&cpu);
        fprintf(raw,"{\"type\":\"gate\",\"filter_layout_exit\":%d,\"scope\":\"10k CPU model; GPU measured separately\"}\n",result);
    } else { ok=false; }
    ok=fclose(raw)==0&&ok; return ok&&result==0 ? 0 : 1;
}
