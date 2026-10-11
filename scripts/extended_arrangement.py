#!/usr/bin/env python3
"""Insert a new sixteen-bar second theme into the preserved Side Street mix."""
import argparse
import json
from pathlib import Path

import numpy as np

from compare_hooks import loudness
from piano_phrasing import read_pcm
from piano_study import BPM, RATE, STEP, electric_piano, sha, write_wav


def frame(bar, step=0):
    return round((bar * 16 + step) * STEP * RATE)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('theme', type=Path, help='Native sixteen-bar theme prefix')
    parser.add_argument('kick', type=Path)
    parser.add_argument('percussion', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    suffixes = ('.wav', '-theme.wav', '-theme-bass.wav', '-theme-strings.wav',
                '-theme-drums.wav', '-theme-keys.wav', '-theme-piano.wav',
                '-focus.wav', '.json', '-cues.txt')
    paths = [Path(str(args.output) + suffix) for suffix in suffixes]
    for path in paths:
        if path.exists():
            raise FileExistsError(f'Output already exists: {path}')
    read = lambda path: read_pcm(path).astype(float) / 32768
    source = read(args.source)
    bass = read(Path(str(args.theme) + '-bass.wav'))
    strings = read(Path(str(args.theme) + '-hook.wav'))
    length = frame(16)
    if len(source) < frame(64) or len(bass) < length or bass.shape != strings.shape:
        raise ValueError('Expected the complete source and sixteen-bar native theme')
    bass, strings = bass[:length].copy(), strings[:length].copy()
    drums = sum(read(path)[frame(16):frame(16) + length] for path in (args.kick, args.percussion))
    keys = np.zeros_like(bass)
    piano = np.zeros_like(bass)
    events = []
    voicings = ((53, 56, 60, 63), (55, 60, 65), (56, 60, 63, 67), (56, 60, 63))

    def note(bus, bar, position, midi, gate, velocity, softness=.016, roll=0.):
        start = frame(bar, position) + round(roll * RATE)
        sound = electric_piano(midi, gate * STEP, velocity, start / RATE)
        t = np.arange(len(sound)) / RATE
        sound *= (.5 - .5 * np.cos(np.pi * np.clip(t / softness, 0, 1)))[:, None]
        count = min(len(sound), len(bus) - start)
        bus[start:start + count] += sound[:count]
        events.append({'voice': 'keys' if bus is keys else 'piano', 'local_bar': bar + 1,
                       'step': position, 'midi': midi, 'gate_seconds': gate * STEP,
                       'velocity': velocity, 'seconds': start / RATE})

    for bar in range(16):
        if bar >= 8 and bar % 2:
            continue
        for index, midi in enumerate(voicings[(bar % 8) // 2]):
            note(keys, bar, 0 if bar % 2 == 0 else 4.1, midi,
                 5.5 if bar >= 8 else 3.0 if bar % 2 == 0 else 1.8,
                 .37 if bar >= 8 else .46 if bar % 2 == 0 else .39,
                 softness=.028, roll=index * .005)
    for bar, pitches in ((9, (72, 68, 65)), (11, (70, 67, 65)),
                         (13, (68, 67, 65)), (15, (63, 60, 65))):
        for position, pitch, gate, velocity in zip((0, 4.1, 8), pitches,
                                                   (1.4, 1.0, 2.4), (.65, .53, .70)):
            note(piano, bar, position, pitch, gate, velocity)
    for bus, target_peak in ((keys, -22.), (piano, -18.8)):
        dry = bus.copy()
        for seconds, level, channel in ((.019, .065, 0), (.027, .060, 1),
                                       (.043, .035, 0), (.051, .030, 1)):
            delay = round(seconds * RATE)
            bus[delay:, channel] += level * dry[:-delay, 1 - channel]
        bus *= 10 ** (target_peak / 20) / np.max(np.abs(bus))

    # Keep the old drum sounds and give the last new bar a gentle release into
    # the already-written quiet middle. No time is removed by these edge fades.
    stems = (bass, strings, drums, keys, piano)
    in_frames, out_frames = round(.002 * RATE), round(.030 * RATE)
    for stem in stems:
        stem[:in_frames] *= np.linspace(0, 1, in_frames)[:, None]
        stem[-out_frames:] *= np.linspace(1, 0, out_frames)[:, None]
    theme = sum(stems)
    insertion = frame(32)
    before, after = source[:insertion].copy(), source[insertion:].copy()
    seam = round(.008 * RATE)
    before[-seam:] *= np.linspace(1, 0, seam)[:, None]
    after[:seam] *= np.linspace(0, 1, seam)[:, None]
    mix = np.concatenate((before, theme, after))
    first = frame(30)
    last = insertion + length + frame(2)
    focus = mix[first:last].copy()
    focus[:240] *= np.linspace(0, 1, 240)[:, None]
    focus[-2400:] *= np.linspace(1, 0, 2400)[:, None]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    for path, samples in zip(paths[:8], (mix, theme, *stems, focus)):
        write_wav(path, samples)
    report = {
        'title': 'Side Street — long cut', 'bpm': BPM, 'bars': 80,
        'frames': len(mix), 'duration_seconds': len(mix) / RATE,
        'source_sha256': sha(args.source), 'native_theme_sha256': sha(Path(str(args.theme) + '.wav')),
        'insert_at_frame': insertion, 'inserted_frames': length,
        'seam_fade_frames': seam, 'events': events,
        'focus_start_frame': first, 'focus_seconds': len(focus) / RATE,
        'mix_peak_dbfs': float(20 * np.log10(np.max(np.abs(mix)))),
        'integrated_lufs': loudness(paths[0]),
        'theme_rms_dbfs': float(20 * np.log10(np.sqrt(np.mean(theme ** 2)))),
        'original_body_rms_dbfs': float(20 * np.log10(np.sqrt(np.mean(source[frame(16):frame(32)] ** 2)))),
        'output_sha256': {path.name: sha(path) for path in paths[:8]},
    }
    with paths[8].open('x') as out:
        out.write(json.dumps(report, indent=2) + '\n')
    with paths[9].open('x') as out:
        out.write('Side Street v8 — long cut\n116 BPM / F minor / 80 bars\n\n')
        for bar, label in ((0, 'Opening'), (8, 'Main strings'), (16, 'Piano answer'),
                           (24, 'Development'), (32, 'Second theme: Db / Eb / Fm'),
                           (40, 'Piano takes the second theme'), (48, 'Quiet moving middle'),
                           (56, 'Original theme returns'), (64, 'Final melodic answer'),
                           (72, 'Outro'), (79, 'Final F minor chord')):
            out.write(f'{frame(bar) / RATE:.2f}s  {label}\n')
    print(f"Wrote {paths[0]}: {len(mix) / RATE:.2f}s; "
          f"peak {report['mix_peak_dbfs']:.2f} dBFS; {report['integrated_lufs']:.1f} LUFS")


if __name__ == '__main__':
    main()
