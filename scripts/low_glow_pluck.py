#!/usr/bin/env python3
"""Give Low Glow a plucked rhythm part, preserving its sustained piano and melody."""
import argparse
import json
from pathlib import Path

import numpy as np

from compare_hooks import loudness
from low_glow_keys import BPM, frame
from piano_phrasing import read_pcm
from piano_study import RATE, sha, write_wav
from plucked_voice import plucked


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('old_keys', type=Path)
    parser.add_argument('score', type=Path, help='Original Low Glow keyboard JSON')
    parser.add_argument('output', type=Path)
    parser.add_argument('--voice', choices=('muted', 'round', 'bright'), default='muted')
    args = parser.parse_args()
    paths = [Path(str(args.output) + suffix) for suffix in
             ('.wav', '-rhythm.wav', '-backing.wav', '-focus.wav', '-reference.wav',
              '-instrument.wav', '.json', '-cues.txt')]
    for path in paths:
        if path.exists():
            raise FileExistsError(f'Output already exists: {path}')
    source, old = [read_pcm(path).astype(float) / 32768 for path in (args.source, args.old_keys)]
    if source.shape != old.shape or len(source) < frame(56):
        raise ValueError('Expected the full Low Glow mix and its current keys')
    score = json.loads(args.score.read_text())
    new = np.zeros_like(source)
    for event in score['events']:
        for index, midi in enumerate(event['midi']):
            start = round((event['seconds'] + index * .0055) * RATE)
            sound = plucked(midi, event['gate_seconds'], event['velocity'], start / RATE, args.voice)
            count = min(len(sound), len(new) - start)
            new[start:start + count] += sound[:count]
    dry = new.copy()
    for seconds, level, channel in ((.014, .050, 0), (.023, .045, 1),
                                   (.037, .020, 0), (.049, .018, 1)):
        delay = round(seconds * RATE)
        new[delay:, channel] += level * dry[:-delay, 1 - channel]
    # Match the rhythm stem's active-groove energy; do not turn up the full mix.
    a, b = frame(8), frame(20)
    gain = np.sqrt(np.mean(old[a:b] ** 2) / np.mean(new[a:b] ** 2))
    gain = min(gain, 10 ** (-15.5 / 20) / np.max(np.abs(new)))
    new *= gain
    # Retain the established two-bar bass spotlight in the new performance.
    first, last = frame(20), frame(22)
    withdraw = np.ones(last - first)
    ramp = round(.035 * RATE)
    withdraw[:ramp] = np.linspace(0, 1, ramp)
    withdraw[-ramp:] = np.linspace(1, 0, ramp)
    new[first:last] *= 1 - withdraw[:, None]
    amount = np.zeros(len(source))
    for first_bar, last_bar in ((4, 24), (28, 55)):
        first, last = frame(first_bar), frame(last_bar)
        amount[first:last] = 1
        edge = round(.025 * RATE)
        amount[first:first + edge] = np.linspace(0, 1, edge)
        amount[last - edge:last] = np.linspace(1, 0, edge)
    pause, return_at = frame(32, -2), frame(32)
    edge = np.linspace(1, 0, round(.012 * RATE))[:, None]
    new[pause - len(edge):pause] *= edge
    new[pause:return_at] = 0
    rhythm = old * (1 - amount[:, None]) + new * amount[:, None]
    backing = source - old
    mix = backing + rhythm
    focus, reference, instrument = [samples[a:b].copy() for samples in (mix, source, rhythm)]
    for samples in (focus, reference, instrument):
        samples[:240] *= np.linspace(0, 1, 240)[:, None]
        samples[-2400:] *= np.linspace(1, 0, 2400)[:, None]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    for path, samples in zip(paths[:6], (mix, rhythm, backing, focus, reference, instrument)):
        write_wav(path, samples)
    report = {
        'title': 'Low Glow — plucked rhythm study', 'voice': args.voice, 'bpm': BPM,
        'frames': len(mix), 'duration_seconds': len(mix) / RATE,
        'source_sha256': sha(args.source), 'old_keys_sha256': sha(args.old_keys),
        'score_sha256': sha(args.score), 'pluck_gain': float(gain),
        'mix_peak_dbfs': float(20 * np.log10(np.max(np.abs(mix)))),
        'integrated_lufs': loudness(paths[0]), 'focus_lufs': loudness(paths[3]),
        'reference_lufs': loudness(paths[4]),
        'rhythm_peak_dbfs': float(20 * np.log10(np.max(np.abs(rhythm)))),
        'output_sha256': {path.name: sha(path) for path in paths[:6]},
        'playback': 'Off at user request',
    }
    with paths[6].open('x') as out:
        out.write(json.dumps(report, indent=2) + '\n')
    with paths[7].open('x') as out:
        out.write('Low Glow — plucked rhythm study\n122 BPM / A minor\n\n')
        for bar, label in ((4, 'Plucked chord groove'), (12, 'String replies'),
                           (20, 'Bass spotlight'), (24, 'Sustained piano middle'),
                           (28, 'Plucks return'), (32, 'Full groove'),
                           (48, 'Quiet piano recollection'), (55, 'Held piano ending')):
            out.write(f'{frame(bar) / RATE:.2f}s  {label}\n')
    print(f"Saved {paths[0]} silently: {report['integrated_lufs']:.1f} LUFS, "
          f"peak {report['mix_peak_dbfs']:.2f} dBFS; "
          f"excerpt {report['focus_lufs']:.1f} vs reference {report['reference_lufs']:.1f} LUFS")


if __name__ == '__main__':
    main()
