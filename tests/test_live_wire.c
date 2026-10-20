#include "ui/live_internal.h"
#include "pb_encode.h"
#include "extension_kind.pb.h"
#include "support/test_check.h"
#include <string.h>
#include <wchar.h>

static bool string_field(pb_ostream_t *s, uint32_t tag, const void *data, size_t bytes)
{
    return pb_encode_tag(s,PB_WT_STRING,tag) && pb_encode_string(s,data,bytes);
}
static bool integer(pb_ostream_t *s, uint32_t tag, uint64_t value)
{
    return pb_encode_tag(s,PB_WT_VARINT,tag) && pb_encode_varint(s,value);
}
static size_t list_body(unsigned char *out, size_t cap, uint32_t count, bool position, bool truncated_field)
{
    unsigned char content[8192]; pb_ostream_t c=pb_ostream_from_buffer(content,sizeof(content));
    if (position) { CHECK(integer(&c,1,0)); }
    if (truncated_field) { CHECK(integer(&c,2,0)); }
    for (uint32_t i=0;i<count;++i) {
        unsigned char row[128]; pb_ostream_t r=pb_ostream_from_buffer(row,sizeof(row));
        const char *uri="spotify:playlist:0000000000000000000001";
        CHECK(string_field(&r,1,uri,strlen(uri))); CHECK(string_field(&c,3,row,r.bytes_written));
    }
    pb_ostream_t s=pb_ostream_from_buffer(out,cap); unsigned char revision[24]={1,2,3};
    CHECK(string_field(&s,1,revision,sizeof(revision))); CHECK(integer(&s,2,count));
    CHECK(string_field(&s,5,content,c.bytes_written)); return s.bytes_written;
}
static void parser(void)
{
    unsigned char data[9000]; LiveList list;
    size_t bytes=list_body(data,sizeof(data),2,true,true);
    CHECK(live_list_decode(data,bytes,&list)); CHECK(list.count==2 && list.total==2 && list.contents && !list.truncated);
    CHECK(strlen(list.revision)==48 && !strncmp(list.revision,"010203",6)); CHECK(!strcmp(list.rows[0].uri,"spotify:playlist:0000000000000000000001"));
    CHECK(!live_list_decode(data,bytes-1,&list));
    bytes=list_body(data,sizeof(data),1,false,true); CHECK(!live_list_decode(data,bytes,&list));
    bytes=list_body(data,sizeof(data),1,true,false); CHECK(!live_list_decode(data,bytes,&list));
    bytes=list_body(data,sizeof(data),65,true,true); CHECK(!live_list_decode(data,bytes,&list));
    bytes=list_body(data,sizeof(data),64,true,true); CHECK(live_list_decode(data,bytes,&list));
    /* Every one-byte perturbation must either reject or retain bounded output. */
    for (size_t i=0;i<bytes;++i) {
        unsigned char original=data[i]; data[i]^=0xff;
        if (live_list_decode(data,bytes,&list)) { CHECK(list.count<=LIVE_ROWS && list.position<=list.total && list.count<=list.total-list.position); }
        data[i]=original;
    }
    wchar_t escaped[64]; CHECK(live_url_component("a/b%?\xc3\xa9",escaped,64));
    CHECK(!wcscmp(escaped,L"a%2Fb%25%3F%C3%A9")); CHECK(!live_url_component("/",escaped,3));
}
static void models(void)
{
    AppLive *h=VirtualAlloc(NULL,sizeof(*h),MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE); CHECK(h);
    CHECK(json_page_pool_init(h->pool_storage,sizeof(h->pool_storage),&h->pool)==JSON_OK);
    CHECK(collection_service_init(h->collection_storage,sizeof(h->collection_storage),h->pool,&h->collections)==COLLECTION_OK);
    h->list=(LiveList){.total=65,.count=2};
    strcpy_s(h->list.rows[0].uri,sizeof(h->list.rows[0].uri),"spotify:playlist:0000000000000000000001");
    strcpy_s(h->list.rows[0].name,sizeof(h->list.rows[0].name),"Quoted \"playlist\" \xc3\xa9");
    strcpy_s(h->list.rows[1].uri,sizeof(h->list.rows[1].uri),"spotify:folder:example");
    CollectionRequest request; CHECK(collection_fetch(h->collections,COLLECTION_LIBRARY,0,1,&request)==COLLECTION_PENDING);
    size_t bytes; CHECK(live_library_body(h,&request,&bytes));
    const CollectionPage *page; CHECK(collection_complete(h->collections,request.ticket,COLLECTION_OK,200,bytes,&page)==COLLECTION_OK);
    CHECK(page->count==2 && page->next==2 && page->total==65 && page->items[0].available && !page->items[1].available);
    CHECK(json_span_equal(page->items[0].name,(JsonString){h->list.rows[0].name,strlen(h->list.rows[0].name)}));
    CHECK(page_release((JsonPage *)&page->json)); CHECK(collection_reset(h->collections,2)==COLLECTION_OK);
    CHECK(collection_fetch(h->collections,COLLECTION_HOME,0,2,&request)==COLLECTION_PENDING);
    CHECK(live_saved_home_body(h,&request,false,&bytes));
    CHECK(collection_complete(h->collections,request.ticket,COLLECTION_OK,200,bytes,&page)==COLLECTION_OK);
    CollectionItem saved; CHECK(page->count==1 && collection_shelf_item(page,0,0,&saved)==COLLECTION_OK && saved.kind==COLLECTION_PLAYLIST_ITEM);
    CHECK(page_release((JsonPage *)&page->json)); CHECK(collection_reset(h->collections,3)==COLLECTION_OK);
    CHECK(collection_fetch(h->collections,COLLECTION_HOME_SECTION,0,3,&request)==COLLECTION_PENDING);
    CHECK(live_saved_home_body(h,&request,true,&bytes));
    CHECK(collection_complete(h->collections,request.ticket,COLLECTION_OK,200,bytes,&page)==COLLECTION_OK);
    CHECK(page->count==1 && page->next==2); /* Native source cursor includes a skipped folder. */
    CHECK(page_release((JsonPage *)&page->json)); CHECK(collection_reset(h->collections,4)==COLLECTION_OK);
    AppRequest r={.kind=COLLECTION_HOME_SECTION,.cursor=20}; strcpy_s(r.uri,sizeof(r.uri),"spotify:section:a\"b");
    strcpy_s(h->timezone,sizeof(h->timezone),"Europe/Amsterdam");
    request=(CollectionRequest){.contract=contract_inspect(API_HOME_SECTION),.limit=64}; char upload[4096];
    CHECK(live_home_body(h,&r,&request,upload,sizeof(upload),&bytes));
    yyjson_doc *doc=yyjson_read(upload,bytes,0); CHECK(doc);
    yyjson_val *variables=yyjson_obj_get(yyjson_doc_get_root(doc),"variables");
    CHECK(!strcmp(yyjson_get_str(yyjson_obj_get(variables,"uri")),r.uri));
    CHECK(!strcmp(yyjson_get_str(yyjson_obj_get(variables,"homeEndUserIntegration")),"INTEGRATION_DESKTOP"));
    CHECK(yyjson_get_uint(yyjson_obj_get(variables,"sectionItemsOffset"))==20);
    yyjson_doc_free(doc); CHECK(json_page_pool_dispose(h->pool)==JSON_OK); CHECK(VirtualFree(h,0,MEM_RELEASE));
}
static void metadata(void)
{
    unsigned char track_data[256], album[64], artist[64], any[512], entity[768], array[1024], body[1200];
    TrackRecord rows[2]={0}; rows[0].id[15]=1; rows[1].id[15]=1;
    pb_ostream_t t=pb_ostream_from_buffer(track_data,sizeof(track_data));
    CHECK(string_field(&t,1,rows[0].id,16)); CHECK(string_field(&t,2,"A track",7)); CHECK(integer(&t,7,10000));
    pb_ostream_t n=pb_ostream_from_buffer(album,sizeof(album)); CHECK(string_field(&n,2,"An album",8));
    CHECK(string_field(&t,3,album,n.bytes_written));
    n=pb_ostream_from_buffer(artist,sizeof(artist)); CHECK(string_field(&n,2,"An artist",9));
    CHECK(string_field(&t,4,artist,n.bytes_written));
    pb_ostream_t a=pb_ostream_from_buffer(any,sizeof(any)); const char *type="type.googleapis.com/spotify.metadata.Track";
    CHECK(string_field(&a,1,type,strlen(type))); CHECK(string_field(&a,2,track_data,t.bytes_written));
    unsigned char header[]={8,0xc8,1}; char uri[MEDIA_URI_BYTES]; CHECK(media_gid_to_uri(rows[0].id,uri)==MEDIA_OK);
    pb_ostream_t e=pb_ostream_from_buffer(entity,sizeof(entity));
    CHECK(string_field(&e,1,header,sizeof(header))); CHECK(string_field(&e,2,uri,strlen(uri))); CHECK(string_field(&e,3,any,a.bytes_written));
    pb_ostream_t x=pb_ostream_from_buffer(array,sizeof(array)); CHECK(integer(&x,2,spotify_extendedmetadata_ExtensionKind_TRACK_V4)); CHECK(string_field(&x,3,entity,e.bytes_written));
    pb_ostream_t b=pb_ostream_from_buffer(body,sizeof(body)); CHECK(string_field(&b,2,array,x.bytes_written));
    CHECK(live_metadata_decode(body,b.bytes_written,rows,2));
    CHECK(rows[0].duration_ms==5000 && !strcmp(rows[0].title,"A track") && !strcmp(rows[0].artist,"An artist") &&
        !strcmp(rows[0].album,"An album") && (rows[0].flags&TRACK_METADATA));
    CHECK((rows[1].flags&TRACK_METADATA)!=0); /* Duplicate occurrences preserve their own row. */
    CHECK(!live_metadata_decode(body,b.bytes_written-1,rows,2));
    CHECK(!live_metadata_decode(body,0,rows,2));
    rows[0].id[15]=2; rows[1].id[15]=2; CHECK(!live_metadata_decode(body,b.bytes_written,rows,2));
}
int main(void) { parser(); models(); metadata(); puts("Live native wire bounds, metadata and typed model projection passed."); return 0; }
