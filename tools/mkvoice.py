#!/usr/bin/env python3
"""Sprach-Clips für Brain Candy erzeugen.

Liest die Sätze aus dem Simulator (`sim/bcsim --voice`) und spricht sie
  - mit der HeyGen-Stimme (Standard): POST /v3/models/audio/tts, Modell heygen-voice-1-turbo-free,
    Schlüssel aus der Umgebungsvariable HEYGEN_API_KEY, Stimme aus HEYGEN_VOICE_ID (Vorgabe: tobias_pitch)
  - oder mit `--say` über die macOS-Stimmen Anna (de) und Samantha (en).
Danach: 16 kHz Mono, Stille abschneiden, normalisieren, G.711 µ-law (8 Bit).
Ergebnis: firmware/voice.bin (alle Clips hintereinander) und firmware/voice_index.h (Schlüssel, Versatz, Länge).

    make -C sim && HEYGEN_API_KEY=... python3 tools/mkvoice.py
"""
import json
import os
import re
import subprocess
import sys
import tempfile
import time
import wave

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RATE = 16000
SAY_VOICES = {"de": "Anna", "en": "Samantha"}
HEYGEN_VOICE = os.environ.get("HEYGEN_VOICE_ID", "bab9f8533b7d43f0845a77712a8e9c03")   # tobias_pitch
HEYGEN_URL = "https://api.heygen.com/v3/models/audio/tts"


def speakable(text):
    # Wörter in Großbuchstaben (NICHT, BLAU, STARK) würden sonst buchstabiert
    text = re.sub(r"\b[A-ZÄÖÜ]{2,}\b", lambda m: m.group(0).capitalize(), text)
    return text.replace("…", "...").replace("–", ",")


def ulaw(sample):
    # 16 Bit linear -> G.711 µ-law
    BIAS, CLIP = 0x84, 32635
    sign = 0x80 if sample < 0 else 0
    if sample < 0:
        sample = -sample
    sample = min(sample, CLIP) + BIAS
    exp = 7
    mask = 0x4000
    while exp > 0 and not (sample & mask):
        exp -= 1
        mask >>= 1
    man = (sample >> (exp + 3)) & 0x0F
    return ~(sign | (exp << 4) | man) & 0xFF


def pcm_say(text, lang, tmp):
    path = os.path.join(tmp, "clip.wav")
    subprocess.run(["say", "-v", SAY_VOICES[lang], "-r", "175", "--file-format=WAVE", "--data-format=LEI16@%d" % RATE,
                    "-o", path, speakable(text)], check=True)
    with wave.open(path, "rb") as w:
        assert w.getframerate() == RATE and w.getsampwidth() == 2 and w.getnchannels() == 1
        return w.readframes(w.getnframes())


def pcm_heygen(text, lang, key, tmp):
    # über curl (nutzt die Zertifikate des Systems); der Schlüssel steht in einer Header-Datei, nicht in der Befehlszeile
    hdr = os.path.join(tmp, "headers")
    with open(hdr, "w") as f:
        f.write(f"X-Api-Key: {key}\nContent-Type: application/json\n")
    os.chmod(hdr, 0o600)
    body = json.dumps({"model": "heygen-voice-1-turbo-free", "voice_id": HEYGEN_VOICE, "text": speakable(text), "language": lang,
                       "output_format": {"encoding": "pcm", "sample_rate_hertz": RATE}})
    for attempt in range(6):
        r = subprocess.run(["curl", "-s", "-w", "\n%{http_code}", "-X", "POST", HEYGEN_URL, "-H", "@" + hdr, "--data-binary", "@-"],
                           input=body.encode(), capture_output=True, check=True)
        out, code = r.stdout.rsplit(b"\n", 1)
        if code == b"200":
            url = json.loads(out)["data"]["audio_url"]
            return subprocess.run(["curl", "-sf", url], capture_output=True, check=True).stdout
        if not (code == b"429" or code.startswith(b"5")) or attempt == 5:   # Drosselung und Serverfehler: später nochmal
            raise SystemExit(f"HeyGen: HTTP {code.decode()} {out[:300]!r}")
        time.sleep(2 ** attempt)


def trim(pcm, short=False):
    """Sprache herausschneiden: Abschnitte in 10-ms-Fenstern finden, einen abgeschnittenen Fetzen am Ende
    verwerfen (HeyGen setzt nach kurzen Sätzen manchmal zu einem neuen Laut an, der mitten drin endet),
    dann sanft ein- und ausblenden. Bei kurzen Ausrufen (short) wiederholt die Stimme sich gern nach einer Pause:
    dort zählt nur der Teil bis zur ersten Pause von mindestens 200 ms."""
    W = RATE // 100
    env = [max((abs(v) for v in pcm[i:i + W]), default=0) for i in range(0, len(pcm), W)]
    thr = 0.04 * max(env + [1])
    segs = []   # [start, end) in Fenstern, Lücken unter 150 ms zusammengefasst
    for i, e in enumerate(env):
        if e <= thr:
            continue
        if segs and i - segs[-1][1] < 15:
            segs[-1][1] = i + 1
        else:
            segs.append([i, i + 1])
    if not segs:
        return pcm
    def fragment(sg):   # abgerissener Ansatz am Dateiende: kurz (bei langen Texten auch deutlich leiser)
        if sg[1] < len(env) - 3:
            return False
        if short:
            return sg[1] - sg[0] < 35
        return sg[1] - sg[0] < 20 and max(env[sg[0]:sg[1]]) < 0.5 * max(env)
    while len(segs) > 1 and fragment(segs[-1]):
        print(f"    (Fetzen am Ende entfernt: {(segs[-1][1] - segs[-1][0]) * 10} ms)")
        segs.pop()
    if short:
        for i in range(len(segs) - 1):
            if segs[i + 1][0] - segs[i][1] >= 20:
                segs = segs[:i + 1]
                break
    a = max(0, segs[0][0] * W - 400)
    b = min(len(pcm), segs[-1][1] * W + 12 * W)
    out = pcm[a:b]
    fi, fo = min(80, len(out)), min(640, len(out))
    for i in range(fi):
        out[i] = int(out[i] * i / fi)
    for i in range(fo):
        out[len(out) - 1 - i] = int(out[len(out) - 1 - i] * i / fo)
    return out


def to_clip(raw, short=False):
    pcm = trim([int.from_bytes(raw[i:i + 2], "little", signed=True) for i in range(0, len(raw) - 1, 2)], short)
    gain = 29000 / max(1, max(abs(v) for v in pcm))
    return bytes(ulaw(int(v * gain)) for v in pcm)


def main():
    use_say = "--say" in sys.argv
    key = os.environ.get("HEYGEN_API_KEY", "")
    if not use_say and not key:
        raise SystemExit("HEYGEN_API_KEY fehlt (oder --say für die macOS-Stimmen)")
    lines = subprocess.run([os.path.join(ROOT, "sim", "bcsim"), "--voice"], check=True, capture_output=True, text=True).stdout
    entries = [l.split("\t", 1) for l in lines.splitlines() if "\t" in l]
    blob = bytearray()
    index = []
    with tempfile.TemporaryDirectory() as tmp:
        for k, text in entries:
            lang = k[:2]
            clip = to_clip(pcm_say(text, lang, tmp) if use_say else pcm_heygen(text, lang, key, tmp), len(text) <= 16)
            index.append((k, len(blob), len(clip)))
            blob += clip
            print(f"{k:8s} {len(clip) / RATE:4.1f} s  {text}")
    source = "macOS „Anna“ (de) und „Samantha“ (en)" if use_say else "HeyGen-Stimmklon „tobias_pitch“"
    with open(os.path.join(ROOT, "firmware", "voice.bin"), "wb") as f:
        f.write(blob)
    with open(os.path.join(ROOT, "firmware", "voice_index.h"), "w", encoding="utf-8") as f:
        f.write("// Erzeugt von tools/mkvoice.py – nicht von Hand ändern.\n")
        f.write(f"// Sprach-Clips (G.711 µ-law, 16 kHz mono) in firmware/voice.bin; Stimme: {source}.\n")
        f.write("#pragma once\n#include <cstdint>\n\n")
        f.write("struct VoiceClip { const char* key; uint32_t off, len; };\n")
        f.write("static const VoiceClip VOICE_CLIPS[] = {\n")
        for k, off, n in index:
            f.write(f'    {{"{k}", {off}, {n}}},\n')
        f.write("};\n")
        f.write(f"static constexpr int VOICE_N = {len(index)};\n")
        f.write(f"static constexpr uint32_t VOICE_BYTES = {len(blob)};\n")
    print(f"{len(index)} Clips, {len(blob) / 1024:.0f} KiB, {len(blob) / RATE:.0f} s ({source})")


if __name__ == "__main__":
    sys.exit(main())
