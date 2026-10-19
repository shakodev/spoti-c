"""Validate private IID literals against the compiler's Windows SDK projection."""
import re
import sys
from pathlib import Path
from uuid import UUID

sdk=Path(sys.argv[1])
own=Path(sys.argv[2]).read_text(encoding='utf-8')
headers={name:(sdk/name).read_text(encoding='utf-8-sig') for name in
         ('winrt/windows.media.h','winrt/windows.foundation.h',
          'winrt/windows.storage.streams.h','um/SystemMediaTransportControlsInterop.h')}
classes={
    'interop':('um/SystemMediaTransportControlsInterop.h','ISystemMediaTransportControlsInterop'),
    'transport':('winrt/windows.media.h','ISystemMediaTransportControls'),
    'transport2':('winrt/windows.media.h','ISystemMediaTransportControls2'),
    'timeline':('winrt/windows.media.h','ISystemMediaTransportControlsTimelineProperties'),
    'uri':('winrt/windows.foundation.h','IUriRuntimeClassFactory'),
    'art':('winrt/windows.storage.streams.h','IRandomAccessStreamReferenceStatics'),
}
for key,(header,name) in classes.items():
    match=re.search(r'MIDL_INTERFACE\("([\da-fA-F-]+)"\)\s*'+name+r'\s*:',headers[header])
    assert match, f'SDK interface missing: {name}'
    classes[key]=UUID(match[1])
for key,arg in [('button','SystemMediaTransportControlsButtonPressedEventArgs'),('seek','PlaybackPositionChangeRequestedEventArgs')]:
    pattern=(r'struct __declspec\(uuid\("([\da-fA-F-]+)"\)\)\s*ITypedEventHandler<'
             r'ABI::Windows::Media::SystemMediaTransportControls\*, ABI::Windows::Media::'+arg+r'\*>')
    match=re.search(pattern,headers['winrt/windows.media.h'])
    assert match, f'SDK delegate missing: {arg}'
    classes[key]=UUID(match[1])
for name,body in re.findall(r'static const IID mc_iid_(\w+)=\{([^;]+)\};',own):
    parts=[int(x,16) for x in re.findall(r'0x([\da-fA-F]+)',body)]
    assert len(parts)==11, f'Invalid IID fields: {name}'
    raw=parts[0].to_bytes(4,'big')+parts[1].to_bytes(2,'big')+parts[2].to_bytes(2,'big')+bytes(parts[3:])
    assert UUID(bytes=raw)==classes.pop(name), f'IID disagrees with SDK: {name}'
assert not classes, f'Unchecked interfaces: {classes.keys()}'
print('SMTC ABI: 8 IIDs match the Windows SDK; C compile-time checks validate vtable slots.')
