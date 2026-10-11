#!/usr/bin/env python3
"""Carry Last Tram into its return with a small bass and organ turnaround."""
import argparse
import json
import subprocess
from pathlib import Path

import numpy as np

from compare_hooks import loudness
from last_tram import BPM, STEP, frame
from organ_voice import organ
from piano_phrasing import read_pcm
from piano_study import RATE, sha, write_wav


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--renderer', type=Path, default=Path('build/music-palette-render'))
    args = parser.parse_args()
    prefix = str(args.output)
    paths = [Path(prefix + suffix) for suffix in
             ('.wav', '-turnaround.wav', '-turn-bass.wav', '-turn-kick.wav', '-turn-organ.wav',
              '-backing.wav', '-focus.wav', '.json', '-cues.txt', '-pickup-score.txt')]
    native = prefix + '-pickup-native'
    for path in paths + [Path(native + suffix) for suffix in
                         ('.wav', '-bass.wav', '-kick.wav', '-percussion.wav', '-strings.wav')]:
        if path.exists():
            raise FileExistsError(f'Output already exists: {path}')
    source = read_pcm(args.source).astype(float) / 32768
    if len(source) < frame(56):
        raise ValueError('Expected a complete Last Tram recording')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    score = f'''# Last Tram's last bridge bar, in local step coordinates.
tempo {BPM}
bars 2
tail 1
kick_tune 32.70319566
echo 0
color 0 245 850 0 0 0
# The first note silently prepares the voice before the audible pickup.
bass 10 38 .85 .71
bass 12 31 1.2 .58
bass 15 35 .50 .54
kick 14 .62
'''
    with paths[9].open('x') as out:
        out.write(score)
    subprocess.run([str(args.renderer.resolve()), str(paths[9]), native], check=True)
    bass, kick, keys = (np.zeros_like(source) for _ in range(3))
    origin, end = frame(31), frame(32)
    for bus, suffix in ((bass, '-bass.wav'), (kick, '-kick.wav')):
        clip = read_pcm(Path(native + suffix)).astype(float) / 32768
        count = end - origin
        bus[origin:end] = clip[:count]
        bus[:frame(31, 12)] = 0
        fade = np.linspace(1, 0, round(.020 * RATE))[:, None]
        bus[end - len(fade):end] *= fade
    onset = frame(31, 12)
    for index, midi in enumerate((53, 59, 62, 68)):
        start = onset + round(index * .0035 * RATE)
        sound = organ(midi, 3.55 * STEP, .34, start / RATE)
        keys[start:start + len(sound)] += sound
    dry = keys.copy()
    for seconds, level, channel in ((.023, .065, 0), (.033, .055, 1), (.047, .028, 0), (.061, .022, 1)):
        delay = round(seconds * RATE)
        keys[delay:, channel] += level * dry[:-delay, 1 - channel]
    keys *= 10 ** (-24.5 / 20) / np.max(np.abs(keys))
    turnaround = bass + kick + keys
    mix = source + turnaround
    first, last = frame(28), frame(36)
    focus = mix[first:last].copy()
    focus[:240] *= np.linspace(0, 1, 240)[:, None]
    focus[-2400:] *= np.linspace(1, 0, 2400)[:, None]
    for path, samples in zip(paths[:7], (mix, turnaround, bass, kick, keys, source, focus)):
        write_wav(path, samples)
    report = {
        'title': 'Last Tram — connected return', 'bpm': BPM,
        'frames': len(mix), 'duration_seconds': len(mix) / RATE,
        'source_sha256': sha(args.source), 'turnaround_start_frame': frame(31, 12),
        'return_frame': end, 'mix_peak_dbfs': float(20 * np.log10(np.max(np.abs(mix)))),
        'integrated_lufs': loudness(paths[0]), 'saved_without_playback': True,
        'output_sha256': {path.name: sha(path) for path in paths[:7]},
    }
    with paths[7].open('x') as out:
        out.write(json.dumps(report, indent=2) + '\n')
    with paths[8].open('x') as out:
        out.write('Last Tram v2 — bass and organ carry the return\n106 BPM / C minor\n\n')
        for bar, label in ((4, 'Broken kick groove'), (8, 'Strings theme'),
                           (16, 'Organ melody'), (24, 'Quiet middle'), (28, 'Rebuild'),
                           (31, 'Bass and held organ pickup'), (32, 'Connected return'),
                           (40, 'Higher answers'), (48, 'Outro'), (55, 'Last C-minor chord')):
            out.write(f'{frame(bar) / RATE:.2f}s  {label}\n')
    print(f"Saved {paths[0]} silently: peak {report['mix_peak_dbfs']:.2f} dBFS, "
          f"{report['integrated_lufs']:.1f} LUFS")


if __name__ == '__main__':
    main()
