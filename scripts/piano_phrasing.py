#!/usr/bin/env python3
"""Compare piano timing, then develop both phrases over Side Street (NumPy + ffmpeg)."""
import argparse
import json
from pathlib import Path
import wave

import numpy as np

from compare_hooks import loudness
from piano_study import BPM, GAP, RATE, START, STEP, sha, write_wav


def read_pcm(path):
    with wave.open(str(path),'rb') as source:
        if (source.getnchannels(),source.getsampwidth(),source.getframerate()) != (2,2,RATE):
            raise ValueError(f'Expected stereo 48 kHz PCM16: {path}')
        frames = source.getnframes()
        samples = np.frombuffer(source.readframes(frames),dtype='<i2').reshape(-1,2).astype(np.int32)
    if len(samples) != frames:
        raise ValueError(f'Truncated recording: {path}')
    return samples


def offset(bar, step=0):
    return round(((16+bar)*16+step)*STEP*RATE)-START


def arrange_full(args, paths, piano, settled_piano, original):
    source = read_pcm(args.backing)
    if len(source) < round(64*16*STEP*RATE):
        raise ValueError('Expected the complete 64-bar Side Street recording')
    backing = source.astype(float)/32768
    lead = np.zeros_like(backing)
    arrangement = ((16,'loose',1.0),(18,'settled',1.0),(20,'loose',.82),
                   (24,'loose',.94),(26,'settled',1.08),
                   (44,'loose',.96),(46,'settled',1.04),
                   (48,'loose',1.0),(50,'settled',1.04),
                   (52,'loose',.90),(54,'settled',1.02),
                   (56,'loose',.66),(58,'settled',.56))
    events = []
    for bar,style,gain in arrangement:
        start = round(bar*16*STEP*RATE)
        phrase = (piano if style == 'loose' else settled_piano)[:offset(2)]
        lead[start:start+len(phrase)] += phrase/32768*gain
        events.append({'bar':bar+1,'style':style,'gain':gain,'phrase_start_seconds':start/RATE,
                       'first_piano_seconds':start/RATE+original['events'][0]['seconds']})
    mix = backing+lead
    args.output.parent.mkdir(parents=True,exist_ok=True)
    for path,samples in zip(paths[:3],(mix,lead,backing)):
        write_wav(path,samples)
    report = {
        'description':'Side Street with a sparse electric-piano question-and-answer arrangement.',
        'bpm':BPM,'sample_rate':RATE,'frames':len(mix),'duration_seconds':len(mix)/RATE,
        'source_sha256':sha(args.backing),'study_report_sha256':sha(Path(str(args.study)+'.json')),
        'piano_first_note_seconds':events[0]['first_piano_seconds'],
        'mix_peak_dbfs':float(20*np.log10(np.max(np.abs(mix)))),
        'mix_lufs':loudness(paths[0]),'source_lufs':loudness(args.backing),
        'phrases':events,'output_sha256':{path.name:sha(path) for path in paths[:3]},
    }
    with paths[3].open('x') as out:
        out.write(json.dumps(report,indent=2)+'\n')
    with paths[4].open('x') as out:
        out.write('Side Street — piano arrangement\n116 BPM / F minor\nExisting track plus a synthesized piano stem.\n\n')
        for event in events:
            out.write(f"{event['first_piano_seconds']:.2f}s  {event['style']} piano phrase, bar {event['bar']}\n")
    print(f"Wrote {paths[0]}: {len(mix)/RATE:.2f}s; piano enters {report['piano_first_note_seconds']:.2f}s; peak {report['mix_peak_dbfs']:.2f} dBFS")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('backing',type=Path,help='Full Side Street WAV used by the piano study')
    parser.add_argument('study',type=Path,help='Prefix of the original piano_study.py output')
    parser.add_argument('output',type=Path,help='Fresh prefix for timing clips, development and audition')
    parser.add_argument('--full-track',action='store_true',help='Arrange the piano throughout the complete source track instead of exporting the short audition')
    args = parser.parse_args()
    suffixes = ('-loose.wav','-settled.wav','-comparison.wav','-developed.wav',
                '-developed-piano.wav','-developed-backing.wav','.wav','.json','-cues.txt')
    if args.full_track:
        suffixes = ('.wav','-piano.wav','-backing.wav','.json','-cues.txt')
    paths = [Path(str(args.output)+suffix) for suffix in suffixes]
    for path in paths:
        if path.exists():
            raise FileExistsError(f'Output already exists: {path}')
    original = json.loads(Path(str(args.study)+'.json').read_text())
    if sha(args.backing) != original['source_sha256']:
        raise ValueError('Backing must match the source used for the original piano study')
    inputs = [Path(str(args.study)+suffix) for suffix in ('-a.wav','-b.wav','-piano.wav')]
    for path in inputs:
        if sha(path) != original['output_sha256'][path.name]:
            raise ValueError(f'Original study artifact changed: {path}')
    backing,loose,piano = map(read_pcm,inputs)
    frames = offset(4)
    if not all(samples.shape == (frames,2) for samples in (backing,loose,piano)):
        raise ValueError('Expected the original four-bar piano study')
    if np.max(np.abs(loose-backing-piano)) > 1:
        raise ValueError('Piano stem does not reconstruct the original study')
    settled_piano = piano.copy()
    moves = []
    # Move the complete last struck note and its reflections. Its sound, gain,
    # envelope and stereo waveform remain exactly the same for the comparison.
    for index,bar in ((2,1),(5,3)):
        event = original['events'][index]
        if event['midi'] != 65:
            raise ValueError('Expected the original C/E-flat/F motif')
        start = round(event['seconds']*RATE)
        end = offset(bar+1)
        target = offset(bar,8)
        shift = target-start
        last = np.flatnonzero(np.any(piano[start:end] != 0,axis=1))[-1]+start+1
        if shift <= 0 or last+shift > end:
            raise ValueError('Final note cannot be moved cleanly into beat three')
        note = piano[start:last].copy()
        settled_piano[start:last] = 0
        settled_piano[target:target+len(note)] += note
        moves.append({'midi':65,'old_frame':start,'new_frame':target,'frames':len(note),
                      'shift_ms':shift/RATE*1000,'waveform_unchanged':True})
    settled = backing+settled_piano
    if args.full_track:
        arrange_full(args,paths,piano,settled_piano,original)
        return
    comparison = np.concatenate((loose,np.zeros((GAP,2),dtype=np.int32),settled))

    source = read_pcm(args.backing)
    full_end = START+offset(16)
    if len(source) < full_end:
        raise ValueError('Expected at least 32 bars of Side Street')
    developed_backing = source[START:full_end].astype(float)/32768
    developed_piano = np.zeros_like(developed_backing)
    # Establish the groove, pair an unsettled phrase with a firm answer, leave
    # a two-bar breath, then bring the pair back with a small lift at the end.
    arrangement = ((4,'loose',1.0),(6,'settled',1.0),(8,'loose',.82),
                   (12,'loose',.94),(14,'settled',1.08))
    phrase_frames = offset(2)
    for bar,style,gain in arrangement:
        phrase = (piano if style == 'loose' else settled_piano)[:phrase_frames]
        start = offset(bar)
        developed_piano[start:start+len(phrase)] += phrase/32768*gain
    fade = np.ones(len(developed_backing))
    fade[:240] = np.linspace(0,1,240)
    fade[-3840:] = np.linspace(1,0,3840)
    developed_backing *= fade[:,None]
    developed_piano *= fade[:,None]
    developed = developed_backing+developed_piano
    audition = np.concatenate((comparison/32768,np.zeros((GAP,2)),developed))
    outputs = (loose/32768,settled/32768,comparison/32768,developed,
               developed_piano,developed_backing,audition)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    for path,samples in zip(paths[:7],outputs):
        write_wav(path,samples)
    levels = [loudness(path) for path in paths[:2]]
    if abs(levels[0]-levels[1]) > .3+1e-9:
        raise ValueError('Timing comparison exceeds 0.3 LU; inspect before auditioning')
    development_start = (len(comparison)+GAP)/RATE
    report = {
        'description':'Original and settled piano timing, followed by a short arranged passage using both.',
        'bpm':BPM,'sample_rate':RATE,'source_start_frame':START,'source_sha256':sha(args.backing),
        'study_report_sha256':sha(Path(str(args.study)+'.json')),'moves':moves,
        'comparison_frames':len(comparison),'developed_frames':len(developed),'audition_frames':len(audition),
        'duration_seconds':len(audition)/RATE,'comparison_lufs':levels,
        'comparison_loudness_spread_lu':abs(levels[0]-levels[1]),
        'development_peak_dbfs':float(20*np.log10(np.max(np.abs(developed)))),
        'arrangement':[{'local_bar':bar,'style':style,'gain':gain,'seconds':offset(bar)/RATE}
                       for bar,style,gain in arrangement],
        'cues':[
            {'seconds':0,'label':'A: loose piano answer'},
            {'seconds':(frames+GAP)/RATE,'label':'B: final piano note lands with the kick'},
            {'seconds':development_start,'label':'C: developed passage — groove'},
            {'seconds':development_start+offset(4)/RATE,'label':'Loose question'},
            {'seconds':development_start+offset(6)/RATE,'label':'Settled answer'},
            {'seconds':development_start+offset(8)/RATE,'label':'Quieter question'},
            {'seconds':development_start+offset(10)/RATE,'label':'Space for the strings'},
            {'seconds':development_start+offset(12)/RATE,'label':'Piano returns'},
            {'seconds':development_start+offset(14)/RATE,'label':'Final settled answer'},
        ],
        'output_sha256':{path.name:sha(path) for path in paths[:7]},
    }
    with paths[7].open('x') as out:
        out.write(json.dumps(report,indent=2)+'\n')
    with paths[8].open('x') as out:
        out.write('Side Street — piano phrasing\n116 BPM / F minor\n\n')
        for cue in report['cues']:
            out.write(f"{cue['seconds']:.2f}s  {cue['label']}\n")
    print(f"Wrote {paths[6]}: {len(audition)/RATE:.2f}s; settled version at {(frames+GAP)/RATE:.2f}s; development at {development_start:.2f}s")


if __name__ == '__main__':
    main()
