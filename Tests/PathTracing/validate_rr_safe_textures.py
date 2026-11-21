"""WARP texture coverage, numeric envelope and unchanged accepted-surface inputs."""
import argparse
import json
import math
from pathlib import Path
import struct
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runner', type=Path, required=True)
    parser.add_argument('--legacy-shader', type=Path, required=True)
    parser.add_argument('--safe-shader', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    out = args.output.resolve()
    if out.exists() and any(out.iterdir()):
        parser.error('Output must be empty')
    out.mkdir(parents=True, exist_ok=True)
    results = []
    for width, height in [(1, 1), (7, 9), (17, 19)]:
        source = out / f'{width}x{height}' / 'input'
        source.mkdir(parents=True)
        identity = [float(i % 5 == 0) for i in range(16)]
        (source / 'frame.bin').write_bytes(struct.pack('<16f2I', *identity, width, height))
        channels = [4, 4, 1, 2, 4, 4, 4, 4]
        data = [[] for _ in channels]
        expected = []
        for i in range(width * height):
            test = i % 12
            values = [[.25, 3, 4, 1], [0, 0, 1, .5], [12], [.2, -.4],
                      [.8, .6, .2, 1], [.04, .04, .04, 0], [1, 2, 3, 1], [2, 1, 2, 0]]
            status = 0
            if test == 1:
                values[0][0], status = float('nan'), 4
            elif test == 2:
                values[2][0], status = 0, 3
            elif test == 3:
                values[1][3], status = -1, 2
            elif test == 4:
                values[6][3], status = 0, 1
            elif test == 5:
                values[7][1], status = -1, 5
            elif test == 6:
                values[0][0], status = 70000, 7
            elif test == 7:
                values[3][0], status = 3, 7
            elif test == 8:
                values[7][0], status = 70000, 7
            elif test == 9:
                values[0][0] = values[2][0] = values[7][0] = 65504
                values[3] = [2, -2]
            elif test == 10:
                values[5][0], status = float('inf'), 2
            elif test == 11:
                values[1][2], status = 0, 3
            expected.append(status)
            for slot in range(8):
                data[slot].extend(values[slot])
        for slot, values in enumerate(data):
            (source / f'in{slot}.bin').write_bytes(struct.pack(f'<{len(values)}f', *values))
        outputs = []
        for mode, shader in [('legacy', args.legacy_shader), ('safe', args.safe_shader)]:
            destination = source.parent / mode
            subprocess.run([str(args.runner.resolve()), str(shader.resolve()), str(source), str(destination)],
                           check=True, timeout=60)
            buffers = []
            for slot, count in enumerate([4, 4, 4, 4, 1, 1, 2, 1, 1]):
                raw = (destination / f'out{slot}.bin').read_bytes()
                assert len(raw) == width * height * count * 4
                buffers.append(struct.unpack(f'<{len(raw)//4}{"I" if slot == 8 else "f"}', raw))
            outputs.append(buffers)
        legacy, safe = outputs
        placeholder = [(0, 0, 0, 1), (0, 0, 0, 1), (0, 0, 0, 1), (0, 0, 1, 0),
                       (1,), (1,), (0, 0), (0,)]
        for pixel, status in enumerate(expected):
            assert safe[8][pixel] == status, (pixel, status, safe[8][pixel])
            for slot, tuple_value in enumerate(placeholder):
                count = len(tuple_value)
                actual = safe[slot][pixel*count:(pixel+1)*count]
                assert all(math.isfinite(v) for v in actual)
                reference = tuple_value if status else legacy[slot][pixel*count:(pixel+1)*count]
                assert actual == reference, (pixel, slot, actual, reference)
        results.append(dict(width=width, height=height, pixels=width*height, passed=True))
    (out / 'report.json').write_text(json.dumps(dict(status='passed', cases=results), indent=2), encoding='utf-8')
    print('WARP texture safety/coverage/accepted-input parity passed:', results)


if __name__ == '__main__':
    main()
