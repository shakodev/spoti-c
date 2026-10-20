#include "core/thread.h"
#include "support/test_check.h"

static DWORD WINAPI owner(void *context)
{
    (void)context;
    CHECK(!core_thread_background_owner());
    CHECK(IsGUIThread(TRUE) && core_thread_is_gui());
    /* Reproduce an OS-created USER32 queue on an explicitly blocking owner. */
    core_thread_register_background();
    CHECK(core_thread_background_owner() && !core_thread_is_gui());
    return 0;
}
int main(void)
{
    CHECK(IsGUIThread(TRUE) && core_thread_is_gui());
    for (unsigned i=0;i<2;++i) {
        HANDLE thread=CreateThread(NULL,0,owner,NULL,0,NULL); CHECK(thread);
        CHECK(WaitForSingleObject(thread,5000)==WAIT_OBJECT_0);
        DWORD result; CHECK(GetExitCodeThread(thread,&result) && !result && CloseHandle(thread));
        CHECK(!core_thread_background_owner() && core_thread_is_gui());
    }
    puts("Explicit background owner survives a USER32 queue; real GUI threads remain guarded and roles do not leak.");
    return 0;
}
