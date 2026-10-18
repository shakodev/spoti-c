#ifndef SPOTIC_PROTOCOL_CREDENTIAL_STORE_H
#define SPOTIC_PROTOCOL_CREDENTIAL_STORE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CREDENTIAL_VERSION 1u
#define CREDENTIAL_CLIENT_CAP 129u
#define CREDENTIAL_REFRESH_CAP 2049u
#define CREDENTIAL_USERNAME_CAP 1024u
#define CREDENTIAL_BLOB_CAP 1024u
#define CREDENTIAL_FILE_LIMIT 16384u

typedef struct CredentialRecord {
    uint32_t version, ap_type, ap_bytes;
    char client_id[CREDENTIAL_CLIENT_CAP], refresh_token[CREDENTIAL_REFRESH_CAP];
    char username[CREDENTIAL_USERNAME_CAP], device_id[41];
    uint8_t ap_blob[CREDENTIAL_BLOB_CAP];
} CredentialRecord;
typedef enum CredentialResult { CREDENTIAL_OK, CREDENTIAL_INVALID, CREDENTIAL_NOT_FOUND,
    CREDENTIAL_LIMIT, CREDENTIAL_CORRUPT, CREDENTIAL_OS_ERROR } CredentialResult;
typedef struct CredentialStore { size_t live_handles, live_allocations; bool cleanup_failed; } CredentialStore;

/* Single owner; start store at {0}. Caller supplies a private local directory.
 * No plaintext/access tokens on disk. Failures clear load output. File paths
 * must leave room for the bounded temporary suffix (<=230 wchar_t). */
CredentialResult credential_record_init(CredentialRecord *record, const char *client_id);
bool credential_record_valid(const CredentialRecord *record);
void credential_record_clear(CredentialRecord *record);
CredentialResult credential_store_save(CredentialStore *store, const wchar_t *path, const CredentialRecord *record);
CredentialResult credential_store_load(CredentialStore *store, const wchar_t *path, CredentialRecord *record);
/* Wipes the record even if deletion fails; NOT_FOUND is successful logout. */
CredentialResult credential_store_logout(CredentialStore *store, const wchar_t *path, CredentialRecord *record);
bool credential_store_balanced(const CredentialStore *store);
#endif
