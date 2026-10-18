#include "media_test.h"
#include "api/storage_resolve.h"

static const uint8_t file_id[20] = {1};
static MediaWire message;
static MediaUrl urls[MEDIA_URLS];

static void fixture(const char *url)
{
    memset(&message, 0, sizeof(message)); media_blob(&message, 4, file_id, 20);
    if (url != NULL) { media_text(&message, 2, url); }
}
static MediaResult decode(size_t *count)
{
    return storage_resolve_decode(message.data, message.bytes, file_id, UINT64_C(2000000000000), 100, urls, count);
}

void storage_tests(void)
{
    static const char *const forms[] = {
        "https://cdn.example/audio?verify=2000000065-signature",
        "https://cdn.example/audio?__token__=exp=2000000065~hmac=fixture",
        "https://cdn.example/audio?Expires=2000000065~FullPath~hmac=fixture",
        "https://cdn.example/audio?2000000065_signature",
        "https://cdn.example/audio?__token__=exp%3D2000000065~hmac%3Dfixture"
    };
    for (size_t i = 0; i < sizeof(forms) / sizeof(forms[0]); ++i) {
        fixture(forms[i]); size_t count = 77;
        CHECK(decode(&count) == MEDIA_OK && count == 1 && urls[0].refresh_at == 60100);
        CHECK(strcmp(urls[0].url, forms[i]) == 0);
    }
    fixture("https://cdn.example/audio?unknown=finite"); size_t count = 0;
    CHECK(decode(&count) == MEDIA_OK && urls[0].refresh_at == 60100);
    fixture("https://cdn.example/audio?verify=2000000006-signature");
    CHECK(decode(&count) == MEDIA_OK && urls[0].refresh_at == 1100);
    fixture("https://cdn.example/audio?verify=2000000005-signature");
    CHECK(decode(&count) == MEDIA_EXPIRED);
    fixture("https://cdn.example/audio?verify=4102444800-signature&Expires=1");
    CHECK(decode(&count) == MEDIA_EXPIRED);
    fixture("https://cdn.example/audio?__token__=exp=4102444800~exp=1~hmac=x");
    CHECK(decode(&count) == MEDIA_EXPIRED);
    fixture("https://cdn.example/audio?unknown=x%26Expires%3D1");
    CHECK(decode(&count) == MEDIA_OK && urls[0].refresh_at == 60100);
    fixture("https://cdn.example/audio?verify=1-signature");
    media_text(&message, 2, "https://cdn.example/audio?Expires=2000000065");
    CHECK(decode(&count) == MEDIA_OK && count == 1);
    static const char *const malformed[] = {
        "http://cdn.example/audio?Expires=2000000065", "https://user:password@cdn.example/audio",
        "https://cdn.example/a b", "https://cdn.example/audio#fragment", "https:///no-host",
        "https://cdn.example/audio?Expires=18446744073709551615", "https://cdn.example/audio?Expires=nan",
        "https://cdn.example/audio?verify=%00-signature", "https://cdn.example/audio?verify=%1"
    };
    for (size_t i = 0; i < sizeof(malformed) / sizeof(malformed[0]); ++i) {
        fixture(malformed[i]); count = 77; memset(urls, 0x5a, sizeof(urls));
        CHECK(decode(&count) == MEDIA_MALFORMED && count == 77 && urls[0].url[0] == 0x5a);
    }
    fixture(forms[0]);
    for (size_t i = 1; i < MEDIA_URLS; ++i) { media_text(&message, 2, forms[i]); }
    CHECK(decode(&count) == MEDIA_OK && count == MEDIA_URLS);
    media_text(&message, 2, forms[0]); CHECK(decode(&count) == MEDIA_LIMIT);
    fixture(NULL); CHECK(decode(&count) == MEDIA_MALFORMED);
    fixture(forms[0]); message.data[2] ^= 1;
    CHECK(decode(&count) == MEDIA_MALFORMED);
    fixture(NULL); media_number(&message, 1, 1); CHECK(decode(&count) == MEDIA_STORAGE);
    fixture(NULL); media_number(&message, 1, 3); CHECK(decode(&count) == MEDIA_RESTRICTED);
    fixture(NULL); media_number(&message, 1, 2); CHECK(decode(&count) == MEDIA_UNSUPPORTED);
    fixture(forms[0]);
    for (size_t n = 0; n < message.bytes; ++n) {
        MediaResult result = storage_resolve_decode(message.data, n, file_id, UINT64_C(2000000000000), 100, urls, &count);
        CHECK(result != MEDIA_OK);
    }
    uint8_t wrong[20] = {2}; fixture(forms[0]);
    CHECK(storage_resolve_decode(message.data, message.bytes, wrong, 0, 100, urls, &count) == MEDIA_MALFORMED);
}
