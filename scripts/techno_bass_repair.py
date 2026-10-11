#!/usr/bin/env python3
"""Replace Pressure Grid's diffuse rumble with clean, short offbeat bass pulses."""
import argparse
import json
import shutil
import subprocess
from pathlib import Path

import numpy as np

from compare_hooks import loudness
from piano_phrasing import read_pcm
from piano_study import RATE, sha, write_wav


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path, help='Pressure Grid study001 directory')
    parser.add_argument('output', type=Path, help='New directory; earlier takes are preserved')
    args = parser.parse_args()
    if args.output.exists():
        raise FileExistsError(f'Output already exists: {args.output}')
    original = json.loads((args.source / 'manifest.json').read_text())
    bpm, bars, n = original['bpm'], original['bars'], original['frames']
    if (bpm, bars, original['tonal_centre']) != (132, 32, 'F'):
        raise ValueError('Expected the first 132 BPM F-centred Pressure Grid study')
    beat = 60 / bpm
    frame = lambda bar, position=0: round((bar * 4 + position) * beat * RATE)
    parts = {name: read_pcm(args.source / f'stem-{name}.wav') for name in ('kick', 'synth', 'hats', 'metal')}
    old_bass = read_pcm(args.source / 'stem-rumble.wav').astype(float) / 32768
    old_mix = read_pcm(args.source / 'mix.wav')
    if any(bus.shape != (n, 2) for bus in [*parts.values(), old_bass, old_mix]):
        raise ValueError('Source stems must cover the complete mix')
    if not np.array_equal(sum(parts.values()) + np.rint(old_bass * 32768).astype(np.int32), old_mix):
        raise ValueError('Source stems no longer reproduce their mix')

    bass = np.zeros((n, 2))
    frequency = 43.6535289
    duration = .43 * beat
    t = np.arange(round(duration * RATE)) / RATE
    phase = 2 * np.pi * frequency * t
    # A stable fundamental and a quiet octave define the pitch. No convolution,
    # saturation, delayed copies, pitch sweep or random phase in this bass voice.
    pulse = np.sin(phase) + .18 * np.sin(2 * phase) + .025 * np.sin(3 * phase)
    envelope = .5 - .5 * np.cos(np.pi * np.clip(t / .011, 0, 1))
    envelope *= np.exp(-t / .26)
    release = .052
    envelope *= .5 + .5 * np.cos(np.pi * np.clip((t - (duration - release)) / release, 0, 1))
    pulse *= envelope
    pulse[-1] = 0
    events = []
    for bar in range(bars):
        for pulse_in_bar in range(4):
            if bar == 31 and pulse_in_bar != 0:
                continue
            onset = frame(bar, pulse_in_bar + .5)
            velocity = (.92, .84, .88, .83)[pulse_in_bar]
            if 16 <= bar < 20:
                velocity *= .72
            if bar >= 28:
                velocity *= .86
            sound = pulse * velocity
            count = min(len(sound), n - onset)
            bass[onset:onset + count] += sound[:count, None]
            events.append({'bar': bar + 1, 'beat': pulse_in_bar + 1.5,
                           'first_frame': onset, 'last_frame_exclusive': onset + count,
                           'frequency_hz': frequency, 'velocity': velocity})
    a, b = frame(8), frame(16)
    old_rms = np.sqrt(np.mean(old_bass[a:b] ** 2))
    gain = min(old_rms / np.sqrt(np.mean(bass[a:b] ** 2)),
               10 ** (-10.5 / 20) / np.max(np.abs(bass)))
    bass *= gain
    parts['bass'] = np.rint(bass * 32768).astype(np.int32)
    mix_pcm = sum(parts.values())
    if np.max(np.abs(mix_pcm)) >= 32112:
        raise ValueError('Repaired mix needs more headroom')
    args.output.mkdir(parents=True)
    for name in ('kick', 'synth', 'hats', 'metal'):
        # Keep the actual files byte-identical, not merely the note patterns.
        shutil.copy2(args.source / f'stem-{name}.wav', args.output / f'stem-{name}.wav')
    write_wav(args.output / 'stem-bass.wav', parts['bass'].astype(float) / 32768)
    mix = mix_pcm.astype(float) / 32768
    write_wav(args.output / 'mix.wav', mix)
    low = (parts['kick'] + parts['bass']).astype(float) / 32768
    write_wav(args.output / 'kick-and-bass.wav', low)
    foundation = (parts['kick'] + parts['bass'] + parts['hats'] + parts['metal']).astype(float) / 32768
    write_wav(args.output / 'foundation.wav', foundation)
    for label, samples in (('focus', mix), ('bass-focus', low)):
        clip = samples[frame(8):frame(12)].copy()
        clip[:240] *= np.linspace(0, 1, 240)[:, None]
        clip[-2400:] *= np.linspace(1, 0, 2400)[:, None]
        write_wav(args.output / f'{label}.wav', clip)
    subprocess.run(['afconvert', '-f', 'm4af', '-d', 'aac', '-b', '256000', '-q', '127',
                    str(args.output / 'mix.wav'), str(args.output / 'mix.m4a')], check=True)
    notes = {
        'title': 'Pressure Grid — clean bass revision 002', 'bpm': bpm, 'bars': bars,
        'tonal_centre': 'F', 'sample_rate': RATE, 'frames': n, 'seconds': n / RATE,
        'source_directory': str(args.source), 'source_mix_sha256': sha(args.source / 'mix.wav'),
        'feedback': 'The bass is garbled.',
        'change': 'Replace the diffuse, saturated kick-derived rumble with short F1 offbeat pulses.',
        'unchanged': ['kick', 'synth', 'hats', 'metal', 'arrangement', 'master gain'],
        'bass_frequency_hz': frequency, 'bass_onset_after_kick_seconds': .5 * beat,
        'bass_duration_seconds': duration, 'gap_before_next_kick_seconds': .07 * beat,
        'bass_gain': float(gain), 'old_bass_groove_rms_dbfs': float(20 * np.log10(old_rms)),
        'new_bass_groove_rms_dbfs': float(20 * np.log10(np.sqrt(np.mean(bass[a:b] ** 2)))),
        'bass_events': events,
        'mix_peak_dbfs': float(20 * np.log10(np.max(np.abs(mix)))),
        'integrated_lufs': loudness(args.output / 'mix.wav'),
        'saved_without_playback': True, 'listener_approved': False,
        'files': {path.name: sha(path) for path in sorted(args.output.glob('*.wav'))},
    }
    with (args.output / 'manifest.json').open('x') as out:
        out.write(json.dumps(notes, indent=2) + '\n')
    shutil.copy2(args.source / 'cues.txt', args.output / 'cues.txt')
    with (args.output / 'READ-ME.txt').open('x') as out:
        out.write('Pressure Grid — bass revision 002\n132 BPM / F / 61.18 seconds\n\n'
                  'The diffuse, saturated kick-reverb bass from study001 is replaced by\n'
                  'short pitched F1 pulses with a quiet octave harmonic. Each pulse starts\n'
                  'half a beat after the kick and ends before the next kick. No bass reverb\n'
                  'or saturation is used. Kick, synth, hats and metal are byte-identical\n'
                  'to study001; their levels and the arrangement are unchanged.\n\n'
                  'mix.m4a / mix.wav: full revision. Five aligned stem files recreate it\n'
                  'exactly at unity gain. bass-focus.wav isolates a short kick/bass passage.\n'
                  'This revision was saved without playback and awaits listener feedback.\n')
    print(f"Saved {args.output / 'mix.wav'} silently: {notes['integrated_lufs']:.1f} LUFS, "
          f"peak {notes['mix_peak_dbfs']:.2f} dBFS; other four stems unchanged.")


if __name__ == '__main__':
    main()
