#!/usr/bin/env python3
"""Compose Last Tram: a slower, broken-kick C-minor groove with warm organ."""
import argparse
import json
import subprocess
from pathlib import Path

import numpy as np

from compare_hooks import loudness
from organ_voice import organ
from piano_phrasing import read_pcm
from piano_study import RATE, sha, write_wav

BPM, BARS = 106, 56
STEP = 60 / BPM / 4
ROOTS = (36, 41, 32, 31)
VOICINGS = ((51, 55, 58, 62), (56, 60, 63, 67), (55, 60, 63, 70), (53, 59, 62, 68))
PHRASES = (
    ((3, 67, 1.5, .71), (7, 63, 1., .61), (11, 62, 2.5, .66)),
    ((2, 63, 1.6, .66), (6, 60, 2., .72), (12, 58, 1.6, .57)),
    ((3, 68, 1.5, .73), (7, 67, 1., .63), (11, 65, 2.5, .70)),
    ((2, 72, 1.5, .72), (6, 68, 1.8, .66), (12, 67, 1.6, .60)),
    ((3, 67, 1.5, .68), (7, 63, 1., .60), (11, 60, 2.5, .65)),
    ((2, 70, 1.5, .72), (6, 68, 1.8, .66), (12, 67, 1.6, .62)),
    ((3, 74, 1.5, .75), (7, 71, 1., .68), (11, 68, 2.5, .70)),
    ((2, 67, 1.6, .68), (6, 65, 1.8, .64), (12, 62, 1.6, .58)),
)


def frame(bar, position=0):
    return round((bar * 16 + position) * STEP * RATE)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    parser.add_argument('--renderer', type=Path, default=Path('build/music-palette-render'))
    args = parser.parse_args()
    prefix = str(args.output)
    paths = [Path(prefix + suffix) for suffix in
             ('.wav', '-organ.wav', '-organ-lead.wav', '-backing.wav', '-focus.wav',
              '.json', '-cues.txt', '-score.txt')]
    native = prefix + '-native'
    for path in paths + [Path(native + suffix) for suffix in
                         ('.wav', '-bass.wav', '-kick.wav', '-percussion.wav', '-strings.wav')]:
        if path.exists():
            raise FileExistsError(f'Output already exists: {path}')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    score = ['# Last Tram — Cm9 / Fm9 / Abmaj9 / G7b9', f'tempo {BPM}', f'bars {BARS}',
             'tail 3', 'kick_tune 32.70319566', 'echo .24', 'pause 510 2']
    native_events = []

    def note(voice, bar, position, midi, gate, velocity):
        position += (.08 if voice == 'strings' else .13) if position % 2 else 0
        score.append(f'{voice} {bar * 16 + position:.5f} {midi} {gate:.5f} {velocity:.5f}')
        native_events.append({'voice': voice, 'bar': bar + 1, 'step': position,
                              'midi': midi, 'gate': gate, 'velocity': velocity})

    for bar in range(BARS + 1):
        quiet, build, end = 24 <= bar < 28, 28 <= bar < 32, bar >= 55
        hats = 0 if quiet or end else .08 if bar < 4 else .17 if build or 20 <= bar < 24 else .21
        clap = 0 if quiet or end or bar < 4 else .14 if build else .28
        cutoff = 130 if quiet else 170 + 25 * (bar - 28) if build else 245 if 32 <= bar < 48 else 215
        score.append(f'color {bar * 16} {cutoff} {800 if bar < 40 else 980} {hats} {clap} {int(bar in (23, 47))}')
        if bar == BARS:
            continue
        kicks = (0,) if bar == 55 else () if quiet else (0, 8) if build else (0, 10) if bar < 4 else (
            (0, 6, 10), (0, 7, 10, 14), (0, 6, 8, 11), (0, 10, 14))[bar % 4]
        for index, position in enumerate(kicks):
            if bar in (23, 31, 47) and position >= 14:
                continue
            velocity = .92 if position == 0 else .80 if index == 1 else .74
            score.append(f'kick {bar * 16 + position} {velocity}')
        if bar == 55:
            note('bass', bar, 0, 36, 3., .82)
            note('strings', bar, 0, 60, 3., .62)
            continue
        idx = (bar % 8) // 2
        root = ROOTS[idx]
        third = root + (3 if idx < 2 else 4)
        seventh = root - (1 if idx == 2 else 2)
        next_root = ROOTS[((bar + 1) % 8) // 2]
        if quiet:
            note('bass', bar, 0, root, 4.5, .65)
        else:
            pattern = ((0, root, 1.6, .86), (3, root + 7, .5, .54), (6, root, .95, .76),
                       (9, third, .65, .60), (11, root + 7, .6, .67), (14, root, .9, .78)) if bar % 2 == 0 else (
                (0, root, 1.2, .85), (3, third, .65, .58), (7, root, 1.2, .80),
                (10, root + 7, .85, .71), (13, seventh, .55, .52), (15, next_root - 1, .45, .50))
            for position, midi, gate, velocity in pattern:
                if bar < 4 and position not in (0, 7, 14):
                    continue
                if build and position not in (0, 6, 7, 10):
                    continue
                if bar >= 52 and position > 10:
                    continue
                note('bass', bar, position, midi, gate, velocity * (.87 if build else 1))
        if 8 <= bar < 16 or 32 <= bar < 48:
            for position, midi, gate, velocity in PHRASES[bar % 8]:
                if bar in (41, 45) and position == 12:
                    midi += 12
                note('strings', bar, position, midi, gate, velocity)
        elif bar in (28, 30, 48, 50):
            phrase = PHRASES[bar % 8][-1]
            note('strings', bar, 10, phrase[1], 3., .53)
    with paths[7].open('x') as out:
        out.write('\n'.join(score) + '\n')
    subprocess.run([str(args.renderer.resolve()), str(paths[7]), native], check=True)
    backing = read_pcm(Path(native + '.wav')).astype(float) / 32768
    chords, lead = np.zeros_like(backing), np.zeros_like(backing)
    events = []

    def chord(bus, bar, position, pitches, gate, velocity, mellow=True):
        onset = frame(bar, position)
        for index, midi in enumerate(pitches):
            start = onset + round(index * .0035 * RATE)
            sound = organ(midi, gate * STEP, velocity, start / RATE, mellow)
            count = min(len(sound), len(bus) - start)
            bus[start:start + count] += sound[:count]
        events.append({'voice': 'organ' if bus is chords else 'organ lead', 'bar': bar + 1,
                       'step': position, 'midi': list(pitches), 'gate': gate, 'velocity': velocity})

    for bar in range(55):
        pitches = VOICINGS[(bar % 8) // 2]
        if bar < 4:
            chord(chords, bar, 1.13 if bar % 2 == 0 else 9.13, pitches, 5., .30)
        elif 24 <= bar < 28:
            if bar % 2 == 0:
                chord(chords, bar, 0, pitches, 22., .28)
            else:
                target = (67, 63) if bar == 25 else (68, 65)
                chord(lead, bar, 3, (target[0],), 3., .40)
                chord(lead, bar, 10, (target[1],), 3., .37)
        elif 28 <= bar < 32:
            chord(chords, bar, 1.13, pitches, 4.5 if bar < 30 else 2.5, .34 + (bar - 28) * .035)
            if bar >= 30:
                chord(chords, bar, 9.13, pitches, 1.8, .40)
        elif bar >= 48:
            chord(chords, bar, 1.13 if bar % 2 == 0 else 9.13, pitches, 4.5,
                  .34 - (bar - 48) * .018)
        elif 8 <= bar < 16 or 32 <= bar < 48:
            chord(chords, bar, 1.13, pitches, 3.2, .48)
            chord(chords, bar, 9.13, pitches, 3.5, .44)
        elif 16 <= bar < 24:
            chord(chords, bar, 0, pitches, 3., .37)
            # The organ gets a compact single-note turn while the strings rest.
            patterns = ((67, 63, 62), (68, 67, 65), (67, 63, 60), (71, 68, 67))
            notes = patterns[(bar % 8) // 2]
            if bar % 2:
                notes = notes[::-1]
            for position, midi, gate, velocity in zip((5.13, 8, 12), notes,
                                                      (1.2, 1.8, 2.0), (.48, .52, .45)):
                chord(lead, bar, position, (midi,), gate, velocity, mellow=False)
        else:
            pattern = ((1.13, 3.5, .55), (7.13, 2.2, .47), (10, 3., .60)) if bar % 2 == 0 else (
                (2, 3.8, .53), (9.13, 2.8, .48), (14, 1.2, .45))
            for position, gate, velocity in pattern:
                chord(chords, bar, position, pitches, gate, velocity)
    chord(chords, 55, 0, (48, 51, 55, 58), 19., .28)
    for bus, target_rms, max_peak in ((chords, -32., -20.5), (lead, -36., -21.5)):
        dry = bus.copy()
        for seconds, level, channel in ((.023, .075, 0), (.033, .065, 1),
                                       (.047, .035, 0), (.061, .030, 1)):
            delay = round(seconds * RATE)
            bus[delay:, channel] += level * dry[:-delay, 1 - channel]
        active = np.max(np.abs(bus), axis=1) > 1e-6
        gain = 10 ** (target_rms / 20) / np.sqrt(np.mean(bus[active] ** 2))
        gain = min(gain, 10 ** (max_peak / 20) / np.max(np.abs(bus)))
        bus *= gain
        pause, return_at = frame(32, -2), frame(32)
        fade = np.linspace(1, 0, round(.012 * RATE))[:, None]
        bus[pause - len(fade):pause] *= fade
        bus[pause:return_at] = 0
    mix = backing + chords + lead
    focus = mix[frame(4):frame(24)].copy()
    focus[:240] *= np.linspace(0, 1, 240)[:, None]
    focus[-2400:] *= np.linspace(1, 0, 2400)[:, None]
    for path, samples in zip(paths[:5], (mix, chords, lead, backing, focus)):
        write_wav(path, samples)
    report = {
        'title': 'Last Tram', 'bpm': BPM, 'key': 'C minor', 'bars': BARS,
        'frames': len(mix), 'duration_seconds': len(mix) / RATE,
        'native_events': native_events, 'organ_events': events,
        'score_sha256': sha(paths[7]), 'mix_peak_dbfs': float(20 * np.log10(np.max(np.abs(mix)))),
        'integrated_lufs': loudness(paths[0]), 'saved_without_playback': True,
        'output_sha256': {path.name: sha(path) for path in paths[:5]},
    }
    with paths[5].open('x') as out:
        out.write(json.dumps(report, indent=2) + '\n')
    with paths[6].open('x') as out:
        out.write('Last Tram\n106 BPM / C minor / Cm9-Fm9-Abmaj9-G7b9\n\n')
        for bar, label in ((0, 'Organ glow'), (4, 'Broken kick groove'), (8, 'Strings theme'),
                           (16, 'Organ takes the melody'), (24, 'Quiet middle'),
                           (28, 'Rebuild'), (32, 'Full return'), (40, 'Higher answers'),
                           (48, 'Outro'), (55, 'Final C-minor-seven chord')):
            out.write(f'{frame(bar) / RATE:.2f}s  {label}\n')
    print(f"Saved {paths[0]} silently: {len(mix) / RATE:.2f}s, "
          f"{report['integrated_lufs']:.1f} LUFS, peak {report['mix_peak_dbfs']:.2f} dBFS")


if __name__ == '__main__':
    main()
