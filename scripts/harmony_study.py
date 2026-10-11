#!/usr/bin/env python3
"""Develop Side Street with warm piano harmony (local NumPy/ffmpeg render)."""
import argparse
import json
from pathlib import Path

import numpy as np

from compare_hooks import loudness
from piano_phrasing import read_pcm
from piano_study import BPM, RATE, STEP, electric_piano, sha, write_wav


def frame(bar, step=0):
    return round((bar*16+step)*STEP*RATE)


# The bass supplies F underneath both voicings. Small inner-voice movements
# connect F minor 7 (A-flat/C/E-flat) to D-flat over F (A-flat/D-flat/F).
VOICINGS = ((56,60,63),(56,61,65))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('backing',type=Path,help='Complete Side Street v3 WAV')
    parser.add_argument('output',type=Path,help='Fresh prefix for full mix, keys, backing and focus clip')
    args = parser.parse_args()
    paths = [Path(str(args.output)+s) for s in ('.wav','-keys.wav','-backing.wav','-focus.wav','.json','-cues.txt')]
    for path in paths:
        if path.exists():
            raise FileExistsError(f'Output already exists: {path}')
    backing = read_pcm(args.backing).astype(float)/32768
    if len(backing) < frame(64):
        raise ValueError('Expected the complete 64-bar Side Street arrangement')
    keys = np.zeros_like(backing)
    events = []

    def chord(bar, step, voicing, length, velocity, softness=.020):
        onset = frame(bar,step)
        for index,midi in enumerate(VOICINGS[voicing]):
            # A tiny roll makes a chord one gesture rather than three hard attacks.
            offset = round(index*.006*RATE)
            note = electric_piano(midi,length*STEP,velocity,onset/RATE)
            t = np.arange(len(note))/RATE
            note *= (.5-.5*np.cos(np.pi*np.clip(t/softness,0,1)))[:,None]
            start = onset+offset
            count = min(len(note),len(keys)-start)
            keys[start:start+count] += note[:count]
        events.append({'bar':bar+1,'step':step,'seconds':onset/RATE,
                       'midi':list(VOICINGS[voicing]),'gate_seconds':length*STEP,'velocity':velocity})

    # Let the familiar groove and single-note piano establish themselves first.
    for bar in range(24,32,2):
        voicing = (bar//2)%2
        chord(bar,0,voicing,2.4,.48)
        chord(bar+1,4.1,voicing,1.8,.40)
    # Sustained, soft middle; then increasingly short, stronger gestures.
    chord(32,0,0,21,.36,.055)
    chord(34,0,1,18,.38,.055)
    chord(36,0,0,5,.42,.032)
    chord(37,4.1,0,2.5,.46)
    chord(38,0,1,2.3,.52)
    chord(39,4.1,1,1.5,.56)
    for bar in range(40,48,2):
        voicing = (bar//2)%2
        chord(bar,0,voicing,2.8,.60)
        chord(bar+1,4.1,voicing,1.8,.48)
    # Thin the harmony as the piano's melodic answer becomes more active.
    for bar in range(48,56,2):
        chord(bar,0,(bar//2)%2,2.7,.43)
    chord(56,0,0,4,.32,.030)
    chord(58,0,1,3,.28,.030)
    chord(60,0,0,6,.24,.040)

    dry = keys.copy()
    for seconds,gain,channel in ((.031,.075,0),(.043,.065,1),(.057,.035,0),(.071,.030,1)):
        delay = round(seconds*RATE)
        keys[delay:,channel] += gain*dry[:-delay,1-channel]
    gain = 10**(-21.5/20)/np.max(np.abs(keys))
    keys *= gain
    # Keep the established breath before the return completely silent.
    pause_start,pause_end = frame(40,-2),frame(40)
    fade_out,fade_in = round(.012*RATE),round(.002*RATE)
    keys[pause_start-fade_out:pause_start] *= np.linspace(1,0,fade_out)[:,None]
    keys[pause_start:pause_end] = 0
    keys[pause_end:pause_end+fade_in] *= np.linspace(0,1,fade_in)[:,None]
    mix = backing+keys
    first,last = frame(24),frame(52)
    focus = mix[first:last].copy()
    focus[:240] *= np.linspace(0,1,240)[:,None]
    focus[-3840:] *= np.linspace(1,0,3840)[:,None]
    args.output.parent.mkdir(parents=True,exist_ok=True)
    for path,samples in zip(paths[:4],(mix,keys,backing,focus)):
        write_wav(path,samples)
    cues = ((24,'Warm harmony enters'),(32,'Soft sustained middle'),(36,'Rebuild'),
            (40,'Full return'),(48,'Harmony thins; piano answers'),(56,'Soft outro'))
    report = {
        'description':'Side Street with two related electric-piano chord voicings, a soft middle and a stronger return.',
        'bpm':BPM,'sample_rate':RATE,'source_sha256':sha(args.backing),
        'frames':len(mix),'duration_seconds':len(mix)/RATE,'keys_gain':float(gain),
        'keys_peak_dbfs':float(20*np.log10(np.max(np.abs(keys)))),
        'mix_peak_dbfs':float(20*np.log10(np.max(np.abs(mix)))),
        'integrated_lufs':loudness(paths[0]),'source_integrated_lufs':loudness(args.backing),
        'focus_start_frame':first,'focus_frames':len(focus),'focus_seconds':len(focus)/RATE,
        'events':events,
        'cues':[{'seconds':frame(bar)/RATE,'focus_seconds':(frame(bar)-first)/RATE,'label':label} for bar,label in cues],
        'output_sha256':{path.name:sha(path) for path in paths[:4]},
    }
    with paths[4].open('x') as out:
        out.write(json.dumps(report,indent=2)+'\n')
    with paths[5].open('x') as out:
        out.write('Side Street — warm piano harmony\n116 BPM / F minor\n\n')
        for cue in report['cues']:
            out.write(f"{cue['seconds']:.2f}s  {cue['label']}\n")
        out.write(f"\nFocus clip starts at {first/RATE:.2f}s of the full track; duration {len(focus)/RATE:.2f}s.\n")
    print(f"Wrote {paths[0]}: {len(mix)/RATE:.2f}s; focus {len(focus)/RATE:.2f}s; peak {report['mix_peak_dbfs']:.2f} dBFS")


if __name__ == '__main__':
    main()
