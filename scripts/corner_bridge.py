#!/usr/bin/env python3
"""Let the piano recall Corner Light's main tune through the quiet middle."""
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
    parser.add_argument('strings', type=Path)
    parser.add_argument('keys', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    paths = [Path(str(args.output) + suffix) for suffix in
             ('.wav', '-bridge-piano.wav', '-backing.wav', '-focus.wav', '.json', '-cues.txt')]
    for path in paths:
        if path.exists():
            raise FileExistsError(f'Output already exists: {path}')
    source, strings, keys = [read_pcm(path).astype(float) / 32768
                             for path in (args.source, args.strings, args.keys)]
    if not source.shape == strings.shape == keys.shape or len(source) < frame(64):
        raise ValueError('Expected matching complete Corner Light mix and stems')
    piano = np.zeros_like(source)
    events = []
    melody = (
        ((0, 62, 4), (6.1, 65, 3), (10, 69, 4.5)),
        ((4.1, 67, 2.5), (10, 65, 4)),
        ((0, 62, 4), (6.1, 65, 3), (10, 69, 4.5)),
        ((4.1, 72, 2.5), (10, 69, 4)),
        ((0, 62, 3.5), (6.1, 65, 2.5), (10, 70, 4)),
        ((4.1, 69, 2.5), (10, 65, 3.5)),
        ((0, 61, 3), (6.1, 64, 2.5), (10, 67, 3)),
        ((4.1, 64, 2.2), (10, 61, 1.8)),
    )
    for local_bar, notes in enumerate(melody):
        for index, (position, midi, gate) in enumerate(notes):
            onset = frame(32 + local_bar, position)
            velocity = (.48 + local_bar * .014) * (1.04 if index == len(notes) - 1 else .93)
            sound = electric_piano(midi, gate * STEP, velocity, onset / RATE)
            t = np.arange(len(sound)) / RATE
            sound *= (.5 - .5 * np.cos(np.pi * np.clip(t / .022, 0, 1)))[:, None]
            piano[onset:onset + len(sound)] += sound
            events.append({'bar': 33 + local_bar, 'step': position, 'midi': midi,
                           'seconds': onset / RATE, 'gate_seconds': gate * STEP,
                           'velocity': velocity})
    dry = piano.copy()
    for seconds, level, channel in ((.019, .065, 0), (.027, .060, 1),
                                   (.043, .035, 0), (.051, .030, 1)):
        delay = round(seconds * RATE)
        piano[delay:, channel] += level * dry[:-delay, 1 - channel]
    piano *= 10 ** (-20.7 / 20) / np.max(np.abs(piano))
    first, last = frame(32), frame(40)
    envelope = np.zeros(len(source))
    envelope[first:last] = 1
    edge = round(.040 * RATE)
    envelope[first:first + edge] = np.linspace(0, 1, edge)
    envelope[last - edge:last] = np.linspace(1, 0, edge)
    backing = source - envelope[:, None] * (strings + .12 * keys)
    pause = frame(40, -2)
    fade = np.linspace(1, 0, round(.012 * RATE))[:, None]
    piano[pause - len(fade):pause] *= fade
    piano[pause:last] = 0
    mix = backing + piano
    focus_first, focus_last = frame(30), frame(44)
    focus = mix[focus_first:focus_last].copy()
    focus[:240] *= np.linspace(0, 1, 240)[:, None]
    focus[-2400:] *= np.linspace(1, 0, 2400)[:, None]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    for path, samples in zip(paths[:4], (mix, piano, backing, focus)):
        write_wav(path, samples)
    report = {
        'title': 'Corner Light — piano middle', 'bpm': BPM, 'key': 'D minor',
        'frames': len(mix), 'duration_seconds': len(mix) / RATE,
        'source_sha256': sha(args.source), 'strings_sha256': sha(args.strings),
        'keys_sha256': sha(args.keys), 'events': events,
        'focus_start_frame': focus_first, 'focus_seconds': len(focus) / RATE,
        'mix_peak_dbfs': float(20 * np.log10(np.max(np.abs(mix)))),
        'integrated_lufs': loudness(paths[0]),
        'output_sha256': {path.name: sha(path) for path in paths[:4]},
    }
    with paths[4].open('x') as out:
        out.write(json.dumps(report, indent=2) + '\n')
    with paths[5].open('x') as out:
        out.write('Corner Light v3 — the piano remembers the main tune\n112 BPM / D minor\n\n')
        for bar, label in ((24, 'Piano feature'), (32, 'Piano takes the main melody'),
                           (36, 'Piano phrase gathers momentum'), (40, 'Strings return'),
                           (56, 'Quiet piano reprise'), (63, 'Final D minor chord')):
            out.write(f'{frame(bar) / RATE:.2f}s  {label}\n')
    print(f"Wrote {paths[0]}: peak {report['mix_peak_dbfs']:.2f} dBFS; "
          f"{report['integrated_lufs']:.1f} LUFS")


if __name__ == '__main__':
    main()
