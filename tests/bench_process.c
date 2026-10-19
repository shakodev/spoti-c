#include "bench_process.h"
#include <tlhelp32.h>
#include <inttypes.h>
#include <string.h>

static void json_string(FILE *f,const char *s)
{
    fputc('"',f);
    for (;*s;++s) {
        unsigned char c=(unsigned char)*s;
        if (c=='"' || c=='\\') { fputc('\\',f); fputc(c,f); }
        else if (c<32) { fprintf(f,"\\u%04x",(unsigned)c); }
        else { fputc(c,f); }
    }
    fputc('"',f);
}
bool bench_identity(FILE *f)
{
    typedef LONG (WINAPI *Version)(OSVERSIONINFOW *);
    FARPROC address=GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"RtlGetVersion"); Version version;
    if (!address) { return false; } memcpy(&version,&address,sizeof(version));
    OSVERSIONINFOW os={.dwOSVersionInfoSize=sizeof(os)}; MEMORYSTATUSEX memory={.dwLength=sizeof(memory)};
    char machine[128]={0},cpu[256]={0}; DWORD count=sizeof(machine);
    if (version(&os) || !GlobalMemoryStatusEx(&memory) || !GetComputerNameA(machine,&count) ||
        !GetEnvironmentVariableA("PROCESSOR_IDENTIFIER",cpu,sizeof(cpu))) { return false; }
    fprintf(f,"{\"schema\":1,\"type\":\"identity\",\"os\":\"%lu.%lu.%lu\",\"machine\":",
        os.dwMajorVersion,os.dwMinorVersion,os.dwBuildNumber); json_string(f,machine);
    fputs(",\"cpu\":",f); json_string(f,cpu);
    fprintf(f,",\"physical_bytes\":%llu,\"processors\":%lu,\"compiler_msc\":%u,"
        "\"warmup_ms\":3000,\"sample_ms\":1000,\"audio\":\"fake 48000 Hz stereo float32\","
        "\"asan\":false,\"trimmed\":false,\"hidden\":false,\"cpu_limit_percent\":0.05}\n",
        (unsigned long long)memory.ullTotalPhys,GetActiveProcessorCount(ALL_PROCESSOR_GROUPS),(unsigned)_MSC_VER);
    return !ferror(f);
}
static BOOL CALLBACK find_window(HWND window,LPARAM context)
{
    BenchProcess *p=(BenchProcess *)context; DWORD pid=0; GetWindowThreadProcessId(window,&pid);
    if (pid==p->process.dwProcessId && IsWindowVisible(window) && !GetWindow(window,GW_OWNER)) {
        p->window=window; return FALSE;
    }
    return TRUE;
}
bool bench_launch(BenchProcess *p,const wchar_t *exe,bool demo)
{
    *p=(BenchProcess){0}; wchar_t name[100],command[2048];
    if (swprintf_s(name,100,L"Local\\SpoticRelease%lu_%llu",GetCurrentProcessId(),
        (unsigned long long)GetTickCount64())<0) { return false; }
    p->mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,NULL,PAGE_READWRITE,0,sizeof(BenchSnapshot),name);
    if (!p->mapping || GetLastError()==ERROR_ALREADY_EXISTS) { (void)bench_close(p); return false; }
    p->shared=MapViewOfFile(p->mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(BenchSnapshot));
    if (!p->shared) { (void)bench_close(p); return false; }
    memset(p->shared,0,sizeof(BenchSnapshot));
    if (swprintf_s(command,2048,L"\"%ls\" %ls --benchmark=%ls",exe,demo ? L"--demo" : L"",name)<0) {
        (void)bench_close(p); return false;
    }
    STARTUPINFOW si={.cb=sizeof(si)};
    if (!CreateProcessW(exe,command,NULL,NULL,FALSE,0,NULL,NULL,&si,&p->process)) {
        (void)bench_close(p); return false;
    }
    (void)WaitForInputIdle(p->process.hProcess,15000);
    uint64_t until=GetTickCount64()+15000; BenchSnapshot snapshot;
    do {
        EnumWindows(find_window,(LPARAM)p);
        if (p->window && bench_snapshot(p,&snapshot) && snapshot.ready) { return true; }
        if (WaitForSingleObject(p->process.hProcess,50)==WAIT_OBJECT_0) { break; }
    } while (GetTickCount64()<until);
    (void)bench_close(p); return false;
}
bool bench_command(BenchProcess *p,BenchCommand command)
{
    DWORD_PTR result=0;
    return p && p->window && SendMessageTimeoutW(p->window,BENCH_MESSAGE,(WPARAM)command,0,
        SMTO_ABORTIFHUNG|SMTO_BLOCK,10000,&result) && result==1;
}
bool bench_snapshot(BenchProcess *p,BenchSnapshot *out)
{
    if (!out || !bench_command(p,BENCH_SNAPSHOT)) { return false; }
    *out=*p->shared; return out->version==BENCH_VERSION;
}
bool bench_close(BenchProcess *p)
{
    bool ok=true;
    if (p->process.hProcess) {
        if (p->window) { (void)PostMessageW(p->window,WM_CLOSE,0,0); }
        if (WaitForSingleObject(p->process.hProcess,15000)!=WAIT_OBJECT_0) {
            (void)TerminateProcess(p->process.hProcess,1); (void)WaitForSingleObject(p->process.hProcess,5000); ok=false;
        }
        DWORD code=1; ok=GetExitCodeProcess(p->process.hProcess,&code)&&code==0&&ok;
        ok=p->shared&&p->shared->balanced&&ok;
        CloseHandle(p->process.hThread); CloseHandle(p->process.hProcess);
    }
    if (p->shared) { UnmapViewOfFile(p->shared); }
    if (p->mapping) { CloseHandle(p->mapping); }
    *p=(BenchProcess){0}; return ok;
}
bool bench_modules(FILE *f,DWORD pid)
{
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,pid);
    if (snapshot==INVALID_HANDLE_VALUE) { return false; }
    MODULEENTRY32W entry={.dwSize=sizeof(entry)}; bool ok=Module32FirstW(snapshot,&entry)!=FALSE;
    if (ok) { do {
        char name[2048]; int length=WideCharToMultiByte(CP_UTF8,0,entry.szExePath,-1,name,sizeof(name),NULL,NULL);
        if (!length) { ok=false; break; }
        fputs("{\"type\":\"loaded_module\",\"path\":",f); json_string(f,name);
        fprintf(f,",\"image_virtual_bytes\":%lu}\n",entry.modBaseSize);
    } while (Module32NextW(snapshot,&entry));
        if (GetLastError()!=ERROR_NO_MORE_FILES) { ok=false; }
    }
    CloseHandle(snapshot); return ok&&!ferror(f);
}
void bench_sample(FILE *f,const char *phase,const ProcessMetrics *m,const CpuMetrics *c)
{
    fprintf(f,"{\"type\":\"sample\",\"phase\":\"%s\",\"qpc\":%" PRIu64 ",\"frequency\":%" PRIu64
        ",\"resident\":%" PRIu64 ",\"peak_resident\":%" PRIu64 ",\"private_resident\":%" PRIu64
        ",\"private_commit\":%" PRIu64 ",\"kernel_100ns\":%" PRIu64 ",\"user_100ns\":%" PRIu64
        ",\"handles\":%u,\"elapsed_s\":%.9f,\"core_percent\":%.9f,\"machine_percent\":%.9f}\n",
        phase,m->qpc,m->frequency,m->resident,m->peak_resident,m->private_resident,m->private_commit,
        m->kernel_100ns,m->user_100ns,m->handles,c ? c->elapsed_seconds : 0,
        c ? c->core_percent : 0,c ? c->machine_percent : 0); fflush(f);
}
void bench_ui(FILE *f,const char *phase,const BenchSnapshot *s)
{
    char adapter[512]={0}; (void)WideCharToMultiByte(CP_UTF8,0,s->adapter,-1,adapter,sizeof(adapter),NULL,NULL);
    fprintf(f,"{\"type\":\"ui\",\"phase\":\"%s\",\"ready\":%u,\"player_state\":%u,"
        "\"player_error\":%u,\"input_messages\":%" PRIu64 ",\"worker_events\":%" PRIu64 ","
        "\"wakes\":%" PRIu64 ",\"timeouts\":%" PRIu64 ",\"wait_ms\":%u,\"renders\":%" PRIu64
        ",\"presents\":%" PRIu64 ",\"arena_high_water\":%" PRIu64 ",\"audio_arena_high_water\":%" PRIu64
        ",\"owned_gpu_logical\":%" PRIu64 ",\"swapchain_estimate\":%" PRIu64
        ",\"gpu_valid\":%u,\"gpu_local\":%" PRIu64 ",\"gpu_nonlocal\":%" PRIu64
        ",\"audio_frames\":%" PRIu64 ",\"underruns\":%" PRIu64 ",\"driver_version\":%" PRIu64
        ",\"gpu_vendor\":%u,\"gpu_device\":%u,\"adapter\":",
        phase,s->ready,s->player_state,s->player_error,s->input_messages,s->window.worker_events,
        s->window.wakes,s->window.timeouts,s->window.last_wait_ms,
        s->accounting.render_count,s->renderer.presents,s->accounting.allocation_high_water,
        s->audio.arena_high_water,s->renderer.owned_gpu_bytes,s->renderer.system_surface_bytes,
        s->gpu_valid,s->gpu_local,s->gpu_nonlocal,s->audio.frames,s->audio.underruns,
        s->driver_version,s->vendor,s->device); json_string(f,adapter); fputs("}\n",f); fflush(f);
}
bool bench_interval(FILE *f,BenchProcess *p,const char *phase,unsigned seconds,ProcessMetrics *last,CpuMetrics *cpu)
{
    ProcessMetrics first,previous;
    if (!seconds || !metrics_process(p->process.hProcess,&first)) { return false; }
    previous=first; bench_sample(f,phase,&first,NULL);
    for (unsigned i=0;i<seconds;++i) {
        if (WaitForSingleObject(p->process.hProcess,1000)!=WAIT_TIMEOUT ||
            !metrics_process(p->process.hProcess,last)) { return false; }
        CpuMetrics interval;
        if (!metrics_cpu(&previous,last,&interval)) { return false; }
        bench_sample(f,phase,last,&interval); previous=*last;
    }
    return metrics_cpu(&first,last,cpu);
}
uint64_t bench_file_size(const wchar_t *path)
{
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExW(path,GetFileExInfoStandard,&data)) { return 0; }
    return ((uint64_t)data.nFileSizeHigh<<32)|data.nFileSizeLow;
}
int bench_child(const wchar_t *exe,const wchar_t *arguments,DWORD timeout)
{
    wchar_t command[2048]; if (swprintf_s(command,2048,L"\"%ls\" %ls",exe,arguments)<0) { return 1; }
    STARTUPINFOW si={.cb=sizeof(si)}; PROCESS_INFORMATION pi={0};
    if (!CreateProcessW(exe,command,NULL,NULL,FALSE,0,NULL,NULL,&si,&pi)) { return 1; }
    DWORD result=1;
    if (WaitForSingleObject(pi.hProcess,timeout)==WAIT_OBJECT_0) { (void)GetExitCodeProcess(pi.hProcess,&result); }
    else { (void)TerminateProcess(pi.hProcess,1); (void)WaitForSingleObject(pi.hProcess,5000); }
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess); return (int)result;
}
