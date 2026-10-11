#!/usr/bin/env python3
"""Consolidate this local composing session into complete, aligned multitracks."""
import argparse
import json
import shutil
from pathlib import Path

import numpy as np

from piano_phrasing import read_pcm
from piano_study import RATE, sha, write_wav


def at(bpm, bar, step=0):
    return round((bar * 16 + step) * 60 / bpm / 4 * RATE)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    if args.output.exists():
        raise FileExistsError(f'Output already exists: {args.output}')
    recordings = Path('recordings')
    read = lambda name: read_pcm(recordings / (name + '.wav')).astype(float) / 32768
    args.output.mkdir(parents=True)
    catalog = []

    def save(slug, title, version, bpm, key, stems):
        destination = args.output / slug
        destination.mkdir()
        mix_path = recordings / (version + '.wav')
        mix_pcm = read_pcm(mix_path).astype(np.int32)
        pcm = {name: np.rint(samples * 32768).astype(np.int32) for name, samples in stems.items()}
        if any(samples.shape != mix_pcm.shape for samples in pcm.values()):
            raise ValueError(f'{title}: stems do not cover the complete track')
        residual = mix_pcm - sum(pcm.values())
        error = int(np.max(np.abs(residual)))
        if error > 8:
            raise ValueError(f'{title}: unexpected stem reconstruction error ({error} PCM units)')
        # The earlier takes each passed through PCM16. Fold only their tiny
        # rounding residue into the chord stem so unity playback is exact.
        pcm['keys'] += residual
        if not np.array_equal(sum(pcm.values()), mix_pcm):
            raise ValueError('Consolidated stems do not sum to the saved mix')
        for name, samples in pcm.items():
            write_wav(destination / ('stem-' + name + '.wav'), samples.astype(float) / 32768)
        shutil.copy2(mix_path, destination / 'mix.wav')
        shutil.copy2(recordings / (version + '.m4a'), destination / 'mix.m4a')
        shutil.copy2(recordings / (version + '-cues.txt'), destination / 'cues.txt')
        manifest = {
            'title': title, 'version': version, 'bpm': bpm, 'key': key,
            'sample_rate': RATE, 'frames': len(mix_pcm), 'seconds': len(mix_pcm) / RATE,
            'stem_alignment': 'All stems begin at sample zero and include the full tail.',
            'unity_sum_exact_pcm': True, 'rounding_residue_peak_pcm': error,
            'mix_source_sha256': sha(mix_path),
            'files': {path.name: sha(path) for path in sorted(destination.iterdir())},
        }
        (destination / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
        catalog.append({k: manifest[k] for k in ('title', 'version', 'bpm', 'key', 'seconds')})
        catalog[-1]['directory'] = slug
        print(f'{title}: {len(pcm)} complete stems, exact unity sum, {len(mix_pcm) / RATE:.2f}s', flush=True)

    # Side Street: reconstruct the instrument buses across the inserted theme.
    insert, length, seam = at(116, 32), at(116, 16), round(.008 * RATE)

    def extend(samples, theme=None):
        before, after = samples[:insert].copy(), samples[insert:].copy()
        before[-seam:] *= np.linspace(1, 0, seam)[:, None]
        after[:seam] *= np.linspace(0, 1, seam)[:, None]
        middle = np.zeros((length, 2)) if theme is None else read('side-street-v8-theme-' + theme)
        return np.concatenate((before, middle, after))

    strings = extend(read('side-street-v2-hook'), 'strings')
    first, last = at(116, 24), at(116, 28)
    amount = np.ones(last - first)
    amount[:at(116, 1)] = np.linspace(0, 1, at(116, 1))
    amount[-at(116, 0, 2):] = np.linspace(1, 0, at(116, 0, 2))
    strings[first:last] -= read('side-street-v2-hook')[first:last] * amount[:, None]
    save('side-street', 'Side Street', 'side-street-v9', 116, 'F minor', {
        'bass': extend(read('side-street-v6-bass'), 'bass'),
        'drums': extend(read('side-street-v2-kick') + read('side-street-v2-percussion'), 'drums'),
        'strings': strings,
        'keys': extend(read('side-street-v6-keys'), 'keys'),
        'piano': extend(read('side-street-v5-piano'), 'piano') + read('side-street-v9-intro-piano'),
        'pickups': extend(read('side-street-v7-pickups')),
    })
    del strings

    # Corner Light: the featured piano and the remembered tune are independent
    # performances; the chord and string withdrawals retain their exact fades.
    strings, keys = read('corner-light-base-v1-hook'), read('corner-light-v1-keys')
    feature = np.zeros(len(keys))
    first, last = at(112, 24), at(112, 28)
    feature[first:last] = 1
    ramp = round(.045 * RATE)
    feature[first:first + ramp] = np.linspace(0, 1, ramp)
    feature[last - ramp:last] = np.linspace(1, 0, ramp)
    bridge = np.zeros(len(keys))
    first, last = at(112, 32), at(112, 40)
    bridge[first:last] = 1
    ramp = round(.040 * RATE)
    bridge[first:first + ramp] = np.linspace(0, 1, ramp)
    bridge[last - ramp:last] = np.linspace(1, 0, ramp)
    strings *= 1 - bridge[:, None]
    keys *= 1 - .16 * feature[:, None] - .12 * bridge[:, None]
    save('corner-light', 'Corner Light', 'corner-light-v3', 112, 'D minor', {
        'bass': read('corner-light-base-v1-bass'),
        'drums': read('corner-light-base-v1-kick') + read('corner-light-base-v1-percussion'),
        'strings': strings, 'keys': keys,
        'piano': read('corner-light-v2-melody') + read('corner-light-v3-bridge-piano'),
    })
    del strings, keys, feature, bridge

    save('low-glow', 'Low Glow', 'low-glow-v2', 122, 'A minor', {
        'bass': read('low-glow-base-v1-bass'),
        'drums': read('low-glow-base-v1-kick') + read('low-glow-base-v1-percussion'),
        'strings': read('low-glow-base-v1-strings'), 'keys': read('low-glow-v1-keys'),
        'piano': read('low-glow-v2-melody'),
    })
    (args.output / 'tracks.json').write_text(json.dumps(catalog, indent=2) + '\n')
    playlist = '#EXTM3U\n' + ''.join(
        f"#EXTINF:{round(track['seconds'])},{track['title']}\n{track['directory']}/mix.m4a\n"
        for track in catalog)
    (args.output / 'Session.m3u8').write_text(playlist)
    (args.output / 'READ-ME.txt').write_text(
        'Local music session — 5 October 2026\n\n'
        'Side Street: 116 BPM, F minor, 2:48 — latest long arrangement.\n'
        'Corner Light: 112 BPM, D minor, 2:20 — piano feature and quiet melodic middle.\n'
        'Low Glow: 122 BPM, A minor, 1:53 — keyboard groove and short string replies.\n\n'
        'Open Session.m3u8 or any mix.m4a to listen. mix.wav is the uncompressed version.\n'
        'Each song folder contains complete 48 kHz stereo PCM16 stems. Start them\n'
        'all at zero with unity gain to recreate the exact saved mix. Effects and\n'
        'arrangement fades are included in the stems.\n\n'
        'All earlier takes remain in recordings/. None were overwritten. These\n'
        'are current working versions. Everything stayed local.\n')


if __name__ == '__main__':
    main()
