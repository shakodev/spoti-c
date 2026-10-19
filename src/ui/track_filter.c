#include "track_filter.h"
#include "core/thread.h"
#include <string.h>

static bool cancelled(const CancelToken *c) { return c && worker_cancelled(c); }
static bool valid(const TrackJob *j)
{
    if (!j || !j->source.read || j->base>j->source.total || j->sort<TRACK_SOURCE_ORDER || j->sort>TRACK_DURATION_ORDER ||
        !memchr(j->query,0,sizeof(j->query)) || !track_utf8(j->query,strlen(j->query)) ||
        !memchr(j->source.revision,0,64) || !j->source.revision[0] || !memchr(j->source.uri,0,192) ||
        (j->metadata && j->count>TRACK_BATCH)) { return false; }
    for (uint32_t i=0;j->metadata && i<j->count;++i) { if (j->rows[i]>=j->source.total) { return false; } }
    return true;
}
static int compare(TrackFilter *f, uint32_t a, uint32_t b)
{
    TrackJob *j=&f->job; uint32_t x=a-j->base, y=b-j->base;
    if (j->sort==TRACK_DURATION_ORDER) {
        uint32_t u=f->index[x].duration_ms, v=f->index[y].duration_ms;
        if (u!=v) { return u<v ? -1 : 1; }
    }
    return a<b ? -1 : a>b;
}
static TrackResult title_order(TrackFilter *f, uint32_t length, unsigned columns, const CancelToken *cancel)
{
    TrackJob *j=&f->job;
    /* Stable LSD radix passes read sequential blocks, avoiding hundreds of
     * thousands of random disk comparisons. Ordinal breaks equal-title ties. */
    while (columns) {
        --columns; uint32_t counts[256]={0};
        for (uint32_t start=0;start<length;) {
            if (cancelled(cancel)) { return TRACK_CANCELLED; }
            uint32_t n=length-start; if (n>TRACK_BATCH) { n=TRACK_BATCH; }
            TrackResult r=j->source.read(j->source.context,j->base+start,n,f->batch); if (r!=TRACK_OK) { return r; }
            for (uint32_t i=0;i<n;++i) {
                uint32_t at=start+i; unsigned char c=columns<f->index[at].title_length ? (unsigned char)f->batch[i].title[columns] : 0;
                if (c>='A' && c<='Z') { c=(unsigned char)(c+32); } f->sort_keys[at]=c; ++counts[c];
            }
            start+=n;
        }
        uint32_t offset=0;
        for (unsigned i=0;i<256;++i) { uint32_t n=counts[i]; counts[i]=offset; offset+=n; }
        for (uint32_t i=0;i<length;++i) { uint32_t ordinal=f->ordered[i]; f->map[counts[f->sort_keys[ordinal-j->base]]++]=ordinal; }
        memcpy(f->ordered,f->map,(size_t)length*sizeof(f->ordered[0]));
    }
    return TRACK_OK;
}
static void sift(TrackFilter *f, uint32_t root, uint32_t end)
{
    uint32_t *map=f->ordered;
    while (root<end/2) {
        uint32_t child=root*2+1;
        if (child+1<end && compare(f,map[child],map[child+1])<0) { ++child; }
        if (compare(f,map[root],map[child])>=0) { break; }
        uint32_t v=map[root]; map[root]=map[child]; map[child]=v; root=child;
    }
}
TrackResult track_filter_execute(TrackFilter *f, const CancelToken *cancel)
{
    if (!f || !valid(&f->job) || core_thread_is_gui()) { return TRACK_INVALID; }
    TrackJob *j=&f->job; j->result=TRACK_OK;
    if (j->metadata) {
        for (uint32_t i=0;i<j->count;++i) {
            if (cancelled(cancel)) { return TRACK_CANCELLED; }
            TrackResult r=j->source.read(j->source.context,j->rows[i],1,&f->batch[i]);
            if (r!=TRACK_OK || !track_record_valid(&f->batch[i])) { return r==TRACK_OK ? TRACK_INVALID : r; }
        }
        return TRACK_OK;
    }
    uint32_t length=j->source.total-j->base; if (length>TRACK_INDEX_CAP) { length=TRACK_INDEX_CAP; }
    TrackSource *old=&f->ordered_source;
    bool ordered=f->ordered_valid && f->ordered_base==j->base && f->ordered_sort==j->sort && old->read==j->source.read &&
        old->context==j->source.context && old->total==j->source.total && old->complete==j->source.complete &&
        !strcmp(old->uri,j->source.uri) && !strcmp(old->revision,j->source.revision);
    if (!ordered) { f->ordered_valid=false; }
    memset(f->matches,0,sizeof(f->matches));
    unsigned title_length=0;
    j->count=j->missing=0; j->coverage=j->source.complete ? TRACK_FULL_INDEX : TRACK_LOADED_ONLY;
    for (uint32_t start=0;start<length;) {
        if (cancelled(cancel)) { return TRACK_CANCELLED; }
        uint32_t n=length-start; if (n>TRACK_BATCH) { n=TRACK_BATCH; }
        TrackResult r=j->source.read(j->source.context,j->base+start,n,f->batch);
        if (r!=TRACK_OK) { return r; }
        for (uint32_t i=0;i<n;++i) {
            TrackRecord *row=&f->batch[i]; if (!track_record_valid(row)) { return TRACK_INVALID; }
            uint32_t at=start+i; f->index[at]=(TrackIdentity){.duration_ms=row->duration_ms,.flags=(uint8_t)row->flags,
                .title_length=(uint8_t)strlen(row->title)};
            memcpy(f->index[at].id,row->id,16);
            if (f->index[at].title_length>title_length) { title_length=f->index[at].title_length; }
            if (!(row->flags&TRACK_METADATA)) { ++j->missing; }
            if (!ordered) { f->ordered[at]=j->base+at; }
            if (track_matches(row,j->query)) { f->matches[at/8]|=(unsigned char)(1u<<(at%8)); }
        }
        start+=n;
    }
    if (j->missing && (j->query[0] || j->sort!=TRACK_SOURCE_ORDER)) { j->coverage=TRACK_LOADED_ONLY; }
    if (j->base || length<j->source.total) { j->coverage=TRACK_WINDOW; }
    if (!ordered && j->sort==TRACK_TITLE_ORDER) {
        TrackResult r=title_order(f,length,title_length,cancel); if (r!=TRACK_OK) { return r; }
    } else if (!ordered && j->sort==TRACK_DURATION_ORDER) {
        for (uint32_t i=length/2;i>0;--i) { if (cancelled(cancel)) { return TRACK_CANCELLED; } sift(f,i-1,length); }
        for (uint32_t i=length;i>1;--i) {
            if (cancelled(cancel)) { return TRACK_CANCELLED; }
            uint32_t v=f->ordered[0]; f->ordered[0]=f->ordered[i-1]; f->ordered[i-1]=v; sift(f,0,i-1);
            if (j->result!=TRACK_OK) { return j->result; }
        }
    }
    if (j->result!=TRACK_OK) { return j->result; }
    f->ordered_valid=true; f->ordered_base=j->base; f->ordered_sort=j->sort; f->ordered_source=j->source;
    for (uint32_t i=0;i<length;++i) {
        uint32_t at=f->ordered[i]-j->base;
        if (f->matches[at/8]&(1u<<(at%8))) { f->map[j->count++]=f->ordered[i]; }
    }
    return j->result;
}
static void run(const Event *e, const CancelToken *cancel, void *context)
{
    (void)e; TrackFilter *f=context; f->job.result=track_filter_execute(f,cancel);
}
static void complete(const Event *e, WorkerOutcome outcome, void *context)
{
    (void)e; TrackFilter *f=context;
    if (outcome!=WORKER_DONE) { f->job.result=TRACK_CANCELLED; }
    atomic_store_explicit(&f->phase,2,memory_order_release);
    Event wake={.type=TRACK_FILTER_EVENT}; (void)event_post(f->events,&wake);
}
TrackResult track_filter_init(TrackFilter *f, Worker *worker, EventQueue *events)
{
    if (!f || !worker || !events) { return TRACK_INVALID; }
    memset(f,0,sizeof(*f)); f->worker=worker; f->events=events; atomic_init(&f->phase,0); return TRACK_OK;
}
TrackResult track_filter_start(TrackFilter *f, const TrackJob *j)
{
    if (!f || !valid(j)) { return TRACK_INVALID; }
    if (atomic_load_explicit(&f->phase,memory_order_acquire)) { return TRACK_PENDING; }
    f->job=*j; f->job.result=TRACK_PENDING; atomic_store(&f->phase,1);
    WorkerJob job={.run=run,.complete=complete,.context=f,.deadline=CORE_CLOCK_NEVER,.priority=WORKER_DISK};
    CoreResult r=worker_submit(f->worker,&job,&f->ticket);
    if (r!=CORE_OK) { atomic_store(&f->phase,0); return TRACK_FULL; } return TRACK_PENDING;
}
void track_filter_cancel(TrackFilter *f)
{
    if (f && atomic_load_explicit(&f->phase,memory_order_acquire)==1) { (void)worker_cancel(f->worker,f->ticket); }
}
const TrackJob *track_filter_done(TrackFilter *f)
{
    return f && atomic_load_explicit(&f->phase,memory_order_acquire)==2 ? &f->job : NULL;
}
void track_filter_release(TrackFilter *f)
{
    if (track_filter_done(f)) { atomic_store_explicit(&f->phase,0,memory_order_release); }
}
TrackResult track_filter_clear(TrackFilter *f)
{
    if (!f) { return TRACK_INVALID; }
    if (atomic_load_explicit(&f->phase,memory_order_acquire)) { return TRACK_PENDING; }
    memset(f->index,0,sizeof(f->index)); memset(f->map,0,sizeof(f->map)); memset(f->sort_keys,0,sizeof(f->sort_keys));
    memset(f->batch,0,sizeof(f->batch)); memset(&f->job,0,sizeof(f->job));
    memset(f->ordered,0,sizeof(f->ordered)); memset(f->matches,0,sizeof(f->matches));
    memset(&f->ordered_source,0,sizeof(f->ordered_source)); f->ordered_valid=false; return TRACK_OK;
}
