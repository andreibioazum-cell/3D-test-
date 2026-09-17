#!/usr/bin/env python3
"""Author Enjoer's original PNG tiles and short PCM16 effects (offline, not in-game)."""
from pathlib import Path
import math
import random
import struct
import wave
import zlib

ROOT = Path(__file__).resolve().parents[2]
SIZE = 32
BASE = {
    'grass_top': (130, 174, 70), 'grass_side': (117, 82, 48),
    'dirt': (117, 82, 48), 'stone': (133, 136, 132),
    'sand': (218, 205, 155), 'water': (58, 135, 184),
    'log_side': (119, 88, 48), 'log_top': (179, 141, 83),
    'leaves': (83, 136, 55),
}
DELTA = [-19, -13, -8, -3, 3, 8, 13, 19]


def png(path, pixels):
    def chunk(tag, data):
        return struct.pack('>I', len(data)) + tag + data + struct.pack('>I', zlib.crc32(tag + data) & 0xffffffff)
    raw = b''.join(b'\0' + bytes(c for pixel in row for c in (*pixel, 255)) for row in pixels)
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>2I5B', SIZE, SIZE, 8, 6, 0, 0, 0))
                     + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


def textures():
    folder = ROOT / 'assets/textures'
    folder.mkdir(parents=True, exist_ok=True)
    for number, (name, base) in enumerate(BASE.items()):
        rng = random.Random(437 + number)
        patches = [[rng.randrange(1, 7) for _ in range(16)] for _ in range(16)]
        grass_edge = [rng.randrange(4, 8) for _ in range(SIZE)]
        pixels = []
        for y in range(SIZE):
            row = []
            for x in range(SIZE):
                k = max(0, min(7, patches[y//2][x//2] + rng.choice([-1, 0, 0, 1])))
                color = base
                if name == 'grass_side' and y < grass_edge[x]:
                    color = BASE['grass_top']
                if name == 'stone' and (x + 2*y + (y//7)*3) % 23 == 0:
                    k = 1
                if name == 'water':
                    k = 6 if (y + x//7) % 11 == 0 else 3 + rng.randrange(2)
                if name == 'log_side':
                    k = rng.randrange(3) if (x//3) % 3 == 0 else 3 + rng.randrange(4)
                if name == 'log_top':
                    radius = max(abs(x-15), abs(y-15))
                    k = 2 + (radius//2) % 4
                d = DELTA[k] // (2 if name in ('sand', 'water') else 1)
                row.append(tuple(max(0, min(255, channel+d)) for channel in color))
            pixels.append(row)
        png(folder / f'{name}.png', pixels)


def effects():
    folder = ROOT / 'assets/sounds'
    folder.mkdir(parents=True, exist_ok=True)
    for name, duration in [('jump', .09), ('break', .095), ('place', .075)]:
        rng = random.Random(32)
        samples = []
        smooth_noise = 0.0
        for i in range(int(44100 * duration)):
            t = i / 44100
            envelope = (1 - t/duration) ** 2 * min(1, t/.004)
            smooth_noise = smooth_noise*.65 + rng.uniform(-1, 1)*.35
            if name == 'jump':
                signal = math.sin(2*math.pi*(390*t+900*t*t))*.16
            elif name == 'break':
                signal = smooth_noise*.5 + math.sin(2*math.pi*95*t)*.08
            else:
                signal = smooth_noise*.17 + math.sin(2*math.pi*180*t)*.15
            samples.append(int(signal*envelope*32767))
        with wave.open(str(folder / f'{name}.wav'), 'wb') as stream:
            stream.setparams((1, 2, 44100, 0, 'NONE', 'not compressed'))
            stream.writeframes(struct.pack(f'<{len(samples)}h', *samples))


if __name__ == '__main__':
    textures()
    effects()
