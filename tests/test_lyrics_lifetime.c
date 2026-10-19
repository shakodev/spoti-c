#include "lyrics_test.h"
#if defined(SPOTIC_ASAN)
#include <sanitizer/asan_interface.h>
#endif

static DWORD WINAPI read_document(void *context)
{
    const LyricsDocument *doc = context;
    for (unsigned i = 0; i < 10000; ++i) {
        CHECK(lyrics_document_retain(doc));
        CHECK(doc->count == 1 && json_span_equal(doc->lines[0].text, lyric_span("held")));
        CHECK(lyrics_document_release(doc));
    }
    CHECK(lyrics_document_release(doc)); return 0;
}
void lyrics_lifetime_tests(void)
{
    const LyricsDocument *doc; LyricsRequest r = lt_request(100, 1);
    const char *body = LYRICS_BODY("LINE_SYNCED","NONE","[{\"startTimeMs\":\"0\",\"words\":\"held\"}]","[]");
    CHECK(lt_deliver(r, body, &doc) == LYRICS_OK);
    const char *held = doc->lines[0].text.data;
    CHECK(lyrics_document_retain(doc) && lyrics_reset(lt_service, 101) == LYRICS_OK);
    CHECK(lyrics_document_release(doc));
    CHECK(memcmp(held,"held",4) == 0 && json_page_pool_resident(lt_pool) == 1);
    CHECK(lyrics_document_release(doc));
#if defined(SPOTIC_ASAN)
    CHECK(__asan_address_is_poisoned(held) && __asan_address_is_poisoned(doc));
#endif
    r = lt_request(102, 1); CHECK(lt_deliver(r, body, &doc) == LYRICS_OK);
    HANDLE threads[2];
    for (size_t i = 0; i < 2; ++i) {
        CHECK(lyrics_document_retain(doc));
        threads[i] = CreateThread(NULL, 0, read_document, (void *)doc, 0, NULL); CHECK(threads[i] != NULL);
    }
    CHECK(lyrics_reset(lt_service, 103) == LYRICS_OK && lyrics_document_release(doc));
    CHECK(WaitForMultipleObjects(2, threads, TRUE, 5000) == WAIT_OBJECT_0);
    for (size_t i = 0; i < 2; ++i) { CHECK(CloseHandle(threads[i])); }
    CHECK(json_page_pool_resident(lt_pool) == 0);
    for (uint64_t generation = 110; generation < 1110; ++generation) {
        r = lt_request(generation, 1); CHECK(lt_deliver(r, body, &doc) == LYRICS_OK);
        CHECK(lyrics_document_release(doc) && lyrics_reset(lt_service, generation + 1) == LYRICS_OK);
        CHECK(json_page_pool_resident(lt_pool) == 0);
    }
}
