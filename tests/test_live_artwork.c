#include "ui/live_internal.h"
#include "ui/collection_artwork.h"
#include "pb_encode.h"
#include "support/test_check.h"
#include <string.h>
#include <wchar.h>

typedef struct Candidate {
    const char *url, *target;
    size_t bytes, target_bytes;
    bool duplicate, wrong_wire, duplicate_target, wrong_target_wire, target_last;
} Candidate;
static const char cover[]="https://example.test/covers/real%20metadata.jpg";
static const char opaque[]="spotify:image:00112233445566778899aabbccddeeff00112233";

static void string(pb_ostream_t *s, uint32_t tag, const void *data, size_t bytes)
{
    CHECK(pb_encode_tag(s,PB_WT_STRING,tag) && pb_encode_string(s,data,bytes));
}
static void integer(pb_ostream_t *s, uint32_t tag, uint64_t value)
{
    CHECK(pb_encode_tag(s,PB_WT_VARINT,tag) && pb_encode_varint(s,value));
}
static void label(pb_ostream_t *s, const Candidate *c)
{
    if (c->wrong_target_wire) { integer(s,1,7); }
    else if (c->target) {
        string(s,1,c->target,c->target_bytes);
        if (c->duplicate_target) { string(s,1,c->target,c->target_bytes); }
    }
}
static size_t encode(unsigned char *out, size_t capacity, const Candidate *candidates,
    uint32_t count, bool duplicate_attributes, bool metadata_first)
{
    unsigned char attrs[15000], meta[16000], contents[17000], item[128];
    pb_ostream_t a=pb_ostream_from_buffer(attrs,sizeof(attrs));
    string(&a,1,"Image metadata",14);
    /* Opaque picture bytes must never become a guessed URL. */
    const unsigned char picture[]={0,0xff,0x11,0x22}; string(&a,3,picture,sizeof(picture));
    for (uint32_t i=0;i<count;++i) {
        unsigned char image[2200]; pb_ostream_t p=pb_ostream_from_buffer(image,sizeof(image));
        if (!candidates[i].target_last) { label(&p,&candidates[i]); }
        if (candidates[i].wrong_wire) { integer(&p,2,7); }
        else if (candidates[i].url) {
            string(&p,2,candidates[i].url,candidates[i].bytes);
            if (candidates[i].duplicate) { string(&p,2,candidates[i].url,candidates[i].bytes); }
        }
        if (candidates[i].target_last) { label(&p,&candidates[i]); }
        string(&a,13,image,p.bytes_written);
    }
    pb_ostream_t m=pb_ostream_from_buffer(meta,sizeof(meta));
    string(&m,2,attrs,a.bytes_written);
    if (duplicate_attributes) { string(&m,2,attrs,a.bytes_written); }
    pb_ostream_t c=pb_ostream_from_buffer(contents,sizeof(contents)); integer(&c,1,0); integer(&c,2,0);
    pb_ostream_t r=pb_ostream_from_buffer(item,sizeof(item));
    const char *uri="spotify:playlist:0000000000000000000001"; string(&r,1,uri,strlen(uri));
    if (metadata_first) { string(&c,4,meta,m.bytes_written); }
    string(&c,3,item,r.bytes_written);
    if (!metadata_first) { string(&c,4,meta,m.bytes_written); }
    pb_ostream_t s=pb_ostream_from_buffer(out,capacity);
    integer(&s,2,1); string(&s,5,contents,c.bytes_written); return s.bytes_written;
}
static void parser(void)
{
    static unsigned char wire[18000]; static LiveList list;
    Candidate candidates[]={{.url=opaque,.bytes=sizeof(opaque)-1},{.url=cover,.bytes=sizeof(cover)-1},
        {.url="https://example.test/later.jpg"}};
    candidates[2].bytes=strlen(candidates[2].url);
    size_t bytes=encode(wire,sizeof(wire),candidates,3,false,false);
    CHECK(live_list_decode(wire,bytes,&list));
    CHECK(!strcmp(list.rows[0].image_uri,cover));
    memset(wire,0xcc,bytes); CHECK(!strcmp(list.rows[0].image_uri,cover)); /* Owned wire copy. */
    bytes=encode(wire,sizeof(wire),candidates,1,false,true);
    CHECK(live_list_decode(wire,bytes,&list) && !strcmp(list.rows[0].image_uri,opaque));
    bytes=encode(wire,sizeof(wire),NULL,0,false,false);
    CHECK(live_list_decode(wire,bytes,&list) && !list.rows[0].image_uri[0]);
    Candidate missing={0}; bytes=encode(wire,sizeof(wire),&missing,1,false,false);
    CHECK(live_list_decode(wire,bytes,&list) && !list.rows[0].image_uri[0]);
    char large[LIVE_IMAGE_BYTES+1]; memset(large,'a',sizeof(large));
    memcpy(large,"https://example.test/",21); large[sizeof(large)-1]=0;
    Candidate boundary={.url=large,.bytes=LIVE_IMAGE_BYTES-1};
    bytes=encode(wire,sizeof(wire),&boundary,1,false,false);
    CHECK(live_list_decode(wire,bytes,&list) && strlen(list.rows[0].image_uri)==LIVE_IMAGE_BYTES-1);
    boundary.bytes=LIVE_IMAGE_BYTES;
    bytes=encode(wire,sizeof(wire),&boundary,1,false,false); CHECK(!live_list_decode(wire,bytes,&list));
    Candidate duplicate={.url=cover,.bytes=sizeof(cover)-1,.duplicate=true};
    bytes=encode(wire,sizeof(wire),&duplicate,1,false,false); CHECK(!live_list_decode(wire,bytes,&list));
    duplicate.duplicate=false; duplicate.wrong_wire=true;
    bytes=encode(wire,sizeof(wire),&duplicate,1,false,false); CHECK(!live_list_decode(wire,bytes,&list));
    bytes=encode(wire,sizeof(wire),candidates,1,true,false); CHECK(!live_list_decode(wire,bytes,&list));
    const char nul[]={'h','t','t','p','s',':','/','/',0,'x'}, invalid[]={'\xc0','\xaf'};
    Candidate malformed={.url=nul,.bytes=sizeof(nul)};
    bytes=encode(wire,sizeof(wire),&malformed,1,false,false); CHECK(!live_list_decode(wire,bytes,&list));
    malformed=(Candidate){.url=invalid,.bytes=sizeof(invalid)};
    bytes=encode(wire,sizeof(wire),&malformed,1,false,false); CHECK(!live_list_decode(wire,bytes,&list));
    Candidate many[LIVE_IMAGE_CANDIDATES+1];
    for (uint32_t i=0;i<LIVE_IMAGE_CANDIDATES+1;++i) { many[i]=candidates[1]; }
    bytes=encode(wire,sizeof(wire),many,LIVE_IMAGE_CANDIDATES,false,false); CHECK(live_list_decode(wire,bytes,&list));
    bytes=encode(wire,sizeof(wire),many,LIVE_IMAGE_CANDIDATES+1,false,false); CHECK(!live_list_decode(wire,bytes,&list));
    bytes=encode(wire,sizeof(wire),candidates,3,false,false); CHECK(!live_list_decode(wire,bytes-1,&list));
}
static void candidate_policy(void)
{
    static unsigned char wire[18000]; static LiveList list;
    const char *urls[]={"https://example.test/default","https://example.test/small",
        "https://example.test/large","https://example.test/xlarge"};
    const char *targets[]={"default","small","large","xlarge"};
    Candidate candidates[4]={0};
    for (unsigned i=0;i<4;++i) {
        candidates[i]=(Candidate){.url=urls[i],.bytes=strlen(urls[i]),
            .target=targets[i],.target_bytes=strlen(targets[i])};
    }
    candidates[1].target_last=true; /* Target and URL wire order is immaterial. */
    size_t bytes=encode(wire,sizeof(wire),candidates,4,false,false);
    CHECK(live_list_decode(wire,bytes,&list) && !strcmp(list.rows[0].image_uri,urls[1]));
    Candidate reordered[]={candidates[1],candidates[2],candidates[0]};
    bytes=encode(wire,sizeof(wire),reordered,3,false,false);
    CHECK(live_list_decode(wire,bytes,&list) && !strcmp(list.rows[0].image_uri,urls[1]));
    reordered[0]=candidates[2]; reordered[1]=candidates[0];
    bytes=encode(wire,sizeof(wire),reordered,2,false,false);
    CHECK(live_list_decode(wire,bytes,&list) && !strcmp(list.rows[0].image_uri,urls[2]));
    /* Repeated small candidates preserve the first supplied small URL. */
    candidates[2].target="small"; candidates[2].target_bytes=5;
    bytes=encode(wire,sizeof(wire),candidates,4,false,false);
    CHECK(live_list_decode(wire,bytes,&list) && !strcmp(list.rows[0].image_uri,urls[1]));
    Candidate pair[]={candidates[0],candidates[1]};
    pair[1].url=opaque; pair[1].bytes=sizeof(opaque)-1;
    bytes=encode(wire,sizeof(wire),pair,2,false,false);
    CHECK(live_list_decode(wire,bytes,&list) && !strcmp(list.rows[0].image_uri,urls[0]));
    bytes=encode(wire,sizeof(wire),pair+1,1,false,false);
    CHECK(live_list_decode(wire,bytes,&list) && !strcmp(list.rows[0].image_uri,opaque));
    pair[1].url=""; pair[1].bytes=0;
    bytes=encode(wire,sizeof(wire),pair,2,false,false);
    CHECK(live_list_decode(wire,bytes,&list) && !strcmp(list.rows[0].image_uri,urls[0]));
    const char *ordinary[]={NULL,"","Small","small ","unmeasured-target"};
    for (unsigned i=0;i<sizeof(ordinary)/sizeof(ordinary[0]);++i) {
        pair[1]=candidates[1]; pair[1].target=ordinary[i];
        pair[1].target_bytes=ordinary[i] ? strlen(ordinary[i]) : 0;
        bytes=encode(wire,sizeof(wire),pair,2,false,false);
        CHECK(live_list_decode(wire,bytes,&list) && !strcmp(list.rows[0].image_uri,urls[0]));
    }
}
static void target_validation(void)
{
    static unsigned char wire[18000]; static LiveList list;
    Candidate candidate={.url=cover,.bytes=sizeof(cover)-1,.target="small",.target_bytes=5};
    candidate.duplicate_target=true;
    size_t bytes=encode(wire,sizeof(wire),&candidate,1,false,false);
    CHECK(!live_list_decode(wire,bytes,&list));
    candidate.duplicate_target=false; candidate.wrong_target_wire=true;
    bytes=encode(wire,sizeof(wire),&candidate,1,false,false); CHECK(!live_list_decode(wire,bytes,&list));
    candidate.wrong_target_wire=false;
    const char nul[]={'s','m','a','l','l',0,'x'}, invalid[]={'\xc0','\xaf'};
    candidate.target=nul; candidate.target_bytes=sizeof(nul);
    bytes=encode(wire,sizeof(wire),&candidate,1,false,false); CHECK(!live_list_decode(wire,bytes,&list));
    candidate.target=invalid; candidate.target_bytes=sizeof(invalid);
    bytes=encode(wire,sizeof(wire),&candidate,1,false,false); CHECK(!live_list_decode(wire,bytes,&list));
    char boundary[LIVE_IMAGE_TARGET_BYTES]; memset(boundary,'x',sizeof(boundary));
    candidate.target=boundary; candidate.target_bytes=sizeof(boundary)-1;
    bytes=encode(wire,sizeof(wire),&candidate,1,false,false); CHECK(live_list_decode(wire,bytes,&list));
    candidate.target_bytes=sizeof(boundary);
    bytes=encode(wire,sizeof(wire),&candidate,1,false,false); CHECK(!live_list_decode(wire,bytes,&list));
}
static void projection(AppLive *h, CollectionKind kind, uint64_t generation, const char *uri)
{
    Candidate candidate={.url=uri,.bytes=strlen(uri)}; static unsigned char wire[18000];
    size_t bytes=encode(wire,sizeof(wire),&candidate,1,false,false);
    CHECK(live_list_decode(wire,bytes,&h->list)); memset(wire,0,bytes);
    CollectionRequest request;
    CHECK(collection_fetch(h->collections,kind,0,generation,&request)==COLLECTION_PENDING);
    bool wrote=kind==COLLECTION_LIBRARY ? live_library_body(h,&request,&bytes) :
        live_saved_home_body(h,&request,kind==COLLECTION_HOME_SECTION,&bytes);
    CHECK(wrote);
    const CollectionPage *page;
    CHECK(collection_complete(h->collections,request.ticket,COLLECTION_OK,200,bytes,&page)==COLLECTION_OK);
    memset(&h->list,0,sizeof(h->list)); memset(h->json_work,0,sizeof(h->json_work));
    memset(h->response,0,sizeof(h->response));
    CollectionItem member;
    if (kind==COLLECTION_HOME) { CHECK(collection_shelf_item(page,0,0,&member)==COLLECTION_OK); }
    else { member=page->items[0]; }
    yyjson_val *value;
    if (kind==COLLECTION_LIBRARY) { value=yyjson_obj_get(yyjson_obj_get(member.source,"entityInfo"),"imageUri"); }
    else {
        yyjson_val *data=yyjson_obj_get(yyjson_obj_get(member.source,"content"),"data");
        yyjson_val *variant=yyjson_arr_get(yyjson_obj_get(yyjson_obj_get(data,"images"),"items"),0);
        value=yyjson_obj_get(yyjson_arr_get(yyjson_obj_get(variant,"sources"),0),"url");
    }
    CHECK(yyjson_is_str(value) && !strcmp(yyjson_get_str(value),uri));
    CollectionArtwork image; AppResult result=app_collection_artwork_read(&member,&image);
    if (!strcmp(uri,cover)) {
        CHECK(result==APP_OK && image.count==1 && !image.sources[0].width && !image.sources[0].height);
        CHECK(!wcscmp(image.sources[0].url,L"https://example.test/covers/real%20metadata.jpg"));
    } else { CHECK(result==APP_UNAVAILABLE && !image.count); }
    CHECK(page_release((JsonPage *)&page->json));
    CHECK(collection_reset(h->collections,generation+1)==COLLECTION_OK);
}
static void projections(void)
{
    AppLive *h=VirtualAlloc(NULL,sizeof(*h),MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE); CHECK(h);
    CHECK(json_page_pool_init(h->pool_storage,sizeof(h->pool_storage),&h->pool)==JSON_OK);
    CHECK(collection_service_init(h->collection_storage,sizeof(h->collection_storage),h->pool,&h->collections)==COLLECTION_OK);
    uint64_t generation=1;
    for (unsigned type=0;type<3;++type) {
        CollectionKind kind=type==0 ? COLLECTION_LIBRARY : type==1 ? COLLECTION_HOME : COLLECTION_HOME_SECTION;
        projection(h,kind,generation++,cover); projection(h,kind,generation++,opaque);
    }
    CHECK(collection_resident_pages(h->collections)==0 && json_page_pool_resident(h->pool)==0);
    CHECK(json_page_pool_dispose(h->pool)==JSON_OK); CHECK(VirtualFree(h,0,MEM_RELEASE));
}
int main(void)
{
    parser(); candidate_policy(); target_validation(); projections();
    puts("Live supplied artwork: bounded wire fields, candidate policy, typed projections and owned page lifetime passed.");
    return 0;
}
