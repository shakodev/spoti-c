#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincrypt.h>
#include <aclapi.h>
#include "protocol/credential_store.h"
#include "support/test_check.h"
#include <string.h>
#include <wchar.h>

void test_ap_auth(void);
static wchar_t directory[MAX_PATH], path[MAX_PATH];
static bool zero(const void *data, size_t bytes)
{
    const uint8_t *p = data;
    for (size_t i = 0; i < bytes; ++i) { if (p[i] != 0) { return false; } } return true;
}
static void write_file(const void *data, DWORD bytes)
{
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    CHECK(file != INVALID_HANDLE_VALUE); DWORD written;
    CHECK(WriteFile(file, data, bytes, &written, NULL) && written == bytes && CloseHandle(file));
}
static void record_fixture(CredentialRecord *r)
{
    CHECK(credential_record_init(r, "fixture-client") == CREDENTIAL_OK);
    memcpy(r->refresh_token, "refresh-secret-fixture", sizeof("refresh-secret-fixture"));
    memcpy(r->username, "canonical-fixture", sizeof("canonical-fixture"));
    r->ap_type = 2; r->ap_bytes = 6; memcpy(r->ap_blob, "\0\xff" "blob", 6); CHECK(credential_record_valid(r));
}
static void roundtrip_and_replace(void)
{
    CredentialStore store = {0}; CredentialRecord record, loaded;
    record_fixture(&record);
    CHECK(credential_store_load(&store, path, &loaded) == CREDENTIAL_NOT_FOUND && zero(&loaded, sizeof(loaded)));
    CHECK(credential_store_save(&store, path, &record) == CREDENTIAL_OK && credential_store_balanced(&store));
    CHECK(credential_store_load(&store, path, &loaded) == CREDENTIAL_OK && memcmp(&record, &loaded, sizeof(record)) == 0);
    uint8_t encrypted[CREDENTIAL_FILE_LIMIT]; DWORD bytes;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    CHECK(file != INVALID_HANDLE_VALUE && ReadFile(file, encrypted, sizeof(encrypted), &bytes, NULL) && CloseHandle(file));
    CHECK(bytes > 0);
    for (size_t i = 0; i + strlen(record.refresh_token) <= bytes; ++i) {
        CHECK(memcmp(encrypted + i, record.refresh_token, strlen(record.refresh_token)) != 0);
    }
    PSECURITY_DESCRIPTOR descriptor = NULL; PACL dacl = NULL; BOOL present, defaulted;
    CHECK(GetNamedSecurityInfoW(path, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, NULL, NULL, &dacl, NULL, &descriptor) == ERROR_SUCCESS);
    SECURITY_DESCRIPTOR_CONTROL control; DWORD revision;
    CHECK(GetSecurityDescriptorControl(descriptor, &control, &revision) && (control & SE_DACL_PROTECTED) != 0);
    CHECK(GetSecurityDescriptorDacl(descriptor, &present, &dacl, &defaulted) && present && dacl != NULL && dacl->AceCount == 2);
    CHECK(LocalFree(descriptor) == NULL);
    char device[41]; memcpy(device, record.device_id, sizeof(device));
    SecureZeroMemory(record.refresh_token, sizeof(record.refresh_token)); SecureZeroMemory(record.username, sizeof(record.username));
    memcpy(record.refresh_token, "rotated", 8); memcpy(record.username, "new-name", 9); record.ap_type = 1;
    CHECK(credential_store_save(&store, path, &record) == CREDENTIAL_OK);
    CHECK(credential_store_load(&store, path, &loaded) == CREDENTIAL_OK && memcmp(&record, &loaded, sizeof(record)) == 0 &&
        strcmp(loaded.device_id, device) == 0);
    /* A reader denying delete makes replacement fail; the committed record
     * must survive, and the exclusive temporary file must be removed. */
    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    CHECK(file != INVALID_HANDLE_VALUE); CredentialRecord next = record; memcpy(next.refresh_token, "blocked", 8);
    CHECK(credential_store_save(&store, path, &next) == CREDENTIAL_OS_ERROR && credential_store_balanced(&store));
    CHECK(CloseHandle(file));
    CHECK(credential_store_load(&store, path, &loaded) == CREDENTIAL_OK && memcmp(&record, &loaded, sizeof(record)) == 0);
    wchar_t pattern[MAX_PATH]; CHECK(swprintf_s(pattern, MAX_PATH, L"%ls.*.tmp", path) > 0);
    WIN32_FIND_DATAW found; HANDLE find = FindFirstFileW(pattern, &found); CHECK(find == INVALID_HANDLE_VALUE && GetLastError() == ERROR_FILE_NOT_FOUND);
    CHECK(credential_store_logout(&store, path, &record) == CREDENTIAL_OK && zero(&record, sizeof(record)));
    CHECK(credential_store_logout(&store, path, &record) == CREDENTIAL_OK);
    credential_record_clear(&loaded); credential_record_clear(&next); CHECK(credential_store_balanced(&store));
}
static void invalid_files(void)
{
    CredentialStore store = {0}; CredentialRecord record, loaded; record_fixture(&record);
    CHECK(credential_store_save(&store, path, &record) == CREDENTIAL_OK);
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    CHECK(file != INVALID_HANDLE_VALUE); DWORD written; uint8_t bad[64] = {0};
    CHECK(WriteFile(file, bad, sizeof(bad), &written, NULL) && CloseHandle(file));
    CHECK(credential_store_load(&store, path, &loaded) == CREDENTIAL_CORRUPT && zero(&loaded, sizeof(loaded)));
    /* A valid DPAPI blob with foreign application entropy exercises the same
     * fail-closed path as an unreadable CurrentUser blob, without another user. */
    DATA_BLOB input = {sizeof(bad), bad}, protected = {0}; uint8_t foreign[] = "foreign-context";
    DATA_BLOB entropy = {sizeof(foreign), foreign};
    CHECK(CryptProtectData(&input, NULL, &entropy, NULL, NULL, CRYPTPROTECT_UI_FORBIDDEN, &protected));
    write_file(protected.pbData, protected.cbData); SecureZeroMemory(protected.pbData, protected.cbData); CHECK(LocalFree(protected.pbData) == NULL);
    CHECK(credential_store_load(&store, path, &loaded) == CREDENTIAL_CORRUPT && zero(&loaded, sizeof(loaded)));
    uint8_t malformed[68] = {'S','P','C','R', 2}; /* Unsupported schema, protected with this application's entropy. */
    uint8_t own[] = "spoti-c/credentials/v1"; input.pbData = malformed; input.cbData = sizeof(malformed);
    entropy.pbData = own; entropy.cbData = sizeof(own); protected = (DATA_BLOB){0};
    CHECK(CryptProtectData(&input, NULL, &entropy, NULL, NULL, CRYPTPROTECT_UI_FORBIDDEN, &protected));
    write_file(protected.pbData, protected.cbData); CHECK(LocalFree(protected.pbData) == NULL);
    CHECK(credential_store_load(&store, path, &loaded) == CREDENTIAL_CORRUPT && zero(&loaded, sizeof(loaded)));
    static uint8_t oversized[CREDENTIAL_FILE_LIMIT + 1]; write_file(oversized, sizeof(oversized));
    CHECK(credential_store_load(&store, path, &loaded) == CREDENTIAL_LIMIT && zero(&loaded, sizeof(loaded)));
    write_file(bad, 0); CHECK(credential_store_load(&store, path, &loaded) == CREDENTIAL_LIMIT);
    CHECK(credential_store_save(&store, path, &record) == CREDENTIAL_OK);
    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL); CHECK(file != INVALID_HANDLE_VALUE);
    CHECK(credential_store_logout(&store, path, &record) == CREDENTIAL_OS_ERROR && zero(&record, sizeof(record))); CHECK(CloseHandle(file));
    CHECK(credential_store_logout(&store, path, &record) == CREDENTIAL_OK && credential_store_balanced(&store));
}
static void bounds(void)
{
    CredentialRecord record; record_fixture(&record); CredentialStore store = {0};
    record.version = 99; CHECK(credential_store_save(&store, path, &record) == CREDENTIAL_INVALID); record.version = 1;
    record.ap_type = 3; CHECK(!credential_record_valid(&record)); record.ap_type = 1;
    record.ap_bytes = 1025; CHECK(!credential_record_valid(&record)); record.ap_bytes = 1024;
    memset(record.refresh_token, 'r', sizeof(record.refresh_token) - 1); record.refresh_token[sizeof(record.refresh_token) - 1] = 0;
    memset(record.username, 'n', sizeof(record.username) - 1); record.username[sizeof(record.username) - 1] = 0;
    CHECK(credential_store_save(&store, path, &record) == CREDENTIAL_OK);
    CredentialRecord loaded; CHECK(credential_store_load(&store, path, &loaded) == CREDENTIAL_OK && memcmp(&record, &loaded, sizeof(record)) == 0);
    record.refresh_token[sizeof(record.refresh_token) - 1] = 'x'; CHECK(!credential_record_valid(&record));
    CHECK(credential_store_logout(&store, path, &record) == CREDENTIAL_OK); credential_record_clear(&loaded);
    CHECK(credential_record_init(&record, "bad client") == CREDENTIAL_INVALID && zero(&record, sizeof(record)));
}
int main(void)
{
    wchar_t temp[MAX_PATH]; CHECK(GetTempPathW(MAX_PATH, temp) != 0);
    CHECK(GetTempFileNameW(temp, L"spc", 0, directory) != 0 && DeleteFileW(directory) && CreateDirectoryW(directory, NULL));
    CHECK(swprintf_s(path, MAX_PATH, L"%ls/record.dat", directory) > 0);
    roundtrip_and_replace(); invalid_files(); bounds(); CHECK(RemoveDirectoryW(directory)); test_ap_auth();
    puts("DPAPI records, bounds, corruption, atomic replacement/logout and encrypted AP authentication passed"); return 0;
}
