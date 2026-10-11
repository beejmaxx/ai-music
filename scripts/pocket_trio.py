#!/usr/bin/env python3
"""A short E-Dorian study using only kick, bass and a plucked instrument."""
import argparse
import json
import subprocess
from pathlib import Path

import numpy as np

from compare_hooks import loudness
from piano_phrasing import read_pcm
from piano_study import RATE, sha, write_wav
from plucked_voice import plucked

BPM, BARS = 116, 16
STEP = 60 / BPM / 4


def frame(bar, position=0):
    return round((bar * 16 + position) * STEP * RATE)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    parser.add_argument('--renderer', type=Path, default=Path('build/music-palette-render'))
    parser.add_argument('--extended', action='store_true', help='Develop the study into a 32-bar arrangement')
    args = parser.parse_args()
    bars = 32 if args.extended else BARS
    prefix = str(args.output)
    paths = [Path(prefix + suffix) for suffix in
             ('.wav', '-plucks.wav', '-bass.wav', '-kick.wav', '.json', '-cues.txt', '-score.txt')]
    native = prefix + '-native'
    for path in paths + [Path(native + suffix) for suffix in
                         ('.wav', '-bass.wav', '-kick.wav', '-percussion.wav', '-strings.wav')]:
        if path.exists():
            raise FileExistsError(f'Output already exists: {path}')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    score = ['# Pocket Trio — Em9 / A13, kick and bass only in the native renderer',
             f'tempo {BPM}', f'bars {bars}', 'tail 2', 'kick_tune 41.2034446', 'echo 0']
    native_events = []

    def bass(bar, position, midi, gate, velocity):
        position += .12 if position % 2 else 0
        score.append(f'bass {bar * 16 + position:.5f} {midi} {gate} {velocity}')
        native_events.append({'bar': bar + 1, 'step': position, 'midi': midi,
                              'gate': gate, 'velocity': velocity})

    for bar in range(bars):
        quiet = args.extended and 16 <= bar < 18
        build = args.extended and 18 <= bar < 20
        outro = args.extended and 28 <= bar < 31
        cutoff = 145 if quiet else 175 if build else 210 if bar < 8 else 235
        score.append(f'color {bar * 16} {cutoff} 700 0 0 0')
        root = 28 if bar % 4 < 2 or bar == bars - 1 else 33
        third = root + (3 if root == 28 else 4)
        for beat in range(4):
            if (quiet or (bar < 4 or build or outro) and beat % 2
                    or bar in (7, 11) and beat == 3 or bar == bars - 1 and beat != 0):
                continue
            score.append(f'kick {bar * 16 + beat * 4} {.90 if beat == 0 else .82 if beat == 2 else .76}')
        if bar == bars - 1:
            bass(bar, 0, 28, 4., .76)
        elif quiet:
            bass(bar, 0, root, 4.8, .64)
        elif build or outro:
            bass(bar, 2, root, 1.7, .70)
            bass(bar, 10, root + (7 if bar % 2 else 0), 1.2, .64)
        elif bar < 4:
            bass(bar, 2, root, 1.2, .79)
            bass(bar, 10, root + (7 if bar % 2 else 0), 1.0, .70)
        else:
            pattern = ((2, root, 1.0, .83), (5, third, .55, .57), (8, root, .85, .74),
                       (12, root + 7, .75, .62), (15, root + 9, .42, .49)) if bar % 2 == 0 else (
                (0, root, 1.1, .81), (6, root + 7, .7, .62), (9, third, .65, .57), (12, root, 1.1, .76))
            for position, midi, gate, velocity in pattern:
                bass(bar, position, midi, gate, velocity)
    with paths[6].open('x') as out:
        out.write('\n'.join(score) + '\n')
    subprocess.run([str(args.renderer.resolve()), str(paths[6]), native], check=True)
    bass_bus = read_pcm(Path(native + '-bass.wav')).astype(float) / 32768
    kick_bus = read_pcm(Path(native + '-kick.wav')).astype(float) / 32768
    keys = np.zeros_like(bass_bus)
    events = []

    def chord(bar, position, notes, gate, velocity, roll=.0055):
        for index, midi in enumerate(notes):
            start = frame(bar, position) + round(index * roll * RATE)
            sound = plucked(midi, gate * STEP, velocity, start / RATE, 'muted')
            count = min(len(sound), len(keys) - start)
            keys[start:start + count] += sound[:count]
        events.append({'bar': bar + 1, 'step': position, 'midi': list(notes),
                       'gate': gate, 'velocity': velocity})

    for bar in range(bars - 1):
        em = bar % 4 < 2
        voicing = (55, 59, 62, 66) if em else (55, 61, 66, 71)
        melodic = (8 <= bar < 16 or 24 <= bar < 28) if args.extended else 8 <= bar < 12
        if bar < 4:
            chord(bar, 3.12 if bar % 2 == 0 else 11.12, voicing, 1.8, .51)
        elif args.extended and 16 <= bar < 18:
            chord(bar, 4, voicing, 4., .35)
            chord(bar, 11.12, (71 if bar % 2 == 0 else 66,), 2.3, .42, 0)
        elif args.extended and 18 <= bar < 20:
            chord(bar, 3.12, voicing, 2., .44)
            chord(bar, 11.12, voicing, 1.5, .53)
        elif args.extended and bar >= 28:
            chord(bar, 3.12 if bar % 2 == 0 else 11.12, voicing, 2.2, .45 - (bar - 28) * .035)
        elif not melodic:
            for position, gate, velocity in ((3.12, 1.5, .65), (7.12, .6, .43), (11.12, 1.8, .70)):
                chord(bar, position, voicing, gate, velocity * (.88 if bar >= 12 else 1))
        else:
            # The same instrument moves from chords to a tiny two-bar answer.
            # Leave the first half-bar to the bass instead of adding another lead.
            chord(bar, 3.12, voicing, 1.2, .50)
            pitches = (71, 67, 66) if em else (71, 69, 66)
            if args.extended and 12 <= bar < 16:
                pitches = (76, 74, 71) if em else (73, 71, 69)
            elif args.extended and bar >= 24:
                pitches = (71, 69, 64) if em else (73, 71, 64)
            positions = (7.12, 10, 14) if bar % 2 == 0 else (6, 10, 13.12)
            for position, midi, gate, velocity in zip(positions, pitches,
                                                      (1.4, 1.5, 1.2), (.63, .54, .58)):
                chord(bar, position, (midi,), gate, velocity, 0)
    chord(bars - 1, 0, (52, 55, 59, 62, 66), 8., .44)
    dry = keys.copy()
    for seconds, level, channel in ((.014, .048, 0), (.023, .041, 1), (.037, .018, 0), (.049, .015, 1)):
        delay = round(seconds * RATE)
        keys[delay:, channel] += level * dry[:-delay, 1 - channel]
    keys *= 10 ** (-16.6 / 20) / np.max(np.abs(keys))
    mix = bass_bus + kick_bus + keys
    for path, samples in zip(paths[:4], (mix, keys, bass_bus, kick_bus)):
        write_wav(path, samples)
    report = {
        'title': 'Pocket Trio', 'bpm': BPM, 'key': 'E Dorian', 'bars': bars,
        'extended': args.extended,
        'instrument_count': 3, 'instruments': ['kick', 'bass', 'plucked instrument'],
        'frames': len(mix), 'duration_seconds': len(mix) / RATE,
        'progression': ['Em9', 'A13'], 'native_events': native_events, 'pluck_events': events,
        'mix_peak_dbfs': float(20 * np.log10(np.max(np.abs(mix)))),
        'integrated_lufs': loudness(paths[0]), 'saved_without_playback': True,
        'output_sha256': {path.name: sha(path) for path in paths[:4]},
    }
    with paths[4].open('x') as out:
        out.write(json.dumps(report, indent=2) + '\n')
    with paths[5].open('x') as out:
        out.write(f'Pocket Trio — three instruments, two chords\n116 BPM / E Dorian / {bars} bars\n\n')
        cues = ((0, 'Sparse statement'), (4, 'Full chord rhythm'), (8, 'The plucks become a melody'))
        cues += (((12, 'Higher melodic answer'), (16, 'Sparse middle'), (18, 'Rebuild'),
                  (20, 'Chord rhythm returns'), (24, 'Descending reply'), (28, 'Outro'),
                  (31, 'Last E-minor-nine chord')) if args.extended else
                 ((12, 'Chord rhythm returns'), (15, 'Last E-minor-nine chord')))
        for bar, label in cues:
            out.write(f'{frame(bar) / RATE:.2f}s  {label}\n')
    print(f"Saved {paths[0]} silently: {len(mix) / RATE:.2f}s, "
          f"{report['integrated_lufs']:.1f} LUFS, peak {report['mix_peak_dbfs']:.2f} dBFS")


if __name__ == '__main__':
    main()
