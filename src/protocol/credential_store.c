#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincrypt.h>
#include <sddl.h>
#include "credential_store.h"
#include "crypto.h"
#include <string.h>
#include <wchar.h>

static const uint8_t entropy_bytes[] = "spoti-c/credentials/v1";
static DATA_BLOB entropy = {sizeof(entropy_bytes), (BYTE *)entropy_bytes};
enum { PLAIN_LIMIT = 8192, HEADER_BYTES = 28, DEVICE_BYTES = 40 };

static size_t text_size(const char *s, size_t cap)
{
    const char *end = memchr(s, 0, cap);
    return end == NULL ? cap : (size_t)(end - s);
}
static bool client_valid(const char *s)
{
    size_t n = text_size(s, CREDENTIAL_CLIENT_CAP);
    if (n == 0 || n == CREDENTIAL_CLIENT_CAP) { return false; }
    for (size_t i = 0; i < n; ++i) {
        char c = s[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~')) { return false; }
    }
    return true;
}
void credential_record_clear(CredentialRecord *r) { if (r != NULL) { SecureZeroMemory(r, sizeof(*r)); } }
bool credential_record_valid(const CredentialRecord *r)
{
    if (r == NULL || r->version != CREDENTIAL_VERSION || !client_valid(r->client_id) ||
        text_size(r->refresh_token, sizeof(r->refresh_token)) == sizeof(r->refresh_token) ||
        text_size(r->username, sizeof(r->username)) == sizeof(r->username) || r->ap_bytes > CREDENTIAL_BLOB_CAP ||
        r->device_id[40] != 0) { return false; }
    for (size_t i = 0; i < DEVICE_BYTES; ++i) {
        char c = r->device_id[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) { return false; }
    }
    if (r->ap_bytes == 0) { return r->ap_type == 0 && r->username[0] == 0; }
    return (r->ap_type == 1 || r->ap_type == 2) && r->username[0] != 0;
}
CredentialResult credential_record_init(CredentialRecord *r, const char *client_id)
{
    if (r == NULL) { return CREDENTIAL_INVALID; }
    credential_record_clear(r);
    if (client_id == NULL) { return CREDENTIAL_INVALID; }
    size_t n = strnlen_s(client_id, CREDENTIAL_CLIENT_CAP);
    if (n == 0 || n == CREDENTIAL_CLIENT_CAP) { return CREDENTIAL_INVALID; }
    memcpy(r->client_id, client_id, n + 1);
    if (!client_valid(r->client_id)) { credential_record_clear(r); return CREDENTIAL_INVALID; }
    Crypto crypto; crypto_init(&crypto); uint8_t random[20];
    if (crypto_random(&crypto, random, sizeof(random)) != CRYPTO_OK) {
        credential_record_clear(r); SecureZeroMemory(random, sizeof(random)); return CREDENTIAL_OS_ERROR;
    }
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < sizeof(random); ++i) {
        r->device_id[2 * i] = hex[random[i] >> 4]; r->device_id[2 * i + 1] = hex[random[i] & 15];
    }
    r->version = CREDENTIAL_VERSION; SecureZeroMemory(random, sizeof(random)); return CREDENTIAL_OK;
}
static void put32(uint8_t *p, uint32_t x)
{
    for (unsigned i = 0; i < 4; ++i) { p[i] = (uint8_t)(x >> (8 * i)); }
}
static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static DWORD encode(const CredentialRecord *r, uint8_t out[PLAIN_LIMIT])
{
    size_t sizes[4] = {strlen(r->client_id), strlen(r->refresh_token), strlen(r->username), r->ap_bytes};
    const void *fields[4] = {r->client_id, r->refresh_token, r->username, r->ap_blob};
    memcpy(out, "SPCR", 4); put32(out + 4, r->version); put32(out + 8, r->ap_type);
    size_t at = HEADER_BYTES;
    memcpy(out + at, r->device_id, DEVICE_BYTES); at += DEVICE_BYTES;
    for (size_t i = 0; i < 4; ++i) {
        put32(out + 12 + i * 4, (uint32_t)sizes[i]);
        memcpy(out + at, fields[i], sizes[i]); at += sizes[i];
    }
    return (DWORD)at;
}
static CredentialResult decode(const uint8_t *p, size_t bytes, CredentialRecord *r)
{
    if (bytes < HEADER_BYTES + DEVICE_BYTES || bytes > PLAIN_LIMIT || memcmp(p, "SPCR", 4) != 0) {
        return CREDENTIAL_CORRUPT;
    }
    r->version = get32(p + 4); r->ap_type = get32(p + 8);
    size_t caps[4] = {sizeof(r->client_id) - 1, sizeof(r->refresh_token) - 1, sizeof(r->username) - 1, sizeof(r->ap_blob)};
    void *fields[4] = {r->client_id, r->refresh_token, r->username, r->ap_blob};
    size_t at = HEADER_BYTES;
    memcpy(r->device_id, p + at, DEVICE_BYTES); at += DEVICE_BYTES;
    for (size_t i = 0; i < 4; ++i) {
        size_t n = get32(p + 12 + i * 4);
        if (n > caps[i] || n > bytes - at || (i != 3 && memchr(p + at, 0, n) != NULL)) { return CREDENTIAL_CORRUPT; }
        memcpy(fields[i], p + at, n); at += n;
        if (i == 3) { r->ap_bytes = (uint32_t)n; }
    }
    return at == bytes && credential_record_valid(r) ? CREDENTIAL_OK : CREDENTIAL_CORRUPT;
}
static bool path_valid(const wchar_t *path)
{
    return path != NULL && path[0] != 0 && wcsnlen_s(path, 231) <= 230;
}
static void close_file(CredentialStore *s, HANDLE h)
{
    if (CloseHandle(h)) { --s->live_handles; } else { s->cleanup_failed = true; }
}
static void free_local(CredentialStore *s, void *p, size_t bytes)
{
    if (p == NULL) { return; }
    SecureZeroMemory(p, bytes);
    if (LocalFree(p) == NULL) { --s->live_allocations; } else { s->cleanup_failed = true; }
}
bool credential_store_balanced(const CredentialStore *s)
{
    return s != NULL && s->live_handles == 0 && s->live_allocations == 0 && !s->cleanup_failed;
}
CredentialResult credential_store_save(CredentialStore *s, const wchar_t *path, const CredentialRecord *r)
{
    if (s == NULL || !path_valid(path) || !credential_record_valid(r)) { return CREDENTIAL_INVALID; }
    uint8_t plain[PLAIN_LIMIT] = {0};
    DATA_BLOB input = {encode(r, plain), plain}, encrypted = {0};
    BOOL protected_ok = CryptProtectData(&input, L"spoti-c credentials", &entropy, NULL, NULL,
        CRYPTPROTECT_UI_FORBIDDEN, &encrypted);
    SecureZeroMemory(plain, sizeof(plain));
    if (!protected_ok) { return CREDENTIAL_OS_ERROR; }
    ++s->live_allocations;
    CredentialResult result = CREDENTIAL_OS_ERROR;
    PSECURITY_DESCRIPTOR descriptor = NULL;
    wchar_t temporary[256] = {0}; bool created = false;
    if (encrypted.cbData > CREDENTIAL_FILE_LIMIT) { result = CREDENTIAL_LIMIT; goto done; }
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;GA;;;SY)(A;;GA;;;OW)",
        SDDL_REVISION_1, &descriptor, NULL)) { goto done; }
    ++s->live_allocations;
    Crypto crypto; crypto_init(&crypto); uint8_t nonce[8];
    if (crypto_random(&crypto, nonce, sizeof(nonce)) != CRYPTO_OK) { goto done; }
    if (swprintf_s(temporary, 256, L"%ls.%02x%02x%02x%02x%02x%02x%02x%02x.tmp", path,
        nonce[0], nonce[1], nonce[2], nonce[3], nonce[4], nonce[5], nonce[6], nonce[7]) < 0) { goto done; }
    SECURITY_ATTRIBUTES attributes = {sizeof(attributes), descriptor, FALSE};
    HANDLE file = CreateFileW(temporary, GENERIC_WRITE, 0, &attributes, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, NULL);
    if (file == INVALID_HANDLE_VALUE) { goto done; }
    ++s->live_handles; created = true;
    DWORD written = 0;
    BOOL saved = WriteFile(file, encrypted.pbData, encrypted.cbData, &written, NULL) &&
        written == encrypted.cbData && FlushFileBuffers(file);
    close_file(s, file);
    if (saved && !s->cleanup_failed && MoveFileExW(temporary, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        result = CREDENTIAL_OK; created = false;
    }
done:
    if (created && !DeleteFileW(temporary)) { s->cleanup_failed = true; }
    if (descriptor != NULL) { free_local(s, descriptor, GetSecurityDescriptorLength(descriptor)); }
    free_local(s, encrypted.pbData, encrypted.cbData); return s->cleanup_failed ? CREDENTIAL_OS_ERROR : result;
}
CredentialResult credential_store_load(CredentialStore *s, const wchar_t *path, CredentialRecord *r)
{
    if (r == NULL) { return CREDENTIAL_INVALID; }
    credential_record_clear(r);
    if (s == NULL || !path_valid(path)) { return CREDENTIAL_INVALID; }
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ? CREDENTIAL_NOT_FOUND : CREDENTIAL_OS_ERROR;
    }
    ++s->live_handles;
    uint8_t encrypted[CREDENTIAL_FILE_LIMIT]; LARGE_INTEGER size;
    CredentialResult result = CREDENTIAL_CORRUPT;
    BY_HANDLE_FILE_INFORMATION info;
    if (!GetFileInformationByHandle(file, &info) || (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) { goto done; }
    if (!GetFileSizeEx(file, &size)) { result = CREDENTIAL_OS_ERROR; goto done; }
    if (size.QuadPart <= 0 || size.QuadPart > CREDENTIAL_FILE_LIMIT) { result = CREDENTIAL_LIMIT; goto done; }
    DWORD read = 0;
    if (!ReadFile(file, encrypted, (DWORD)size.QuadPart, &read, NULL) || read != (DWORD)size.QuadPart) { goto done; }
    DATA_BLOB input = {read, encrypted}, plain = {0};
    if (CryptUnprotectData(&input, NULL, &entropy, NULL, NULL, CRYPTPROTECT_UI_FORBIDDEN, &plain)) {
        ++s->live_allocations; result = decode(plain.pbData, plain.cbData, r);
        free_local(s, plain.pbData, plain.cbData);
    }
done:
    close_file(s, file); SecureZeroMemory(encrypted, sizeof(encrypted));
    if (s->cleanup_failed) { result = CREDENTIAL_OS_ERROR; }
    if (result != CREDENTIAL_OK) { credential_record_clear(r); }
    return result;
}
CredentialResult credential_store_logout(CredentialStore *s, const wchar_t *path, CredentialRecord *r)
{
    credential_record_clear(r);
    if (s == NULL || !path_valid(path)) { return CREDENTIAL_INVALID; }
    if (DeleteFileW(path)) { return CREDENTIAL_OK; }
    DWORD error = GetLastError();
    return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ? CREDENTIAL_OK : CREDENTIAL_OS_ERROR;
}
