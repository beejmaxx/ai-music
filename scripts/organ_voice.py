#!/usr/bin/env python3
"""A warm additive organ voice with a restrained, shared vibrato."""
import numpy as np

from piano_study import RATE


def organ(midi, gate, velocity, onset=0., mellow=True):
    if gate <= 0 or not 0 < velocity <= 1:
        raise ValueError('Invalid organ note')
    release = .110
    t = np.arange(round((gate + release) * RATE)) / RATE
    frequency = 440 * 2 ** ((midi - 69) / 12)
    absolute = t + onset
    wobble = frequency * .00060 / 5.4 * (np.cos(2 * np.pi * 5.4 * onset)
                                        - np.cos(2 * np.pi * 5.4 * absolute))
    phase = 2 * np.pi * frequency * t + wobble
    voice = np.zeros_like(t)
    partials = ((1, .64), (2, .25), (3, .15), (4, .065), (6, .026))
    for harmonic, level in partials:
        if frequency * harmonic >= 13000:
            continue
        brightness = 1 if harmonic == 1 else (.48 + .62 * velocity) * (.82 if mellow else 1.15)
        voice += level * brightness * np.sin(phase * harmonic)
    # A brief second harmonic gives struck chords a gentle front edge.
    voice += .075 * velocity * np.sin(phase * 2) * np.exp(-t / .036)
    attack = .5 - .5 * np.cos(np.pi * np.clip(t / .011, 0, 1))
    envelope = attack * (.84 + .16 * np.exp(-t / .15))
    envelope *= .5 + .5 * np.cos(np.pi * np.clip((t - gate) / release, 0, 1))
    voice *= envelope * velocity ** .8
    pan = .060 * np.sin(2 * np.pi * .78 * absolute)
    tremolo = 1 - .018 * np.cos(2 * np.pi * 5.4 * absolute)
    voice *= tremolo
    return np.column_stack((voice * (1 - pan), voice * (1 + pan)))
