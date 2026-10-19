#ifndef SPOTIC_BENCH_PROCESS_H
#define SPOTIC_BENCH_PROCESS_H
#include "ui/benchmark.h"
#include <stdio.h>
typedef struct BenchProcess {
    PROCESS_INFORMATION process;
    HANDLE mapping;
    BenchSnapshot *shared;
    HWND window;
} BenchProcess;
bool bench_launch(BenchProcess *,const wchar_t *exe,bool demo);
bool bench_command(BenchProcess *,BenchCommand);
bool bench_snapshot(BenchProcess *,BenchSnapshot *);
bool bench_close(BenchProcess *);
bool bench_identity(FILE *);
bool bench_modules(FILE *,DWORD pid);
void bench_sample(FILE *,const char *,const ProcessMetrics *,const CpuMetrics *);
void bench_ui(FILE *,const char *,const BenchSnapshot *);
bool bench_interval(FILE *,BenchProcess *,const char *,unsigned seconds,ProcessMetrics *,CpuMetrics *);
uint64_t bench_file_size(const wchar_t *);
int bench_child(const wchar_t *exe,const wchar_t *arguments,DWORD timeout);
#endif
