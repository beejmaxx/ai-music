#!/usr/bin/env python3
"""Save the silent composing session as three complete, aligned multitracks."""
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
    parser.add_argument('output', type=Path)
    parser.add_argument('--recordings', type=Path, default=Path('recordings'))
    args = parser.parse_args()
    if args.output.exists():
        raise FileExistsError(f'Output already exists: {args.output}')
    args.output.mkdir(parents=True)
    recordings = args.recordings
    catalog = []
    read = lambda name: read_pcm(recordings / (name + '.wav')).astype(float) / 32768

    def save(slug, title, version, bpm, key, stems, residue_bus, description):
        destination = args.output / slug
        destination.mkdir()
        original = recordings / (version + '.wav')
        mix = read_pcm(original)
        pcm = {name: np.rint(samples * 32768).astype(np.int32) for name, samples in stems.items()}
        if any(part.shape != mix.shape for part in pcm.values()):
            raise ValueError(f'{title}: a stem does not span the complete arrangement')
        residual = mix - sum(pcm.values())
        error = int(np.max(np.abs(residual)))
        if error > 8:
            raise ValueError(f'{title}: unexpected reconstruction error ({error} PCM units)')
        # Earlier revisions were saved as PCM16; retain the exact mix and fold
        # only this tiny accumulated rounding residue into the chord bus.
        pcm[residue_bus] += residual
        for name, samples in pcm.items():
            write_wav(destination / ('stem-' + name + '.wav'), samples.astype(float) / 32768)
        shutil.copy2(original, destination / 'mix.wav')
        compressed = recordings / (version + '.m4a')
        if compressed.exists():
            shutil.copy2(compressed, destination / 'mix.m4a')
        else:
            subprocess.run(['afconvert', '-f', 'm4af', '-d', 'aac', '-b', '256000', '-q', '127',
                            str(destination / 'mix.wav'), str(destination / 'mix.m4a')], check=True)
        shutil.copy2(recordings / (version + '-cues.txt'), destination / 'cues.txt')
        # Verify the files a listener/editor will actually receive, not only
        # the floating-point arrays used while composing.
        delivered = sum(read_pcm(path) for path in sorted(destination.glob('stem-*.wav')))
        if not np.array_equal(delivered, read_pcm(destination / 'mix.wav')):
            raise ValueError(f'{title}: saved stems do not reconstruct the mix exactly')
        manifest = {
            'title': title, 'version': version, 'bpm': bpm, 'key': key,
            'seconds': len(mix) / RATE, 'frames': len(mix), 'sample_rate': RATE,
            'description': description, 'saved_without_playback': True,
            'stem_alignment': 'Every stem starts at sample zero and includes the complete tail.',
            'unity_sum_exact_pcm': True, 'rounding_residue_peak_pcm': error,
            'rounding_residue_bus': residue_bus, 'source_mix_sha256': sha(original),
            'files': {path.name: sha(path) for path in sorted(destination.iterdir()) if path.is_file()},
        }
        with (destination / 'manifest.json').open('x') as out:
            out.write(json.dumps(manifest, indent=2) + '\n')
        catalog.append({field: manifest[field] for field in
                        ('title', 'version', 'bpm', 'key', 'seconds', 'description')})
        catalog[-1]['directory'] = slug
        print(f'{title}: {len(pcm)} aligned stems, exact sum, {len(mix) / RATE:.2f}s', flush=True)

    save('amber-steps', 'Amber Steps', 'amber-steps-v2', 120, 'G major', {
        'bass': read('amber-steps-v1-native-bass'),
        'kick': read('amber-steps-v1-native-kick'),
        'percussion': read('amber-steps-v1-native-percussion'),
        'strings': read('amber-steps-v2-strings'),
        'plucks': read('amber-steps-v2-plucks'),
        'piano-chords': read('amber-steps-v1-piano'),
        'piano-melody': read('amber-steps-v2-melody'),
    }, 'plucks', 'Plucked chord groove, string answers, a quiet piano middle and a closing piano melody.')

    middle = json.loads((recordings / 'last-tram-v3.json').read_text())
    insert, length, seam = (middle[k] for k in ('insertion_frame', 'inserted_frames', 'seam_frames'))

    def extend(samples, middle_name):
        before, after = samples[:insert].copy(), samples[insert:].copy()
        before[-seam:] *= np.linspace(1, 0, seam)[:, None]
        after[:seam] *= np.linspace(0, 1, seam)[:, None]
        passage = read('last-tram-v3-middle-' + middle_name)
        if len(passage) != length:
            raise ValueError('Inserted Last Tram stem length differs from its score')
        return np.concatenate((before, passage, after))

    save('last-tram', 'Last Tram', 'last-tram-v4', 106, 'C minor', {
        'bass': extend(read('last-tram-v1-native-bass') + read('last-tram-v2-turn-bass'), 'bass'),
        'kick': extend(read('last-tram-v1-native-kick') + read('last-tram-v2-turn-kick'), 'kick'),
        'percussion': extend(read('last-tram-v1-native-percussion'), 'percussion'),
        'strings': extend(read('last-tram-v1-native-strings'), 'strings'),
        'organ-chords': extend(read('last-tram-v1-organ') + read('last-tram-v2-turn-organ'), 'organ'),
        'organ-melody': extend(read('last-tram-v1-organ-lead'), 'lead'),
        'hand-drums': read('last-tram-v4-hand-drums'),
    }, 'organ-chords', 'Broken kick, warm organ and strings; relative-major detour, connected return and hand-drum replies.')

    save('low-glow-plucked', 'Low Glow — plucked rhythm', 'low-glow-pluck-001', 122, 'A minor', {
        'bass': read('low-glow-base-v1-bass'),
        'kick': read('low-glow-base-v1-kick'),
        'percussion': read('low-glow-base-v1-percussion'),
        'strings': read('low-glow-base-v1-strings'),
        'rhythm': read('low-glow-pluck-001-rhythm'),
        'piano-melody': read('low-glow-v2-melody'),
        'hand-drums': read('low-glow-v3-pickups'),
    }, 'rhythm', 'Alternate plucked rhythm instrument; the sustained piano middle, melody and bass spotlight remain.')

    with (args.output / 'tracks.json').open('x') as out:
        out.write(json.dumps(catalog, indent=2) + '\n')
    with (args.output / 'Session.m3u8').open('x') as out:
        out.write('#EXTM3U\n' + ''.join(f"#EXTINF:{round(track['seconds'])},{track['title']}\n"
                                       f"{track['directory']}/mix.m4a\n" for track in catalog))
    with (args.output / 'READ-ME.txt').open('x') as out:
        out.write('Silent composing session — 5 October 2026, 02:02–03:02 Shanghai\n\n')
        for track in catalog:
            out.write(f"{track['title']} / {track['bpm']} BPM / {track['key']} / {track['seconds']:.2f}s\n"
                      f"{track['description']}\n\n")
        out.write('These are saved working takes. They were not played during this session.\n'
                  'Open Session.m3u8 or a mix.m4a when you want to listen. Nothing autoplays.\n'
                  'Each folder also has an uncompressed mix.wav and seven complete stems.\n'
                  'All stems are stereo 48 kHz PCM16, start at zero, and sum at unity gain\n'
                  'to the exact saved mix. Effects and fades are already included.\n\n'
                  'The first-hour session and all earlier takes remain untouched.\n'
                  'This package and all source work remain local.\n')


if __name__ == '__main__':
    main()
