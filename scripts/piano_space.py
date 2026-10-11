#!/usr/bin/env python3
"""Give Side Street's piano an early hint and a clearer featured passage."""
import argparse
import json
from pathlib import Path

import numpy as np

from compare_hooks import loudness
from piano_phrasing import read_pcm
from piano_study import BPM, RATE, STEP, sha, write_wav


def frame(bar, step=0):
    return round((bar * 16 + step) * STEP * RATE)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path, help='The 80-bar Side Street long cut')
    parser.add_argument('strings', type=Path, help='Original 64-bar strings stem')
    parser.add_argument('piano', type=Path, help='Original full piano stem')
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    paths = [Path(str(args.output) + suffix) for suffix in
             ('.wav', '-intro-piano.wav', '-backing.wav', '-focus.wav', '.json', '-cues.txt')]
    for path in paths:
        if path.exists():
            raise FileExistsError(f'Output already exists: {path}')
    source, strings, piano = [read_pcm(path).astype(float) / 32768
                              for path in (args.source, args.strings, args.piano)]
    if len(source) < frame(80) or min(len(strings), len(piano)) < frame(64):
        raise ValueError('Expected the long cut and original full-length instrument stems')
    backing = source.copy()
    # Smoothly withdraw the strings while the existing C/Eb/F piano phrase is
    # featured. It is the same performance, with space around it.
    first, last = frame(24), frame(28)
    amount = np.ones(last - first)
    fade_out = frame(1)
    fade_in = frame(0, 2)
    amount[:fade_out] = np.linspace(0, 1, fade_out)
    amount[-fade_in:] = np.linspace(1, 0, fade_in)
    backing[first:last] -= strings[first:last] * amount[:, None]

    intro = np.zeros_like(source)
    start, end = frame(6), frame(8)
    excerpt = piano[frame(16):frame(16) + end - start].copy()
    excerpt *= 10 ** (-6.5 / 20)
    excerpt[:240] *= np.linspace(0, 1, 240)[:, None]
    excerpt[-2400:] *= np.linspace(1, 0, 2400)[:, None]
    intro[start:end] = excerpt
    mix = backing + intro
    parts, cues, elapsed = [], [], 0.
    for a, b, label in ((4, 10, 'A quiet hint before the strings enter'),
                        (22, 30, 'Piano has the featured passage')):
        clip = mix[frame(a):frame(b)].copy()
        clip[:240] *= np.linspace(0, 1, 240)[:, None]
        clip[-2400:] *= np.linspace(1, 0, 2400)[:, None]
        parts.extend((clip, np.zeros((round(.65 * RATE), 2))))
        cues.append({'seconds': elapsed, 'label': label})
        elapsed += len(clip) / RATE + .65
    focus = np.concatenate(parts[:-1])
    args.output.parent.mkdir(parents=True, exist_ok=True)
    for path, samples in zip(paths[:4], (mix, intro, backing, focus)):
        write_wav(path, samples)
    report = {
        'title': 'Side Street — piano space', 'bpm': BPM, 'bars': 80,
        'frames': len(mix), 'duration_seconds': len(mix) / RATE,
        'source_sha256': sha(args.source), 'strings_sha256': sha(args.strings),
        'piano_sha256': sha(args.piano), 'intro_piano_gain_db': -6.5,
        'focus_seconds': len(focus) / RATE, 'focus_cues': cues,
        'mix_peak_dbfs': float(20 * np.log10(np.max(np.abs(mix)))),
        'integrated_lufs': loudness(paths[0]),
        'output_sha256': {path.name: sha(path) for path in paths[:4]},
    }
    with paths[4].open('x') as out:
        out.write(json.dumps(report, indent=2) + '\n')
    with paths[5].open('x') as out:
        out.write('Side Street v9 — more space for the piano\n116 BPM / F minor / 80 bars\n\n')
        for bar, label in ((6, 'Quiet piano hint'), (8, 'Strings arrive'),
                           (24, 'Piano takes the foreground'), (28, 'Strings return'),
                           (32, 'Second theme'), (48, 'Quiet middle'),
                           (56, 'Full return'), (64, 'Final piano answer'),
                           (79, 'Final F minor chord')):
            out.write(f'{frame(bar) / RATE:.2f}s  {label}\n')
    print(f"Wrote {paths[0]}: peak {report['mix_peak_dbfs']:.2f} dBFS; "
          f"{report['integrated_lufs']:.1f} LUFS; focus {len(focus) / RATE:.2f}s")


if __name__ == '__main__':
    main()
