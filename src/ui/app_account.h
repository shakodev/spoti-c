#ifndef SPOTIC_APP_ACCOUNT_H
#define SPOTIC_APP_ACCOUNT_H
#include "app.h"
#include "api/oauth.h"
#include "api/token_manager.h"
typedef struct AppAccount {
    OAuthFlow *oauth;
    TokenManager *tokens;
    CredentialStore *store;
    CredentialRecord *credentials;
    const wchar_t *credential_path;
    bool open_browser;
} AppAccount;
/* Network/account owner ONLY, after receiving AppServices.auth's queued intent.
 * LOGIN begins the existing PKCE flow; that owner keeps driving its existing
 * OAuth/AP/token lifecycle and publishes app_session only after authentication.
 * LOGOUT is invoked after AP quiescence; it cancels OAuth, wipes token/record
 * data and deletes the existing DPAPI record. PENDING requires owner retirement
 * before releasing service storage. Never bind this directly as a UI callback. */
AppResult app_account_action(AppAccount *, AppAuth);
#endif
