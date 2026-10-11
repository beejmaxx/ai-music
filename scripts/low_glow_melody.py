#!/usr/bin/env python3
"""Develop Low Glow's chord-top notes into a small, singing piano phrase."""
import argparse
import json
from pathlib import Path

import numpy as np

from compare_hooks import loudness
from low_glow_keys import BPM, STEP, frame
from piano_phrasing import read_pcm
from piano_study import RATE, electric_piano, sha, write_wav


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    paths = [Path(str(args.output) + suffix) for suffix in
             ('.wav', '-melody.wav', '-backing.wav', '-focus.wav', '.json', '-cues.txt')]
    for path in paths:
        if path.exists():
            raise FileExistsError(f'Output already exists: {path}')
    backing = read_pcm(args.source).astype(float) / 32768
    if len(backing) < frame(56):
        raise ValueError('Expected the complete Low Glow recording')
    melody = np.zeros_like(backing)
    events = []

    def note(bar, position, midi, gate, velocity, level=1.):
        start = frame(bar, position)
        sound = electric_piano(midi, gate * STEP, velocity, start / RATE)
        t = np.arange(len(sound)) / RATE
        sound *= (.5 - .5 * np.cos(np.pi * np.clip(t / .018, 0, 1)))[:, None]
        count = min(len(sound), len(melody) - start)
        melody[start:start + count] += sound[:count] * level
        events.append({'bar': bar + 1, 'step': position, 'seconds': start / RATE,
                       'midi': midi, 'gate_seconds': gate * STEP,
                       'velocity': velocity, 'level': level})

    for bar, pitches in ((24, (76, 79, 76, 72)), (26, (76, 78, 76, 72)),
                         (28, (76, 79, 76, 72)), (30, (74, 77, 76, 71))):
        note(bar, 2, pitches[0], 3.0, .47)
        note(bar, 8, pitches[1], 3.8, .54)
        note(bar + 1, 4.15, pitches[2], 3.5, .48)
        note(bar + 1, 10, pitches[3], 1.2 if bar == 30 else 2.5, .44)
    # One quiet recollection of the first phrase, with room for the strings'
    # late answer. The motif then leaves instead of repeating through the outro.
    for bar, position, midi, gate, velocity in (
        (48, 2, 76, 2.5, .43), (48, 8, 79, 3.1, .48),
        (49, 0, 76, 2.5, .42), (49, 4, 72, 2.0, .38),
    ):
        note(bar, position, midi, gate, velocity, level=.62)
    dry = melody.copy()
    for seconds, level, channel in ((.019, .060, 0), (.027, .055, 1),
                                   (.043, .030, 0), (.051, .025, 1)):
        delay = round(seconds * RATE)
        melody[delay:, channel] += level * dry[:-delay, 1 - channel]
    gain = 10 ** (-22 / 20) / np.max(np.abs(melody))
    melody *= gain
    pause, return_at = frame(32, -2), frame(32)
    fade = np.linspace(1, 0, round(.012 * RATE))[:, None]
    melody[pause - len(fade):pause] *= fade
    melody[pause:return_at] = 0
    mix = backing + melody
    first, last = frame(22), frame(36)
    focus = mix[first:last].copy()
    focus[:240] *= np.linspace(0, 1, 240)[:, None]
    focus[-2400:] *= np.linspace(1, 0, 2400)[:, None]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    for path, samples in zip(paths[:4], (mix, melody, backing, focus)):
        write_wav(path, samples)
    report = {
        'title': 'Low Glow — a melody inside the groove', 'bpm': BPM,
        'key': 'A minor', 'bars': 56, 'frames': len(mix),
        'duration_seconds': len(mix) / RATE, 'source_sha256': sha(args.source),
        'melody_gain': float(gain), 'events': events,
        'focus_start_frame': first, 'focus_seconds': len(focus) / RATE,
        'mix_peak_dbfs': float(20 * np.log10(np.max(np.abs(mix)))),
        'integrated_lufs': loudness(paths[0]),
        'output_sha256': {path.name: sha(path) for path in paths[:4]},
    }
    with paths[4].open('x') as out:
        out.write(json.dumps(report, indent=2) + '\n')
    with paths[5].open('x') as out:
        out.write('Low Glow v2 — a melody inside the groove\n122 BPM / A minor\n\n')
        for bar, label in ((4, 'Keyboard groove'), (12, 'String replies'),
                           (24, 'Piano melody grows from the chord riff'),
                           (28, 'Rebuild'), (32, 'Full return'),
                           (40, 'Higher string replies'), (48, 'Quiet piano recollection'),
                           (55, 'Last A minor chord')):
            out.write(f'{frame(bar) / RATE:.2f}s  {label}\n')
    print(f"Wrote {paths[0]}: peak {report['mix_peak_dbfs']:.2f} dBFS; "
          f"{report['integrated_lufs']:.1f} LUFS; focus {len(focus) / RATE:.2f}s")


if __name__ == '__main__':
    main()
