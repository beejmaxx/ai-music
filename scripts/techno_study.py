#!/usr/bin/env python3
"""Compose Pressure Grid: a local, silent 132 BPM hypnotic techno study."""
import argparse
import json
import subprocess
from pathlib import Path

import numpy as np
from scipy import signal

from compare_hooks import loudness
from piano_phrasing import read_pcm
from piano_study import RATE, sha, write_wav

BPM, BARS = 132, 32
BEAT = 60 / BPM
STEP = BEAT / 4


def frame(bar, position=0):
    return round((bar * 16 + position) * STEP * RATE)


def filter_audio(samples, frequency, kind, order=2):
    return signal.sosfilt(signal.butter(order, frequency, btype=kind, fs=RATE, output='sos'), samples, axis=0)


def metal_hat(velocity, opened, pan, seed):
    t = np.arange(round((.43 if opened else .16) * RATE)) / RATE
    rng = np.random.default_rng(seed)
    frequencies = (3203, 4381, 5663, 6967, 8251, 9709)
    phases = rng.uniform(0, 2 * np.pi, len(frequencies))
    metal = sum(np.sin(2 * np.pi * hz * t + phase) for hz, phase in zip(frequencies, phases)) / 3
    noise = filter_audio(rng.standard_normal(len(t)), 4300, 'highpass')
    sound = filter_audio(.50 * metal + .22 * noise, 12500, 'lowpass')
    sound *= np.exp(-t / (.085 if opened else .021))
    sound *= .5 - .5 * np.cos(np.pi * np.clip(t / .0012, 0, 1))
    sound[-480:] *= np.linspace(1, 0, 480)
    sound *= velocity * 10 ** ((-25.5 if opened else -27.) / 20) / np.max(np.abs(sound))
    return np.column_stack((sound * (1 - pan), sound * (1 + pan)))


def metal_tick(velocity, pan):
    t = np.arange(round(.18 * RATE)) / RATE
    sound = sum(level * np.sin(2 * np.pi * hz * t) * np.exp(-t / decay)
                for hz, level, decay in ((883, .7, .027), (1297, .32, .016), (1849, .19, .009)))
    sound *= .5 - .5 * np.cos(np.pi * np.clip(t / .001, 0, 1))
    sound[-480:] *= np.linspace(1, 0, 480)
    sound *= velocity * 10 ** (-29.5 / 20) / np.max(np.abs(sound))
    return np.column_stack((sound * (1 - pan), sound * (1 + pan)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path, help='New directory for the complete study and stems')
    parser.add_argument('--renderer', type=Path, default=Path('build/music-palette-render'))
    args = parser.parse_args()
    if args.output.exists():
        raise FileExistsError(f'Output already exists: {args.output}')
    args.output.mkdir(parents=True)
    score_path = args.output / 'score.txt'
    native = args.output / 'native'
    score = ['# Pressure Grid — F tonal centre, no chord progression', f'tempo {BPM}',
             f'bars {BARS}', 'tail 3', 'kick_tune 43.6535289', 'echo .26']
    events, kicks = [], []
    accent = (.82, .49, .60, .54, .73, .46, .57, .64)
    pitches = (53, 53, 60, 53, 53, 56, 53, 53)
    for bar in range(BARS + 1):
        cutoff = (640 + bar * 35 if bar < 4 else 820 + (bar - 4) * 48 if bar < 12
                  else 1320 + (bar - 12) * 150 if bar < 16 else 480 + (bar - 16) * 80 if bar < 20
                  else 1850 + (bar - 20) * 100 if bar < 28 else 1400 - (bar - 28) * 170)
        # The native kit is muted; the new percussion has its own phrasing.
        score.append(f'color {bar * 16} 180 {cutoff} 0 0 0')
        if bar == BARS:
            continue
        for beat in range(4):
            if bar == 31 and beat != 0:
                continue
            velocity = .92 if beat == 0 else .87 if beat == 2 else .85
            score.append(f'kick {bar * 16 + beat * 4} {velocity}')
            kicks.append(frame(bar, beat * 4))
        if bar < 4 or bar >= 30:
            continue
        for step in range(16):
            absolute = bar * 16 + step
            if absolute % 3 != 2:
                continue
            cell = (absolute - 2) // 3
            if 16 <= bar < 20 and cell % 2:
                continue
            midi = pitches[cell % len(pitches)]
            velocity = accent[cell % len(accent)] * (.78 if 16 <= bar < 20 else 1)
            gate = .46 if cell % 4 else .69
            score.append(f'strings {absolute} {midi} {gate} {velocity}')
            events.append({'voice': 'synth', 'bar': bar + 1, 'step': step,
                           'midi': midi, 'gate_steps': gate, 'velocity': velocity})
    with score_path.open('x') as out:
        out.write('\n'.join(score) + '\n')
    subprocess.run([str(args.renderer.resolve()), str(score_path), str(native)], check=True)
    kick = read_pcm(Path(str(native) + '-kick.wav')).astype(float) / 32768
    synth = read_pcm(Path(str(native) + '-strings.wav')).astype(float) / 32768
    n = len(kick)
    kick *= 10 ** (-8.3 / 20) / np.max(np.abs(kick))
    synth = filter_audio(synth, 210, 'highpass')
    synth *= 10 ** (-16.8 / 20) / np.max(np.abs(synth))

    # A deterministic diffuse tail derived from this exact kick. Sub frequencies
    # stay mono; filtering and ducking separate the body from each new transient.
    rng = np.random.default_rng(132005)
    ir = np.zeros(round(1.65 * RATE))
    for index, seconds in enumerate(np.arange(.072, 1.60, .0177)):
        at = round((seconds + rng.uniform(-.003, .003)) * RATE)
        ir[at] += (1 if index % 3 else -.64) * np.exp(-seconds / .47) * rng.uniform(.6, 1.)
    for beats, level in ((.5, .70), (.75, .48), (1.25, .31), (1.75, .17)):
        ir[round(beats * BEAT * RATE)] += level
    room = signal.fftconvolve(kick[:, 0], ir)[:n]
    room = filter_audio(filter_audio(room, 29, 'highpass'), 205, 'lowpass', 4)
    room = filter_audio(np.tanh(room * 4.2), 290, 'lowpass', 2)
    last_hit = np.zeros(n, dtype=np.int64)
    last_hit[np.asarray(kicks)] = kicks
    last_hit = np.maximum.accumulate(last_hit)
    age = (np.arange(n) - last_hit) / RATE
    target_duck = 1 - .94 * np.exp(-age / .115)
    coefficient = np.exp(-1 / (.0018 * RATE))
    duck = signal.lfilter([1 - coefficient], [1, -coefficient], target_duck)
    room *= duck
    a, b = frame(8), frame(16)
    room *= min(10 ** (-27 / 20) / np.sqrt(np.mean(room[a:b] ** 2)),
                10 ** (-14.5 / 20) / np.max(np.abs(room)))
    lower = np.ones(n)
    first, last, ramp = frame(16), frame(20), round(.04 * RATE)
    lower[first:last] = .72
    lower[first:first + ramp] = np.linspace(1, .72, ramp)
    lower[last - ramp:last] = np.linspace(.72, 1, ramp)
    room *= lower
    rumble = np.column_stack((room, room))
    hats, ticks = np.zeros_like(kick), np.zeros_like(kick)

    def add(bus, sound, bar, step, label, velocity):
        start = frame(bar, step)
        count = min(len(sound), n - start)
        bus[start:start + count] += sound[:count]
        events.append({'voice': label, 'bar': bar + 1, 'step': step, 'velocity': velocity})

    for bar in range(2, 31):
        if 16 <= bar < 20:
            if bar in (17, 19):
                add(hats, metal_hat(.40, False, -.08, 13250 + bar), bar, 14, 'closed hat', .40)
            continue
        offbeats = (6, 14) if bar < 4 or bar >= 28 else (2, 6, 10, 14)
        for index, step in enumerate(offbeats):
            opened = 12 <= bar < 16 or 20 <= bar < 28
            velocity = (.79 if index % 2 == 0 else .66) * (.76 if bar >= 28 else 1)
            sound = metal_hat(velocity, opened and step in (6, 14), .11 if index % 2 else -.11,
                              132000 + bar * 16 + step)
            add(hats, sound, bar, step, 'hat', velocity)
        if 8 <= bar < 16 or 20 <= bar < 28:
            for step, velocity in ((1.055, .31), (7.055, .39), (11.055, .29), (15.055, .34)):
                sound = metal_hat(velocity, False, -.16, 2300 + bar * 16 + round(step))
                add(hats, sound, bar, step, 'ghost hat', velocity)
        if 12 <= bar < 16 or 24 <= bar < 28:
            for step in range(16):
                if (bar * 16 + step) % 5 == 1:
                    velocity = .73 if step < 8 else .56
                    add(ticks, metal_tick(velocity, .15), bar, step + .035, 'metal tick', velocity)
    # A short, filtered reflection connects the metallic answer to the motif.
    delay = round(3 * STEP * RATE)
    ticks[delay:] += .16 * filter_audio(ticks[:-delay], 3500, 'lowpass')[:, ::-1]
    parts = {'kick': kick, 'rumble': rumble, 'synth': synth, 'hats': hats, 'metal': ticks}
    tail = round(.9 * RATE)
    for bus in parts.values():
        bus[:96] *= np.linspace(0, 1, 96)[:, None]
        bus[-tail:] *= np.linspace(1, 0, tail)[:, None]
    floating_mix = sum(parts.values())
    gain = 10 ** (-2.3 / 20) / np.max(np.abs(floating_mix))
    # Quantize the actual buses first: the saved stems then sum exactly without
    # a residual correction or a hidden limiter on the master.
    pcm = {name: np.rint(bus * gain * 32768).astype(np.int32) for name, bus in parts.items()}
    mix_pcm = sum(pcm.values())
    if np.max(np.abs(mix_pcm)) >= 32112:
        raise ValueError('Insufficient output headroom')
    for name, bus in pcm.items():
        write_wav(args.output / f'stem-{name}.wav', bus.astype(float) / 32768)
    mix_path = args.output / 'mix.wav'
    mix = mix_pcm.astype(float) / 32768
    write_wav(mix_path, mix)
    foundation = (pcm['kick'] + pcm['rumble'] + pcm['hats'] + pcm['metal']).astype(float) / 32768
    write_wav(args.output / 'foundation.wav', foundation)
    focus = mix[frame(20):frame(28)].copy()
    focus[:240] *= np.linspace(0, 1, 240)[:, None]
    focus[-2400:] *= np.linspace(1, 0, 2400)[:, None]
    write_wav(args.output / 'focus.wav', focus)
    subprocess.run(['afconvert', '-f', 'm4af', '-d', 'aac', '-b', '256000', '-q', '127',
                    str(mix_path), str(args.output / 'mix.m4a')], check=True)
    report = {
        'title': 'Pressure Grid — techno study 001', 'direction': 'Hypnotic, groove-led techno',
        'bpm': BPM, 'bars': BARS, 'tonal_centre': 'F', 'sample_rate': RATE,
        'frames': n, 'seconds': n / RATE, 'saved_without_playback': True,
        'listener_approved': False, 'native_score_sha256': sha(score_path),
        'synth_cycle_steps': 3, 'metal_cycle_steps': 5, 'events': events,
        'rumble_source': 'Convolution of the original kick with a deterministic diffuse impulse, filtered and ducked.',
        'master_gain_db': float(20 * np.log10(gain)),
        'mix_peak_dbfs': float(20 * np.log10(np.max(np.abs(mix)))),
        'integrated_lufs': loudness(mix_path), 'stems_sum_exact_pcm': True,
        'files': {p.name: sha(p) for p in sorted(args.output.glob('*.wav'))},
    }
    with (args.output / 'manifest.json').open('x') as out:
        out.write(json.dumps(report, indent=2) + '\n')
    with (args.output / 'cues.txt').open('x') as out:
        out.write('Pressure Grid — first techno direction study\n132 BPM / F tonal centre / 32 bars\n\n')
        for bar, label in ((0, 'Kick and low texture'), (4, 'Three-step synth cell'),
                           (8, 'Quiet hat detail'), (12, 'Metallic counter-rhythm'),
                           (16, 'Closed filter and reduced percussion'), (20, 'Open return'),
                           (24, 'Counter-rhythm returns'), (28, 'Peel away'), (31, 'Last pulse')):
            out.write(f'{frame(bar) / RATE:.2f}s  {label}\n')
    print(f"Saved {mix_path} silently: {n / RATE:.2f}s, {report['integrated_lufs']:.1f} LUFS, "
          f"peak {report['mix_peak_dbfs']:.2f} dBFS; five aligned stems.")


if __name__ == '__main__':
    main()
