#!/usr/bin/env python3
"""Make a level-matched A/B/C audition from three Side Street renders (needs ffmpeg)."""
import argparse
from array import array
import hashlib
import json
import math
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import wave


RATE = 48000
BPM = 116
START = round(16 * 4 * 60 / BPM * RATE)
END = round(24 * 4 * 60 / BPM * RATE)
GAP = round(1.25 * RATE)
LABELS = ("A: original", "B: melodic phrasing", "C: expressive sound")


def digest(path):
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def excerpt(path):
    with wave.open(str(path), "rb") as source:
        if (source.getnchannels(), source.getsampwidth(), source.getframerate()) != (2, 2, RATE):
            raise ValueError(f"Expected stereo 48 kHz PCM16: {path}")
        if source.getnframes() < END:
            raise ValueError(f"Recording is too short: {path}")
        source.setpos(START)
        result = array("h", source.readframes(END - START))
    if sys.byteorder != "little":
        result.byteswap()
    if len(result) != 2 * (END - START):
        raise ValueError(f"Truncated recording: {path}")
    return result


def write_wav(path, samples):
    data = array("h", samples)
    if sys.byteorder != "little":
        data.byteswap()
    with path.open("xb") as output, wave.open(output, "wb") as recording:
        recording.setparams((2, 2, RATE, 0, "NONE", "not compressed"))
        recording.writeframes(data.tobytes())


def loudness(path):
    result = subprocess.run([
        "ffmpeg", "-nostdin", "-hide_banner", "-nostats", "-i", str(path),
        "-af", "ebur128=framelog=verbose", "-f", "null", "-",
    ], text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True)
    matches = re.findall(r"\bI:\s+(-?\d+(?:\.\d+)?) LUFS", result.stderr)
    if not matches or not math.isfinite(float(matches[-1])) or float(matches[-1]) <= -69:
        raise ValueError(f"Could not measure active audio: {path}")
    return float(matches[-1])


def edge_fades(samples):
    # Short, identical fades make the excerpt boundaries click-free.
    frames = len(samples) // 2
    for frame in range(round(.005 * RATE)):
        gain = frame / (.005 * RATE)
        for channel in range(2):
            samples[2 * frame + channel] = round(samples[2 * frame + channel] * gain)
    fade_out = round(.020 * RATE)
    for offset in range(fade_out):
        gain = (fade_out - 1 - offset) / fade_out
        frame = frames - fade_out + offset
        for channel in range(2):
            samples[2 * frame + channel] = round(samples[2 * frame + channel] * gain)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("original", type=Path, help="Renderer output prefix for the original")
    parser.add_argument("melody", type=Path, help="Renderer output prefix for --hook melody")
    parser.add_argument("expression", type=Path, help="Renderer output prefix for --hook expression")
    parser.add_argument("output", type=Path, help="New output prefix for clips, sequence and report")
    args = parser.parse_args()
    if not shutil.which("ffmpeg"):
        parser.error("ffmpeg is required to measure EBU R128 integrated loudness")
    prefixes = (args.original, args.melody, args.expression)
    outputs = [Path(str(args.output) + suffix) for suffix in ("-a.wav", "-b.wav", "-c.wav", ".wav", ".json", "-cues.txt")]
    for path in outputs:
        if path.exists():
            raise FileExistsError(f"Output already exists: {path}")

    source_hashes = {}
    for suffix in ("-bass.wav", "-kick.wav", "-percussion.wav"):
        hashes = [digest(Path(str(prefix) + suffix)) for prefix in prefixes]
        if len(set(hashes)) != 1:
            raise ValueError(f"Accompaniment differs between renders: {suffix}")
        source_hashes[suffix] = hashes[0]
    original = excerpt(Path(str(args.original) + ".wav"))
    hooks = [excerpt(Path(str(prefix) + "-hook.wav")) for prefix in prefixes]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    clips, entries = [], []
    with tempfile.TemporaryDirectory(prefix="ai-music-hook-levels-") as temp:
        temp = Path(temp)
        measured = []
        for index, hook in enumerate(hooks):
            path = temp / f"hook-{index}.wav"
            write_wav(path, hook)
            measured.append(loudness(path))
        for index, hook in enumerate(hooks):
            gain_db = measured[0] - measured[index]
            gain = 10 ** (gain_db / 20)
            # Only the hook changes. Reusing the exact master minus its original
            # hook preserves the same accompaniment and its quantization residue.
            samples = array("h")
            for mix, old, new in zip(original, hooks[0], hook):
                sample = round(mix - old + new * gain)
                if abs(sample) >= 32440:
                    raise ValueError("Matched comparison would clip; lower the source mix")
                samples.append(sample)
            edge_fades(samples)
            path = temp / f"mix-{index}.wav"
            write_wav(path, samples)
            clips.append(samples)
            entries.append({
                "label": LABELS[index], "start_seconds": index * ((END - START + GAP) / RATE),
                "duration_seconds": (END - START) / RATE,
                "raw_hook_lufs": measured[index], "hook_gain_db": gain_db,
                "matched_mix_lufs": loudness(path),
                "peak_dbfs": 20 * math.log10(max(map(abs, samples)) / 32768),
                "source_hook_sha256": digest(Path(str(prefixes[index]) + "-hook.wav")),
            })
    mix_spread = max(entry["matched_mix_lufs"] for entry in entries) - min(entry["matched_mix_lufs"] for entry in entries)
    if mix_spread > .3 + 1e-9:
        raise ValueError(f"Mix loudness differs by {mix_spread:.1f} LU; inspect before auditioning")
    sequence = array("h")
    for index, samples in enumerate(clips):
        write_wav(outputs[index], samples)
        if index:
            sequence.extend(array("h", [0]) * (GAP * 2))
        sequence.extend(samples)
    write_wav(outputs[3], sequence)
    report = {
        "source_first_bar": 17, "source_last_bar": 24, "source_start_frame": START,
        "frames_per_clip": END - START, "sample_rate": RATE, "gap_seconds": GAP / RATE,
        "duration_seconds": len(sequence) / (2 * RATE), "reference_mix_sha256": digest(Path(str(args.original) + ".wav")),
        "accompaniment_sha256": source_hashes, "hook_target_lufs": measured[0],
        "mix_loudness_spread_lu": mix_spread, "clips": entries,
        "output_sha256": {path.name: digest(path) for path in outputs[:4]},
    }
    with outputs[4].open("x") as out:
        out.write(json.dumps(report, indent=2) + "\n")
    with outputs[5].open("x") as out:
        out.write("Side Street hook comparison — bars 17–24\nSame accompaniment; hook loudness matched.\n\n")
        for entry in entries:
            out.write(f"{entry['start_seconds']:.2f}s  {entry['label']}\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
