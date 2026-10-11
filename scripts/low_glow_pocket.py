#!/usr/bin/env python3
"""Give Low Glow a bass spotlight and small hand-drum transition pickups."""
import argparse
import json
from pathlib import Path

import numpy as np

from compare_hooks import loudness
from low_glow_keys import BPM, frame
from percussion_finish import hand_drum
from piano_phrasing import read_pcm
from piano_study import RATE, sha, write_wav


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('keys', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    paths = [Path(str(args.output) + suffix) for suffix in
             ('.wav', '-keys.wav', '-pickups.wav', '-rest.wav', '-focus.wav', '.json', '-cues.txt')]
    for path in paths:
        if path.exists():
            raise FileExistsError(f'Output already exists: {path}')
    source, old_keys = [read_pcm(p).astype(float) / 32768 for p in (args.source, args.keys)]
    if source.shape != old_keys.shape or len(source) < frame(56):
        raise ValueError('Expected matching complete Low Glow mix and keys')
    rest = source - old_keys
    keys = old_keys.copy()
    first, last = frame(20), frame(22)
    withdraw = np.ones(last - first)
    ramp = round(.035 * RATE)
    withdraw[:ramp] = np.linspace(0, 1, ramp)
    withdraw[-ramp:] = np.linspace(1, 0, ramp)
    keys[first:last] *= 1 - withdraw[:, None]
    pickups = np.zeros_like(source)
    events = []
    for bar, hits in (
        (11, ((14, 57, .56), (15.15, 60, .65))),
        (23, ((12, 59, .65), (14, 56, .58), (15.15, 52, .74))),
        (31, ((10, 56, .55), (11.33, 53, .62), (12.67, 52, .73))),
        (47, ((12, 59, .62), (14, 56, .53), (15.15, 52, .68))),
    ):
        for index, (position, midi, velocity) in enumerate(hits):
            start = frame(bar, position)
            sound = hand_drum(midi, velocity, -.12 if index % 2 == 0 else .12, 7200 + len(events))
            pickups[start:start + len(sound)] += sound
            events.append({'bar': bar + 1, 'step': position, 'midi': midi, 'velocity': velocity})
    dry = pickups.copy()
    for seconds, level, channel in ((.013, .045, 0), (.019, .040, 1)):
        delay = round(seconds * RATE)
        pickups[delay:, channel] += level * dry[:-delay, 1 - channel]
    pause, return_at = frame(32, -2), frame(32)
    fade = np.linspace(1, 0, round(.012 * RATE))[:, None]
    pickups[pause - len(fade):pause] *= fade
    pickups[pause:return_at] = 0
    mix = rest + keys + pickups
    focus = mix[frame(18):frame(26)].copy()
    focus[:240] *= np.linspace(0, 1, 240)[:, None]
    focus[-2400:] *= np.linspace(1, 0, 2400)[:, None]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    for path, samples in zip(paths[:5], (mix, keys, pickups, rest, focus)):
        write_wav(path, samples)
    report = {
        'title': 'Low Glow — bass spotlight and transitions', 'bpm': BPM,
        'frames': len(mix), 'duration_seconds': len(mix) / RATE,
        'source_sha256': sha(args.source), 'keys_sha256': sha(args.keys),
        'bass_spotlight_bars': [21, 22], 'pickup_events': events,
        'mix_peak_dbfs': float(20 * np.log10(np.max(np.abs(mix)))),
        'integrated_lufs': loudness(paths[0]),
        'output_sha256': {p.name: sha(p) for p in paths[:5]},
    }
    with paths[5].open('x') as out:
        out.write(json.dumps(report, indent=2) + '\n')
    with paths[6].open('x') as out:
        out.write('Low Glow v3 — bass spotlight and transition pickups\n122 BPM / A minor\n\n')
        for bar, label in ((4, 'Keyboard groove'), (12, 'String replies'),
                           (20, 'Bass spotlight'), (22, 'Keys answer'),
                           (24, 'Piano melody'), (32, 'Full return'),
                           (40, 'Higher replies'), (48, 'Quiet recollection'),
                           (55, 'Final A minor chord')):
            out.write(f'{frame(bar) / RATE:.2f}s  {label}\n')
    print(f"Wrote {paths[0]}: peak {report['mix_peak_dbfs']:.2f} dBFS; "
          f"{report['integrated_lufs']:.1f} LUFS; saved without playback")


if __name__ == '__main__':
    main()
