#include "headless/internal.h"
#include "support/test_check.h"

static Headless owner;
static unsigned wakes;
static void wake(void *context) { CHECK(context==&wakes); ++wakes; }
void headless_display_tests(void)
{
    owner.config.state_changed=wake; owner.config.state_context=&wakes;
    MediaDisplay display={.title="Network owner title",.artist="Artist",.cover_id={0x55},.has_cover=true}, out={0};
    uint8_t id[16]={0}; id[15]=1;
    CHECK(!headless_track_info(&owner,id,&out));
    hl_publish_display(&owner,id,&display); CHECK(wakes==1);
    CHECK(headless_track_info(&owner,id,&out) && !strcmp(out.title,display.title) && out.has_cover && out.cover_id[0]==0x55);
    strcpy_s(display.title,sizeof(display.title),"Updated for the same ID");
    hl_publish_display(&owner,id,&display); CHECK(headless_track_info(&owner,id,&out) && !strcmp(out.title,display.title));
    for (unsigned i=2;i<=5;++i) { id[15]=(uint8_t)i; hl_publish_display(&owner,id,&display); }
    id[15]=1; CHECK(!headless_track_info(&owner,id,&out)); id[15]=2; CHECK(headless_track_info(&owner,id,&out));
    CHECK(!headless_track_info(NULL,id,&out) && !headless_track_info(&owner,NULL,&out) && !headless_track_info(&owner,id,NULL));
    PlayerQueueSnapshot queue; CHECK(!headless_queue_snapshot(&owner,&queue));
    atomic_store(&owner.closing,true); CHECK(!headless_track_info(&owner,id,&out));
    puts("Metadata handoff: requested IDs, replacement, bounded eviction and closed admission passed.");
}
