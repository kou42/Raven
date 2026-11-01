"""python MessagePackInterop.py /path/to/raven-messagepack-interop (requires msgpack)."""
import math
import random
import struct
import subprocess
import sys

import msgpack


def exchange(payload):
    return subprocess.run([sys.argv[1]], input=payload, capture_output=True, check=True).stdout


def equal(expected, actual):
    assert type(expected) is type(actual), (type(expected), type(actual))
    if isinstance(expected, float):
        assert expected == actual
        if expected == 0:
            assert math.copysign(1, expected) == math.copysign(1, actual)
    elif isinstance(expected, list):
        assert len(expected) == len(actual)
        for a, b in zip(expected, actual):
            equal(a, b)
    elif isinstance(expected, dict):
        assert expected.keys() == actual.keys()
        for key in expected:
            equal(expected[key], actual[key])
    else:
        assert expected == actual


rng = random.Random(348)
values = [None, True, False, 0, -1, -(2**63), 2**64 - 1, -0.0, 1.5,
          "日本語\x00\n\\\"😀", b"\x00\xff", [], {},
          {"z": [None, True, {"a": 2**64 - 1}], "a": b"\xff"}]
for size in (0, 15, 16, 31, 32, 255, 256, 65535, 65536):
    values.extend(["x" * size, b"\xff" * size, [None] * size,
                   {str(i): i for i in range(size)}])
for _ in range(200):
    values.append({"integer": rng.randrange(-(2**63), 2**64),
                   "float": rng.uniform(-1e100, 1e100),
                   "array": [rng.randrange(-1000, 1000) for _ in range(rng.randrange(20))]})
for value in values:
    encoded = msgpack.packb(value, use_bin_type=True)
    raven = exchange(encoded)
    equal(value, msgpack.unpackb(raven, raw=False, strict_map_key=True))
    # Raven出力を再入力しても決定的な表現を維持することを検査します。
    assert exchange(raven) == raven
for number in (-0.0, 1.5, 2**-149, 3.4028234663852886e38):
    payload = b"\xca" + struct.pack(">f", number)
    equal(msgpack.unpackb(payload), msgpack.unpackb(exchange(payload)))
print(f"MessagePack interop: {len(values) + 4} cases passed (msgpack {msgpack.__version__})")
