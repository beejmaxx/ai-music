#!/usr/bin/env python3
"""Make a silent, level-matched preview reel from the saved second session."""
import argparse
import json
import subprocess
import tempfile
from pathlib import Path

import numpy as np

from compare_hooks import loudness
from piano_phrasing import read_pcm
from piano_study import RATE, sha, write_wav


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('session', type=Path)
    args = parser.parse_args()
    destination = args.session / 'preview'
    if destination.exists():
        raise FileExistsError(f'Output already exists: {destination}')
    excerpts = [
        ('amber-steps', 120, 16, 24, 'Amber Steps — plucks and strings'),
        ('amber-steps', 120, 48, 56, 'Amber Steps — closing piano phrase'),
        ('last-tram', 106, 8, 16, 'Last Tram — broken kick and strings'),
        ('last-tram', 106, 24, 32, 'Last Tram — relative-major detour'),
        ('low-glow-plucked', 122, 8, 20, 'Low Glow — plucked rhythm'),
    ]
    clips, reports = [], []
    with tempfile.TemporaryDirectory(prefix='music-reel-') as scratch:
        for index, (slug, bpm, a, b, label) in enumerate(excerpts):
            source = args.session / slug / 'mix.wav'
            first, last = [round(bar * 4 * 60 / bpm * RATE) for bar in (a, b)]
            clip = read_pcm(source)[first:last].astype(float) / 32768
            clip[:384] *= np.linspace(0, 1, 384)[:, None]
            clip[-2400:] *= np.linspace(1, 0, 2400)[:, None]
            temporary = Path(scratch) / f'{index}.wav'
            write_wav(temporary, clip)
            measured = loudness(temporary)
            clips.append(clip)
            reports.append({'label': label, 'source': str(source), 'source_sha256': sha(source),
                            'source_bars_zero_based': [a, b], 'source_first_frame': first,
                            'source_last_frame': last, 'input_lufs': measured,
                            'input_peak_dbfs': float(20 * np.log10(np.max(np.abs(clip))))})
    target = min(-22., *(r['input_lufs'] - 1 - r['input_peak_dbfs'] for r in reports))
    destination.mkdir()
    reel, elapsed = [], 0.
    for index, (clip, report) in enumerate(zip(clips, reports)):
        gain_db = target - report['input_lufs']
        clip *= 10 ** (gain_db / 20)
        path = destination / f'{index + 1:02d}.wav'
        write_wav(path, clip)
        measured = loudness(path)
        if abs(measured - target) > .2:
            raise ValueError('Preview excerpt did not reach its common loudness target')
        report.update(start_seconds=elapsed, duration_seconds=len(clip) / RATE,
                      gain_db=gain_db, output_lufs=measured, file=path.name,
                      output_sha256=sha(path))
        reel.append(clip)
        elapsed += len(clip) / RATE
        if index < len(clips) - 1:
            reel.append(np.zeros((round(.8 * RATE), 2)))
            elapsed += .8
    full = np.concatenate(reel)
    output = destination / 'preview-reel.wav'
    write_wav(output, full)
    subprocess.run(['afconvert', '-f', 'm4af', '-d', 'aac', '-b', '256000', '-q', '127',
                    str(output), str(destination / 'preview-reel.m4a')], check=True)
    with (destination / 'cues.txt').open('x') as out:
        out.write('Saved preview reel — no playback was started\n\n')
        for report in reports:
            out.write(f"{report['start_seconds']:.2f}s  {report['label']}\n")
        out.write(f'\nExcerpts matched to {target:.1f} LUFS; full mixes remain at their original levels.\n')
    with (destination / 'manifest.json').open('x') as out:
        out.write(json.dumps({'saved_without_playback': True, 'target_lufs': target,
                              'duration_seconds': len(full) / RATE, 'excerpts': reports,
                              'output_sha256': sha(output)}, indent=2) + '\n')
    print(f'Saved a {len(full) / RATE:.2f}s preview reel silently; all excerpts at {target:.1f} LUFS.')


if __name__ == '__main__':
    main()
