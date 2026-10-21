"""Index native parity captures with honest reference/baseline labels and hashes."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import struct


def create(output, root, build, reference_version):
    captures = []
    for path in sorted(output.glob('*.png')):
        data = path.read_bytes()
        if data[:8] != b'\x89PNG\r\n\x1a\n':
            raise ValueError(f'{path}: expected PNG')
        width, height = struct.unpack('!II', data[16:24])
        captures.append(dict(file=path.name, width=width, height=height,
                             sha256=hashlib.sha256(data).hexdigest()))
    extraction = json.loads((root / 'ref/manifest.json').read_text())['spotify']
    executable = build / 'spotic.exe'
    cache = (build / 'CMakeCache.txt').read_text()
    build_type = next(line.split('=', 1)[1] for line in cache.splitlines()
                      if line.startswith('CMAKE_BUILD_TYPE:'))
    manifest = dict(
        date=datetime.now(timezone.utc).isoformat(), theme='dark',
        kind='synthetic native DX11 WARP fixtures; no live-route acceptance',
        reference=dict(installed_version=reference_version,
                       extracted_version=extraction['version'],
                       archive_sha256=extraction['sha256'],
                       fresh_desktop_capture=False, dpi=None, zoom=None,
                       limitation='Computer Use stopped; retained reference crops are from 2026-10-20, unknown DPI.'),
        executable=dict(configuration=build_type, asan='ENABLE_ASAN:BOOL=ON' in cache,
                        bytes=executable.stat().st_size,
                        sha256=hashlib.sha256(executable.read_bytes()).hexdigest()),
        captures=captures)
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf8')
    html = '''<!doctype html><html lang="en"><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Native desktop comparison — issues 41 and 43</title>
<style>body{background:#121212;color:#fff;font:16px system-ui;margin:24px}h1{font-size:26px}
p,figcaption{color:#b3b3b3;line-height:1.5}label{display:inline-block;margin:0 16px 16px 0}
select{font:inherit;color:#fff;background:#2a2a2a;border:1px solid #777;padding:8px;border-radius:6px}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:16px}figure{margin:0;min-width:0}
img{width:100%;height:auto;background:#000;display:block}figcaption{padding:8px 0}
#reference{margin-top:24px;max-width:1000px}a{color:#72e9a0}:focus-visible{outline:2px solid white;outline-offset:4px}
@media(max-width:800px){.grid{grid-template-columns:1fr}}</style>
<h1>Native desktop comparison</h1>
<p>Issues #41 and #43 — synthetic fixtures at known dimensions. The left pane is the preceding
native implementation, the right pane is this change. These comparisons show changes, not Spotify parity.
Actual Spotify crops are shown separately where available; content, viewport and DPI differ.</p>
<label>Route <select id="route"><option value="playlist">Playlist</option><option value="search">Search</option>
<option value="album">Album</option><option value="artist">Artist</option><option value="lyrics">Lyrics</option>
<option value="components">Components</option></select></label>
<label>Width <select id="width"><option>default</option><option>narrow</option><option>wide</option></select></label>
<label>Scale <select id="dpi"><option value="96">100%</option><option value="120">125%</option>
<option value="144">150%</option><option value="192">200%</option></select></label>
<label>State <select id="state"><option value="">Content</option><option value="loading">Loading (Search)</option>
<option value="empty">Empty (Search)</option><option value="error">Error (Search)</option><option value="menu">Menu (Search)</option></select></label>
<p id="dimensions"></p><div class="grid"><figure><img id="before" alt="Previous native fixture"><figcaption id="beforeCaption"></figcaption></figure>
<figure><img id="after" alt="Current native fixture"><figcaption id="afterCaption"></figcaption></figure></div>
<figure id="reference"><img id="referenceImage" alt="Retained Spotify public main pane"><figcaption>
Spotify 1.3.5.3058 public main-pane crop, 2026-10-20. Different content and dimensions; reference DPI/zoom unmeasured.</figcaption></figure>
<p id="referenceGap"></p><p><a href="manifest.json">Capture identities and reference limitations</a> ·
<a href="../desktop-parity.md">Verification and remaining acceptance</a> · <a href="../../specs/spotify-desktop-parity.md">Reference specification</a></p>
<script id="data" type="application/json">MANIFEST</script><script>
const manifest=JSON.parse(document.getElementById('data').textContent);
const el=id=>document.getElementById(id);
function show(){const route=el('route').value,component=route==='components',state=route==='search'?el('state').value:'';
const width=component?'default':state?'default':el('width').value,dpi=el('dpi').value;
el('width').disabled=component||!!state;el('state').disabled=route!=='search';
const stem=component?'components':route==='playlist'?'playlist':'catalog-'+route;
const filename=stem+(state?'-'+state:'')+'-'+width+'-'+dpi+'.png';
const item=manifest.captures.find(x=>x.file===filename);el('after').hidden=!item;
if(item){el('after').src=filename;el('dimensions').textContent=`${item.width} × ${item.height} physical pixels; ${dpi} DPI native fixture.`;}
else{el('dimensions').textContent='Capture not available for this state.';}
el('afterCaption').textContent='Current native fixture — '+filename;
el('before').hidden=component||!!state;if(!component&&!state)el('before').src='../detail-routes/'+stem+'-'+width+'-'+dpi+'.png';
el('beforeCaption').textContent=component||state?'No preceding capture for this state.':'Previous native fixture — before this change';
const reference=route==='album'||route==='search';el('reference').hidden=!reference;
if(reference)el('referenceImage').src='../detail-routes/reference-'+route+'-main.png';
el('referenceGap').textContent=reference?'Retained reference only: no matching-content pixel parity is asserted.':'No publishable runtime reference capture is retained for this route; fresh desktop comparison remains pending.';}
for(const id of ['route','width','dpi','state'])el(id).addEventListener('change',show);show();
</script></html>'''
    html = html.replace('MANIFEST', json.dumps(manifest).replace('</', '<\\/'))
    (output / 'index.html').write_text(html, encoding='utf8')
    print(f'Indexed {len(captures)} captures with dimensions and SHA-256 identities.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--reference-version', required=True)
    args = parser.parse_args()
    create(args.output, Path(__file__).resolve().parents[1], args.build, args.reference_version)
