#ifndef SPOTIC_API_CONTRACT_H
#define SPOTIC_API_CONTRACT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define API_CONTRACT_REQUEST_CAP ((size_t)16384)
#define API_CONTRACT_RESPONSE_CAP ((size_t)262144)
#define API_CONTRACT_FIELD_LIMIT ((size_t)32)

typedef enum ApiOperation {
    API_HOME, API_HOME_SECTION, API_LIBRARY, API_PLAYLIST,
    API_SEARCH, API_SEARCH_TRACKS, API_ALBUM, API_ALBUM_TRACKS,
    API_ARTIST, API_LYRICS, API_SEARCH_LIST, API_OPERATION_COUNT
} ApiOperation;
typedef enum ApiContractResult {
    API_CONTRACT_OK, API_CONTRACT_INVALID, API_CONTRACT_UNAVAILABLE,
    API_CONTRACT_TOO_LARGE
} ApiContractResult;
typedef enum ApiTransport { API_PATHFINDER, API_HTTP_JSON, API_NATIVE_RPC } ApiTransport;
typedef enum ApiMethod { API_METHOD_UNKNOWN, API_METHOD_GET, API_METHOD_POST, API_METHOD_RPC } ApiMethod;
typedef enum ApiFieldType {
    API_FIELD_STRING, API_FIELD_UINT, API_FIELD_BOOL, API_FIELD_STRING_ARRAY,
    API_FIELD_OBJECT, API_FIELD_ARRAY
} ApiFieldType;
typedef struct ApiField {
    const char *name; /* Variable name or JSON Pointer; immutable UTF-8. */
    ApiFieldType type;
    bool required;
    bool nullable;
} ApiField;

/* Proof of each item is independent: a document hash never proves its headers,
 * native RPC observation never proves a backing HTTPS request, and synthetic
 * negative fixtures never prove successful service acceptance. */
enum {
    API_PROOF_SOURCE = 1u << 0, API_PROOF_URL = 1u << 1,
    API_PROOF_METHOD = 1u << 2, API_PROOF_HEADERS = 1u << 3,
    API_PROOF_VARIABLES = 1u << 4, API_PROOF_RESPONSE = 1u << 5,
    API_PROOF_TOKENS = 1u << 6, API_PROOF_RETRY = 1u << 7,
    API_PROOF_POSITIVE = 1u << 8, API_PROOF_ERROR = 1u << 9,
    API_PROOF_PAGINATION = 1u << 10,
    API_PROOF_ALL = (1u << 11) - 1
};
typedef struct ApiContract {
    ApiOperation operation;
    const char *name;
    ApiTransport transport;
    ApiMethod method;
    const char *url; /* NULL for native RPC or unresolved HTTP; explicit templates. */
    const char *wire_operation;
    const char *persisted_sha256; /* NULL for non-GraphQL operations. */
    const char *source_file;
    const char *source_sha256;
    const char *evidence_id;
    const ApiField *headers;
    size_t header_count;
    const ApiField *variables;
    size_t variable_count;
    const ApiField *response;
    size_t response_count;
    size_t request_cap;
    size_t response_cap;
    uint32_t proofs;
    const char *blocker; /* Nonempty for every incomplete catalog entry. */
    const char *rpc_service; /* Exact local service identity; NULL for HTTP. */
} ApiContract;

/* No allocation, I/O, token acquisition or endpoint execution. All pointers
 * borrow immutable process-lifetime storage; caller must never free them.
 * Lookup clears output on every failure. Lookup returns proven descriptive
 * metadata, never an executable endpoint or an installed transport capability.
 * Callers must branch on transport: native RPC requires the official client's
 * local bridge or a separately implemented service; it cannot be sent by HTTP.
 * Inspect exposes incomplete evidence for diagnostics only. */
const ApiContract *contract_inspect(ApiOperation operation);
ApiContractResult contract_lookup(ApiOperation operation, const ApiContract **output);
ApiContractResult contract_validate(const ApiContract *contract);
uint32_t contract_missing_proofs(const ApiContract *contract);
ApiContractResult contract_check_body(const ApiContract *contract, bool response, size_t bytes);
#endif
