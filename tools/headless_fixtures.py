"""Independent, public synthetic 320-kbps Vorbis/CTR corpus for issue #19.

Uses the already pinned vorbis and crypto fixture requirements. No Spotify
recordings, credentials, traffic or keys. Default verifies committed digests.
"""
import argparse
import hashlib
import json
from pathlib import Path
import numpy as np
import soundfile as sf
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

DEST = Path(__file__).resolve().parents[1] / 'tests/fixtures/audio'

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--write', action='store_true')
    args = parser.parse_args()
    if args.write:
        DEST.mkdir(parents=True, exist_ok=True)
        # Stereo noise avoids an unrealistically compressible benchmark signal.
        rate, frames = 48000, 48000 * 12 + 137
        rng = np.random.default_rng(19)
        pcm = (rng.uniform(-0.24, 0.24, (frames, 2))).astype('float32')
        with sf.SoundFile(str(DEST / 'synthetic.ogg'), 'w', rate, 2,
                          format='OGG', subtype='VORBIS', compression_level=0.1) as output:
            for at in range(0, frames, 4096):
                output.write(pcm[at:at + 4096])
        ogg = (DEST / 'synthetic.ogg').read_bytes()
        oracle, decoded_rate = sf.read(str(DEST / 'synthetic.ogg'), dtype='float32')
        assert decoded_rate == rate and len(oracle) == frames
        (DEST / 'synthetic.f32').write_bytes(oracle.astype('<f4').tobytes())
        prefix = bytes(144) + np.array([0, 1, 0, 1], dtype='<f4').tobytes() + bytes(7)
        key, iv = bytes(range(16)), bytes.fromhex('72e067fbddcbcf77ebe8bc643f630d93')
        enc = Cipher(algorithms.AES(key), modes.CTR(iv)).encryptor()
        (DEST / 'synthetic.enc').write_bytes(enc.update(prefix + ogg) + enc.finalize())
        nominal = int.from_bytes(ogg[48:52], 'little', signed=True)
        manifest = dict(schema=1, frames=frames, sample_rate=rate, channels=2,
                        nominal_bitrate=nominal, measured_bitrate=len(ogg)*8*rate/frames,
                        oracle='libsndfile/libvorbis', soundfile=sf.__version__,
                        libsndfile=sf.__libsndfile_version__, numpy=np.__version__, files={})
        for name in ['synthetic.ogg', 'synthetic.enc', 'synthetic.f32']:
            data = (DEST / name).read_bytes()
            manifest['files'][name] = dict(bytes=len(data), sha256=hashlib.sha256(data).hexdigest())
        (DEST / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    manifest = json.loads((DEST / 'manifest.json').read_text())
    for name, record in manifest['files'].items():
        data = (DEST / name).read_bytes()
        assert len(data) == record['bytes'] and hashlib.sha256(data).hexdigest() == record['sha256']
    print(json.dumps({k: v for k, v in manifest.items() if k != 'files'}))

if __name__ == '__main__':
    main()
