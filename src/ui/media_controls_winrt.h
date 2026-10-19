#ifndef SPOTIC_MEDIA_CONTROLS_WINRT_H
#define SPOTIC_MEDIA_CONTROLS_WINRT_H
#include "media_controls.h"
#include "media_controls_abi.h"
typedef struct MediaDelegate MediaDelegate;
typedef struct MediaWinrt {
    HWND window;
    McTransport *transport; McTransport2 *transport2;
    McDisplay *display; McMusic *music; McTimeline *timeline;
    McUriFactory *uri; McArtFactory *art;
    MediaDelegate *button, *seek;
    EventRegistrationToken button_token, seek_token;
    HRESULT last_error;
    unsigned stage; /* Setup stage for explicit platform diagnostics. */
    DWORD owner;
    bool runtime, button_registered, seek_registered;
} MediaWinrt;
/* Bind to the UI owner's top-level HWND. MC storage outlives this binding.
 * MEDIA_UNSUPPORTED means a valid window-key-only degraded controller. */
MediaAdapter media_winrt_adapter(MediaWinrt *, HWND);
MediaResult media_winrt_close(void *);
MediaDelegate *media_delegate_create(MediaControls *, bool seek);
void media_delegate_detach(MediaDelegate *);
void media_delegate_release(MediaDelegate *);
McButton *media_delegate_button(MediaDelegate *);
McSeek *media_delegate_seek(MediaDelegate *);
unsigned media_delegate_live_count(void);
#endif
