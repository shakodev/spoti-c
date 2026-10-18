#include "token_internal.h"
#include <string.h>

TokenResult token_client_start(TokenManager *m, bool answer)
{
    ClientTokenRequest *request = &m->scratch.client_request;
    SecureZeroMemory(&m->scratch, sizeof(m->scratch));
    if (answer) {
        request->request_type = (spotify_clienttoken_http_v0_ClientTokenRequestType)2; request->which_request = 3;
        spotify_clienttoken_http_v0_ChallengeAnswersRequest *answers = &request->request.challenge_answers;
        memcpy(answers->state, m->challenge_state, sizeof(answers->state)); answers->answers_count = 1;
        answers->answers[0].ChallengeType = (spotify_clienttoken_http_v0_ChallengeType)3;
        answers->answers[0].which_answer = 4;
        static const char hex[] = "0123456789ABCDEF";
        for (size_t i = 0; i < sizeof(m->suffix); ++i) {
            answers->answers[0].answer.hash_cash.suffix[2 * i] = hex[m->suffix[i] >> 4];
            answers->answers[0].answer.hash_cash.suffix[2 * i + 1] = hex[m->suffix[i] & 15];
        }
    } else {
        m->client_rounds = 0; m->deadline = core_deadline_after(m->now, 30000);
        request->request_type = (spotify_clienttoken_http_v0_ClientTokenRequestType)1; request->which_request = 2;
        spotify_clienttoken_http_v0_ClientDataRequest *data = &request->request.client_data;
        memcpy(data->client_version, m->version, strlen(m->version) + 1);
        memcpy(data->client_id, m->client_id, strlen(m->client_id) + 1); data->which_data = 3;
        spotify_clienttoken_data_v0_ConnectivitySdkData *connectivity = &data->data.connectivity_sdk_data;
        connectivity->has_platform_specific_data = true; connectivity->platform_specific_data.which_data = 4;
        memcpy(connectivity->device_id, m->config.credentials->device_id, sizeof(m->config.credentials->device_id));
        spotify_clienttoken_data_v0_NativeDesktopWindowsData *windows = &connectivity->platform_specific_data.data.desktop_windows;
        windows->os_version = (int32_t)m->config.windows_major; windows->os_build = (int32_t)m->config.windows_build; windows->platform_id = 2;
        SYSTEM_INFO process, native; GetSystemInfo(&process); GetNativeSystemInfo(&native);
        windows->unknown_value_5 = process.wProcessorArchitecture; windows->unknown_value_6 = native.wProcessorArchitecture;
        windows->image_file_machine = process.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64 ? IMAGE_FILE_MACHINE_AMD64 :
            process.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64 ? IMAGE_FILE_MACHINE_ARM64 : IMAGE_FILE_MACHINE_I386;
        windows->pe_machine = native.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64 ? IMAGE_FILE_MACHINE_AMD64 :
            native.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64 ? IMAGE_FILE_MACHINE_ARM64 : IMAGE_FILE_MACHINE_I386;
        /* Native tag 10 is is_windows_store, not an arbitrary true flag. */
        windows->unknown_value_10 = false;
    }
    size_t bytes;
    ProtoResult result = proto_encode(spotify_clienttoken_http_v0_ClientTokenRequest_fields, request, m->upload, sizeof(m->upload), &bytes);
    SecureZeroMemory(&m->scratch, sizeof(m->scratch));
    return result == PROTO_OK ? token_submit(m, false, bytes) : TOKEN_FULL;
}
static int nibble(char c)
{
    if (c >= '0' && c <= '9') { return c - '0'; }
    if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
    if (c >= 'A' && c <= 'F') { return c - 'A' + 10; }
    return -1;
}
TokenResult token_client_response(TokenManager *m, size_t bytes)
{
    ClientTokenResponse *response = &m->scratch.client_response;
    SecureZeroMemory(&m->scratch, sizeof(m->scratch));
    if (proto_decode(spotify_clienttoken_http_v0_ClientTokenResponse_fields, m->response, bytes, response) != PROTO_OK) { return TOKEN_MALFORMED; }
    if (response->response_type == 1 && response->which_response == 2) {
        spotify_clienttoken_http_v0_GrantedTokenResponse *granted = &response->response.granted_token;
        if (granted->domains_count == 0) { return TOKEN_SCOPE; }
        for (size_t i = 0; i < granted->domains_count; ++i) { if (!token_domain_valid(granted->domains[i].domain)) { return TOKEN_SCOPE; } }
        TokenResult result = token_install(m, TOKEN_CLIENT, granted->token, (uint32_t)granted->expires_after_seconds,
            (uint32_t)granted->refresh_after_seconds);
        if (result != TOKEN_OK) { return result; }
        SecureZeroMemory(m->domains, sizeof(m->domains)); m->domain_count = granted->domains_count;
        for (size_t i = 0; i < m->domain_count; ++i) { memcpy(m->domains[i], granted->domains[i].domain, sizeof(m->domains[i])); }
        return TOKEN_OK;
    }
    if (response->response_type != 2 || response->which_response != 3) { return TOKEN_UNSUPPORTED; }
    spotify_clienttoken_http_v0_ChallengesResponse *challenges = &response->response.challenges;
    if (challenges->challenges_count == 0 || challenges->state[0] == 0) { return TOKEN_MALFORMED; }
    if (challenges->challenges_count != 1) { return TOKEN_UNSUPPORTED; }
    if (++m->client_rounds > 2) { return TOKEN_CHALLENGE_LIMIT; }
    spotify_clienttoken_http_v0_Challenge *challenge = &challenges->challenges[0];
    if (challenge->type != 3 || challenge->which_parameters != 4) { return TOKEN_UNSUPPORTED; }
    const char *prefix = challenge->parameters.evaluate_hashcash_parameters.prefix; size_t n = strlen(prefix);
    if (n == 0 || n % 2 != 0 || n / 2 > sizeof(m->prefix)) { return TOKEN_MALFORMED; }
    SecureZeroMemory(m->prefix, sizeof(m->prefix));
    for (size_t i = 0; i < n / 2; ++i) {
        int hi = nibble(prefix[2 * i]), lo = nibble(prefix[2 * i + 1]);
        if (hi < 0 || lo < 0) { return TOKEN_MALFORMED; } m->prefix[i] = (uint8_t)((hi << 4) | lo);
    }
    memcpy(m->challenge_state, challenges->state, sizeof(m->challenge_state));
    m->context_bytes = 0; SecureZeroMemory(m->context, sizeof(m->context));
    return token_hash_begin(m, m->prefix, n / 2, challenge->parameters.evaluate_hashcash_parameters.length, false);
}
