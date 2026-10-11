#!/usr/bin/env python3
"""Give Side Street's piano a melodic peak and a resolved ending (local render)."""
import argparse
import json
from pathlib import Path

import numpy as np

from compare_hooks import loudness
from piano_phrasing import read_pcm
from piano_study import RATE, BPM, STEP, electric_piano, sha, write_wav


def frame(bar, step=0):
    return round((bar*16+step)*STEP*RATE)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source',type=Path,help='Side Street v4 mix')
    parser.add_argument('old_piano',type=Path,help='Piano stem from Side Street v3')
    parser.add_argument('study',type=Path,help='Original piano study JSON, for its established level')
    parser.add_argument('output',type=Path,help='Fresh output prefix')
    args = parser.parse_args()
    suffixes = ('.wav','-piano.wav','-backing.wav','-focus.wav','.json','-cues.txt')
    paths = [Path(str(args.output)+s) for s in suffixes]
    for path in paths:
        if path.exists(): raise FileExistsError(f'Output already exists: {path}')
    source = read_pcm(args.source).astype(float)/32768
    previous = read_pcm(args.old_piano).astype(float)/32768
    if source.shape != previous.shape or len(source) < frame(64):
        raise ValueError('Expected matching full-length Side Street mix and piano stem')
    reference = json.loads(args.study.read_text())
    gain = reference['piano_gain']
    backing = source.copy()
    backing[frame(48):] -= previous[frame(48):]
    piano = np.zeros_like(backing)
    events = []

    def note(bar,step,midi,length,velocity,phrase_gain=1.,soft=False):
        swing = .10 if step%2 else 0
        start = frame(bar,step+swing)
        # Keep the same tremolo/pan phase within each two-bar gesture.
        gesture_time = ((bar%2)*16+step+swing)*STEP
        sound = electric_piano(midi,length*STEP,velocity,gesture_time)
        if soft:
            t = np.arange(len(sound))/RATE
            sound *= (.5-.5*np.cos(np.pi*np.clip(t/.018,0,1)))[:,None]
        count = min(len(sound),len(piano)-start)
        piano[start:start+count] += sound[:count]*phrase_gain
        events.append({'seconds':start/RATE,'bar':bar+1,'step':step+swing,'midi':midi,
                       'gate_seconds':length*STEP,'velocity':velocity,'phrase_gain':phrase_gain})

    # Familiar question, higher answer, then a return to the tonic.
    phrases = ((48,(60,63,65),7,1.0),(50,(60,63,68),8,1.04),
               (52,(60,63,65),7,.90),(54,(68,67,65),8,1.02),
               (56,(60,63,65),7,.64),(58,(63,60,53),8,.54))
    for bar,pitches,last_step,level in phrases:
        note(bar,14,pitches[0],1.05,.67,level)
        note(bar+1,0,pitches[1],.75 if bar==54 else 1.45,.78,level)
        note(bar+1,last_step,pitches[2],1.10 if bar<58 else 2.5,.88,level)
    # A held, low F-minor-seven voicing lands with the song's existing last hit.
    for midi,velocity in ((53,.32),(56,.27),(60,.25),(63,.20)):
        note(63,0,midi,20,velocity,.64,soft=True)
    dry = piano.copy()
    for seconds,level,channel in ((.019,.065,0),(.027,.060,1),(.043,.035,0),(.051,.030,1)):
        delay = round(seconds*RATE)
        piano[delay:,channel] += level*dry[:-delay,1-channel]
    piano *= gain
    # The exported stem remains complete: preserve its earlier phrasing exactly.
    piano[:frame(48)] = previous[:frame(48)]
    backing[:frame(48)] -= previous[:frame(48)]
    mix = backing+piano
    first = frame(48)
    focus = mix[first:].copy()
    focus[:240] *= np.linspace(0,1,240)[:,None]
    args.output.parent.mkdir(parents=True,exist_ok=True)
    for path,samples in zip(paths[:4],(mix,piano,backing,focus)):
        write_wav(path,samples)
    cues = ((48,'Familiar question'),(50,'Higher piano answer'),(54,'Descending answer'),
            (56,'Quieter outro'),(58,'Low final response'),(63,'Held final chord'))
    report = {
        'description':'Side Street with a developed final piano melody and a soft resolved ending.',
        'bpm':BPM,'sample_rate':RATE,'frames':len(mix),'duration_seconds':len(mix)/RATE,
        'source_sha256':sha(args.source),'old_piano_sha256':sha(args.old_piano),
        'piano_gain':gain,'events':events,'focus_start_frame':first,'focus_seconds':len(focus)/RATE,
        'mix_peak_dbfs':float(20*np.log10(np.max(np.abs(mix)))),'integrated_lufs':loudness(paths[0]),
        'cues':[{'seconds':frame(bar)/RATE,'focus_seconds':(frame(bar)-first)/RATE,'label':label} for bar,label in cues],
        'output_sha256':{path.name:sha(path) for path in paths[:4]},
    }
    with paths[4].open('x') as out: out.write(json.dumps(report,indent=2)+'\n')
    with paths[5].open('x') as out:
        out.write('Side Street — developed piano finale\n116 BPM / F minor\n\n')
        for cue in report['cues']: out.write(f"{cue['seconds']:.2f}s  {cue['label']}\n")
    print(f"Wrote {paths[0]}: {len(mix)/RATE:.2f}s; finale excerpt {len(focus)/RATE:.2f}s; peak {report['mix_peak_dbfs']:.2f} dBFS")


if __name__ == '__main__': main()
