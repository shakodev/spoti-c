#include "mercury_internal.h"
#include "pubsub.pb.h"

static ApResult subscribed(MercurySub *s, const MercuryResponse *response)
{
    if (response->count > MERCURY_ALIASES) { return AP_LIMIT; }
    if (response->count == 0) {
        memcpy(s->aliases[0], s->uri, sizeof(s->uri)); s->count = 1;
    } else {
        for (size_t i = 0; i < response->count; ++i) {
            Subscription sub = Subscription_init_zero;
            MercurySpan part = response->parts[i];
            if (proto_decode(Subscription_fields, part.data, part.bytes, &sub) != PROTO_OK ||
                !sub.has_uri || sub.uri[0] == '\0') { return AP_MALFORMED; }
            if (sub.has_status_code && sub.status_code >= 400) { return AP_IO_ERROR; }
            memcpy(s->aliases[i], sub.uri, sizeof(sub.uri));
        }
        s->count = response->count;
    }
    s->active = true; return AP_OK;
}

/* Match the pinned librespot component-wise form-url-encoded prefix behavior.
 * Encode comparison incrementally so a 256-byte UTF-8 URI needs no growing
 * temporary buffer (each byte may expand to three characters). */
static bool matches(const char *uri, const char *prefix)
{
    static const char hex[] = "0123456789ABCDEF";
    bool component = false;
    while (*uri != '\0' && *prefix != '\0') {
        unsigned char ch = (unsigned char)*uri++;
        if (ch == '/') {
            component = true;
            if (*prefix++ != '/') { return false; }
        } else if (!component || (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') || ch == '*' || ch == '-' || ch == '.' || ch == '_') {
            if ((unsigned char)*prefix++ != ch) { return false; }
        } else if (ch == ' ') {
            if (*prefix++ != '+') { return false; }
        } else {
            char encoded[3] = {'%', hex[ch >> 4], hex[ch & 15]};
            for (size_t i = 0; i < 3 && *prefix != '\0'; ++i) {
                if (*prefix++ != encoded[i]) { return false; }
            }
        }
    }
    return *prefix == '\0';
}

ApResult mercury_complete(MercuryClient *c, MercuryPending *p)
{
    Header header = Header_init_zero;
    if (p->count == 0 || p->lengths[0] == 0 ||
        proto_decode(Header_fields, c->pool + p->offset, p->lengths[0], &header) != PROTO_OK) {
        mercury_finish(c, p, AP_MALFORMED, NULL); return AP_MALFORMED;
    }
    MercuryResponse response = {0};
    response.uri = header.has_uri && header.uri[0] != '\0' ? header.uri : p->uri;
    response.status = header.status_code; response.event = p->command == MERCURY_EVENT;
    response.count = p->count - 1;
    size_t pos = p->offset + p->lengths[0];
    for (size_t i = 1; i < p->count; ++i) {
        response.parts[i - 1] = (MercurySpan){c->pool + pos, p->lengths[i]}; pos += p->lengths[i];
    }
    ApResult result = response.status >= 400 ? AP_IO_ERROR : AP_OK;
    if (response.event) {
        bool found = false;
        if (response.uri[0] == '\0') { result = AP_MALFORMED; }
        if (result == AP_OK) {
            for (size_t i = 0; i < MERCURY_SUBSCRIPTIONS; ++i) {
                MercurySub *s = &c->subs[i];
                if (!s->live || !s->active || s->id != p->listeners[i]) { continue; }
                for (size_t j = 0; j < s->count; ++j) {
                    if (matches(response.uri, s->aliases[j])) {
                        s->callback(s->context, (MercuryId){s->id}, AP_OK, &response); found = true; break;
                    }
                }
            }
            if (!found) { result = AP_STALE; }
        }
        mercury_release(c, p);
    } else {
        if (result == AP_OK && p->subscription >= 0) { result = subscribed(&c->subs[p->subscription], &response); }
        mercury_finish(c, p, result, &response);
    }
    return result;
}
