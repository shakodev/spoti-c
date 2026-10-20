#include "collection_artwork.h"
#include "app.h"
#include <bcrypt.h>
#include <winhttp.h>
#include <string.h>

#define ART_IDENTITY_BYTES (4u+COLLECTION_ARTWORK_SOURCES*(12u+ART_URL_CHARS-1u))
typedef struct Reader {
    CollectionArtwork *out;
    uint8_t key[ART_IDENTITY_BYTES];
    uint32_t used, seen;
} Reader;

static AppResult field(yyjson_val *parent,const char *name,yyjson_val **out)
{
    JsonResult r=json_base_field(parent,name,out);
    return r==JSON_OK ? APP_OK : r==JSON_MISSING || r==JSON_NULL ? APP_UNAVAILABLE : APP_INVALID;
}
static void u32(uint8_t *out,uint32_t value)
{
    for (unsigned i=0;i<4;++i) { out[i]=(uint8_t)(value>>(i*8)); }
}
static AppResult dimension(yyjson_val *source,const char *name,uint32_t *out)
{
    uint64_t value=0; JsonResult result=json_base_u64(yyjson_obj_get(source,name),UINT32_MAX,&value);
    *out=(uint32_t)value;
    return result==JSON_OK || result==JSON_NULL || result==JSON_MISSING ? APP_OK : APP_INVALID;
}
static AppResult append(Reader *reader,yyjson_val *value,uint32_t width,uint32_t height)
{
    JsonString url;
    JsonResult result=json_base_string(value,ART_URL_CHARS-1,&url);
    if (result==JSON_MISSING || result==JSON_NULL) { return APP_UNAVAILABLE; }
    if (result==JSON_RANGE) { return APP_FULL; }
    if (result!=JSON_OK) { return APP_INVALID; }
    if (!url.bytes) { return APP_UNAVAILABLE; }
    wchar_t *copy=reader->out->urls[reader->out->count];
    int length=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,url.data,(int)url.bytes,copy,ART_URL_CHARS-1);
    if (!length) { return APP_INVALID; }
    copy[length]=0;
    for (size_t i=0;i<url.bytes;++i) {
        unsigned char c=(unsigned char)url.data[i];
        if (c<=0x20 || c==0x7f || c=='#' || c=='\\') { return APP_INVALID; }
    }
    if (url.bytes<8 || _strnicmp(url.data,"https://",8)) { return APP_UNAVAILABLE; }
    URL_COMPONENTS parts={.dwStructSize=sizeof(parts)};
    parts.dwSchemeLength=parts.dwHostNameLength=parts.dwUserNameLength=parts.dwPasswordLength=(DWORD)-1;
    if (!WinHttpCrackUrl(copy,(DWORD)length,ICU_REJECT_USERPWD,&parts) ||
        parts.nScheme!=INTERNET_SCHEME_HTTPS || !parts.dwHostNameLength ||
        parts.dwUserNameLength || parts.dwPasswordLength) { return APP_INVALID; }
    u32(reader->key+reader->used,(uint32_t)url.bytes);
    u32(reader->key+reader->used+4,width); u32(reader->key+reader->used+8,height);
    reader->used+=12;
    memcpy(reader->key+reader->used,url.data,url.bytes); reader->used+=(uint32_t)url.bytes;
    reader->out->sources[reader->out->count++]=(ArtworkSource){copy,width,height};
    return APP_OK;
}
static AppResult sources(Reader *reader,yyjson_val *value)
{
    if (!value || yyjson_is_null(value)) { return APP_UNAVAILABLE; }
    if (!yyjson_is_arr(value)) { return APP_INVALID; }
    size_t count=yyjson_arr_size(value);
    if (count>COLLECTION_ARTWORK_SOURCES-reader->seen) { return APP_FULL; }
    reader->seen+=(uint32_t)count;
    for (size_t i=0;i<count;++i) {
        yyjson_val *source=yyjson_arr_get(value,i);
        if (yyjson_is_null(source)) { continue; }
        if (!yyjson_is_obj(source)) { return APP_INVALID; }
        uint32_t width,height;
        if (dimension(source,"width",&width)!=APP_OK || dimension(source,"height",&height)!=APP_OK ||
            (!width != !height)) { return APP_INVALID; }
        AppResult r=append(reader,yyjson_obj_get(source,"url"),width,height);
        if (r!=APP_OK && r!=APP_UNAVAILABLE) { return r; }
    }
    return APP_OK;
}
static AppResult home(Reader *reader,const CollectionItem *item)
{
    const char *expected=item->kind==COLLECTION_PLAYLIST_ITEM ? "Playlist" :
        item->kind==COLLECTION_TRACK ? "Track" : item->kind==COLLECTION_ALBUM ? "Album" :
        item->kind==COLLECTION_ARTIST ? "Artist" : NULL;
    if (!expected) { return APP_UNAVAILABLE; }
    yyjson_val *content=NULL,*data=NULL,*cover=NULL,*value=NULL;
    AppResult r=field(item->source,"content",&content);
    if (r!=APP_OK) { return r; }
    r=field(content,"data",&data); if (r!=APP_OK) { return r; }
    if (!yyjson_is_obj(data)) { return APP_INVALID; }
    JsonString discriminator;
    if (json_view_string(data,"__typename",&discriminator)!=JSON_OK ||
        !json_span_equal(discriminator,(JsonString){expected,strlen(expected)})) { return APP_INVALID; }
    if (item->kind==COLLECTION_PLAYLIST_ITEM) {
        r=field(data,"images",&cover); if (r!=APP_OK) { return r; }
        r=field(cover,"items",&value); if (r!=APP_OK) { return r; }
        if (!yyjson_is_arr(value)) { return APP_INVALID; }
        size_t count=yyjson_arr_size(value);
        if (count>COLLECTION_ARTWORK_SOURCES) { return APP_FULL; }
        for (size_t i=0;i<count;++i) {
            yyjson_val *variants=NULL;
            r=field(yyjson_arr_get(value,i),"sources",&variants);
            if (r==APP_UNAVAILABLE) { continue; }
            if (r!=APP_OK) { return r; }
            r=sources(reader,variants);
            if (r!=APP_OK && r!=APP_UNAVAILABLE) { return r; }
        }
        return APP_OK;
    }
    if (item->kind==COLLECTION_TRACK) {
        r=field(data,"albumOfTrack",&data); if (r!=APP_OK) { return r; }
    } else if (item->kind==COLLECTION_ARTIST) {
        r=field(data,"visuals",&data); if (r!=APP_OK) { return r; }
    }
    r=field(data,item->kind==COLLECTION_ARTIST ? "avatarImage" : "coverArt",&cover);
    if (r!=APP_OK) { return r; }
    r=field(cover,"sources",&value);
    return r==APP_OK ? sources(reader,value) : r;
}
AppResult app_collection_artwork_read(const CollectionItem *item,CollectionArtwork *out)
{
    if (!out) { return APP_INVALID; }
    memset(out,0,sizeof(*out));
    if (!item) { return APP_INVALID; }
    if (!item->source || yyjson_is_null(item->source)) { return APP_UNAVAILABLE; }
    if (!yyjson_is_obj(item->source)) { return APP_INVALID; }
    if (item->kind!=COLLECTION_PLAYLIST_ITEM && item->kind!=COLLECTION_TRACK &&
        item->kind!=COLLECTION_ALBUM && item->kind!=COLLECTION_ARTIST &&
        item->kind!=COLLECTION_LIKED) { return APP_UNAVAILABLE; }
    Reader reader={.out=out,.used=4};
    yyjson_val *info=yyjson_obj_get(item->source,"entityInfo"); AppResult result;
    if (info) {
        yyjson_val *url=NULL; result=field(info,"imageUri",&url);
        if (result==APP_OK) { result=append(&reader,url,0,0); }
    } else { result=home(&reader,item); }
    if (result==APP_OK) {
        if (!out->count) { result=APP_UNAVAILABLE; }
        else {
            u32(reader.key,out->count);
            if (BCryptHash(BCRYPT_SHA1_ALG_HANDLE,NULL,0,reader.key,reader.used,
                out->content,(ULONG)sizeof(out->content))<0) { result=APP_FAILED; }
        }
    }
    if (result!=APP_OK) { memset(out,0,sizeof(*out)); }
    return result;
}
AppResult app_collection_image(App *app,const CollectionItem *item,uint32_t requested_size)
{
    if (!app || !item || !app->alive || app->widgets.in_frame || !requested_size ||
        requested_size>ART_MAX_EDGE) { return APP_INVALID; }
    if (!app->services.artwork) { return APP_UNAVAILABLE; }
    CollectionArtwork image;
    AppResult result=app_collection_artwork_read(item,&image);
    if (result!=APP_OK) { return result; }
    AppEntity entity;
    result=app_entity(item,&entity); if (result!=APP_OK) { return result; }
    ArtworkId id={.size=requested_size};
    memcpy(id.content,image.content,sizeof(id.content));
    return app_image(app,app->model.generation,&entity,id,image.sources,image.count);
}
