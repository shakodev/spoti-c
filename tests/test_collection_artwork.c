#include "ui/collection_artwork.h"
#include "ui/app.h"
#include "support/test_check.h"
#include <stdio.h>
#include <string.h>
#include <wchar.h>

static CollectionArtwork output;
static AppResult read_flags(const char *json,CollectionItemKind kind,yyjson_read_flag flags)
{
    yyjson_doc *doc=yyjson_read(json,strlen(json),flags); CHECK(doc);
    CollectionItem item={.kind=kind,.source=yyjson_doc_get_root(doc)};
    AppResult result=app_collection_artwork_read(&item,&output);
    yyjson_doc_free(doc);
    if (result!=APP_OK) {
        CollectionArtwork empty={0}; CHECK(!memcmp(&output,&empty,sizeof(empty)));
    } else {
        CHECK(output.count>0 && output.count<=COLLECTION_ARTWORK_SOURCES);
        for (uint32_t i=0;i<output.count;++i) { CHECK(output.sources[i].url==output.urls[i]); }
    }
    return result;
}
static AppResult read(const char *json,CollectionItemKind kind)
{
    return read_flags(json,kind,0);
}
static void sources_and_lifetime(void)
{
    CHECK(read("{\"content\":{\"data\":{\"__typename\":\"Playlist\",\"images\":{\"items\":["
        "{\"sources\":[{\"url\":\"https://images.example/a\",\"width\":64,\"height\":64}]},"
        "{\"sources\":[{\"url\":\"https://images.example/b\",\"width\":null,\"height\":null}]}]}}}}",
        COLLECTION_PLAYLIST_ITEM)==APP_OK);
    CHECK(output.count==2 && output.sources[0].width==64 && output.sources[0].height==64);
    CHECK(output.sources[1].width==0 && output.sources[1].height==0);
    CHECK(!wcscmp(output.urls[0],L"https://images.example/a") && !wcscmp(output.urls[1],L"https://images.example/b"));
    CHECK(read("{\"content\":{\"data\":{\"__typename\":\"Track\",\"albumOfTrack\":{\"coverArt\":"
        "{\"sources\":[{\"url\":\"https://images.example/track\",\"width\":256,\"height\":144}]}}}}}",
        COLLECTION_TRACK)==APP_OK);
    CHECK(output.count==1 && output.sources[0].height==144);
    CHECK(read("{\"content\":{\"data\":{\"__typename\":\"Album\",\"coverArt\":"
        "{\"sources\":[{\"url\":\"https://images.example/album\",\"width\":300,\"height\":300}]}}}}",
        COLLECTION_ALBUM)==APP_OK);
    CHECK(output.sources[0].width==300); /* Preserve metadata; admission chooses a source later. */
    CHECK(read("{\"content\":{\"data\":{\"__typename\":\"Artist\",\"visuals\":{\"avatarImage\":"
        "{\"sources\":[{\"url\":\"https://images.example/artist\"}]}}}}}",COLLECTION_ARTIST)==APP_OK);
    CHECK(!output.sources[0].width && !output.sources[0].height);
    CHECK(read("{\"entityInfo\":{\"imageUri\":\"HTTPS://images.example/caf\\u00e9-\\ud83c\\udfb5?a=%20\"}}",
        COLLECTION_PLAYLIST_ITEM)==APP_OK);
    const wchar_t expected[]=L"HTTPS://images.example/caf\x00e9-\xd83c\xdfb5?a=%20";
    CHECK(!wcscmp(output.urls[0],expected)); /* DOM is already freed; URL is independently owned. */
    CHECK(read("{\"entityInfo\":{\"imageUri\":\"https://images.example/liked\"}}",COLLECTION_LIKED)==APP_OK);
}
static void missing_and_unsupported(void)
{
    const char *missing[]={
        "{}", "{\"content\":null}", "{\"content\":{\"data\":null}}",
        "{\"content\":{\"data\":{\"__typename\":\"Playlist\"}}}",
        "{\"content\":{\"data\":{\"__typename\":\"Playlist\",\"images\":null}}}",
        "{\"content\":{\"data\":{\"__typename\":\"Playlist\",\"images\":{\"items\":[]}}}}",
        "{\"content\":{\"data\":{\"__typename\":\"Playlist\",\"images\":{\"items\":[{\"sources\":null}]}}}}",
        "{\"content\":{\"data\":{\"__typename\":\"Playlist\",\"images\":{\"items\":[{\"sources\":[null,{}]}]}}}}",
        "{\"entityInfo\":null}", "{\"entityInfo\":{}}", "{\"entityInfo\":{\"imageUri\":null}}",
        "{\"entityInfo\":{\"imageUri\":\"\"}}", "{\"entityInfo\":{\"imageUri\":\"spotify:image:abcd\"}}",
        "{\"entityInfo\":{\"imageUri\":\"http://images.example/a\"}}"
    };
    for (unsigned i=0;i<sizeof(missing)/sizeof(missing[0]);++i) {
        CHECK(read(missing[i],COLLECTION_PLAYLIST_ITEM)==APP_UNAVAILABLE);
    }
    CHECK(read("{\"entityInfo\":{\"imageUri\":\"https://images.example/a\"}}",COLLECTION_FOLDER)==APP_UNAVAILABLE);
    CHECK(read("{\"entityInfo\":{\"imageUri\":\"https://images.example/a\"}}",COLLECTION_UNKNOWN)==APP_UNAVAILABLE);
    CollectionItem no_source={.kind=COLLECTION_ALBUM};
    CHECK(app_collection_artwork_read(&no_source,&output)==APP_UNAVAILABLE && !output.count);
    CHECK(app_collection_artwork_read(NULL,&output)==APP_INVALID && !output.count);
    CHECK(app_collection_artwork_read(&no_source,NULL)==APP_INVALID);
}
static void malformed(void)
{
    const char *bad[]={
        "[]", "{\"content\":[]}", "{\"content\":{\"data\":[]}}",
        "{\"content\":{\"data\":{\"__typename\":\"Album\"}}}",
        "{\"content\":{\"data\":{\"__typename\":1}}}",
        "{\"content\":{\"data\":{\"__typename\":\"Playlist\",\"images\":[]}}}",
        "{\"content\":{\"data\":{\"__typename\":\"Playlist\",\"images\":{\"items\":{}}}}}",
        "{\"content\":{\"data\":{\"__typename\":\"Playlist\",\"images\":{\"items\":[7]}}}}",
        "{\"content\":{\"data\":{\"__typename\":\"Playlist\",\"images\":{\"items\":[{\"sources\":{}}]}}}}",
        "{\"content\":{\"data\":{\"__typename\":\"Playlist\",\"images\":{\"items\":[{\"sources\":[7]}]}}}}",
        "{\"entityInfo\":[]}", "{\"entityInfo\":{\"imageUri\":7}}",
        "{\"entityInfo\":{\"imageUri\":\"https:///\"}}",
        "{\"entityInfo\":{\"imageUri\":\"https://user:password@images.example/a\"}}",
        "{\"entityInfo\":{\"imageUri\":\"https://images.example/a#fragment\"}}",
        "{\"entityInfo\":{\"imageUri\":\"https://images.example/a b\"}}",
        "{\"entityInfo\":{\"imageUri\":\"https://images.example/a\\\\b\"}}",
        "{\"entityInfo\":{\"imageUri\":\"https://images.example/a\\u0000b\"}}"
    };
    for (unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i) {
        CHECK(read(bad[i],COLLECTION_PLAYLIST_ITEM)==APP_INVALID);
    }
    CHECK(read_flags("{\"entityInfo\":{\"imageUri\":\"https://images.example/\xc0\x80\"}}",
        COLLECTION_PLAYLIST_ITEM,YYJSON_READ_ALLOW_INVALID_UNICODE)==APP_INVALID);
    const char *dimension[]={
        "\"width\":-1,\"height\":64", "\"width\":1.5,\"height\":64",
        "\"width\":true,\"height\":64", "\"width\":4294967296,\"height\":64",
        "\"width\":null,\"height\":64", "\"width\":64,\"height\":0"
    };
    char json[512];
    for (unsigned i=0;i<sizeof(dimension)/sizeof(dimension[0]);++i) {
        CHECK(snprintf(json,sizeof(json),"{\"content\":{\"data\":{\"__typename\":\"Album\",\"coverArt\":"
            "{\"sources\":[{\"url\":\"https://images.example/a\",%s}]}}}}",dimension[i])>0);
        CHECK(read(json,COLLECTION_ALBUM)==APP_INVALID);
    }
    /* A later invalid source must not publish earlier valid candidates. */
    CHECK(read("{\"content\":{\"data\":{\"__typename\":\"Album\",\"coverArt\":{\"sources\":["
        "{\"url\":\"https://images.example/a\"},{\"url\":false}]}}}}",COLLECTION_ALBUM)==APP_INVALID);
}
static void bounds(void)
{
    char json[4096]; size_t used=0;
    int n=snprintf(json,sizeof(json),"{\"content\":{\"data\":{\"__typename\":\"Album\",\"coverArt\":{\"sources\":[");
    CHECK(n>0); used=(size_t)n;
    for (unsigned i=0;i<9;++i) {
        n=snprintf(json+used,sizeof(json)-used,"%s{\"url\":\"https://images.example/%u\"}",i ? "," : "",i);
        CHECK(n>0 && (size_t)n<sizeof(json)-used); used+=(size_t)n;
        if (i==7) {
            char eight[4096]; memcpy(eight,json,used);
            CHECK(strcpy_s(eight+used,sizeof(eight)-used,"]}}}}")==0);
            CHECK(read(eight,COLLECTION_ALBUM)==APP_OK && output.count==8);
        }
    }
    CHECK(strcpy_s(json+used,sizeof(json)-used,"]}}}}")==0);
    CHECK(read(json,COLLECTION_ALBUM)==APP_FULL);
    CHECK(read("{\"content\":{\"data\":{\"__typename\":\"Playlist\",\"images\":{\"items\":["
        "{\"sources\":[null,null,null,null,null]},"
        "{\"sources\":[null,null,null,null]}]}}}}",COLLECTION_PLAYLIST_ITEM)==APP_FULL);
    char url[ART_URL_CHARS+1]; memset(url,'a',ART_URL_CHARS); url[ART_URL_CHARS]=0;
    memcpy(url,"https://images.example/",23);
    CHECK(snprintf(json,sizeof(json),"{\"entityInfo\":{\"imageUri\":\"%s\"}}",url)>0);
    CHECK(read(json,COLLECTION_PLAYLIST_ITEM)==APP_FULL);
}
static void identities_and_preparation(void)
{
    const char *first="{\"content\":{\"data\":{\"__typename\":\"Album\",\"coverArt\":{\"sources\":["
        "{\"url\":\"https://images.example/a\",\"width\":64,\"height\":64},"
        "{\"url\":\"https://images.example/b\",\"width\":128,\"height\":128}]}}}}";
    CHECK(read(first,COLLECTION_ALBUM)==APP_OK); uint8_t id[20]; memcpy(id,output.content,sizeof(id));
    CHECK(read(first,COLLECTION_ALBUM)==APP_OK && !memcmp(id,output.content,sizeof(id)));
    CHECK(read("{\"content\":{\"data\":{\"__typename\":\"Album\",\"coverArt\":{\"sources\":["
        "{\"url\":\"https://images.example/a\",\"width\":64,\"height\":64},"
        "{\"url\":\"https://images.example/changed\",\"width\":128,\"height\":128}]}}}}",
        COLLECTION_ALBUM)==APP_OK && memcmp(id,output.content,sizeof(id)));
    CHECK(read("{\"content\":{\"data\":{\"__typename\":\"Album\",\"coverArt\":{\"sources\":["
        "{\"url\":\"https://images.example/a\",\"width\":64,\"height\":64},"
        "{\"url\":\"https://images.example/b\",\"width\":256,\"height\":256}]}}}}",
        COLLECTION_ALBUM)==APP_OK && memcmp(id,output.content,sizeof(id)));
    static App app; CollectionItem item={.kind=COLLECTION_ALBUM};
    CHECK(app_collection_image(&app,&item,64)==APP_INVALID);
    app.alive=true;
    CHECK(app_collection_image(&app,&item,64)==APP_UNAVAILABLE);
    CHECK(app_collection_image(&app,NULL,64)==APP_INVALID);
    CHECK(app_collection_image(&app,&item,0)==APP_INVALID);
    CHECK(app_collection_image(&app,&item,ART_MAX_EDGE+1)==APP_INVALID);
    app.widgets.in_frame=true;
    CHECK(app_collection_image(&app,&item,64)==APP_INVALID);
    CHECK(app_collection_image(NULL,&item,64)==APP_INVALID);
}
int main(void)
{
    sources_and_lifetime(); missing_and_unsupported(); malformed(); bounds(); identities_and_preparation();
    puts("collection artwork: typed paths, copied Unicode URLs, limits, malformed data and source identities passed");
    return 0;
}
