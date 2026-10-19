#ifndef SPOTIC_COLLECTION_API_H
#define SPOTIC_COLLECTION_API_H
#include "api_contract.h"
#include "json_view.h"

#define COLLECTION_ROW_CAP 128u
#define COLLECTION_END UINT32_MAX
#define COLLECTION_MODEL_BUDGET ((size_t)64 * 1024)
typedef enum CollectionKind { COLLECTION_HOME, COLLECTION_HOME_SECTION,
    COLLECTION_LIBRARY, COLLECTION_PLAYLIST } CollectionKind;
typedef enum CollectionResult {
    COLLECTION_OK, COLLECTION_PARTIAL, COLLECTION_PENDING, COLLECTION_FULL,
    COLLECTION_INVALID, COLLECTION_STALE, COLLECTION_CANCELLED, COLLECTION_AUTH,
    COLLECTION_RATE_LIMIT, COLLECTION_ERROR, COLLECTION_TOO_LARGE, COLLECTION_NO_MEMORY,
    COLLECTION_ORDER, COLLECTION_REVISION, COLLECTION_BOUNDARY
} CollectionResult;
typedef enum CollectionItemKind { COLLECTION_UNKNOWN, COLLECTION_SHELF,
    COLLECTION_TRACK, COLLECTION_ALBUM, COLLECTION_ARTIST, COLLECTION_PLAYLIST_ITEM,
    COLLECTION_EPISODE, COLLECTION_FOLDER, COLLECTION_LIKED, COLLECTION_UNAVAILABLE
} CollectionItemKind;
typedef struct CollectionItem {
    CollectionItemKind kind;
    JsonString id, uri, name, discriminator;
    yyjson_val *source; /* Borrowed immutable object, typed readers only. */
    uint8_t compact_id[16]; /* Spotify URI identity; rowId remains distinct. */
    bool has_compact_id, available, duplicate, pinned;
} CollectionItem;
typedef struct CollectionPage {
    JsonPage json;
    CollectionKind kind;
    uint32_t offset, next, total, count, pinned_count;
    bool partial, loading, boundary;
    JsonString revision, name;
    yyjson_val *metadata, *pinned; /* Kept separate from paginated rows. */
    CollectionItem items[COLLECTION_ROW_CAP];
} CollectionPage;
typedef struct CollectionTicket { uint32_t slot; uint64_t serial; } CollectionTicket;
typedef struct CollectionRequest {
    CollectionTicket ticket;
    const ApiContract *contract;
    CollectionKind kind;
    uint64_t generation;
    uint32_t cursor, limit;
    char *body; /* Transport writes <= capacity; keep until complete, even after cancel. */
    size_t capacity;
} CollectionRequest;
typedef struct CollectionService CollectionService;

/* Caller allocates aligned service storage from the shared compact-model budget.
 * No heap allocations. ONE network owner calls all service methods. The UI
 * receives retained immutable pages through a synchronized queue and may only
 * call page_retain/release. Retained input/pool/service storage must outlive UI.
 * Reset releases service's current page; destroy is reset + wait for all slots
 * to be idle. Do not memset live service storage. */
size_t collection_service_size(void);
size_t collection_service_alignment(void);
CollectionResult collection_service_init(void *storage, size_t bytes, JsonPagePool *pool, CollectionService **output);
/* Only while no slots are occupied. Copies an optional account-specific liked
 * playlist URI supplied by the adapter; never infers a universal URI. */
CollectionResult collection_configure(CollectionService *service, uint32_t page_limit, JsonString liked_uri);
CollectionResult collection_reset(CollectionService *service, uint64_t generation);
CollectionResult collection_fetch(CollectionService *service, CollectionKind kind,
    uint32_t cursor, uint64_t generation, CollectionRequest *request);
/* Delivery is a raw PathFinder body or a native generated-client projection,
 * NOT the predecessor fixture envelope. Status is the transport/native status;
 * result reports transport cancellation/failure. No automatic retry.
 * OK, PARTIAL and BOUNDARY publish one owned reference: release it even if the
 * next cursor is END. Every other outcome clears output and publishes nothing. */
CollectionResult collection_complete(CollectionService *service, CollectionTicket ticket,
    CollectionResult result, unsigned status, size_t bytes, const CollectionPage **output);
CollectionResult collection_cancel(CollectionService *service, CollectionTicket ticket);
size_t collection_resident_pages(const CollectionService *service);
/* Reconstruct a typed shelf member; it borrows the shelf page's reference. */
CollectionResult collection_shelf_item(const CollectionPage *page, uint32_t shelf,
    uint32_t index, CollectionItem *output);
#endif
