#!/usr/bin/env python3
"""Cut a 16-bit PCM WAV to its first SECONDS and summarise it.

    wav_summary.py IN.wav OUT.wav [SECONDS]

Prints one line: rms_dbfs=<mono RMS> peak_dbfs=<largest sample> peak_hz=<loudest bin>.
"""

import array
import cmath
import math
import sys
import wave

FFT_SIZE = 8192


def fft(values):
    n = len(values)
    bits = n.bit_length() - 1
    out = [values[int(format(i, f"0{bits}b")[::-1], 2)] for i in range(n)]
    size = 2
    while size <= n:
        step = cmath.exp(-2j * math.pi / size)
        half = size // 2
        for start in range(0, n, size):
            w = 1
            for k in range(half):
                a = out[start + k]
                b = out[start + k + half] * w
                out[start + k] = a + b
                out[start + k + half] = a - b
                w *= step
        size *= 2
    return out


def main():
    if len(sys.argv) not in (3, 4):
        sys.exit(__doc__)
    seconds = float(sys.argv[3]) if len(sys.argv) == 4 else 10.0
    with wave.open(sys.argv[1], "rb") as src:
        channels, width, rate = src.getnchannels(), src.getsampwidth(), src.getframerate()
        if width != 2:
            sys.exit("expected 16-bit samples")
        count = int(rate * seconds)
        if src.getnframes() < count:
            sys.exit(f"{sys.argv[1]}: {src.getnframes() / rate:.3f} s, shorter than {seconds} s")
        raw = src.readframes(count)
    with wave.open(sys.argv[2], "wb") as dst:
        dst.setnchannels(channels)
        dst.setsampwidth(width)
        dst.setframerate(rate)
        dst.writeframes(raw)

    samples = array.array("h", raw)
    if sys.byteorder == "big":
        samples.byteswap()
    mono = [sum(samples[i:i + channels]) / (channels * 32768.0) for i in range(0, len(samples), channels)]

    rms = math.sqrt(sum(v * v for v in mono) / len(mono))
    peak = max(abs(v) for v in samples) / 32768.0

    window = [0.5 - 0.5 * math.cos(2 * math.pi * i / FFT_SIZE) for i in range(FFT_SIZE)]
    power = [0.0] * (FFT_SIZE // 2)
    for start in range(0, len(mono) - FFT_SIZE + 1, FFT_SIZE):
        spectrum = fft([mono[start + i] * window[i] for i in range(FFT_SIZE)])
        for k in range(1, FFT_SIZE // 2):
            power[k] += abs(spectrum[k]) ** 2
    loudest = max(range(1, FFT_SIZE // 2), key=power.__getitem__)

    def db(v):
        return 20 * math.log10(v) if v > 0 else -math.inf

    print(f"rms_dbfs={db(rms):.2f} peak_dbfs={db(peak):.2f} peak_hz={loudest * rate / FFT_SIZE:.1f}")


if __name__ == "__main__":
    main()
