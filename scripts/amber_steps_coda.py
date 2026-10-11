#!/usr/bin/env python3
"""Develop Amber Steps' earlier plucked idea into a closing piano phrase."""
import argparse
import json
from pathlib import Path

import numpy as np

from amber_steps import BPM, STEP, frame
from compare_hooks import loudness
from piano_phrasing import read_pcm
from piano_study import RATE, electric_piano, sha, write_wav


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('plucks', type=Path)
    parser.add_argument('strings', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    paths = [Path(str(args.output) + suffix) for suffix in
             ('.wav', '-melody.wav', '-plucks.wav', '-strings.wav', '-backing.wav',
              '-focus.wav', '.json', '-cues.txt')]
    for path in paths:
        if path.exists():
            raise FileExistsError(f'Output already exists: {path}')
    source, plucks, strings = [read_pcm(path).astype(float) / 32768
                               for path in (args.source, args.plucks, args.strings)]
    if len(source) < frame(64) or source.shape != plucks.shape or source.shape != strings.shape:
        raise ValueError('Expected the full Amber Steps mix and aligned stems')
    first, last = frame(48), frame(56)
    mask = np.zeros(len(source))
    mask[first:last] = 1
    ramp = round(.045 * RATE)
    mask[first:first + ramp] = np.linspace(0, 1, ramp)
    mask[last - ramp:last] = np.linspace(1, 0, ramp)
    new_strings = strings * (1 - mask[:, None])
    new_plucks = plucks * (1 - (1 - 10 ** (-2 / 20)) * mask[:, None])
    backing = source + (new_strings - strings) + (new_plucks - plucks)
    melody = np.zeros_like(source)
    # The opening E/G/B shape comes from the earlier plucked feature. It grows
    # upward over Cmaj9, then descends through the D9 chord into the G-major outro.
    phrases = {
        48: ((2, 64, 2.5, .61), (6, 67, 2.3, .56), (10, 71, 4.4, .65)),
        49: ((2, 74, 4.6, .67), (9.10, 71, 3.6, .53)),
        50: ((1.10, 67, 3.0, .56), (6, 69, 2.2, .51), (10, 71, 4.2, .60)),
        51: ((1.10, 76, 3.3, .67), (6, 74, 2.4, .58), (10, 71, 2.7, .55), (14, 67, 1.5, .49)),
        52: ((2, 66, 2.5, .60), (6, 69, 2.3, .55), (10, 72, 4.5, .64)),
        53: ((2, 71, 3.3, .59), (7.10, 69, 2.4, .53), (12, 66, 2.5, .51)),
        54: ((1.10, 64, 3.0, .53), (6, 66, 2.1, .54), (10, 69, 4.0, .59)),
        55: ((2, 66, 3.2, .54), (7.10, 64, 2.4, .49), (12, 62, 2.8, .47)),
        56: ((2, 67, 6.2, .48),),
        58: ((2, 64, 6.5, .39),),
    }
    events = []
    for bar, notes in phrases.items():
        for position, midi, gate, velocity in notes:
            start = frame(bar, position)
            sound = electric_piano(midi, gate * STEP, velocity, start / RATE)
            t = np.arange(len(sound)) / RATE
            sound *= (.5 - .5 * np.cos(np.pi * np.clip(t / .017, 0, 1)))[:, None]
            count = min(len(sound), len(melody) - start)
            melody[start:start + count] += sound[:count]
            events.append({'bar': bar + 1, 'step': position, 'midi': midi,
                           'gate': gate, 'velocity': velocity})
    dry = melody.copy()
    for seconds, level, channel in ((.021, .07, 0), (.031, .06, 1), (.049, .029, 0), (.063, .023, 1)):
        delay = round(seconds * RATE)
        melody[delay:, channel] += level * dry[:-delay, 1 - channel]
    melody *= 10 ** (-18.7 / 20) / np.max(np.abs(melody))
    mix = backing + melody
    focus = mix[frame(46):frame(60)].copy()
    focus[:240] *= np.linspace(0, 1, 240)[:, None]
    focus[-2400:] *= np.linspace(1, 0, 2400)[:, None]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    for path, samples in zip(paths[:6], (mix, melody, new_plucks, new_strings, backing, focus)):
        write_wav(path, samples)
    report = {
        'title': 'Amber Steps — closing piano phrase', 'bpm': BPM, 'key': 'G major',
        'frames': len(mix), 'duration_seconds': len(mix) / RATE,
        'source_sha256': sha(args.source), 'source_plucks_sha256': sha(args.plucks),
        'source_strings_sha256': sha(args.strings), 'melody_events': events,
        'strings_rest_bars_zero_based': [48, 56], 'plucks_change_db': -2,
        'mix_peak_dbfs': float(20 * np.log10(np.max(np.abs(mix)))),
        'integrated_lufs': loudness(paths[0]), 'saved_without_playback': True,
        'output_sha256': {path.name: sha(path) for path in paths[:6]},
    }
    with paths[6].open('x') as out:
        out.write(json.dumps(report, indent=2) + '\n')
    with paths[7].open('x') as out:
        out.write('Amber Steps v2 — closing piano phrase\n120 BPM / G major\n\n')
        for bar, label in ((0, 'First plucks'), (4, 'Main groove'), (16, 'Strings answer'),
                           (24, 'Plucked melodic feature'), (32, 'Piano middle'),
                           (36, 'Rebuild'), (40, 'Strings return'), (48, 'Piano develops the earlier idea'),
                           (56, 'Outro and last piano replies'), (63, 'Last G-major-seven chord')):
            out.write(f'{frame(bar) / RATE:.2f}s  {label}\n')
    print(f"Saved {paths[0]} silently: peak {report['mix_peak_dbfs']:.2f} dBFS, "
          f"{report['integrated_lufs']:.1f} LUFS")


if __name__ == '__main__':
    main()
