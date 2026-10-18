#include "cdn_internal.h"
static HttpResult more_ciphertext(CdnClient *c)
{
    if (c->encrypted == sizeof(c->tls_input)) { return HTTP_BODY_TOO_LARGE; }
    size_t got; HttpResult r = cdn_socket_read(c,c->tls_input+c->encrypted,sizeof(c->tls_input)-c->encrypted,&got);
    if (r != HTTP_OK) { return r; }
    if (!got) { return HTTP_TRUNCATED; } c->encrypted += got; return HTTP_OK;
}
HttpResult cdn_tls_open(CdnClient *c)
{
    SCHANNEL_CRED credentials = {0}; credentials.dwVersion = SCHANNEL_CRED_VERSION;
    credentials.grbitEnabledProtocols = SP_PROT_TLS1_2_CLIENT;
    credentials.dwFlags = SCH_CRED_AUTO_CRED_VALIDATION | SCH_CRED_NO_DEFAULT_CREDS |
        SCH_USE_STRONG_CRYPTO | SCH_CRED_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT;
    TimeStamp expires; SecInvalidateHandle(&c->credential); SecInvalidateHandle(&c->context);
    SECURITY_STATUS status = AcquireCredentialsHandleW(NULL,UNISP_NAME_W,SECPKG_CRED_OUTBOUND,
        NULL,&credentials,NULL,NULL,&c->credential,&expires);
    c->tls_status = status;
    if (status != SEC_E_OK) { return HTTP_OS_ERROR; } c->credential_ready = true;
    bool need_read = false;
    for (unsigned step = 0; step < 64; ++step) {
        if (need_read) { HttpResult r = more_ciphertext(c); if (r != HTTP_OK) { return r; } }
        SecBuffer input[] = {{(ULONG)c->encrypted,SECBUFFER_TOKEN,c->tls_input},{0,SECBUFFER_EMPTY,NULL}};
        SecBufferDesc incoming = {SECBUFFER_VERSION,2,input};
        SecBuffer token = {sizeof(c->tls_output),SECBUFFER_TOKEN,c->tls_output};
        SecBufferDesc outgoing = {SECBUFFER_VERSION,1,&token}; unsigned long attributes = 0;
        status = InitializeSecurityContextW(&c->credential,c->context_ready ? &c->context : NULL,c->url.host,
            ISC_REQ_CONFIDENTIALITY | ISC_REQ_REPLAY_DETECT | ISC_REQ_SEQUENCE_DETECT | ISC_REQ_STREAM,
            0,SECURITY_NATIVE_DREP,c->encrypted ? &incoming : NULL,0,&c->context,&outgoing,&attributes,&expires);
        c->tls_status = status;
        c->context_ready = SecIsValidHandle(&c->context) != 0;
        if (status == SEC_E_INCOMPLETE_MESSAGE) { need_read = true; continue; }
        if (status != SEC_E_OK && status != SEC_I_CONTINUE_NEEDED) { return HTTP_OS_ERROR; }
        if (token.cbBuffer > sizeof(c->tls_output)) { return HTTP_BODY_TOO_LARGE; }
        if (token.cbBuffer) { HttpResult r = cdn_socket_send(c,token.pvBuffer,token.cbBuffer); if (r != HTTP_OK) { return r; } }
        size_t extra = input[1].BufferType == SECBUFFER_EXTRA ? input[1].cbBuffer : 0;
        if (extra > c->encrypted) { return HTTP_OS_ERROR; }
        if (extra) { memmove(c->tls_input,c->tls_input+c->encrypted-extra,extra); } c->encrypted = extra;
        if (status == SEC_E_OK) {
            if (!(attributes & ISC_RET_CONFIDENTIALITY) ||
                QueryContextAttributesW(&c->context,SECPKG_ATTR_STREAM_SIZES,&c->sizes) != SEC_E_OK ||
                !c->sizes.cbMaximumMessage || c->sizes.cbMaximumMessage > 16384 ||
                (uint64_t)c->sizes.cbHeader+c->sizes.cbMaximumMessage+c->sizes.cbTrailer > sizeof(c->tls_output)) { return HTTP_OS_ERROR; }
            return HTTP_OK;
        }
        need_read = !c->encrypted;
    }
    return HTTP_OS_ERROR;
}
HttpResult cdn_tls_send(CdnClient *c, const void *data, size_t bytes)
{
    const uint8_t *p = data;
    while (bytes) {
        ULONG take = bytes > c->sizes.cbMaximumMessage ? c->sizes.cbMaximumMessage : (ULONG)bytes;
        memcpy(c->tls_output+c->sizes.cbHeader,p,take);
        SecBuffer parts[] = {{c->sizes.cbHeader,SECBUFFER_STREAM_HEADER,c->tls_output},
            {take,SECBUFFER_DATA,c->tls_output+c->sizes.cbHeader},
            {c->sizes.cbTrailer,SECBUFFER_STREAM_TRAILER,c->tls_output+c->sizes.cbHeader+take},{0,SECBUFFER_EMPTY,NULL}};
        SecBufferDesc message = {SECBUFFER_VERSION,4,parts};
        if (EncryptMessage(&c->context,0,&message,0) != SEC_E_OK) { return HTTP_OS_ERROR; }
        for (unsigned i = 0; i < 3; ++i) {
            HttpResult r = cdn_socket_send(c,parts[i].pvBuffer,parts[i].cbBuffer); if (r != HTTP_OK) { return r; }
        }
        p += take; bytes -= take;
    }
    return HTTP_OK;
}
HttpResult cdn_tls_read(CdnClient *c, void *out, size_t capacity, size_t *bytes)
{
    *bytes = 0;
    for (;;) {
        if (c->plain_bytes) {
            size_t n = c->plain_bytes < capacity ? c->plain_bytes : capacity;
            memcpy(out,c->tls_input+c->plain_at,n); c->plain_at += n; c->plain_bytes -= n; *bytes = n; return HTTP_OK;
        }
        if (c->extra_bytes) {
            memmove(c->tls_input,c->tls_input+c->extra_at,c->extra_bytes);
            c->encrypted = c->extra_bytes; c->extra_bytes = 0;
        }
        if (!c->encrypted) { HttpResult r = more_ciphertext(c); if (r != HTTP_OK) { return r; } }
        SecBuffer parts[] = {{(ULONG)c->encrypted,SECBUFFER_DATA,c->tls_input},
            {0,SECBUFFER_EMPTY,NULL},{0,SECBUFFER_EMPTY,NULL},{0,SECBUFFER_EMPTY,NULL}};
        SecBufferDesc message = {SECBUFFER_VERSION,4,parts};
        SECURITY_STATUS status = DecryptMessage(&c->context,&message,0,NULL);
        if (status == SEC_E_INCOMPLETE_MESSAGE) { HttpResult r = more_ciphertext(c); if (r != HTTP_OK) { return r; } continue; }
        if (status == SEC_I_CONTEXT_EXPIRED) { return HTTP_OK; }
        /* Renegotiation/client authentication are unsupported, never bypassed. */
        if (status != SEC_E_OK) { return HTTP_OS_ERROR; }
        size_t encrypted = c->encrypted; c->encrypted = 0;
        for (unsigned i = 0; i < 4; ++i) {
            if (parts[i].BufferType == SECBUFFER_EXTRA) {
                if (parts[i].cbBuffer > encrypted) { return HTTP_OS_ERROR; }
                c->extra_bytes = parts[i].cbBuffer; c->extra_at = encrypted-c->extra_bytes;
            } else if (parts[i].BufferType == SECBUFFER_DATA) {
                uintptr_t base = (uintptr_t)c->tls_input, data = (uintptr_t)parts[i].pvBuffer;
                if (data < base || data-base > encrypted || parts[i].cbBuffer > encrypted-(data-base)) { return HTTP_OS_ERROR; }
                c->plain_at = data-base; c->plain_bytes = parts[i].cbBuffer;
            }
        }
    }
}
