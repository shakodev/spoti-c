#ifndef SPOTIC_MEDIA_CONTROLS_ABI_H
#define SPOTIC_MEDIA_CONTROLS_ABI_H
/* SDK C projections, not reconstructed vtables. Validated with SDK 10.0.26100.0. */
#include <windows.h>
#include <roapi.h>
#include <windows.media.h>
#include <SystemMediaTransportControlsInterop.h>
#include <stddef.h>
typedef __x_ABI_CWindows_CMedia_CISystemMediaTransportControls McTransport;
typedef __x_ABI_CWindows_CMedia_CISystemMediaTransportControls2 McTransport2;
typedef __x_ABI_CWindows_CMedia_CISystemMediaTransportControlsDisplayUpdater McDisplay;
typedef __x_ABI_CWindows_CMedia_CIMusicDisplayProperties McMusic;
typedef __x_ABI_CWindows_CMedia_CISystemMediaTransportControlsTimelineProperties McTimeline;
typedef __x_ABI_CWindows_CFoundation_CTimeSpan McTime;
typedef __x_ABI_CWindows_CFoundation_CIUriRuntimeClassFactory McUriFactory;
typedef __x_ABI_CWindows_CFoundation_CIUriRuntimeClass McUri;
typedef __x_ABI_CWindows_CStorage_CStreams_CIRandomAccessStreamReferenceStatics McArtFactory;
typedef __x_ABI_CWindows_CStorage_CStreams_CIRandomAccessStreamReference McArt;
typedef __x_ABI_CWindows_CMedia_CISystemMediaTransportControlsButtonPressedEventArgs McButtonArgs;
typedef __x_ABI_CWindows_CMedia_CIPlaybackPositionChangeRequestedEventArgs McSeekArgs;
typedef __FITypedEventHandler_2_Windows__CMedia__CSystemMediaTransportControls_Windows__CMedia__CSystemMediaTransportControlsButtonPressedEventArgs McButton;
typedef __FITypedEventHandler_2_Windows__CMedia__CSystemMediaTransportControls_Windows__CMedia__CPlaybackPositionChangeRequestedEventArgs McSeek;
typedef __FITypedEventHandler_2_Windows__CMedia__CSystemMediaTransportControls_Windows__CMedia__CSystemMediaTransportControlsButtonPressedEventArgsVtbl McButtonVtbl;
typedef __FITypedEventHandler_2_Windows__CMedia__CSystemMediaTransportControls_Windows__CMedia__CPlaybackPositionChangeRequestedEventArgsVtbl McSeekVtbl;
typedef enum __x_ABI_CWindows_CMedia_CSystemMediaTransportControlsButton McButtonValue;
typedef enum __x_ABI_CWindows_CMedia_CMediaPlaybackStatus McStatus;
#define MC_BUTTON(name) SystemMediaTransportControlsButton_##name
#define MC_STATUS(name) MediaPlaybackStatus_##name
#define MC_MUSIC MediaPlaybackType_Music
/* Kept private to avoid a WindowsApp import dependency just for IID storage. */
static const IID mc_iid_interop={0xddb0472d,0xc911,0x4a1f,{0x86,0xd9,0xdc,0x3d,0x71,0xa9,0x5f,0x5a}};
static const IID mc_iid_transport={0x99fa3ff4,0x1742,0x42a6,{0x90,0x2e,0x08,0x7d,0x41,0xf9,0x65,0xec}};
static const IID mc_iid_transport2={0xea98d2f6,0x7f3c,0x4af2,{0xa5,0x86,0x72,0x88,0x98,0x08,0xef,0xb1}};
static const IID mc_iid_timeline={0x5125316a,0xc3a2,0x475b,{0x85,0x07,0x93,0x53,0x4d,0xc8,0x8f,0x15}};
static const IID mc_iid_uri={0x44a9796f,0x723e,0x4fdf,{0xa2,0x18,0x03,0x3e,0x75,0xb0,0xc0,0x84}};
static const IID mc_iid_art={0x857309dc,0x3fbf,0x4e7d,{0x98,0x6f,0xef,0x3b,0x1a,0x07,0xa9,0x64}};
static const IID mc_iid_button={0x0557e996,0x7b23,0x5bae,{0xaa,0x81,0xea,0x0d,0x67,0x11,0x43,0xa4}};
static const IID mc_iid_seek={0x44e34f15,0xbdc0,0x50a7,{0xac,0xe4,0x39,0xe9,0x1f,0xb7,0x53,0xf1}};
_Static_assert(sizeof(McTime)==8 && sizeof(EventRegistrationToken)==8,"WinRT scalar ABI");
_Static_assert(offsetof(ISystemMediaTransportControlsInteropVtbl,GetForWindow)==6*sizeof(void *),"IInspectable interop prefix");
_Static_assert(offsetof(__x_ABI_CWindows_CMedia_CISystemMediaTransportControlsVtbl,add_ButtonPressed)==32*sizeof(void *),"SDK SMTC event slot");
_Static_assert(offsetof(__x_ABI_CWindows_CMedia_CISystemMediaTransportControls2Vtbl,UpdateTimelineProperties)==12*sizeof(void *),"SDK timeline slot");
_Static_assert(offsetof(McButtonVtbl,Invoke)==3*sizeof(void *),"Delegate IUnknown prefix");
#endif
