#include "api_contract.h"
#include <string.h>

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))
#define SNAPSHOT "c20d9408009a281ee4152b63077a8309eb17ec172bbf80b80dc7879d586bb099"
#define SNAPSHOT_EV "ev_1598043638d84cda7402b5b188cf254b6ab9c1a85e83d6a54351050338a8f2e3"
#define NATIVE_EV "ev_d9373a110e6d8f723a4c2ccf46fc3431ba041c63e3bad0c1e3b91818566661ca"
#define SEARCH_EV "ev_40a8864feaeed9523967983d24c87a0f7b49671e3921eb187ddef6af5b19344d"
#define QUERY_URL "https://api-partner.spotify.com/pathfinder/v2/query"
#define F(n,t,r,z) {n,t,r,z}
static const ApiField query_headers[] = {
    F("Authorization",API_FIELD_STRING,true,false), F("Accept",API_FIELD_STRING,true,false),
    F("Content-Type",API_FIELD_STRING,true,false), F("App-Platform",API_FIELD_STRING,true,false),
    F("Spotify-App-Version",API_FIELD_STRING,true,false), F("Accept-Language",API_FIELD_STRING,false,false)
};
static const ApiField lyrics_headers[] = {
    F("Authorization",API_FIELD_STRING,true,false), F("Accept",API_FIELD_STRING,true,false),
    F("App-Platform",API_FIELD_STRING,true,false), F("Spotify-App-Version",API_FIELD_STRING,true,false),
    F("Accept-Language",API_FIELD_STRING,false,false)
};
static const ApiField home_vars[] = {
    F("homeEndUserIntegration",API_FIELD_STRING,true,false), F("timeZone",API_FIELD_STRING,true,false),
    F("sp_t",API_FIELD_STRING,true,false), F("facet",API_FIELD_STRING,true,false),
    F("sectionItemsLimit",API_FIELD_UINT,true,false), F("includeEpisodeContentRatingsV2",API_FIELD_BOOL,true,false)
};
static const ApiField section_vars[] = {
    F("uri",API_FIELD_STRING,true,false), F("homeEndUserIntegration",API_FIELD_STRING,true,false),
    F("timeZone",API_FIELD_STRING,true,false), F("sp_t",API_FIELD_STRING,true,false),
    F("sectionItemsOffset",API_FIELD_UINT,true,false), F("sectionItemsLimit",API_FIELD_UINT,true,false),
    F("includeEpisodeContentRatingsV2",API_FIELD_BOOL,true,false)
};
static const ApiField album_vars[] = {
    F("uri",API_FIELD_STRING,true,false), F("locale",API_FIELD_STRING,true,false),
    F("offset",API_FIELD_UINT,true,false), F("limit",API_FIELD_UINT,true,false)
};
static const ApiField artist_vars[] = {
    F("uri",API_FIELD_STRING,true,false), F("locale",API_FIELD_STRING,true,false),
    F("preReleaseV2",API_FIELD_BOOL,true,false)
};
static const ApiField search_tracks_vars[] = {
    F("searchTerm",API_FIELD_STRING,true,false), F("offset",API_FIELD_UINT,true,false),
    F("limit",API_FIELD_UINT,true,false), F("numberOfTopResults",API_FIELD_UINT,true,false),
    F("includeAudiobooks",API_FIELD_BOOL,true,false), F("includePreReleases",API_FIELD_BOOL,true,false),
    F("includeAlbumPreReleases",API_FIELD_BOOL,true,false), F("includeAuthors",API_FIELD_BOOL,true,false),
    F("includeEpisodeContentRatingsV2",API_FIELD_BOOL,true,false)
};
static const ApiField search_list_vars[] = {
    F("query",API_FIELD_STRING,true,false), F("limit",API_FIELD_UINT,true,false),
    F("offset",API_FIELD_UINT,true,false), F("numberOfTopResults",API_FIELD_UINT,true,false),
    F("includeArtistHasConcertsField",API_FIELD_BOOL,true,false), F("includeLocalConcertsField",API_FIELD_BOOL,false,false),
    F("includeAudiobooks",API_FIELD_BOOL,true,false), F("includeAuthors",API_FIELD_BOOL,true,false),
    F("includePreReleases",API_FIELD_BOOL,true,false), F("includeAlbumPreReleases",API_FIELD_BOOL,true,false),
    F("includeEpisodeContentRatingsV2",API_FIELD_BOOL,true,false), F("isPrefix",API_FIELD_BOOL,true,true),
    F("sectionFilters",API_FIELD_STRING_ARRAY,true,false), F("clientFeatures",API_FIELD_ARRAY,false,false)
};
static const ApiField lyrics_vars[] = {
    F("trackId",API_FIELD_STRING,true,false), F("image",API_FIELD_STRING,true,false),
    F("format",API_FIELD_STRING,true,false), F("vocalRemoval",API_FIELD_BOOL,true,false),
    F("clientLanguage",API_FIELD_STRING,false,false)
};
static const ApiField home_response[] = {
    F("/data/home/__typename",API_FIELD_STRING,true,false),
    F("/data/home/sectionContainer/sections/items",API_FIELD_ARRAY,true,false),
    F("/data/home/sectionContainer/sections/totalCount",API_FIELD_UINT,true,false)
};
static const ApiField album_response[] = {F("/data/albumUnion/__typename",API_FIELD_STRING,true,false)};
static const ApiField section_response[] = {F("/data/homeSections/__typename",API_FIELD_STRING,true,false)};
static const ApiField search_response[] = {F("/data/searchV2",API_FIELD_OBJECT,true,false)};
static const ApiField artist_response[] = {F("/data/artistUnion/__typename",API_FIELD_STRING,true,false)};
static const ApiField lyrics_response[] = {
    F("/lyrics/syncType",API_FIELD_STRING,true,false), F("/lyrics/lines",API_FIELD_ARRAY,true,false),
    F("/lyrics/previewLines",API_FIELD_ARRAY,true,false), F("/colors",API_FIELD_OBJECT,true,false)
};
#define STATIC_HTTP (API_PROOF_SOURCE | API_PROOF_URL | API_PROOF_METHOD | API_PROOF_HEADERS | API_PROOF_TOKENS | API_PROOF_RETRY)
#define OBSERVED_HTTP (STATIC_HTTP | API_PROOF_VARIABLES | API_PROOF_RESPONSE | API_PROOF_POSITIVE)
#define ROW(op,label,kind,verb,url,wire,hash,source,ev,vars,resp,proof,why) \
    {op,label,kind,verb,url,wire,hash,source,SNAPSHOT,ev,query_headers,COUNT(query_headers), \
     vars,COUNT(vars),resp,COUNT(resp),API_CONTRACT_REQUEST_CAP,API_CONTRACT_RESPONSE_CAP,proof,why}
static const ApiContract contracts[] = {
    ROW(API_HOME,"home",API_PATHFINDER,API_METHOD_POST,QUERY_URL,"home",
        "76243c78b0e20ecdbe41b794dec8cbe73f75e585b0a7201b8d2e84578412847a",
        "v8_context_snapshot.bin:26662,83314",SNAPSHOT_EV,home_vars,home_response,OBSERVED_HTTP,
        "Error and pagination fixture coverage is incomplete"),
    ROW(API_HOME_SECTION,"home_section",API_PATHFINDER,API_METHOD_POST,QUERY_URL,"homeSection",
        "76243c78b0e20ecdbe41b794dec8cbe73f75e585b0a7201b8d2e84578412847a",
        "v8_context_snapshot.bin:26662",SNAPSHOT_EV,section_vars,section_response,STATIC_HTTP,
        "Section response, variables and pagination require independent validation"),
    {API_LIBRARY,"library",API_NATIVE_RPC,API_METHOD_UNKNOWN,NULL,"YourLibraryService.All",NULL,
        "v8_context_snapshot.bin:90879,82379",SNAPSHOT,NATIVE_EV,NULL,0,NULL,0,NULL,0,
        API_CONTRACT_REQUEST_CAP,API_CONTRACT_RESPONSE_CAP,API_PROOF_SOURCE,
        "Observed native Esperanto RPC does not establish its backing HTTPS contract"},
    {API_PLAYLIST,"playlist",API_NATIVE_RPC,API_METHOD_UNKNOWN,NULL,"PlaylistDataService.Get",NULL,
        "v8_context_snapshot.bin:33872,82379",SNAPSHOT,NATIVE_EV,NULL,0,NULL,0,NULL,0,
        API_CONTRACT_REQUEST_CAP,API_CONTRACT_RESPONSE_CAP,API_PROOF_SOURCE,
        "Primary view uses native Esperanto RPC; playlistSection is not a replacement"},
    {API_SEARCH,"search",API_PATHFINDER,API_METHOD_POST,QUERY_URL,"searchDesktop",
        "1148393611bbc58e84e47aed35ecc731275df9f9eb660956962e352dd3631d89",
        "ref/xpui/xpui-routes-search.js","06ef397f7adcbbf695f2bc08b2aef737ad05325712554f0d75135f0da547b4f0",
        SEARCH_EV,query_headers,COUNT(query_headers),NULL,0,NULL,0,API_CONTRACT_REQUEST_CAP,
        API_CONTRACT_RESPONSE_CAP,STATIC_HTTP,"Legacy searchDesktop and list/suggestion variables must not be conflated"},
    {API_SEARCH_TRACKS,"search_tracks",API_PATHFINDER,API_METHOD_POST,QUERY_URL,"searchTracks",
        "b02683192a98dde7966b5e6655a79eeb62713eab703eda9902c932818dd52751",
        "ref/xpui/xpui-routes-search.js","06ef397f7adcbbf695f2bc08b2aef737ad05325712554f0d75135f0da547b4f0",
        SEARCH_EV,query_headers,COUNT(query_headers),search_tracks_vars,COUNT(search_tracks_vars),
        search_response,COUNT(search_response),API_CONTRACT_REQUEST_CAP,
        API_CONTRACT_RESPONSE_CAP,OBSERVED_HTTP | API_PROOF_PAGINATION,"Category error fixture coverage remains incomplete"},
    ROW(API_ALBUM,"album",API_PATHFINDER,API_METHOD_POST,QUERY_URL,"getAlbum",
        "6a74b456cd1735c9193d9e8ec8cc5184cad7ce13572210315229db3975964361",
        "v8_context_snapshot.bin:2410,36920",SNAPSHOT_EV,album_vars,album_response,API_PROOF_ALL,NULL),
    ROW(API_ALBUM_TRACKS,"album_tracks",API_PATHFINDER,API_METHOD_POST,QUERY_URL,"queryAlbumTracks",
        "6a74b456cd1735c9193d9e8ec8cc5184cad7ce13572210315229db3975964361",
        "v8_context_snapshot.bin:2410",SNAPSHOT_EV,album_vars,album_response,OBSERVED_HTTP | API_PROOF_PAGINATION,
        "Independent queryAlbumTracks error fixture coverage remains incomplete"),
    ROW(API_ARTIST,"artist",API_PATHFINDER,API_METHOD_POST,QUERY_URL,"queryArtistOverview",
        "9f8134ef565e78621f1e1793555bd6633c5ac144ae0f89604ed3ae3f80b3c8e6",
        "v8_context_snapshot.bin:41231",SNAPSHOT_EV,artist_vars,artist_response,OBSERVED_HTTP,
        "Error/continuation coverage is incomplete; 3308.js contains a different document hash"),
    {API_LYRICS,"lyrics",API_HTTP_JSON,API_METHOD_GET,
        "https://spclient.wg.spotify.com/color-lyrics/v2/track/{trackId}/image/{image}",NULL,NULL,
        "v8_context_snapshot.bin:2200,24063",SNAPSHOT,SNAPSHOT_EV,lyrics_headers,COUNT(lyrics_headers),
        lyrics_vars,COUNT(lyrics_vars),lyrics_response,COUNT(lyrics_response),
        API_CONTRACT_REQUEST_CAP,API_CONTRACT_RESPONSE_CAP,OBSERVED_HTTP | API_PROOF_ERROR,
        "Timing/rights unions and explicit non-pagination proof remain incomplete"},
    {API_SEARCH_LIST,"search_list",API_PATHFINDER,API_METHOD_POST,QUERY_URL,"searchTopResultsList",
        "12d940b3f7381506a82ff5382eb59b0ed65118656e6bcdfbe4cebc528f75d29d",
        "ref/xpui/xpui-routes-search.js","06ef397f7adcbbf695f2bc08b2aef737ad05325712554f0d75135f0da547b4f0",
        SEARCH_EV,query_headers,COUNT(query_headers),search_list_vars,COUNT(search_list_vars),
        search_response,COUNT(search_response),API_CONTRACT_REQUEST_CAP,
        API_CONTRACT_RESPONSE_CAP,OBSERVED_HTTP,"List-branch error and continuation fixtures remain incomplete"}
};
_Static_assert(COUNT(contracts) == API_OPERATION_COUNT, "Every operation needs a catalog entry");

static bool text(const char *value) { return value != NULL && value[0] != '\0'; }
static bool hex64(const char *value)
{
    if (value == NULL) { return false; }
    size_t i = 0;
    for (; i < 64 && value[i] != '\0'; ++i) {
        if (!((value[i] >= '0' && value[i] <= '9') || (value[i] >= 'a' && value[i] <= 'f'))) { return false; }
    }
    return i == 64 && value[i] == '\0';
}
static bool fields(const ApiField *list, size_t count)
{
    if (count > API_CONTRACT_FIELD_LIMIT || (count != 0 && list == NULL)) { return false; }
    for (size_t i = 0; i < count; ++i) {
        if (!text(list[i].name) || (unsigned)list[i].type > API_FIELD_ARRAY) { return false; }
        for (size_t j = 0; j < i; ++j) {
            if (strcmp(list[i].name, list[j].name) == 0) { return false; }
        }
    }
    return true;
}
uint32_t contract_missing_proofs(const ApiContract *contract)
{
    return contract == NULL ? API_PROOF_ALL : API_PROOF_ALL & ~contract->proofs;
}
ApiContractResult contract_validate(const ApiContract *c)
{
    if (c == NULL || (unsigned)c->operation >= API_OPERATION_COUNT || !text(c->name) ||
        (unsigned)c->transport > API_NATIVE_RPC || (unsigned)c->method > API_METHOD_POST ||
        (c->proofs & ~(uint32_t)API_PROOF_ALL) != 0 ||
        c->request_cap == 0 || c->request_cap > API_CONTRACT_REQUEST_CAP ||
        c->response_cap == 0 || c->response_cap > API_CONTRACT_RESPONSE_CAP ||
        !fields(c->headers,c->header_count) || !fields(c->variables,c->variable_count) ||
        !fields(c->response,c->response_count)) { return API_CONTRACT_INVALID; }
    if ((c->proofs & API_PROOF_SOURCE) != 0 && (!text(c->source_file) || !hex64(c->source_sha256) ||
        !text(c->evidence_id) || strncmp(c->evidence_id,"ev_",3) != 0 || !hex64(c->evidence_id + 3))) {
        return API_CONTRACT_INVALID;
    }
    if (c->persisted_sha256 != NULL && !hex64(c->persisted_sha256)) { return API_CONTRACT_INVALID; }
    if ((c->proofs & API_PROOF_URL) != 0 && (!text(c->url) || strncmp(c->url,"https://",8) != 0)) {
        return API_CONTRACT_INVALID;
    }
    if ((c->proofs & API_PROOF_METHOD) != 0 && c->method == API_METHOD_UNKNOWN) { return API_CONTRACT_INVALID; }
    if (contract_missing_proofs(c) != 0) {
        return text(c->blocker) ? API_CONTRACT_UNAVAILABLE : API_CONTRACT_INVALID;
    }
    if (!text(c->url) || c->method == API_METHOD_UNKNOWN ||
        c->transport == API_NATIVE_RPC || c->header_count == 0 || c->response_count == 0 ||
        (c->transport == API_PATHFINDER && (!text(c->wire_operation) || !hex64(c->persisted_sha256)))) {
        return API_CONTRACT_INVALID;
    }
    return API_CONTRACT_OK;
}
const ApiContract *contract_inspect(ApiOperation operation)
{
    return (unsigned)operation >= API_OPERATION_COUNT ? NULL : &contracts[operation];
}
ApiContractResult contract_lookup(ApiOperation operation, const ApiContract **output)
{
    if (output == NULL) { return API_CONTRACT_INVALID; }
    *output = NULL;
    const ApiContract *c = contract_inspect(operation);
    ApiContractResult result = contract_validate(c);
    if (result == API_CONTRACT_OK) { *output = c; }
    return result;
}
ApiContractResult contract_check_body(const ApiContract *contract, bool response, size_t bytes)
{
    ApiContractResult result = contract_validate(contract);
    if (result != API_CONTRACT_OK) { return result; }
    return bytes > (response ? contract->response_cap : contract->request_cap) ?
        API_CONTRACT_TOO_LARGE : API_CONTRACT_OK;
}
