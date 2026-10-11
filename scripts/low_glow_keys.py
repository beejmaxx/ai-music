#!/usr/bin/env python3
"""Write Low Glow's syncopated electric-piano riff over its native rhythm parts."""
import argparse
import json
from pathlib import Path

import numpy as np

from compare_hooks import loudness
from piano_phrasing import read_pcm
from piano_study import RATE, electric_piano, sha, write_wav

BPM = 122
STEP = 60 / BPM / 4
VOICINGS = (
    ((55, 60, 64), (60, 64, 67)),
    ((54, 60, 64), (60, 64, 69)),
    ((57, 60, 64), (60, 64, 67)),
    ((56, 62, 64), (59, 62, 68)),
)


def frame(bar, step=0):
    return round((bar * 16 + step) * STEP * RATE)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    paths = [Path(str(args.output) + suffix) for suffix in
             ('.wav', '-keys.wav', '-backing.wav', '-focus.wav', '.json', '-cues.txt')]
    for path in paths:
        if path.exists():
            raise FileExistsError(f'Output already exists: {path}')
    backing = read_pcm(args.source).astype(float) / 32768
    if len(backing) < frame(56):
        raise ValueError('Expected the complete 56-bar Low Glow rhythm track')
    keys = np.zeros_like(backing)
    events = []

    def chord(bar, position, gate, velocity, high=False, pitches=None, soft=.006):
        notes = VOICINGS[(bar % 8) // 2][int(high)] if pitches is None else pitches
        onset = frame(bar, position)
        for index, midi in enumerate(notes):
            start = onset + round(index * .0025 * RATE)
            sound = electric_piano(midi, gate * STEP, velocity, onset / RATE)
            t = np.arange(len(sound)) / RATE
            sound *= (.5 - .5 * np.cos(np.pi * np.clip(t / soft, 0, 1)))[:, None]
            count = min(len(sound), len(keys) - start)
            keys[start:start + count] += sound[:count]
        events.append({'bar': bar + 1, 'step': position, 'seconds': onset / RATE,
                       'midi': list(notes), 'gate_seconds': gate * STEP, 'velocity': velocity})

    for bar in range(4):
        chord(bar, 0 if bar % 2 == 0 else 6.15, 5 if bar % 2 == 0 else 2,
              .32 + bar * .015, high=bool(bar % 2), soft=.018)
    for bar in range(4, 55):
        if 24 <= bar < 28:
            if bar % 2 == 0:
                notes = (55, 60, 64, 71) if bar == 24 else (54, 60, 64, 69)
                chord(bar, 0, 23, .31, pitches=notes, soft=.045)
            else:
                chord(bar, 12.15, 1.5, .22, pitches=(64,), soft=.018)
            continue
        if 28 <= bar < 32:
            positions = (0, 14) if bar == 28 else (2, 8) if bar == 29 else (2, 6.15, 10)
            for index, position in enumerate(positions):
                chord(bar, position, 2.8 if bar == 28 and index == 0 else .95,
                      .36 + (bar - 28) * .055, high=index % 2 == 1, soft=.012)
            continue
        if bar >= 48:
            for position in ((2, 8) if bar % 2 == 0 else (2, 6.15)):
                chord(bar, position, 1.4, .48 - (bar - 48) * .026,
                      high=position > 6, soft=.014)
            continue
        reply = (12 <= bar < 20 or 32 <= bar < 48) and bar % 2 == 1
        if bar % 2 == 0:
            pattern = ((2, .95, .65, False), (5.15, .65, .44, False),
                       (8, 1.10, .74, True), (11.15, .55, .40, False),
                       (14, .8, .58, True))
        else:
            pattern = ((2, 1.05, .64, False), (6.15, .8, .56, True),
                       (8, .7, .49, False))
            if not reply:
                pattern += ((14.15, .85, .57, True),)
        for position, gate, velocity, high in pattern:
            chord(bar, position, gate, velocity, high=high)
    chord(55, 0, 19, .30, pitches=(57, 60, 64, 67), soft=.022)
    dry = keys.copy()
    for seconds, level, channel in ((.015, .048, 0), (.023, .045, 1),
                                   (.039, .022, 0), (.047, .019, 1)):
        delay = round(seconds * RATE)
        keys[delay:, channel] += level * dry[:-delay, 1 - channel]
    gain = 10 ** (-17.5 / 20) / np.max(np.abs(keys))
    keys *= gain
    pause, return_at = frame(32, -2), frame(32)
    fade = np.linspace(1, 0, round(.012 * RATE))[:, None]
    keys[pause - len(fade):pause] *= fade
    keys[pause:return_at] = 0
    mix = backing + keys
    first, last = frame(8), frame(20)
    focus = mix[first:last].copy()
    focus[:240] *= np.linspace(0, 1, 240)[:, None]
    focus[-2400:] *= np.linspace(1, 0, 2400)[:, None]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    for path, samples in zip(paths[:4], (mix, keys, backing, focus)):
        write_wav(path, samples)
    report = {
        'title': 'Low Glow', 'bpm': BPM, 'key': 'A minor', 'bars': 56,
        'progression': ['Am7', 'D9', 'Fmaj7', 'E7'],
        'frames': len(mix), 'duration_seconds': len(mix) / RATE,
        'source_sha256': sha(args.source), 'keys_gain': float(gain), 'events': events,
        'focus_start_frame': first, 'focus_seconds': len(focus) / RATE,
        'mix_peak_dbfs': float(20 * np.log10(np.max(np.abs(mix)))),
        'integrated_lufs': loudness(paths[0]),
        'output_sha256': {path.name: sha(path) for path in paths[:4]},
    }
    with paths[4].open('x') as out:
        out.write(json.dumps(report, indent=2) + '\n')
    with paths[5].open('x') as out:
        out.write('Low Glow\n122 BPM / A minor / Am7-D9-Fmaj7-E7\n\n')
        for bar, label in ((0, 'Opening'), (4, 'Keyboard groove'), (12, 'String replies'),
                           (20, 'Keys and bass'), (24, 'Quiet middle'), (28, 'Rebuild'),
                           (32, 'Full return'), (40, 'Higher replies'), (48, 'Outro'),
                           (55, 'Last A minor chord')):
            out.write(f'{frame(bar) / RATE:.2f}s  {label}\n')
    print(f"Wrote {paths[0]}: {len(mix) / RATE:.2f}s; "
          f"peak {report['mix_peak_dbfs']:.2f} dBFS; {report['integrated_lufs']:.1f} LUFS")


if __name__ == '__main__':
    main()
