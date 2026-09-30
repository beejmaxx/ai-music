#!/usr/bin/env python3
"""Fetch the small streaming model from Google's public bucket. No Python ML stack."""
import argparse
import base64
import hashlib
from pathlib import Path
import time
import urllib.request

# Official magenta-rt-public objects, pinned by size and MD5 on 2026-09-30.
# The audio decoder is bundled in the exported .mlxfn; no encoder is needed
# for text-conditioned generation.
FILES = [
    ("models/mrt2_small/mrt2_small.mlxfn", 455654550, "VvwOnA/XIlQ4sHmToMljaA=="),
    ("models/mrt2_small/mrt2_small_state.safetensors", 8676998, "K4j815nxfJ3UgPtAIHZKPw=="),
    ("resources/musiccoca/audio_preprocessor.tflite", 8729640, "qxQIbRKmchTvGytr9GY+Fg=="),
    ("resources/musiccoca/mapper.tflite", 86166664, "AlobDIvhZcllz0ahbcVqwA=="),
    ("resources/musiccoca/music_encoder.tflite", 370935584, "4JN37oXmUC+QRI0tRW2UCg=="),
    ("resources/musiccoca/pretrained_vector_quantizer.tflite", 72422108, "3XjQ/rNJh9TnyJoUlJXFmg=="),
    ("resources/musiccoca/spm.model", 517448, "ZiFTeB92h9Kkzw07r8Fv2Q=="),
    ("resources/musiccoca/text_encoder.tflite", 418674324, "PwfSnmYSJsjk7PPEU6kMug=="),
]


def verified(path, size, checksum):
    if not path.is_file() or path.stat().st_size != size:
        return False
    digest = hashlib.md5()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return base64.b64encode(digest.digest()).decode() == checksum


def download(root, name, size, checksum):
    target = root / name
    if verified(target, size, checksum):
        print(f"Verified {name}", flush=True)
        return
    target.parent.mkdir(parents=True, exist_ok=True)
    partial = target.with_suffix(target.suffix + ".part")
    url = "https://storage.googleapis.com/magenta-rt-public/magenta-rt-2/" + name
    for attempt in range(3):
        try:
            offset = partial.stat().st_size if partial.exists() else 0
            if offset >= size:
                partial.unlink()
                offset = 0
            request = urllib.request.Request(url, headers={"Range": f"bytes={offset}-"})
            print(f"Downloading {name} ({size / 1e6:.1f} MB)", flush=True)
            with urllib.request.urlopen(request, timeout=60) as response:
                append = offset > 0 and response.status == 206
                with partial.open("ab" if append else "wb") as output:
                    for chunk in iter(lambda: response.read(1024 * 1024), b""):
                        output.write(chunk)
            if not verified(partial, size, checksum):
                partial.unlink(missing_ok=True)
                raise RuntimeError(f"Integrity check failed: {name}")
            partial.replace(target)
            print(f"Verified {name}", flush=True)
            return
        except Exception:
            if attempt == 2:
                raise
            time.sleep(2)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--directory", type=Path, default=Path(__file__).resolve().parents[1] / "models")
    args = parser.parse_args()
    print(f"Model assets: {sum(item[1] for item in FILES) / 1e9:.2f} GB in {args.directory}", flush=True)
    for item in FILES:
        download(args.directory, *item)
