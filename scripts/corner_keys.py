#!/usr/bin/env python3
"""Complete Corner Light with an original electric-piano part (local render)."""
import argparse
import json
from pathlib import Path

import numpy as np

from compare_hooks import loudness
from piano_phrasing import read_pcm
from piano_study import RATE, electric_piano, sha, write_wav

BPM=112
STEP=60/BPM/4
VOICINGS=((53,57,60,64),(53,57,60,62),(53,58,62),(55,61,64))

def frame(bar,step=0): return round((bar*16+step)*STEP*RATE)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('backing',type=Path)
    parser.add_argument('output',type=Path)
    args=parser.parse_args()
    paths=[Path(str(args.output)+s) for s in ('.wav','-keys.wav','-backing.wav','-focus.wav','.json','-cues.txt')]
    for path in paths:
        if path.exists(): raise FileExistsError(f'Output already exists: {path}')
    backing=read_pcm(args.backing).astype(float)/32768
    if len(backing)<frame(64): raise ValueError('Expected the full 64-bar Corner Light recording')
    keys=np.zeros_like(backing);events=[]
    def chord(bar,position,gate,velocity,pitches=None,soft=.020):
        pitches=VOICINGS[(bar//2)%4] if pitches is None else pitches
        onset=frame(bar,position)
        for index,midi in enumerate(pitches):
            start=onset+round(index*.005*RATE)
            sound=electric_piano(midi,gate*STEP,velocity,onset/RATE)
            t=np.arange(len(sound))/RATE
            sound*=(.5-.5*np.cos(np.pi*np.clip(t/soft,0,1)))[:,None]
            count=min(len(sound),len(keys)-start)
            keys[start:start+count]+=sound[:count]
        events.append({'bar':bar+1,'seconds':onset/RATE,'midi':list(pitches),'velocity':velocity,'gate_seconds':gate*STEP})
    chord(0,0,5,.25,soft=.035);chord(2,0,5,.23,soft=.035)
    for bar in range(4,63):
        if 32<=bar<36:
            if bar%2==0: chord(bar,0,22,.37,soft=.055)
            continue
        if 36<=bar<40:
            chord(bar,0 if bar%2==0 else 4.14,4-(bar-36)*.6,.40+(bar-36)*.05)
            continue
        if bar>=56:
            if bar%2==0: chord(bar,0,4,.34-(bar-56)*.015,soft=.032)
            continue
        feature=24<=bar<28
        velocity=.57 if 40<=bar<56 else .52
        chord(bar,0 if bar%2==0 else 4.14,5 if feature else 2.9 if bar%2==0 else 1.8,
              velocity if bar%2==0 else velocity*.83)
        if bar%2 and (16<=bar<24 or 48<=bar<56):
            chord(bar,12.14,1.1,.24,pitches=VOICINGS[(bar//2)%4][-2:])
    chord(63,0,18,.28,pitches=(50,53,57,62),soft=.030)
    dry=keys.copy()
    for seconds,level,channel in ((.031,.075,0),(.043,.065,1),(.057,.035,0),(.071,.030,1)):
        delay=round(seconds*RATE)
        keys[delay:,channel]+=level*dry[:-delay,1-channel]
    gain=10**(-20/20)/np.max(np.abs(keys));keys*=gain
    pause,return_at=frame(40,-2),frame(40)
    ramp=np.linspace(1,0,round(.012*RATE))[:,None]
    keys[pause-len(ramp):pause]*=ramp;keys[pause:return_at]=0
    fade=np.linspace(0,1,round(.002*RATE))[:,None]
    keys[return_at:return_at+len(fade)]*=fade
    mix=backing+keys
    first,last=frame(4),frame(24)
    focus=mix[first:last].copy();focus[:240]*=np.linspace(0,1,240)[:,None];focus[-3840:]*=np.linspace(1,0,3840)[:,None]
    args.output.parent.mkdir(parents=True,exist_ok=True)
    for path,samples in zip(paths[:4],(mix,keys,backing,focus)):write_wav(path,samples)
    report={'title':'Corner Light','bpm':BPM,'key':'D minor','source_sha256':sha(args.backing),
            'frames':len(mix),'duration_seconds':len(mix)/RATE,'focus_start_frame':first,'focus_seconds':len(focus)/RATE,
            'keys_gain':float(gain),'events':events,'mix_peak_dbfs':float(20*np.log10(np.max(np.abs(mix)))),
            'integrated_lufs':loudness(paths[0]),'output_sha256':{path.name:sha(path) for path in paths[:4]}}
    with paths[4].open('x') as out:out.write(json.dumps(report,indent=2)+'\n')
    with paths[5].open('x') as out:
        out.write('Corner Light\n112 BPM / D minor / Dm–B-flat–Gm–A7\n\n')
        for bar,label in ((0,'Opening'),(4,'Piano groove'),(8,'Main melody'),(16,'Higher answer'),(24,'Bass and keys'),(32,'Breakdown'),(36,'Rebuild'),(40,'Return'),(48,'Final answer'),(56,'Last refrain'),(60,'Outro'),(63,'Final D minor chord')):
            out.write(f'{frame(bar)/RATE:.2f}s  {label}\n')
    print(f"Wrote {paths[0]}: {len(mix)/RATE:.2f}s; peak {report['mix_peak_dbfs']:.2f} dBFS; {report['integrated_lufs']:.1f} LUFS")

if __name__=='__main__':main()
