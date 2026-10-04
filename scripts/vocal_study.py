#!/usr/bin/env python3
"""Audition an original wordless vowel-synth melody over Side Street (requires NumPy)."""
import argparse
import hashlib
import json
from pathlib import Path
import wave

import numpy as np


RATE, BPM = 48000, 116
STEP = 60 / BPM / 4
START = round(16 * 16 * STEP * RATE)
END = round(32 * 16 * STEP * RATE)

# Frequency, level and bandwidth reference: Csound manual, tenor a/o tables.
# https://csound.com/docs/manual/MiscFormants.html
# The synthesis, melody, pitch contours and articulation below are original.
AH = np.array([[650,1080,2650,2900,3250], [0,-6,-7,-8,-22], [80,90,120,130,140]], dtype=float)
OH = np.array([[400,800,2600,2800,3000], [0,-10,-12,-12,-26], [70,80,100,130,135]], dtype=float)

# Sixteenth-note position, MIDI pitch, gate, velocity, target vowel (0=oh, 1=ah).
# A slower line beneath the existing pluck, with room between its phrases.
PHRASE = (
    ((1,60,3.8,.80,.90), (8,56,3.1,.70,.40)),
    ((0,58,3.2,.76,.70), (7,53,5.2,.86,.95)),
    ((1,56,3.8,.79,.70), (8,60,4.2,.80,.85)),
    ((1,58,3.2,.74,.45), (7,56,2.6,.70,.55), (11,53,3.4,.86,.95)),
)


def syllable(midi, gate, velocity, vowel, seed):
    release = .17
    t = np.arange(round((gate + release) * RATE)) / RATE
    rng = np.random.default_rng(seed)
    fundamental = 440 * 2 ** ((midi - 69) / 12)
    # Pitch approaches the note from slightly below; vibrato enters after attack.
    vibrato = 13 * np.sin(2*np.pi*5.2*t) * np.clip((t-.18)/.24, 0, 1)
    cents = -28*np.exp(-t/.055) + vibrato + 2*np.sin(2*np.pi*.9*t + seed % 7)
    frequency = fundamental * np.exp2(cents / 1200)
    phase = 2*np.pi*np.cumsum(frequency) / RATE
    mouth = .18 + (vowel - .18) * (1 - np.exp(-t/.085))
    centers = OH[0,:,None] + (AH[0]-OH[0])[:,None] * mouth
    widths = OH[2,:,None] + (AH[2]-OH[2])[:,None] * mouth
    levels = np.power(10, (OH[1,:,None] + (AH[1]-OH[1])[:,None] * mouth) / 20)
    voiced, power = np.zeros_like(t), np.zeros_like(t)
    for harmonic in range(1, int(6200 / fundamental) + 1):
        hz = harmonic * frequency
        distance = (hz[None,:] - centers) / (widths * .65)
        weight = .035 / harmonic**1.2 + np.sum(levels / (1 + distance**2), axis=0)
        weight *= 1 / np.sqrt(1 + (hz/4400)**8)
        voiced += weight * np.sin(harmonic * phase)
        power += .5 * weight**2
    voiced /= np.sqrt(np.maximum(power, 1e-12))

    # A quiet, band-shaped breath component. No recorded voice or other sample.
    bins = np.fft.rfftfreq(len(t), 1/RATE)
    breath_shape = (1-np.exp(-(bins/650)**2)) * np.exp(-.5*((bins-1700)/1500)**2)
    breath = np.fft.irfft(np.fft.rfft(rng.normal(size=len(t))) * breath_shape, n=len(t))
    breath /= max(float(np.sqrt(np.mean(breath**2))), 1e-12)
    attack = .060 if gate > .5 else .045
    envelope = .5 - .5*np.cos(np.pi * np.clip(t/attack, 0, 1))
    envelope *= .90 + .10*np.exp(-t/.20)
    envelope *= .5 + .5*np.cos(np.pi * np.clip((t-gate)/release, 0, 1))
    return velocity * envelope * (voiced + .035*breath*(.6 + .4*np.exp(-t/.12)))


def write_wav(path, samples):
    if not np.isfinite(samples).all() or np.max(np.abs(samples)) >= .98:
        raise ValueError(f"Non-finite or clipping audio: {path}")
    pcm = np.rint(samples * 32768).astype('<i2')
    with path.open('xb') as out, wave.open(out, 'wb') as audio:
        audio.setparams((2,2,RATE,0,'NONE','not compressed'))
        audio.writeframes(pcm.tobytes())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('backing', type=Path, help='Full Side Street v1 or v2 PCM16 WAV')
    parser.add_argument('output', type=Path, help='New prefix for mix, vocal-like lead, backing and notes')
    args = parser.parse_args()
    paths = [Path(str(args.output) + suffix) for suffix in ('.wav','-lead.wav','-backing.wav','.json','-notes.txt')]
    for path in paths:
        if path.exists():
            raise FileExistsError(f'Output already exists: {path}')
    with wave.open(str(args.backing), 'rb') as source:
        if (source.getnchannels(),source.getsampwidth(),source.getframerate()) != (2,2,RATE) or source.getnframes() < END:
            raise ValueError('Expected a full stereo 48 kHz PCM16 Side Street render')
        source.setpos(START)
        backing = np.frombuffer(source.readframes(END-START), dtype='<i2').reshape(-1,2).astype(float) / 32768
    if len(backing) != END-START:
        raise ValueError('Truncated backing recording')
    dry = np.zeros(len(backing))
    events = []
    # Four bars establish the familiar groove before the new lead joins it.
    for bar in range(4,16):
        for step,midi,length,velocity,vowel in PHRASE[bar % 4]:
            start = round(((16+bar)*16 + step)*STEP*RATE) - START
            note = syllable(midi, length*STEP, velocity, vowel, 20261004 + bar*16 + step)
            count = min(len(note),len(dry)-start)
            dry[start:start+count] += note[:count]
            events.append({'seconds':start/RATE,'midi':midi,'gate_seconds':length*STEP,'velocity':velocity,'vowel':vowel})
    lead = np.column_stack((dry,dry))
    # Quiet asymmetric reflections keep the new line in the existing stereo space.
    for seconds,gain,channel in ((.043,.06,0),(.067,.055,1),(.75*60/BPM,.15,0),(1.5*60/BPM,.10,1),(2.25*60/BPM,.045,0)):
        delay = round(seconds*RATE)
        lead[delay:,channel] += gain * dry[:-delay]
    active = round(4*16*STEP*RATE)
    scale = 10**(-28/20) / np.sqrt(np.mean(lead[active:]**2))
    lead *= scale
    # The existing backing is unchanged except for short fades at excerpt edges.
    fade = np.ones(len(backing))
    fade[:240] = np.linspace(0,1,240)
    fade[-3840:] = np.linspace(1,0,3840)
    backing *= fade[:,None]
    lead *= fade[:,None]
    mix = backing + lead
    args.output.parent.mkdir(parents=True,exist_ok=True)
    for path,samples in zip(paths[:3],(mix,lead,backing)):
        write_wav(path,samples)
    report = {
        'description':'Original synthesized ah/oh lead over the existing Side Street groove; no recorded singer or vocal samples.',
        'bpm':BPM,'source_start_frame':START,'frames':len(mix),'duration_seconds':len(mix)/RATE,
        'voice_enters_seconds':events[0]['seconds'],'source_sha256':hashlib.sha256(args.backing.read_bytes()).hexdigest(),
        'voice_gain':float(scale),'lead_active_rms_dbfs':float(20*np.log10(np.sqrt(np.mean(lead[active:]**2)))),
        'mix_peak_dbfs':float(20*np.log10(np.max(np.abs(mix)))),
        'formant_reference':'https://csound.com/docs/manual/MiscFormants.html',
        'events':events,'sha256':{path.name:hashlib.sha256(path.read_bytes()).hexdigest() for path in paths[:3]},
    }
    with paths[3].open('x') as out:
        out.write(json.dumps(report,indent=2)+'\n')
    with paths[4].open('x') as out:
        out.write('Side Street — wordless vocal-like lead study\n116 BPM / F minor\n\n')
        out.write(f"0.00s  Existing groove and string-like pluck\n{events[0]['seconds']:.2f}s  Synthesized ah/oh lead enters\n")
        out.write('The voice uses additive harmonics, moving vowel resonances, breath noise and gentle pitch contours.\n')
    print(f"Wrote {paths[0]}: {len(mix)/RATE:.2f}s; voice enters {events[0]['seconds']:.2f}s; peak {report['mix_peak_dbfs']:.2f} dBFS")


if __name__ == '__main__':
    main()
