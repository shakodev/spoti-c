#include "token_internal.h"
#include "pb_decode.h"
#include "pb_encode.h"
#include <string.h>

/* Encode the selected nanopb field path, avoiding the generated 56 KiB
 * LoginRequest union and 32 KiB Challenges array. The original descriptors
 * still validate every produced/received message; independent fixtures use
 * the complete Python protobuf classes. */
typedef bool (*WriteFields)(pb_ostream_t *, const TokenManager *);
static bool bytes_field(pb_ostream_t *s, uint32_t tag, const void *p, size_t n)
{
    return pb_encode_tag(s, PB_WT_STRING, tag) && pb_encode_string(s, p, n);
}
static bool message_field(pb_ostream_t *s, uint32_t tag, WriteFields write, const TokenManager *m)
{
    pb_ostream_t sizing = PB_OSTREAM_SIZING;
    return write(&sizing, m) && pb_encode_tag(s, PB_WT_STRING, tag) &&
        pb_encode_varint(s, sizing.bytes_written) && write(s, m);
}
static bool client_info(pb_ostream_t *s, const TokenManager *m)
{
    return bytes_field(s, 1, m->client_id, strlen(m->client_id)) &&
        bytes_field(s, 2, m->config.credentials->device_id, 40);
}
static bool stored(pb_ostream_t *s, const TokenManager *m)
{
    return bytes_field(s, 1, m->config.credentials->username, strlen(m->config.credentials->username)) &&
        bytes_field(s, 2, m->config.credentials->ap_blob, m->config.credentials->ap_bytes);
}
static bool duration(pb_ostream_t *s, const TokenManager *m)
{
    uint64_t elapsed = m->now - m->hash_started;
    return (elapsed / 1000 == 0 || (pb_encode_tag(s, PB_WT_VARINT, 1) && pb_encode_varint(s, elapsed / 1000))) &&
        (elapsed % 1000 == 0 || (pb_encode_tag(s, PB_WT_VARINT, 2) && pb_encode_varint(s, (elapsed % 1000) * 1000000)));
}
static bool hashcash(pb_ostream_t *s, const TokenManager *m)
{
    return bytes_field(s, 1, m->suffix, sizeof(m->suffix)) && message_field(s, 2, duration, m);
}
static bool solution(pb_ostream_t *s, const TokenManager *m) { return message_field(s, 1, hashcash, m); }
static bool solutions(pb_ostream_t *s, const TokenManager *m) { return message_field(s, 1, solution, m); }
TokenResult token_login_start(TokenManager *m, bool answer)
{
    if (!credential_record_valid(m->config.credentials) || strcmp(m->client_id, m->config.credentials->client_id) != 0) { return TOKEN_CLIENT_MISMATCH; }
    if (m->config.credentials->ap_type != 1 || strlen(m->config.credentials->username) > 256) { return TOKEN_UNSUPPORTED; }
    if (!answer) { m->login_rounds = 0; m->deadline = core_deadline_after(m->now, 30000); }
    pb_ostream_t stream = pb_ostream_from_buffer(m->upload, sizeof(m->upload));
    bool encoded = message_field(&stream, 1, client_info, m) &&
        (!answer || (bytes_field(&stream, 2, m->context, m->context_bytes) && message_field(&stream, 3, solutions, m))) &&
        message_field(&stream, 100, stored, m);
    if (!encoded || proto_validate(spotify_login5_v3_LoginRequest_fields, m->upload, stream.bytes_written) != PROTO_OK) { return TOKEN_FULL; }
    return token_submit(m, true, stream.bytes_written);
}
static TokenResult challenge(pb_istream_t *s, TokenManager *m)
{
    unsigned seen = 0;
    while (s->bytes_left != 0) {
        pb_wire_type_t wire; uint32_t tag; bool eof;
        if (!pb_decode_tag(s, &wire, &tag, &eof)) { return TOKEN_MALFORMED; }
        if (tag != 1) { return TOKEN_UNSUPPORTED; }
        if (wire != PB_WT_STRING || ++seen > 1) { return TOKEN_UNSUPPORTED; }
        pb_istream_t item; if (!pb_make_string_substream(s, &item)) { return TOKEN_MALFORMED; }
        unsigned variants = 0;
        while (item.bytes_left != 0) {
            if (!pb_decode_tag(&item, &wire, &tag, &eof)) { return TOKEN_MALFORMED; }
            if (tag != 1) { return TOKEN_UNSUPPORTED; }
            if (wire != PB_WT_STRING || ++variants != 1) { return TOKEN_MALFORMED; }
            pb_istream_t hash;
            if (!pb_make_string_substream(&item, &hash) ||
                !pb_decode(&hash, spotify_login5_v3_challenges_HashcashChallenge_fields, &m->scratch.login_challenge) ||
                !pb_close_string_substream(&item, &hash)) { return TOKEN_MALFORMED; }
        }
        if (variants != 1) { return TOKEN_UNSUPPORTED; }
        if (!pb_close_string_substream(s, &item)) { return TOKEN_MALFORMED; }
    }
    return seen == 1 ? TOKEN_OK : TOKEN_MALFORMED;
}
TokenResult token_login_response(TokenManager *m, size_t bytes)
{
    if (proto_validate(spotify_login5_v3_LoginResponse_fields, m->response, bytes) != PROTO_OK) { return TOKEN_MALFORMED; }
    SecureZeroMemory(&m->scratch, sizeof(m->scratch));
    SecureZeroMemory(m->context, sizeof(m->context)); m->context_bytes = 0;
    pb_istream_t stream = pb_istream_from_buffer(m->response, bytes); uint32_t response = 0; bool context = false;
    while (stream.bytes_left != 0) {
        pb_wire_type_t wire; uint32_t tag; bool eof;
        if (!pb_decode_tag(&stream, &wire, &tag, &eof)) { return TOKEN_MALFORMED; }
        if (tag >= 1 && tag <= 3) {
            if (response != 0) { return TOKEN_MALFORMED; } response = tag;
            if (tag == 2) {
                uint64_t error;
                if (!pb_decode_varint(&stream, &error)) { return TOKEN_MALFORMED; }
                return error == 4 ? TOKEN_TIMEOUT : TOKEN_REJECTED;
            }
            pb_istream_t sub;
            if (wire != PB_WT_STRING || !pb_make_string_substream(&stream, &sub)) { return TOKEN_MALFORMED; }
            if (tag == 1) {
                if (!pb_decode(&sub, spotify_login5_v3_LoginOk_fields, &m->scratch.login_ok)) { return TOKEN_MALFORMED; }
            } else {
                TokenResult result = challenge(&sub, m); if (result != TOKEN_OK) { return result; }
            }
            if (!pb_close_string_substream(&stream, &sub)) { return TOKEN_MALFORMED; }
        } else if (tag == 5) {
            if (context) { return TOKEN_MALFORMED; } context = true;
            pb_istream_t sub;
            if (!pb_make_string_substream(&stream, &sub)) { return TOKEN_MALFORMED; }
            if (sub.bytes_left > sizeof(m->context)) { return TOKEN_FULL; } m->context_bytes = sub.bytes_left;
            if (!pb_read(&sub, m->context, m->context_bytes) || !pb_close_string_substream(&stream, &sub)) { return TOKEN_MALFORMED; }
        } else if (!pb_skip_field(&stream, wire)) { return TOKEN_MALFORMED; }
    }
    if (response == 1) {
        TokenLoginOk *ok = &m->scratch.login_ok;
        if (strcmp(ok->username, m->config.credentials->username) != 0) { return TOKEN_REJECTED; }
        uint32_t expiry = (uint32_t)ok->access_token_expires_in;
        uint32_t refresh = expiry > 30 ? expiry - 30 : expiry;
        return token_install(m, TOKEN_BEARER, ok->access_token, expiry, refresh);
    }
    if (response != 3 || !context || m->context_bytes == 0) { return TOKEN_MALFORMED; }
    if (++m->login_rounds > 2) { return TOKEN_CHALLENGE_LIMIT; }
    return token_hash_begin(m, m->scratch.login_challenge.prefix.bytes, m->scratch.login_challenge.prefix.size,
        m->scratch.login_challenge.length, true);
}
