#include "protocol/ap_resolve.h"
#include "api/json_base.h"
#include "support/test_check.h"
#include <string.h>

static char input[JSON_BASE_INPUT_LIMIT + YYJSON_PADDING_SIZE];
static _Alignas(16) uint8_t pool[65542];

static ApResult parse(const char *text, ApEndpoints *output)
{
    size_t bytes = strlen(text); CHECK(bytes <= JSON_BASE_INPUT_LIMIT);
    memcpy(input, text, bytes);
    return ap_resolve_parse(input, bytes, sizeof(input), pool, sizeof(pool), output);
}

void test_ap_resolve(void)
{
    ApEndpoints endpoints;
    HttpRequestId id;
    CHECK(ap_resolve_start(NULL, input, JSON_BASE_INPUT_LIMIT, &id) == AP_INVALID);
    /* Shape observed by REA from the official resolver; hosts are fixture data,
     * not production fallbacks. Six AP entries must fit without truncation. */
    CHECK(parse("{\"accesspoint\":[\"ap-gew4.spotify.com:4070\",\"ap-gew4.spotify.com:443\","
        "\"ap-gew4.spotify.com:80\",\"ap-guc3.spotify.com:4070\",\"ap-gue1.spotify.com:443\",\"ap-gew1.spotify.com:80\"],"
        "\"dealer\":[\"gew4-dealer.spotify.com:443\",\"guc3-dealer.spotify.com:443\",\"gue1-dealer.spotify.com:443\",\"gew1-dealer.spotify.com:443\"],"
        "\"spclient\":[\"gew4-spclient.spotify.com:443\",\"guc3-spclient.spotify.com:443\",\"gue1-spclient.spotify.com:443\",\"gew1-spclient.spotify.com:443\"]}",
        &endpoints) == AP_OK);
    CHECK(endpoints.accesspoint_count == 6 && endpoints.dealer_count == 4 && endpoints.spclient_count == 4);
    CHECK(endpoints.accesspoint[5].port == 80 && strcmp(endpoints.accesspoint[5].host, "ap-gew1.spotify.com") == 0);
    CHECK(parse("{\"accesspoint\":[\"ap-a.spotify.com:4070\",\"ap-b.spotify.com:443\"],"
        "\"dealer\":[\"dealer.spotify.com:443\"],\"spclient\":[\"spclient.wg.spotify.com:443\"]}", &endpoints) == AP_OK);
    CHECK(endpoints.accesspoint_count == 2 && endpoints.dealer_count == 1 && endpoints.spclient_count == 1);
    CHECK(strcmp(endpoints.accesspoint[0].host, "ap-a.spotify.com") == 0 && endpoints.accesspoint[0].port == 4070);
    CHECK(strcmp(endpoints.accesspoint[1].host, "ap-b.spotify.com") == 0 && endpoints.accesspoint[1].port == 443);
    memset(input, 0, sizeof(input)); memset(pool, 0, sizeof(pool));
    CHECK(strcmp(endpoints.dealer[0].host, "dealer.spotify.com") == 0); /* Owned output. */
    static const char *const invalid[] = {
        "null", "[]", "{}", "{\"accesspoint\":null}",
        "{\"accesspoint\":[1],\"dealer\":[],\"spclient\":[]}",
        "{\"accesspoint\":[\"host:0\"]}", "{\"accesspoint\":[\"host:65536\"]}",
        "{\"accesspoint\":[\"host:9999999999999999999999999\"]}",
        "{\"accesspoint\":[\"host:-1\"]}", "{\"accesspoint\":[\"host:443/path\"]}",
        "{\"accesspoint\":[\"user@host:443\"]}", "{\"accesspoint\":[\"host\"]}",
        "{\"accesspoint\":[\"host:443\\u0000evil\"]}", "{\"accesspoint\":[\"a..host:443\"]}",
        "{\"accesspoint\":[\"-host:443\"]}", "{\"accesspoint\":[\"host-:443\"]}",
        "{\"accesspoint\":[\"host.:443\"]}", "{\"accesspoint\":[\"http://host:443\"]}",
        "{\"accesspoint\":[\"h\xc0\x80:443\"]}",
        "{\"accesspoint\":[\"h:1\",\"h:2\",\"h:3\",\"h:4\",\"h:5\",\"h:6\",\"h:7\",\"h:8\",\"h:9\"]}"
    };
    ApEndpoints saved = endpoints;
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        ApResult result = parse(invalid[i], &endpoints);
        CHECK((result == AP_MALFORMED || result == AP_LIMIT) && memcmp(&saved, &endpoints, sizeof(saved)) == 0);
    }
    CHECK(parse("{\"accesspoint\":[\"ap\\u002da:65535\"],\"dealer\":[\"dealer:443\"],\"spclient\":[\"spclient:443\"]}", &endpoints) == AP_OK);
    CHECK(strcmp(endpoints.accesspoint[0].host, "ap-a") == 0 && endpoints.accesspoint[0].port == 65535);
    memcpy(input, "{}", 2);
    CHECK(ap_resolve_parse(input, 2, sizeof(input), pool, 32, &endpoints) == AP_NO_MEMORY);
    ApEndpoint bad = {.host = "a", .port = 0}; CHECK(!ap_endpoint_valid(&bad));
    memset(bad.host, 'a', sizeof(bad.host)); bad.port = 1; CHECK(!ap_endpoint_valid(&bad));
}
