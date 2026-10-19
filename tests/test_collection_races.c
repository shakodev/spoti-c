#include "collection_test.h"
#include <windows.h>

typedef struct PageReader {
    const CollectionPage *page;
    HANDLE start;
} PageReader;

static DWORD WINAPI read_page(void *context)
{
    PageReader *reader = context;
    CHECK(WaitForSingleObject(reader->start, 5000) == WAIT_OBJECT_0);
    for (unsigned i = 0; i < 20000; ++i) {
        CHECK(page_retain((JsonPage *)&reader->page->json));
        CHECK(reader->page->items[0].name.bytes == 4);
        CHECK(memcmp(reader->page->items[0].name.data, "held", 4) == 0);
        collection_test_release(reader->page);
    }
    collection_test_release(reader->page);
    return 0;
}

void collection_race_tests(void)
{
    const CollectionPage *page;
    CollectionRequest r = collection_test_fetch(COLLECTION_LIBRARY, 0, 60);
    CHECK(collection_test_deliver(r, "{\"statusCode\":200,\"header\":{\"totalCount\":1},\"entity\":["
        "{\"entityInfo\":{\"uri\":\"album:1\",\"name\":\"held\"},\"album\":{}}]}", &page) == COLLECTION_OK);
    HANDLE start = CreateEventW(NULL, TRUE, FALSE, NULL); CHECK(start != NULL);
    PageReader reader = {page, start}; HANDLE threads[2];
    for (size_t i = 0; i < 2; ++i) {
        CHECK(page_retain((JsonPage *)&page->json));
        threads[i] = CreateThread(NULL, 0, read_page, &reader, 0, NULL); CHECK(threads[i] != NULL);
    }
    CHECK(collection_reset(collection_test_service, 61) == COLLECTION_OK);
    collection_test_release(page);
    CHECK(SetEvent(start));
    CHECK(WaitForMultipleObjects(2, threads, TRUE, 10000) == WAIT_OBJECT_0);
    for (size_t i = 0; i < 2; ++i) { CHECK(CloseHandle(threads[i])); }
    CHECK(CloseHandle(start));
    CHECK(collection_resident_pages(collection_test_service) == 0);
}
