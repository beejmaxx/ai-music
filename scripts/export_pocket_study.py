#!/usr/bin/env python3
"""Package a Pocket Trio take as an exact three-stem multitrack."""
import argparse
import json
import shutil
import subprocess
from pathlib import Path

import numpy as np

from piano_phrasing import read_pcm
from piano_study import RATE, sha, write_wav


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path, help='Take prefix without extension')
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    if args.output.exists():
        raise FileExistsError(f'Output already exists: {args.output}')
    prefix = str(args.source)
    mix = read_pcm(Path(prefix + '.wav'))
    parts = {name: read_pcm(Path(prefix + '-' + name + '.wav')) for name in ('bass', 'kick', 'plucks')}
    if any(part.shape != mix.shape for part in parts.values()):
        raise ValueError('Pocket Trio stems must span the complete mix')
    residual = mix - sum(parts.values())
    error = int(np.max(np.abs(residual)))
    if error > 2:
        raise ValueError(f'Unexpected reconstruction error: {error} PCM units')
    parts['plucks'] += residual
    args.output.mkdir(parents=True)
    for name, samples in parts.items():
        write_wav(args.output / f'stem-{name}.wav', samples.astype(float) / 32768)
    shutil.copy2(prefix + '.wav', args.output / 'mix.wav')
    shutil.copy2(prefix + '-cues.txt', args.output / 'cues.txt')
    shutil.copy2(prefix + '.json', args.output / 'score.json')
    subprocess.run(['afconvert', '-f', 'm4af', '-d', 'aac', '-b', '256000', '-q', '127',
                    str(args.output / 'mix.wav'), str(args.output / 'mix.m4a')], check=True)
    delivered = sum(read_pcm(path) for path in sorted(args.output.glob('stem-*.wav')))
    if not np.array_equal(delivered, mix):
        raise ValueError('Saved Pocket Trio stems do not recreate the mix exactly')
    manifest = {'title': 'Pocket Trio', 'version': args.source.name,
                'frames': len(mix), 'seconds': len(mix) / RATE, 'bpm': 116, 'key': 'E Dorian',
                'stem_alignment': 'All three stems start at sample zero and include the full tail.',
                'unity_sum_exact_pcm': True, 'rounding_residue_peak_pcm': error,
                'saved_without_playback': True, 'source_mix_sha256': sha(Path(prefix + '.wav')),
                'files': {p.name: sha(p) for p in sorted(args.output.iterdir())}}
    with (args.output / 'manifest.json').open('x') as out:
        out.write(json.dumps(manifest, indent=2) + '\n')
    print(f'Pocket Trio: {len(mix) / RATE:.2f}s, three aligned stems, exact unity sum, saved silently.')


if __name__ == '__main__':
    main()
