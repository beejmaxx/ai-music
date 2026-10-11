#!/usr/bin/env python3
"""Give Corner Light a four-bar piano feature and a quiet melodic reprise."""
import argparse
import json
from pathlib import Path

import numpy as np

from compare_hooks import loudness
from corner_keys import BPM, STEP, frame
from piano_phrasing import read_pcm
from piano_study import RATE, electric_piano, sha, write_wav


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('keys', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    suffixes = ('.wav', '-melody.wav', '-backing.wav', '-focus.wav', '.json', '-cues.txt')
    paths = [Path(str(args.output) + suffix) for suffix in suffixes]
    for path in paths:
        if path.exists():
            raise FileExistsError(f'Output already exists: {path}')
    source = read_pcm(args.source).astype(float) / 32768
    keys = read_pcm(args.keys).astype(float) / 32768
    if source.shape != keys.shape or len(source) < frame(64):
        raise ValueError('Expected matching full-length Corner Light mix and keys')
    melody = np.zeros_like(source)
    events = []

    def note(bar, position, pitch, gate, velocity, level=1.):
        start = frame(bar, position)
        sound = electric_piano(pitch, gate * STEP, velocity, start / RATE)
        t = np.arange(len(sound)) / RATE
        sound *= (.5 - .5 * np.cos(np.pi * np.clip(t / .012, 0, 1)))[:, None]
        count = min(len(sound), len(melody) - start)
        melody[start:start + count] += sound[:count] * level
        events.append({'bar': bar + 1, 'step': position, 'seconds': start / RATE,
                       'midi': pitch, 'gate_seconds': gate * STEP,
                       'velocity': velocity, 'level': level})

    # A descending D-minor question; the B-flat harmony lifts its answer.
    phrase = (
        (24, 2, 69, 1.8, .63), (24, 5.14, 65, .9, .48),
        (24, 7.14, 64, .55, .36), (24, 10, 62, 3., .65),
        (25, 4.14, 65, 1.6, .55), (25, 8, 64, 1.3, .45),
        (25, 12.14, 62, 2., .55),
        (26, 2, 65, 1.8, .61), (26, 5.14, 69, 1.1, .58),
        (26, 8, 72, 1., .55), (26, 10, 74, 2.5, .71),
        (27, 4.14, 72, 1.4, .57), (27, 8, 69, 1.7, .54),
        (27, 12.14, 65, 2., .63),
    )
    for event in phrase:
        note(*event)
    # The outro recalls the contour in the first half of each bar, leaving
    # the late string notes room to answer instead of doubling them.
    reprise = (
        (56, 0, 69, 1.6, .52), (56, 3.14, 65, .8, .43),
        (56, 5.14, 62, 1.2, .49), (57, 0, 65, 1.2, .44),
        (57, 4, 62, 2., .50),
        (58, 0, 65, 1.6, .50), (58, 3.14, 69, .8, .43),
        (58, 5.14, 72, 1.2, .48), (59, 0, 69, 1.2, .43),
        (59, 4, 65, 2., .49),
    )
    for event in reprise:
        note(*event, level=.68)
    dry = melody.copy()
    for seconds, level, channel in ((.019, .065, 0), (.027, .060, 1),
                                   (.043, .035, 0), (.051, .030, 1)):
        delay = round(seconds * RATE)
        melody[delay:, channel] += level * dry[:-delay, 1 - channel]
    gain = 10 ** (-18.5 / 20) / np.max(np.abs(melody))
    melody *= gain

    # Make a little space in the chord part only while the piano is featured.
    # This is a local balance change; the previous complete recording is kept.
    envelope = np.zeros(len(source))
    first, last = frame(24), frame(28)
    envelope[first:last] = 1
    ramp = round(.045 * RATE)
    envelope[first:first + ramp] = np.linspace(0, 1, ramp)
    envelope[last - ramp:last] = np.linspace(1, 0, ramp)
    backing = source - keys * (.16 * envelope[:, None])
    mix = backing + melody
    focus_first, focus_last = frame(22), frame(30)
    focus = mix[focus_first:focus_last].copy()
    focus[:240] *= np.linspace(0, 1, 240)[:, None]
    focus[-2400:] *= np.linspace(1, 0, 2400)[:, None]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    for path, samples in zip(paths[:4], (mix, melody, backing, focus)):
        write_wav(path, samples)
    report = {
        'title': 'Corner Light — piano feature', 'bpm': BPM, 'key': 'D minor',
        'frames': len(mix), 'duration_seconds': len(mix) / RATE,
        'source_sha256': sha(args.source), 'keys_sha256': sha(args.keys),
        'melody_gain': float(gain), 'events': events,
        'focus_start_frame': focus_first, 'focus_seconds': len(focus) / RATE,
        'mix_peak_dbfs': float(20 * np.log10(np.max(np.abs(mix)))),
        'integrated_lufs': loudness(paths[0]),
        'output_sha256': {path.name: sha(path) for path in paths[:4]},
    }
    with paths[4].open('x') as out:
        out.write(json.dumps(report, indent=2) + '\n')
    with paths[5].open('x') as out:
        out.write('Corner Light v2 — piano feature\n112 BPM / D minor\n\n')
        for bar, label in ((24, 'Piano question'), (26, 'Higher piano answer'),
                           (28, 'Strings return'), (56, 'Quiet piano reprise'),
                           (63, 'Final D minor chord')):
            out.write(f'{frame(bar) / RATE:.2f}s  {label}\n')
    print(f"Wrote {paths[0]}: {len(mix) / RATE:.2f}s; "
          f"peak {report['mix_peak_dbfs']:.2f} dBFS; {report['integrated_lufs']:.1f} LUFS")


if __name__ == '__main__':
    main()
