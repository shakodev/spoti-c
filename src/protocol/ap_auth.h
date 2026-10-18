#ifndef SPOTIC_PROTOCOL_AP_AUTH_H
#define SPOTIC_PROTOCOL_AP_AUTH_H
#include "credential_store.h"
#include "ap_packet.h"

typedef struct ApAuth {
    CredentialRecord *record; /* Exclusive borrow through session shutdown. */
    ApResult result;
    char token[1025];
    bool use_token;
} ApAuth;
/* Initialize before attaching to ApConfig. NULL token selects returned stored
 * credentials. OAuth success alone never implies AP compatibility: AP rejection
 * is surfaced as AP_AUTH_FAILED. Token login reconnects exactly once with the
 * returned type/blob before exposing AUTHENTICATED or dispatching welcome. */
ApResult ap_auth_init(ApAuth *auth, CredentialRecord *record, const char *access_token);
void ap_auth_clear(ApAuth *auth); /* Only after session shutdown. */
ApResult ap_auth_result(const ApAuth *auth);
#endif
