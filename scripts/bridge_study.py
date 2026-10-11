#!/usr/bin/env python3
"""Put a moving-harmony middle into Side Street, retaining the surrounding song."""
import argparse
import json
from pathlib import Path

import numpy as np

from compare_hooks import loudness
from piano_phrasing import read_pcm
from piano_study import BPM, RATE, STEP, electric_piano, sha, write_wav


def frame(bar,step=0): return round((bar*16+step)*STEP*RATE)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source',type=Path)
    parser.add_argument('old_bass',type=Path)
    parser.add_argument('old_keys',type=Path)
    parser.add_argument('bridge_bass',type=Path)
    parser.add_argument('keys_report',type=Path)
    parser.add_argument('output',type=Path)
    args = parser.parse_args()
    paths = [Path(str(args.output)+s) for s in ('.wav','-bass.wav','-keys.wav','-rest.wav','-focus.wav','.json','-cues.txt')]
    for path in paths:
        if path.exists(): raise FileExistsError(f'Output already exists: {path}')
    source,bass,keys = [read_pcm(path).astype(float)/32768 for path in (args.source,args.old_bass,args.old_keys)]
    if not source.shape==bass.shape==keys.shape: raise ValueError('Source and original stems must have matching lengths')
    first,last = frame(32),frame(40)
    replacement = read_pcm(args.bridge_bass).astype(float)/32768
    if len(replacement)<last-first: raise ValueError('Bridge bass is too short')
    rest = source-bass-keys
    bridge_keys = np.zeros((last-first,2))
    key_gain = json.loads(args.keys_report.read_text())['keys_gain']
    # Rootless voicings above the moving bass: Fm9, Dbmaj9, Abmaj7, Eb6/9.
    voicings = ((56,60,63,67),(53,56,60,63),(55,60,63),(55,58,60,65))
    events = []
    for local_bar,position,length,velocity in ((0,0,21,.36),(2,0,18,.38),(4,0,5,.42),
                                              (5,4.1,2.5,.46),(6,0,2.3,.52),(7,4.1,1.5,.56)):
        onset=frame(32+local_bar,position)-first
        pitches=voicings[local_bar//2]
        for index,midi in enumerate(pitches):
            start=onset+round(index*.006*RATE)
            sound=electric_piano(midi,length*STEP,velocity,(first+onset)/RATE)
            t=np.arange(len(sound))/RATE
            softness=.055 if local_bar<4 else .025
            sound*=(.5-.5*np.cos(np.pi*np.clip(t/softness,0,1)))[:,None]
            count=min(len(sound),len(bridge_keys)-start)
            bridge_keys[start:start+count]+=sound[:count]
        events.append({'bar':33+local_bar,'step':position,'midi':list(pitches),'velocity':velocity})
    dry=bridge_keys.copy()
    for seconds,gain,channel in ((.031,.075,0),(.043,.065,1),(.057,.035,0),(.071,.030,1)):
        delay=round(seconds*RATE)
        bridge_keys[delay:,channel]+=gain*dry[:-delay,1-channel]
    bridge_keys*=key_gain
    new_bass=bass.copy();new_keys=keys.copy()
    new_bass[first:last]=replacement[:last-first]
    new_keys[first:last]=bridge_keys
    # Preserve the last tail crossing into the bridge with a short crossfade.
    fade=np.linspace(0,1,round(.040*RATE))[:,None]
    new_bass[first:first+len(fade)]=bass[first:first+len(fade)]*(1-fade)+new_bass[first:first+len(fade)]*fade
    new_keys[first:first+len(fade)]=keys[first:first+len(fade)]*(1-fade)+new_keys[first:first+len(fade)]*fade
    pause=frame(40,-2)
    ramp=np.linspace(1,0,round(.012*RATE))[:,None]
    for stem in (new_bass,new_keys):
        stem[pause-len(ramp):pause]*=ramp
        stem[pause:last]=0
    mix=rest+new_bass+new_keys
    # Any original non-bass/key residue at the established pause stays untouched.
    focus_start,focus_end=frame(30),frame(46)
    focus=mix[focus_start:focus_end].copy()
    focus[:240]*=np.linspace(0,1,240)[:,None]
    focus[-3840:]*=np.linspace(1,0,3840)[:,None]
    args.output.parent.mkdir(parents=True,exist_ok=True)
    for path,samples in zip(paths[:5],(mix,new_bass,new_keys,rest,focus)): write_wav(path,samples)
    report={'description':'Side Street with an F/Db/Ab/Eb harmonic journey through the middle.',
            'bpm':BPM,'frames':len(mix),'duration_seconds':len(mix)/RATE,
            'source_sha256':sha(args.source),'bridge_bass_sha256':sha(args.bridge_bass),
            'bridge_start_frame':first,'bridge_end_frame':last,'events':events,
            'focus_start_frame':focus_start,'focus_seconds':len(focus)/RATE,
            'mix_peak_dbfs':float(20*np.log10(np.max(np.abs(mix)))),
            'integrated_lufs':loudness(paths[0]),
            'output_sha256':{path.name:sha(path) for path in paths[:5]}}
    with paths[5].open('x') as out: out.write(json.dumps(report,indent=2)+'\n')
    with paths[6].open('x') as out:
        out.write('Side Street — moving middle harmony\n116 BPM\n\n')
        for bar,label in ((32,'F minor'),(34,'D-flat major'),(36,'A-flat major'),(38,'E-flat'),(40,'Return to F minor')):
            out.write(f'{frame(bar)/RATE:.2f}s  {label} (focus {(frame(bar)-focus_start)/RATE:.2f}s)\n')
    print(f"Wrote {paths[0]}; bridge {first/RATE:.2f}–{last/RATE:.2f}s; focus {len(focus)/RATE:.2f}s")


if __name__=='__main__': main()
