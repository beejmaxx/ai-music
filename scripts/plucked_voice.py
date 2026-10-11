#!/usr/bin/env python3
"""A deterministic, damped string voice for short, playable chord patterns."""
import numpy as np

from piano_study import RATE


PRESETS = {
    'muted': {'position': .18, 'decay': .34, 'release': .065, 'attack': .0018, 'tilt': 1.27},
    'round': {'position': .24, 'decay': .49, 'release': .090, 'attack': .0028, 'tilt': 1.40},
    'bright': {'position': .13, 'decay': .42, 'release': .080, 'attack': .0015, 'tilt': 1.12},
}


def plucked(midi, gate, velocity, onset=0., preset='muted'):
    """Render a gently inharmonic string without recorded samples or distortion."""
    if preset not in PRESETS or not 0 < velocity <= 1 or gate <= 0:
        raise ValueError('Invalid plucked-string note')
    patch = PRESETS[preset]
    t = np.arange(round((gate + patch['release']) * RATE)) / RATE
    fundamental = 440 * 2 ** ((midi - 69) / 12)
    count = min(28, int(13000 / fundamental))
    voice = np.zeros_like(t)
    norm = 0.
    # Every partial is below 13 kHz. Tiny string stiffness gives the upper
    # modes a little spread; the fundamental itself remains exactly tuned.
    stiffness = .000015
    for n in range(1, count + 1):
        frequency = fundamental * n * np.sqrt((1 + stiffness * n * n) / (1 + stiffness))
        if frequency >= 13000:
            continue
        amplitude = (np.sin(np.pi * n * patch['position']) * np.sin(np.pi * n * .31)
                     / n ** (patch['tilt'] + .60 * (1 - velocity)))
        damping = .026 + patch['decay'] / n ** .78
        voice += amplitude * np.sin(2 * np.pi * frequency * t) * np.exp(-t / damping)
        norm += abs(amplitude)
    voice /= max(norm, .01)
    # A quiet filtered pick transient makes repeated notes less organ-like.
    seed = int(midi * 100003 + round(onset * RATE)) % (2 ** 32)
    noise = np.random.default_rng(seed).standard_normal(len(t))
    frequencies = np.fft.rfftfreq(len(t), 1 / RATE)
    band = (1 - np.exp(-(frequencies / 750) ** 2)) * np.exp(-(frequencies / 4300) ** 4)
    noise = np.fft.irfft(np.fft.rfft(noise) * band, n=len(t))
    voice += .014 * velocity * noise * np.exp(-t / .005)
    attack = .5 - .5 * np.cos(np.pi * np.clip(t / patch['attack'], 0, 1))
    release = .5 + .5 * np.cos(np.pi * np.clip((t - gate) / patch['release'], 0, 1))
    voice *= attack * release * velocity ** 1.15
    pan = .045 * np.sin(2 * np.pi * .29 * onset + midi * .17)
    return np.column_stack((voice * (1 - pan), voice * (1 + pan)))
