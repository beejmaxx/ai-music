#!/usr/bin/env python3
"""Add restrained hand-drum pickups to Side Street's section transitions."""
import argparse
import json
from pathlib import Path

import numpy as np

from compare_hooks import loudness
from piano_phrasing import read_pcm
from piano_study import BPM, RATE, STEP, sha, write_wav


def frame(bar, step=0):
    return round((bar * 16 + step) * STEP * RATE)


def hand_drum(midi, velocity, pan, seed):
    """A short membrane body and a soft, band-limited finger transient."""
    t = np.arange(round(.32 * RATE)) / RATE
    frequency = 440 * 2 ** ((midi - 69) / 12)
    phase = 2 * np.pi * frequency * (t + .32 * .012 * (1 - np.exp(-t / .012)))
    body = (.74 * np.sin(phase) * np.exp(-t / .071)
            + .30 * np.sin(phase * 1.48 + .22) * np.exp(-t / .041)
            + .16 * np.sin(phase * 2.07 + .61) * np.exp(-t / .022))
    rng = np.random.default_rng(seed)
    noise = rng.standard_normal(len(t))
    frequencies = np.fft.rfftfreq(len(t), 1 / RATE)
    band = (1 - np.exp(-(frequencies / 900) ** 2)) * np.exp(-(frequencies / 4300) ** 4)
    noise = np.fft.irfft(np.fft.rfft(noise) * band, n=len(t))
    sound = body + .09 * noise * np.exp(-t / .007)
    sound *= (.5 - .5 * np.cos(np.pi * np.clip(t / .0009, 0, 1)))
    sound *= (.5 + .5 * np.cos(np.pi * np.clip((t - .27) / .05, 0, 1)))
    sound = np.tanh(sound * 1.12)
    sound *= velocity * 10 ** (-22.5 / 20) / np.max(np.abs(sound))
    return np.column_stack((sound * (1 - pan), sound * (1 + pan)))


def rim(velocity, pan, seed):
    t = np.arange(round(.080 * RATE)) / RATE
    rng = np.random.default_rng(seed)
    noise = rng.standard_normal(len(t))
    noise = np.convolve(noise, np.ones(5) / 5, mode='same')
    sound = (.5 * np.sin(2 * np.pi * 1620 * t) * np.exp(-t / .006)
             + .31 * np.sin(2 * np.pi * 2290 * t) * np.exp(-t / .0035)
             + .16 * noise * np.exp(-t / .0025))
    sound *= (.5 - .5 * np.cos(np.pi * np.clip(t / .0005, 0, 1)))
    sound[-480:] *= np.linspace(1, 0, 480)
    sound *= velocity * 10 ** (-31 / 20) / np.max(np.abs(sound))
    return np.column_stack((sound * (1 - pan), sound * (1 + pan)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    paths = [Path(str(args.output) + suffix) for suffix in
             ('.wav', '-pickups.wav', '-backing.wav', '-focus.wav', '.json', '-cues.txt')]
    for path in paths:
        if path.exists():
            raise FileExistsError(f'Output already exists: {path}')
    source = read_pcm(args.source).astype(float) / 32768
    if len(source) < frame(64):
        raise ValueError('Expected the complete Side Street recording')
    pickups = np.zeros_like(source)
    events = []

    def hit(bar, step, midi, velocity, pan=0., voice='hand drum'):
        onset = frame(bar, step)
        seed = 3100 + len(events)
        sound = hand_drum(midi, velocity, pan, seed) if voice == 'hand drum' else rim(velocity, pan, seed)
        count = min(len(sound), len(pickups) - onset)
        pickups[onset:onset + count] += sound[:count]
        events.append({'bar': bar + 1, 'step': step, 'seconds': onset / RATE,
                       'voice': voice, 'midi': midi if voice == 'hand drum' else None,
                       'velocity': velocity, 'pan': pan, 'seed': seed})

    # Each small fill has a direction; none becomes a repeating drum solo.
    for bar, pattern in (
        (15, ((14, 53, .63), (15.12, 56, .78))),
        (23, ((13.12, 60, .66), (15.12, 56, .79))),
        (31, ((12, 60, .80), (13.67, 56, .68), (14.7, 53, .85))),
        (39, ((10, 56, .65), (11.33, 55, .73), (12.67, 53, .84))),
        (47, ((14, 56, .63), (15.12, 60, .83))),
        (55, ((12, 60, .71), (14, 56, .62), (15.12, 53, .74))),
    ):
        for index, (step, midi, velocity) in enumerate(pattern):
            hit(bar, step, midi, velocity, -.13 if index % 2 == 0 else .13)
    for bar in list(range(17, 32, 4)) + list(range(41, 56, 4)):
        hit(bar, 10.12, 0, .70, -.14, 'rim')
        if bar < 30 or bar < 54 and bar >= 40:
            hit(bar + 2, 3.12, 0, .53, .14, 'rim')

    # A very close room gives the hand strokes a little depth, without an echo.
    dry = pickups.copy()
    for seconds, level, channel in ((.013, .055, 0), (.019, .050, 1),
                                   (.031, .025, 0), (.043, .021, 1)):
        delay = round(seconds * RATE)
        pickups[delay:, channel] += level * dry[:-delay, 1 - channel]
    pause, return_at = frame(40, -2), frame(40)
    ramp = np.linspace(1, 0, round(.012 * RATE))[:, None]
    pickups[pause - len(ramp):pause] *= ramp
    pickups[pause:return_at] = 0
    mix = source + pickups

    # Hear the transition to the middle, then the transition into the finale.
    parts = []
    focus_cues = []
    elapsed = 0.
    for first_bar, last_bar, label in ((28, 34, 'Into the quiet middle'),
                                       (38, 42, 'Into the return'),
                                       (46, 50, 'Into the final refrain')):
        clip = mix[frame(first_bar):frame(last_bar)].copy()
        clip[:240] *= np.linspace(0, 1, 240)[:, None]
        clip[-2400:] *= np.linspace(1, 0, 2400)[:, None]
        focus_cues.append({'seconds': elapsed, 'label': label})
        parts.extend((clip, np.zeros((round(.65 * RATE), 2))))
        elapsed += len(clip) / RATE + .65
    focus = np.concatenate(parts[:-1])
    args.output.parent.mkdir(parents=True, exist_ok=True)
    for path, samples in zip(paths[:4], (mix, pickups, source, focus)):
        write_wav(path, samples)
    report = {
        'title': 'Side Street — percussion transitions', 'bpm': BPM,
        'source_sha256': sha(args.source), 'frames': len(mix),
        'duration_seconds': len(mix) / RATE, 'events': events,
        'focus_seconds': len(focus) / RATE, 'focus_cues': focus_cues,
        'mix_peak_dbfs': float(20 * np.log10(np.max(np.abs(mix)))),
        'integrated_lufs': loudness(paths[0]),
        'output_sha256': {path.name: sha(path) for path in paths[:4]},
    }
    with paths[4].open('x') as out:
        out.write(json.dumps(report, indent=2) + '\n')
    with paths[5].open('x') as out:
        out.write('Side Street v7 — restrained percussion pickups\n116 BPM / F minor\n\n')
        for bar in (15, 23, 31, 39, 47, 55):
            out.write(f'{frame(bar) / RATE:.2f}s  Transition pickup, bar {bar + 1}\n')
    print(f"Wrote {paths[0]}: peak {report['mix_peak_dbfs']:.2f} dBFS, "
          f"{report['integrated_lufs']:.1f} LUFS; focus {len(focus) / RATE:.2f}s")


if __name__ == '__main__':
    main()
