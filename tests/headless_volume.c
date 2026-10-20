#include "headless_support.h"

static void notify(void *context) { CHECK(SetEvent((HANDLE)context)); }

void headless_volume_receipts(void)
{
    HeadlessFixture fixture; hf_open(&fixture);
    HANDLE changed=CreateEventW(NULL,TRUE,FALSE,NULL); CHECK(changed);
    HeadlessConfig config={.cache_directory=fixture.cache,.fixture_url=fixture.url,
        .state_changed=notify,.state_context=changed};
    Headless *player=NULL; CHECK(headless_open(&config,&player));
    CHECK(WaitForSingleObject(changed,2000)==WAIT_OBJECT_0); CHECK(ResetEvent(changed));
    PlayerCommand command={.type=PLAYER_VOLUME,.volume=1,.ticket=7};
    CHECK(headless_submit(player,&command)==PLAYER_OK);
    CHECK(WaitForSingleObject(changed,2000)==WAIT_OBJECT_0);
    CHECK(headless_snapshot(player).volume_ticket==7 && headless_snapshot(player).volume==1);
    CHECK(ResetEvent(changed)); command.ticket=0;
    CHECK(headless_submit(player,&command)==PLAYER_OK);
    CHECK(WaitForSingleObject(changed,2000)==WAIT_OBJECT_0);
    CHECK(headless_snapshot(player).volume_ticket==0 && headless_snapshot(player).volume==1);
    HeadlessMetrics metrics; CHECK(headless_close(player,&metrics) && metrics.balanced);
    CHECK(CloseHandle(changed)); hf_close(&fixture);
    puts("volume receipts notify the UI even when the level is unchanged; no polling");
}
