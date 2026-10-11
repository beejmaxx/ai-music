#!/usr/bin/env python3
"""Give Last Tram's organ feature a quiet hand-drum response and selected fills."""
import argparse
import json
from pathlib import Path

import numpy as np

from compare_hooks import loudness
from last_tram import BPM, frame
from percussion_finish import hand_drum, rim
from piano_phrasing import read_pcm
from piano_study import RATE, sha, write_wav


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path, help='Last Tram v3, the 64-bar arrangement')
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    paths = [Path(str(args.output) + suffix) for suffix in
             ('.wav', '-hand-drums.wav', '-backing.wav', '-focus.wav', '.json', '-cues.txt')]
    for path in paths:
        if path.exists():
            raise FileExistsError(f'Output already exists: {path}')
    source = read_pcm(args.source).astype(float) / 32768
    if len(source) < frame(64):
        raise ValueError('Expected the complete 64-bar Last Tram arrangement')
    percussion = np.zeros_like(source)
    events = []

    def hit(bar, position, midi, velocity, pan, voice='hand drum'):
        start = frame(bar, position)
        seed = 18010 + len(events)
        sound = hand_drum(midi, velocity, pan, seed) if voice == 'hand drum' else rim(velocity, pan, seed)
        percussion[start:start + len(sound)] += sound
        events.append({'bar': bar + 1, 'step': position, 'voice': voice,
                       'midi': midi if voice == 'hand drum' else None,
                       'velocity': velocity, 'pan': pan, 'seed': seed})

    # Leave small replies between the organ notes, rather than running a second
    # constant percussion loop underneath the whole composition.
    for bar in range(16, 24):
        if bar % 2 == 0:
            hit(bar, 3.13, 55, .35, -.10)
            hit(bar, 14.13, 60, .43, .10)
        else:
            hit(bar, 4.06, 60, .31, .10)
            hit(bar, 10.13, 55, .38, -.10)
    for bar, phrase in (
        (7, ((13.13, 62, .49), (15.13, 60, .56))),
        (15, ((12, 59, .56), (14, 55, .47), (15.13, 60, .61))),
        (23, ((13.13, 58, .46), (15.13, 63, .57))),
        (31, ((10, 62, .51), (12, 59, .46), (13.13, 55, .56))),
        (47, ((13.13, 62, .46), (15.13, 60, .59))),
        (55, ((12, 62, .49), (14, 59, .43), (15.13, 55, .55))),
    ):
        for index, (position, midi, velocity) in enumerate(phrase):
            hit(bar, position, midi, velocity, -.12 if index % 2 == 0 else .12)
    for bar in (41, 45, 49, 53):
        hit(bar, 9.13, 0, .55, -.08, 'rim')
    dry = percussion.copy()
    for seconds, level, channel in ((.013, .055, 0), (.019, .050, 1), (.031, .023, 0), (.043, .020, 1)):
        delay = round(seconds * RATE)
        percussion[delay:, channel] += level * dry[:-delay, 1 - channel]
    mix = source + percussion
    focus = mix[frame(14):frame(25)].copy()
    focus[:240] *= np.linspace(0, 1, 240)[:, None]
    focus[-2400:] *= np.linspace(1, 0, 2400)[:, None]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    for path, samples in zip(paths[:4], (mix, percussion, source, focus)):
        write_wav(path, samples)
    report = {
        'title': 'Last Tram — hand-drum replies', 'bpm': BPM, 'key': 'C minor',
        'bars': 64, 'frames': len(mix), 'duration_seconds': len(mix) / RATE,
        'source_sha256': sha(args.source), 'events': events,
        'mix_peak_dbfs': float(20 * np.log10(np.max(np.abs(mix)))),
        'integrated_lufs': loudness(paths[0]), 'saved_without_playback': True,
        'output_sha256': {path.name: sha(path) for path in paths[:4]},
    }
    with paths[4].open('x') as out:
        out.write(json.dumps(report, indent=2) + '\n')
    source_cues = args.source.with_name(args.source.stem + '-cues.txt')
    with paths[5].open('x') as out:
        out.write(source_cues.read_text().replace('Last Tram v3 — relative-major middle',
                                                 'Last Tram v4 — hand-drum replies'))
        out.write('\nQuiet hand-drum replies during the organ feature; selected transition fills.\n')
    print(f"Saved {paths[0]} silently: {len(mix) / RATE:.2f}s, "
          f"{report['integrated_lufs']:.1f} LUFS, peak {report['mix_peak_dbfs']:.2f} dBFS")


if __name__ == '__main__':
    main()
