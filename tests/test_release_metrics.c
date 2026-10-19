#include "core/metrics.h"
#include "support/test_check.h"
#include <math.h>

static void cpu(void)
{
    ProcessMetrics a={.frequency=10000000,.processors=12,.qpc=1};
    ProcessMetrics b=a; b.qpc+=10000000; b.user_100ns=50000; b.kernel_100ns=10000;
    CpuMetrics c;
    CHECK(metrics_cpu(&a,&b,&c) && c.elapsed_seconds==1);
    CHECK(fabs(c.core_percent-0.6)<0.000001 && fabs(c.machine_percent-0.05)<0.000001);
    CHECK(c.user_100ns==50000 && c.kernel_100ns==10000);
    b.qpc=a.qpc; CHECK(!metrics_cpu(&a,&b,&c));
    b.qpc=0; CHECK(!metrics_cpu(&a,&b,&c));
    b.qpc=10000001; b.processors=0; CHECK(!metrics_cpu(&a,&b,&c));
    b.processors=12; b.frequency=1; CHECK(!metrics_cpu(&a,&b,&c));
    b.frequency=a.frequency; a.user_100ns=50001; CHECK(!metrics_cpu(&a,&b,&c));
    CHECK(!metrics_cpu(NULL,&b,&c) && !metrics_cpu(&a,&b,NULL));
    ProcessMetrics p;
    CHECK(!metrics_process(NULL,&p) && !metrics_process(GetCurrentProcess(),NULL));
    CHECK(metrics_process(GetCurrentProcess(),&p));
    CHECK(p.resident && p.peak_resident>=p.resident && p.private_resident<=p.resident);
    CHECK(p.private_commit && p.handles && p.processors && p.frequency);
}

static void gate(void)
{
    ReleaseMeasurement m={.peak_resident=19999999,.resident_limit=20000000,
        .executable_bytes=1999999,.startup_ms=29.999,.machine_cpu_percent=0.049999,
        .process_valid=true,.cpu_valid=true,.idle=true,.cold_verified=true,.live_verified=true};
    CHECK(metrics_release_failures(&m)==0);
    m.peak_resident=20000000; CHECK(metrics_release_failures(&m)==METRICS_FAIL_RESIDENT);
    m.peak_resident=1; m.executable_bytes=2000000; CHECK(metrics_release_failures(&m)==METRICS_FAIL_SIZE);
    m.executable_bytes=1; m.startup_ms=30; CHECK(metrics_release_failures(&m)==METRICS_FAIL_STARTUP);
    m.startup_ms=NAN; CHECK(metrics_release_failures(&m)==METRICS_FAIL_STARTUP);
    m.startup_ms=1; m.machine_cpu_percent=0.05; CHECK(metrics_release_failures(&m)==METRICS_FAIL_CPU);
    m.machine_cpu_percent=NAN; CHECK(metrics_release_failures(&m)==METRICS_FAIL_CPU);
    m.machine_cpu_percent=-1; CHECK(metrics_release_failures(&m)==METRICS_FAIL_CPU);
    m.machine_cpu_percent=0; m.idle_presents=1; CHECK(metrics_release_failures(&m)==METRICS_FAIL_IDLE);
    m.idle_presents=0; m.idle_wakes=1; CHECK(metrics_release_failures(&m)==METRICS_FAIL_IDLE);
    m.idle_wakes=0; m.idle_timeouts=1; CHECK(metrics_release_failures(&m)==METRICS_FAIL_IDLE);
    m.idle_timeouts=0; m.live_verified=false; m.cold_verified=false;
    CHECK(metrics_release_failures(&m)==(METRICS_FAIL_STARTUP|METRICS_FAIL_EVIDENCE));
    CHECK(metrics_release_failures(NULL)==METRICS_FAIL_EVIDENCE);
}
int main(void)
{
    DWORD before,after; CHECK(GetProcessHandleCount(GetCurrentProcess(),&before));
    cpu(); gate();
    CHECK(GetProcessHandleCount(GetCurrentProcess(),&after) && before==after);
    puts("process counters, CPU normalization, strict thresholds and missing evidence passed"); return 0;
}
