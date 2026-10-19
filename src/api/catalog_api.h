#ifndef SPOTIC_CATALOG_API_H
#define SPOTIC_CATALOG_API_H
#include "api_contract.h"
#include "json_view.h"

#define CATALOG_PAGE_LIMIT 100u
#define CATALOG_END UINT32_MAX
#define CATALOG_KEY_CAP 1024u
typedef enum CatalogResult {
    CATALOG_OK, CATALOG_PARTIAL, CATALOG_PENDING, CATALOG_DEBOUNCING, CATALOG_FULL,
    CATALOG_INVALID, CATALOG_STALE, CATALOG_CANCELLED, CATALOG_AUTH, CATALOG_RATE_LIMIT,
    CATALOG_ERROR, CATALOG_TOO_LARGE, CATALOG_NO_MEMORY, CATALOG_ORDER,
    CATALOG_BOUNDARY, CATALOG_NOT_FOUND, CATALOG_RESTRICTED, CATALOG_UNSUPPORTED,
    CATALOG_HASH_MISMATCH
} CatalogResult;
typedef enum SearchCategory { SEARCH_TOP, SEARCH_TRACKS, SEARCH_ALBUMS, SEARCH_ARTISTS, SEARCH_LIST } SearchCategory;
typedef enum ArtistSection { ARTIST_OVERVIEW, ARTIST_TOP_TRACKS, ARTIST_ALBUMS,
    ARTIST_SINGLES, ARTIST_POPULAR_RELEASES, ARTIST_RELATED } ArtistSection;
typedef enum CatalogKind { CATALOG_ITEM_UNSUPPORTED, CATALOG_ITEM_UNAVAILABLE,
    CATALOG_TRACK, CATALOG_ALBUM, CATALOG_ARTIST } CatalogKind;
typedef struct CatalogId { uint8_t bytes[16]; bool valid; } CatalogId;
typedef struct CatalogImage { JsonString url; uint32_t width, height; } CatalogImage;
typedef struct CatalogItem {
    CatalogKind kind;
    JsonString uri, name, discriminator, restriction;
    CatalogId id;
    CatalogImage image;
    bool available;
    union {
        struct { CatalogId playable_id; JsonString playable_uri, album_uri, artist_name;
            uint32_t duration_ms; yyjson_val *relinking; } track;
        struct { JsonString artist_name; uint32_t total_tracks; } album;
        struct { uint64_t followers; } artist;
    } as;
} CatalogItem;
typedef struct CatalogPage {
    JsonPage json;
    ApiOperation operation;
    uint32_t offset, limit, next, total, count;
    bool partial, boundary, truncated, total_known;
    JsonString requested_key;
    CatalogItem detail, items[CATALOG_PAGE_LIMIT];
} CatalogPage;
typedef struct CatalogTicket { uint32_t slot; uint64_t serial; } CatalogTicket;
typedef struct CatalogRequest {
    CatalogTicket ticket;
    const ApiContract *contract; /* Canonical URL/method/hash/header allowlist. */
    const char *upload; size_t upload_bytes;
    char *body; size_t capacity;
    uint64_t generation;
} CatalogRequest;
typedef struct CatalogConfig {
    JsonString locale; /* Copied, <=63 UTF-8 bytes; empty is evidenced. */
    uint32_t debounce_ms, image_width, album_limit;
    bool pre_release_v2;
} CatalogConfig;
typedef struct CatalogService CatalogService;

/* All service methods have ONE network owner. UI only retains/releases immutable
 * JsonPages delivered through a synchronized queue. Exactly the same pool must
 * be passed to collections and catalog. Models/requests use caller model memory.
 * Init only fresh storage; storage and pool outlive every outstanding reference. */
size_t catalog_service_size(void);
size_t catalog_service_alignment(void);
CatalogResult catalog_service_init(void *storage, size_t bytes, JsonPagePool *pool,
    const CatalogConfig *config, CatalogService **output);
CatalogResult catalog_reset(CatalogService *service, uint64_t generation);
CatalogResult search_request(CatalogService *service, JsonString query, SearchCategory category,
    uint32_t offset, uint32_t limit, uint64_t generation, uint64_t now_ms, CatalogRequest *output);
CatalogResult album_request(CatalogService *service, JsonString uri, uint32_t cursor,
    uint64_t generation, CatalogRequest *output);
/* Sections select bounded data from ONE overview document; cursor must be zero.
 * Unevidenced discography continuation operations return UNSUPPORTED. */
CatalogResult artist_request(CatalogService *service, JsonString uri, ArtistSection section,
    uint32_t cursor, uint64_t generation, CatalogRequest *output);
uint32_t catalog_next_wait_ms(const CatalogService *service, uint64_t now_ms);
CatalogResult catalog_cancel(CatalogService *service, CatalogTicket ticket);
CatalogResult catalog_cancel_generation(CatalogService *service, uint64_t generation);
/* Transport reports the executed hash independently of response JSON. Request
 * upload/input remain borrowed until complete, including after cancellation.
 * OK/PARTIAL/BOUNDARY publish one owned reference, which the caller must release.
 * Other outcomes clear output. No automatic retry or hash substitution. */
CatalogResult catalog_complete(CatalogService *service, CatalogTicket ticket, CatalogResult result,
    unsigned status, size_t bytes, JsonString executed_hash, const CatalogPage **output);
#endif
