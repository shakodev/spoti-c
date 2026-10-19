#include "app_account.h"
#include "core/thread.h"

AppResult app_account_action(AppAccount *owner, AppAuth action)
{
    if (!owner || core_thread_is_gui() || (action!=APP_LOGIN && action!=APP_LOGOUT)) { return APP_INVALID; }
    if (action==APP_LOGIN) {
        if (!owner->oauth) { return APP_UNAVAILABLE; }
        OAuthResult r=oauth_begin(owner->oauth,owner->open_browser);
        return r==OAUTH_OK || r==OAUTH_PENDING ? APP_PENDING : r==OAUTH_FULL ? APP_FULL : APP_FAILED;
    }
    bool pending=false, failed=false;
    if (owner->oauth) {
        OAuthResult r=oauth_cancel(owner->oauth);
        pending|=r==OAUTH_PENDING; failed|=r!=OAUTH_OK && r!=OAUTH_PENDING && r!=OAUTH_CANCELLED;
    }
    if (owner->tokens) {
        TokenResult r=token_logout(owner->tokens);
        pending|=r==TOKEN_PENDING; failed|=r!=TOKEN_OK && r!=TOKEN_PENDING && r!=TOKEN_CLOSED;
    }
    if (owner->credentials) {
        CredentialResult r=owner->store && owner->credential_path ?
            credential_store_logout(owner->store,owner->credential_path,owner->credentials) : CREDENTIAL_INVALID;
        if (r!=CREDENTIAL_OK) { credential_record_clear(owner->credentials); failed=true; }
    }
    return failed ? APP_FAILED : pending ? APP_PENDING : APP_OK;
}
