#include "ap_internal.h"
#include "ap_auth.h"
#include "proto_codec.h"
#include "authentication.pb.h"
#include <string.h>

ApResult ap_auth_init(ApAuth *a, CredentialRecord *r, const char *token)
{
    if (a == NULL) { return AP_INVALID; }
    SecureZeroMemory(a, sizeof(*a));
    if (!credential_record_valid(r)) { return AP_INVALID; }
    if (token == NULL) { if (r->ap_bytes == 0) { return AP_INVALID; } }
    else {
        size_t n = strnlen_s(token, sizeof(a->token));
        if (n == 0 || n == sizeof(a->token)) { return AP_LIMIT; }
        memcpy(a->token, token, n + 1); a->use_token = true;
    }
    a->record = r; a->result = AP_PENDING; return AP_OK;
}
void ap_auth_clear(ApAuth *a) { if (a != NULL) { SecureZeroMemory(a, sizeof(*a)); } }
ApResult ap_auth_result(const ApAuth *a) { return a == NULL ? AP_INVALID : a->result; }

ApResult ap_auth_send(ApSession *s)
{
    ApAuth *a = s->config.auth;
    if (a == NULL || s->login_sent) { return AP_OK; }
    CredentialRecord *r = a->record;
    if (!credential_record_valid(r) || (!a->use_token && r->ap_bytes == 0)) { return ap_close(s, AP_INVALID, false); }
    ClientResponseEncrypted login = ClientResponseEncrypted_init_zero;
    login.login_credentials.typ = (AuthenticationType)(a->use_token ? 3 : r->ap_type);
    login.login_credentials.has_auth_data = true;
    size_t n = a->use_token ? strlen(a->token) : r->ap_bytes;
    login.login_credentials.auth_data.size = (pb_size_t)n;
    memcpy(login.login_credentials.auth_data.bytes, a->use_token ? (const void *)a->token : r->ap_blob, n);
    if (!a->use_token) {
        login.login_credentials.has_username = true;
        memcpy(login.login_credentials.username, r->username, sizeof(r->username));
    }
    login.system_info.cpu_family = CpuFamily_CPU_X86_64;
    login.system_info.os = Os_OS_WINDOWS;
    login.system_info.has_device_id = true;
    memcpy(login.system_info.device_id, r->device_id, sizeof(r->device_id));
    login.system_info.has_system_information_string = true;
    memcpy(login.system_info.system_information_string, "spoti-c Windows", sizeof("spoti-c Windows"));
    login.has_version_string = true;
    memcpy(login.version_string, "spoti-c 0.1.0", sizeof("spoti-c 0.1.0"));
    uint8_t wire[4096]; size_t bytes = 0;
    ProtoResult encoded = proto_encode(ClientResponseEncrypted_fields, &login, wire, sizeof(wire), &bytes);
    SecureZeroMemory(&login, sizeof(login));
    ApResult result = encoded == PROTO_OK ? ap_send(s, AP_CMD_LOGIN, wire, bytes) : AP_LIMIT;
    SecureZeroMemory(wire, sizeof(wire));
    if (result != AP_OK) { return ap_close(s, result, false); }
    a->result = AP_PENDING; return AP_OK;
}
ApResult ap_auth_accept(ApSession *s, const APWelcome *welcome)
{
    ApAuth *a = s->config.auth;
    if (a == NULL) { return AP_OK; }
    CredentialRecord next = *a->record;
    next.ap_type = (uint32_t)welcome->reusable_auth_credentials_type;
    next.ap_bytes = welcome->reusable_auth_credentials.size;
    memcpy(next.username, welcome->canonical_username, sizeof(next.username));
    SecureZeroMemory(next.ap_blob, sizeof(next.ap_blob));
    memcpy(next.ap_blob, welcome->reusable_auth_credentials.bytes, next.ap_bytes);
    bool valid = credential_record_valid(&next);
    if (valid) { *a->record = next; }
    credential_record_clear(&next);
    if (!valid) { return AP_MALFORMED; }
    bool reconnect = a->use_token;
    a->use_token = false; SecureZeroMemory(a->token, sizeof(a->token));
    a->result = reconnect ? AP_PENDING : AP_OK;
    return reconnect ? AP_RECONNECT : AP_OK;
}
void ap_auth_closed(ApSession *s, ApResult result, bool continuing)
{
    ApAuth *a = s->config.auth;
    if (a == NULL || continuing) { return; }
    SecureZeroMemory(a->token, sizeof(a->token)); a->use_token = false; a->result = result;
}
