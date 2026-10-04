#!/usr/bin/env python3
"""Audition a three-note electric-piano answer over Side Street (NumPy + ffmpeg)."""
import argparse
import hashlib
import json
from pathlib import Path
import tempfile
import wave

import numpy as np

from compare_hooks import loudness


RATE, BPM = 48000, 116
STEP = 60 / BPM / 4
START = round(16 * 16 * STEP * RATE)
END = round(20 * 16 * STEP * RATE)
GAP = round(1.25 * RATE)

# Bar within a two-bar motif, sixteenth, pitch, gate, velocity.
# The original strings have no attacks here: a pickup, then a rising answer.
MOTIF = ((0,14,60,1.05,.67), (1,0,63,1.45,.78), (1,7,65,1.10,.88))


def electric_piano(midi, gate, velocity, onset):
    release = .18
    t = np.arange(round((gate+release)*RATE)) / RATE
    frequency = 440*2**((midi-69)/12)
    phase = 2*np.pi*frequency*t
    # A mellow body with a brief, velocity-sensitive metallic tine.
    index = .18 + .95*velocity**2*np.exp(-t/.13)
    body = np.sin(phase + index*np.sin(phase))
    tine = np.sin(phase + .70*np.exp(-t/.035)*np.sin(14*phase))
    voice = body*(.78*np.exp(-t/1.25)+.22*np.exp(-t/.16))
    voice += .11*velocity*tine*np.exp(-t/.075)
    envelope = .5-.5*np.cos(np.pi*np.clip(t/.0045,0,1))
    envelope *= .5+.5*np.cos(np.pi*np.clip((t-gate)/release,0,1))
    tremolo = 1-.055*np.cos(2*np.pi*4.4*(t+onset))
    dry = velocity*voice*envelope*tremolo
    # A small stereo movement; the core pitch remains firmly in the center.
    pan = .07*np.sin(2*np.pi*.55*(t+onset))
    return np.column_stack((dry*(1-pan),dry*(1+pan)))


def read_excerpt(path):
    with wave.open(str(path),'rb') as source:
        if (source.getnchannels(),source.getsampwidth(),source.getframerate()) != (2,2,RATE) or source.getnframes() < END:
            raise ValueError('Expected a full stereo 48 kHz PCM16 Side Street recording')
        source.setpos(START)
        data = np.frombuffer(source.readframes(END-START),dtype='<i2').reshape(-1,2)
    if len(data) != END-START:
        raise ValueError('Truncated backing recording')
    return data.astype(float)/32768


def write_wav(path, samples):
    if not np.isfinite(samples).all() or np.max(np.abs(samples)) >= .98:
        raise ValueError(f'Non-finite or clipping audio: {path}')
    with path.open('xb') as out, wave.open(out,'wb') as recording:
        recording.setparams((2,2,RATE,0,'NONE','not compressed'))
        recording.writeframes(np.rint(samples*32768).astype('<i2').tobytes())


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('backing',type=Path,help='Full Side Street v1 or v2 WAV')
    parser.add_argument('output',type=Path,help='New output prefix for the A/B comparison and piano stem')
    args = parser.parse_args()
    suffixes = ('-a.wav','-b.wav','-piano.wav','.wav','.json','-cues.txt')
    paths = [Path(str(args.output)+suffix) for suffix in suffixes]
    for path in paths:
        if path.exists():
            raise FileExistsError(f'Output already exists: {path}')
    backing = read_excerpt(args.backing)
    piano = np.zeros_like(backing)
    events = []
    for repeat in range(2):
        for bar,step,midi,length,velocity in MOTIF:
            # Follow the light swing already used by the bass and strings.
            position = (16+repeat*2+bar)*16 + step + (.10 if step%2 else 0)
            start = round(position*STEP*RATE)-START
            note = electric_piano(midi,length*STEP,velocity,start/RATE)
            count = min(len(note),len(piano)-start)
            piano[start:start+count] += note[:count]
            events.append({'seconds':start/RATE,'midi':midi,'gate_seconds':length*STEP,'velocity':velocity})
    # A few quiet, close reflections soften the dry tine without a rhythmic echo.
    dry = piano.copy()
    for seconds,gain,channel in ((.019,.065,0),(.027,.060,1),(.043,.035,0),(.051,.030,1)):
        delay = round(seconds*RATE)
        piano[delay:,channel] += gain*dry[:-delay,1-channel]
    gain = 10**(-33.5/20)/np.sqrt(np.mean(piano**2))
    fade = np.ones(len(backing))
    fade[:240] = np.linspace(0,1,240)
    fade[-960:] = np.linspace(1,0,960)
    backing *= fade[:,None]
    piano *= fade[:,None]
    # Keep the accompaniment identical, and measure that extra loudness cannot
    # dominate this small musical comparison. Only trim the added piano if needed.
    with tempfile.TemporaryDirectory(prefix='ai-music-piano-levels-') as temp:
        temp = Path(temp)
        original_path = temp/'a.wav'
        write_wav(original_path,backing)
        original_lufs = loudness(original_path)
        for attempt in range(9):
            candidate = backing+piano*gain
            candidate_path = temp/f'b-{attempt}.wav'
            write_wav(candidate_path,candidate)
            candidate_lufs = loudness(candidate_path)
            if abs(candidate_lufs-original_lufs) <= .3+1e-9:
                break
            gain *= 10**(-.5/20)
        else:
            raise ValueError('Mix loudness differs by more than 0.3 LU; inspect the piano level')
    piano *= gain
    revised = backing+piano
    sequence = np.concatenate((backing,np.zeros((GAP,2)),revised))
    args.output.parent.mkdir(parents=True,exist_ok=True)
    for path,samples in zip(paths[:4],(backing,revised,piano,sequence)):
        write_wav(path,samples)
    report = {
        'description':'Original three-note electric-piano answer, synthesized without recorded samples; no added vocal.',
        'bpm':BPM,'source_first_bar':17,'source_last_bar':20,'source_start_frame':START,
        'source_sha256':sha(args.backing),'sample_rate':RATE,'frames_per_clip':len(backing),'gap_frames':GAP,
        'duration_seconds':len(sequence)/RATE,'piano_gain':float(gain),
        'piano_rms_dbfs':float(20*np.log10(np.sqrt(np.mean(piano**2)))),
        'piano_peak_dbfs':float(20*np.log10(np.max(np.abs(piano)))),
        'mix_loudness_spread_lu':abs(candidate_lufs-original_lufs),'events':events,
        'clips':[
            {'label':'A: existing groove','start_seconds':0,'duration_seconds':len(backing)/RATE,'lufs':original_lufs},
            {'label':'B: with electric-piano answer','start_seconds':(len(backing)+GAP)/RATE,
             'duration_seconds':len(backing)/RATE,'lufs':candidate_lufs,
             'first_piano_seconds':(len(backing)+GAP)/RATE+events[0]['seconds']},
        ],
        'peak_dbfs':float(20*np.log10(np.max(np.abs(sequence)))),
        'output_sha256':{path.name:sha(path) for path in paths[:4]},
    }
    with paths[4].open('x') as out:
        out.write(json.dumps(report,indent=2)+'\n')
    with paths[5].open('x') as out:
        out.write('Side Street — electric-piano answer\n116 BPM / F minor / same four-bar backing in A and B\n\n')
        for clip in report['clips']:
            out.write(f"{clip['start_seconds']:.2f}s  {clip['label']}\n")
        out.write(f"{report['clips'][1]['first_piano_seconds']:.2f}s  First piano answer: C–E-flat–F\n")
    print(f"Wrote {paths[3]}: {len(sequence)/RATE:.2f}s; B at {report['clips'][1]['start_seconds']:.2f}s; first piano at {report['clips'][1]['first_piano_seconds']:.2f}s; loudness difference {report['mix_loudness_spread_lu']:.1f} LU")


if __name__ == '__main__':
    main()
