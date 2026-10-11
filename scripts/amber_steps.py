#!/usr/bin/env python3
"""Compose Amber Steps: a G-major groove for bass, plucked chords and piano."""
import argparse
import json
import subprocess
from pathlib import Path

import numpy as np

from compare_hooks import loudness
from piano_phrasing import read_pcm
from piano_study import RATE, electric_piano, sha, write_wav
from plucked_voice import plucked

BPM, BARS = 120, 64
STEP = 60 / BPM / 4
ROOTS = (31, 40, 36, 38)
LOW = ((59, 62, 66, 69), (55, 59, 62, 66), (52, 59, 62, 67), (54, 60, 64, 69))
HIGH = ((62, 66, 71), (62, 67, 71), (62, 67, 71), (60, 64, 69))
MELODY_ROOTS = (67, 64, 60, 62)


def frame(bar, position=0):
    return round((bar * 16 + position) * STEP * RATE)


def harmony(bar):
    if bar == 63:
        return 0
    if 32 <= bar < 40:
        return (bar - 32) // 2
    if bar >= 56:
        return min(3, (bar - 56) // 2)
    return ((bar - 40 if bar >= 40 else bar) // 4) % 4


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    parser.add_argument('--renderer', type=Path, default=Path('build/music-palette-render'))
    args = parser.parse_args()
    prefix = str(args.output)
    paths = [Path(prefix + suffix) for suffix in
             ('.wav', '-plucks.wav', '-piano.wav', '-backing.wav', '-focus.wav', '.json', '-cues.txt', '-score.txt')]
    native = Path(prefix + '-native')
    for path in paths + [Path(str(native) + suffix) for suffix in
                         ('.wav', '-bass.wav', '-kick.wav', '-percussion.wav', '-strings.wav')]:
        if path.exists():
            raise FileExistsError(f'Output already exists: {path}')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    score = ['# Amber Steps — G major / Em / C / D', f'tempo {BPM}', f'bars {BARS}',
             'tail 3', 'kick_tune 49', 'echo .19', 'pause 638 2']
    native_notes = []

    def native_note(voice, bar, position, midi, gate, velocity):
        offset = .06 if voice == 'strings' else .12
        position += offset if position % 2 else 0
        score.append(f'{voice} {bar * 16 + position:.5f} {midi} {gate:.5f} {velocity:.5f}')
        native_notes.append({'voice': voice, 'bar': bar + 1, 'step': position,
                             'midi': midi, 'gate': gate, 'velocity': velocity})

    for bar in range(BARS + 1):
        quiet = 32 <= bar < 36
        build = 36 <= bar < 40
        end = bar >= 63
        bass_cutoff = 140 if quiet else 175 + 25 * (bar - 36) if build else 255 if 40 <= bar < 56 else 230
        strings_cutoff = 1030 if 48 <= bar < 56 else 900
        hats = 0 if quiet or end else .10 if bar < 4 else .15 if 24 <= bar < 28 or bar >= 60 else .22
        clap = 0 if quiet or end or bar < 4 or 24 <= bar < 28 else .14 if build else .28
        score.append(f'color {bar * 16} {bass_cutoff} {strings_cutoff} {hats} {clap} {int(bar in (15, 31, 55))}')
        if bar == BARS:
            continue
        for beat in range(4):
            if quiet or (bar < 4 or build) and beat % 2:
                continue
            if (bar in (15, 31, 39, 55) and beat == 3) or (bar == 63 and beat != 0):
                continue
            score.append(f'kick {bar * 16 + beat * 4} {.94 if beat == 0 else .87 if beat == 2 else .82}')
        if bar == 63:
            native_note('bass', bar, 0, 31, 3, .82)
            native_note('strings', bar, 0, 67, 3, .62)
            continue
        chord = harmony(bar)
        root = ROOTS[chord]
        third = root + (3 if chord == 1 else 4)
        next_root = ROOTS[harmony(bar + 1)]
        if quiet:
            native_note('bass', bar, 0, root, 4.5, .69)
        elif bar % 2 == 0:
            upper = root - 2 if chord == 1 else root + 9
            for position, midi, gate, velocity in (
                (2, root, .95, .88), (5, root, .55, .55), (7, third, .65, .66),
                (10, root, 1., .82), (12, root + 7, .8, .67), (15, upper, .45, .53),
            ):
                if (bar < 4 or bar >= 60) and position not in (2, 10):
                    continue
                if build and position not in (2, 7, 10):
                    continue
                native_note('bass', bar, position, midi, gate, velocity * (.86 if build else 1))
        else:
            for position, midi, gate, velocity in (
                (0, root, 1.05, .84), (3, root + 7, .55, .59), (6, root, .8, .76),
                (9, third, .7, .64), (12, root, 1.3, .80), (15, next_root - 1, .45, .53),
            ):
                if (bar < 4 or bar >= 60) and position not in (0, 12):
                    continue
                if build and position not in (0, 6, 9):
                    continue
                native_note('bass', bar, position, midi, gate, velocity * (.86 if build else 1))
        if ((16 <= bar < 24) or (40 <= bar < 56)) and bar % 2:
            tonic = MELODY_ROOTS[chord]
            third_up = 3 if chord == 1 else 4
            pitches = (tonic, tonic + third_up, tonic + 7) if bar % 4 == 1 else (tonic + third_up, tonic + 2, tonic)
            if bar in (51, 55):
                pitches = (*pitches[:2], pitches[2] + 12)
            for position, midi, gate, velocity in zip((2, 6, 10), pitches, (1.2, 1.0, 2.8), (.66, .61, .76)):
                native_note('strings', bar, position, midi, gate, velocity)
        elif bar in (56, 58):
            native_note('strings', bar, 10, MELODY_ROOTS[chord], 2.8, .53)
    with paths[7].open('x') as out:
        out.write('\n'.join(score) + '\n')
    subprocess.run([str(args.renderer.resolve()), str(paths[7]), str(native)], check=True)
    backing = read_pcm(Path(str(native) + '.wav')).astype(float) / 32768
    plucks, piano = np.zeros_like(backing), np.zeros_like(backing)
    events = []

    def chord(bus, bar, position, notes, gate, velocity, voice='pluck', roll=.0055):
        onset = frame(bar, position)
        for index, midi in enumerate(notes):
            start = onset + round(index * roll * RATE)
            sound = (plucked(midi, gate * STEP, velocity, start / RATE, 'round') if voice == 'pluck'
                     else electric_piano(midi, gate * STEP, velocity, start / RATE))
            if voice == 'piano':
                t = np.arange(len(sound)) / RATE
                sound *= (.5 - .5 * np.cos(np.pi * np.clip(t / .028, 0, 1)))[:, None]
            count = min(len(sound), len(bus) - start)
            bus[start:start + count] += sound[:count]
        events.append({'voice': voice, 'bar': bar + 1, 'step': position,
                       'midi': list(notes), 'gate': gate, 'velocity': velocity})

    for bar in range(63):
        idx = harmony(bar)
        if bar < 4:
            chord(plucks, bar, 2 if bar % 2 == 0 else 10, LOW[idx][-2:], 2.5, .38)
            continue
        if 32 <= bar < 40:
            if bar % 2 == 0:
                chord(piano, bar, 0, LOW[idx], 22 if bar < 36 else 5, .30 if bar < 36 else .39, 'piano')
            elif bar >= 36:
                chord(piano, bar, 4.1, LOW[idx], 2.3, .40, 'piano')
            tonic = MELODY_ROOTS[idx]
            third_up = 3 if idx == 1 else 4
            if bar % 2 == 0:
                chord(piano, bar, 3, (tonic + third_up,), 3.2, .46, 'piano')
                chord(piano, bar, 10, (tonic + 2,), 3.0, .42, 'piano')
            else:
                chord(piano, bar, 4.1, (tonic,), 4.5 if bar < 39 else 3.0, .48, 'piano')
            continue
        if 24 <= bar < 28:
            if bar % 2 == 0:
                pitches = (64, 67, 71, 74 if bar == 24 else 72)
                positions = (2, 5.1, 8, 12)
            else:
                pitches, positions = (71, 67, 64, 62), (2, 6, 10, 14)
            for position, midi in zip(positions, pitches):
                chord(plucks, bar, position, (midi,), 2.2, .67, roll=0)
            continue
        if bar >= 56:
            chord(plucks, bar, 0 if bar % 2 == 0 else 4.1, LOW[idx], 2.5,
                  .43 - (bar - 56) * .028)
            continue
        reply = (16 <= bar < 24 or 40 <= bar < 56) and bar % 2 == 1
        if reply:
            pattern = ((0, False, 1.4, .44), (14, True, .75, .45))
        elif bar % 2 == 0:
            pattern = ((1.1, False, 1.5, .70), (6, False, .8, .53),
                       (9.1, True, 1.2, .74), (14, False, .7, .58))
        else:
            pattern = ((2, False, 1.1, .69), (7.1, False, .65, .49),
                       (10, True, 1.8, .77), (15.1, True, .65, .50))
        for position, high, gate, velocity in pattern:
            chord(plucks, bar, position, HIGH[idx] if high else LOW[idx], gate, velocity)
    chord(piano, 63, 0, (55, 59, 62, 66), 20, .30, 'piano')
    for bus, peak in ((plucks, -17.0), (piano, -20.8)):
        dry = bus.copy()
        for seconds, level, channel in ((.017, .055, 0), (.027, .05, 1),
                                       (.041, .025, 0), (.053, .022, 1)):
            delay = round(seconds * RATE)
            bus[delay:, channel] += level * dry[:-delay, 1 - channel]
        bus *= 10 ** (peak / 20) / np.max(np.abs(bus))
        pause, return_at = frame(40, -2), frame(40)
        fade = np.linspace(1, 0, round(.012 * RATE))[:, None]
        bus[pause - len(fade):pause] *= fade
        bus[pause:return_at] = 0
    mix = backing + plucks + piano
    focus = mix[frame(8):frame(28)].copy()
    focus[:240] *= np.linspace(0, 1, 240)[:, None]
    focus[-2400:] *= np.linspace(1, 0, 2400)[:, None]
    for path, samples in zip(paths[:5], (mix, plucks, piano, backing, focus)):
        write_wav(path, samples)
    report = {
        'title': 'Amber Steps', 'bpm': BPM, 'key': 'G major', 'bars': BARS,
        'frames': len(mix), 'duration_seconds': len(mix) / RATE,
        'progression': ['Gmaj9', 'Em9', 'Cmaj9', 'D9'],
        'native_events': native_notes, 'keyboard_events': events,
        'score_sha256': sha(paths[7]), 'mix_peak_dbfs': float(20 * np.log10(np.max(np.abs(mix)))),
        'integrated_lufs': loudness(paths[0]), 'saved_without_playback': True,
        'output_sha256': {path.name: sha(path) for path in paths[:5]},
    }
    with paths[5].open('x') as out:
        out.write(json.dumps(report, indent=2) + '\n')
    with paths[6].open('x') as out:
        out.write('Amber Steps\n120 BPM / G major / Gmaj9-Em9-Cmaj9-D9\n\n')
        for bar, label in ((0, 'First plucks'), (4, 'Main groove'), (16, 'Strings answer'),
                           (24, 'Plucked melodic feature'), (32, 'Piano middle'),
                           (36, 'Rebuild'), (40, 'Full return'), (48, 'Higher answer'),
                           (56, 'Outro'), (63, 'Last G-major-seven chord')):
            out.write(f'{frame(bar) / RATE:.2f}s  {label}\n')
    print(f"Saved {paths[0]} silently: {len(mix) / RATE:.2f}s, "
          f"{report['integrated_lufs']:.1f} LUFS, peak {report['mix_peak_dbfs']:.2f} dBFS")


if __name__ == '__main__':
    main()
