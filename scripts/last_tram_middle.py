#!/usr/bin/env python3
"""Insert an eight-bar relative-major passage into the complete Last Tram take."""
import argparse
import json
import subprocess
from pathlib import Path

import numpy as np

from compare_hooks import loudness
from last_tram import BPM, STEP, frame
from organ_voice import organ
from piano_phrasing import read_pcm
from piano_study import RATE, sha, write_wav


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path, help='Last Tram v2 complete mix')
    parser.add_argument('output', type=Path)
    parser.add_argument('--renderer', type=Path, default=Path('build/music-palette-render'))
    args = parser.parse_args()
    prefix = str(args.output)
    suffixes = ('.wav', '-middle.wav', '-middle-bass.wav', '-middle-kick.wav',
                '-middle-percussion.wav', '-middle-strings.wav', '-middle-organ.wav',
                '-middle-lead.wav', '-focus.wav', '.json', '-cues.txt', '-score.txt')
    paths = [Path(prefix + suffix) for suffix in suffixes]
    native = prefix + '-middle-native'
    for path in paths + [Path(native + suffix) for suffix in
                         ('.wav', '-bass.wav', '-kick.wav', '-percussion.wav', '-strings.wav')]:
        if path.exists():
            raise FileExistsError(f'Output already exists: {path}')
    source = read_pcm(args.source).astype(float) / 32768
    if len(source) < frame(56):
        raise ValueError('Expected the complete 56-bar Last Tram recording')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    score = ['# Last Tram middle — Ebmaj9 / Abmaj9 / Fm9 / G7b9', f'tempo {BPM}',
             'bars 8', 'tail 0', 'kick_tune 32.70319566', 'echo .21']
    roots = (39, 32, 41, 31)
    notes = []

    def note(voice, bar, position, midi, gate, velocity):
        position += .13 if voice == 'bass' and position % 2 else .08 if position % 2 else 0
        score.append(f'{voice} {bar * 16 + position:.5f} {midi} {gate:.5f} {velocity:.5f}')
        notes.append({'voice': voice, 'bar': bar + 1, 'step': position,
                      'midi': midi, 'gate': gate, 'velocity': velocity})

    for bar in range(8):
        root = roots[bar // 2]
        third = root + (3 if bar // 2 == 2 else 4)
        score.append(f'color {bar * 16} {195 if bar < 4 else 175} 780 {.12 if bar < 4 else .08} {.12 if bar < 4 else .06} 0')
        for position in ((0, 10) if bar < 4 else (0, 8) if bar < 7 else (0,)):
            score.append(f'kick {bar * 16 + position} {.82 if position == 0 else .68}')
        if bar % 2 == 0:
            for position, midi, gate, velocity in ((0, root, 3.0, .76), (6, root + 7, 1.1, .54),
                                                   (10, root, 2.6, .69), (14, third, 1., .51)):
                note('bass', bar, position, midi, gate, velocity)
        else:
            for position, midi, gate, velocity in ((0, root, 3.1, .73), (7, root + 7, 1.6, .55),
                                                   (10, root, 2., .66)):
                note('bass', bar, position, midi, gate, velocity)
        if bar >= 4:
            phrase = {
                4: ((3, 68, 2.0, .54), (9, 67, 3.1, .49)),
                5: ((2, 65, 3.1, .54), (10, 63, 3.0, .48)),
                6: ((3, 62, 2.4, .51), (9, 59, 3.0, .47)),
                7: ((2, 60, 2.5, .45), (7, 59, 3.2, .48)),
            }[bar]
            for position, midi, gate, velocity in phrase:
                note('strings', bar, position, midi, gate, velocity)
    with paths[11].open('x') as out:
        out.write('\n'.join(score) + '\n')
    subprocess.run([str(args.renderer.resolve()), str(paths[11]), native], check=True)
    backing = read_pcm(Path(native + '.wav')).astype(float) / 32768
    chords, melody = np.zeros_like(backing), np.zeros_like(backing)
    events = []

    def keys(bus, bar, position, pitches, gate, velocity):
        for index, midi in enumerate(pitches):
            start = frame(bar, position) + round(index * .0035 * RATE)
            sound = organ(midi, gate * STEP, velocity, start / RATE, mellow=bus is chords)
            count = min(len(sound), len(bus) - start)
            bus[start:start + count] += sound[:count]
        events.append({'voice': 'organ' if bus is chords else 'organ lead',
                       'bar': bar + 1, 'step': position, 'midi': list(pitches),
                       'gate': gate, 'velocity': velocity})

    voicings = ((55, 58, 62, 65), (55, 58, 60, 63), (56, 60, 63, 67), (53, 59, 62, 68))
    for bar in range(8):
        keys(chords, bar, 1.13, voicings[bar // 2], 4.8 if bar % 2 == 0 else 3., .35)
        if bar % 2 == 0:
            keys(chords, bar, 10, voicings[bar // 2], 2.7, .30)
    # Broad organ phrases open the passage; the established strings then answer
    # in a lower register and guide the harmony back toward C minor.
    phrases = {
        0: ((3, 67, 2.3, .49), (7.13, 70, 2.0, .46), (11, 74, 3.4, .54)),
        1: ((2, 72, 2.4, .49), (6, 70, 2.5, .46), (10, 67, 3.0, .44)),
        2: ((3, 75, 2.8, .55), (8, 74, 2., .48), (12, 72, 2.5, .46)),
        3: ((2, 70, 2.6, .47), (7.13, 67, 2.1, .43), (12, 68, 2.5, .46)),
    }
    for bar, phrase in phrases.items():
        for position, midi, gate, velocity in phrase:
            keys(melody, bar, position, (midi,), gate, velocity)
    for bus, target_rms, peak in ((chords, -33., -21.), (melody, -34.5, -21.0)):
        dry = bus.copy()
        for seconds, level, channel in ((.023, .065, 0), (.033, .055, 1), (.047, .026, 0), (.061, .024, 1)):
            delay = round(seconds * RATE)
            bus[delay:, channel] += level * dry[:-delay, 1 - channel]
        active = np.max(np.abs(bus), axis=1) > 1e-6
        gain = min(10 ** (target_rms / 20) / np.sqrt(np.mean(bus[active] ** 2)),
                   10 ** (peak / 20) / np.max(np.abs(bus)))
        bus *= gain
        bus[-round(.5 * RATE):] *= np.linspace(1, 0, round(.5 * RATE))[:, None]
    native_stems = [read_pcm(Path(native + suffix)).astype(float) / 32768 for suffix in
                    ('-bass.wav', '-kick.wav', '-percussion.wav', '-strings.wav')]
    middle = backing + chords + melody
    seam = round(.008 * RATE)
    insertion = frame(24)
    before, after = source[:insertion].copy(), source[insertion:].copy()
    before[-seam:] *= np.linspace(1, 0, seam)[:, None]
    after[:seam] *= np.linspace(0, 1, seam)[:, None]
    for bus in [middle, *native_stems, chords, melody]:
        bus[:seam] *= np.linspace(0, 1, seam)[:, None]
        bus[-seam:] *= np.linspace(1, 0, seam)[:, None]
    mix = np.concatenate((before, middle, after))
    focus = mix[frame(22):frame(34)].copy()
    focus[:240] *= np.linspace(0, 1, 240)[:, None]
    focus[-2400:] *= np.linspace(1, 0, 2400)[:, None]
    for path, samples in zip(paths[:9], (mix, middle, *native_stems, chords, melody, focus)):
        write_wav(path, samples)
    report = {
        'title': 'Last Tram — relative-major middle', 'bpm': BPM, 'key': 'C minor',
        'bars': 64, 'frames': len(mix), 'duration_seconds': len(mix) / RATE,
        'source_sha256': sha(args.source), 'score_sha256': sha(paths[11]),
        'insertion_frame': insertion, 'inserted_frames': len(middle), 'seam_frames': seam,
        'middle_progression': ['Ebmaj9', 'Abmaj9', 'Fm9', 'G7b9'],
        'native_events': notes, 'organ_events': events,
        'mix_peak_dbfs': float(20 * np.log10(np.max(np.abs(mix)))),
        'integrated_lufs': loudness(paths[0]), 'saved_without_playback': True,
        'output_sha256': {path.name: sha(path) for path in paths[:9]},
    }
    with paths[9].open('x') as out:
        out.write(json.dumps(report, indent=2) + '\n')
    with paths[10].open('x') as out:
        out.write('Last Tram v3 — relative-major middle\n106 BPM / C minor / 64 bars\n\n')
        for bar, label in ((0, 'Organ glow'), (4, 'Broken kick groove'), (8, 'Strings theme'),
                           (16, 'Organ takes the melody'), (24, 'Relative-major detour'),
                           (28, 'Lower strings answer'), (32, 'Quiet minor middle'),
                           (36, 'Rebuild'), (39, 'Bass and held organ pickup'),
                           (40, 'Connected return'), (48, 'Higher answers'),
                           (56, 'Outro'), (63, 'Last C-minor chord')):
            out.write(f'{frame(bar) / RATE:.2f}s  {label}\n')
    print(f"Saved {paths[0]} silently: {len(mix) / RATE:.2f}s, "
          f"{report['integrated_lufs']:.1f} LUFS, peak {report['mix_peak_dbfs']:.2f} dBFS")


if __name__ == '__main__':
    main()
