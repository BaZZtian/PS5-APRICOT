# SPDX-License-Identifier: GPL-3.0-or-later
"""Bounded backwards-LZ code unpacker; no executable code or game data built in.

The documented backwards-LZ format is also described by ndspy's codeCompression
documentation. That independently licensed implementation is used only as an
external comparison in development, not imported or copied here.
"""
import struct

class InvalidCompression(ValueError):
    pass

def unpack(data, *, max_output=16*1024*1024, required=False):
    data = bytes(data)
    if max_output < 0 or len(data) > max_output:
        raise InvalidCompression('Code input exceeds the output limit')
    candidates = []
    oversized = False
    for appended in range(0, min(32, len(data)-7), 4):
        end = len(data)-appended
        descriptor, extra = struct.unpack_from('<II', data, end-8)
        header, compressed = descriptor >> 24, descriptor & 0xffffff
        if not (8 <= header <= compressed <= end) or extra == 0:
            continue
        if data[end-header:end-8] != b'\xff' * (header-8):
            continue
        if end+extra+appended > max_output:
            oversized = True
            continue
        candidates.append((appended, end, header, compressed, extra))
    if not candidates:
        if oversized:
            raise InvalidCompression('Code expansion exceeds the output limit')
        if required:
            raise InvalidCompression('No valid backwards-LZ footer')
        return data
    results = []
    for appended, end, header, compressed, extra in candidates:
        prefix = end-compressed
        source = end-header
        output = bytearray(end+extra)
        output[:prefix] = data[:prefix]
        write = len(output)
        valid = True
        try:
            while write > prefix:
                if source <= prefix:
                    raise InvalidCompression('Truncated backwards-LZ flags')
                source -= 1
                flags = data[source]
                for bit in range(7, -1, -1):
                    if write == prefix:
                        break
                    if flags & (1 << bit):
                        if source-prefix < 2:
                            raise InvalidCompression('Truncated backwards-LZ reference')
                        source -= 2
                        first, second = data[source+1], data[source]
                        length = (first >> 4)+3
                        displacement = ((first & 15) << 8 | second)+3
                        if length > write-prefix or displacement > len(output)-write:
                            raise InvalidCompression('Backwards-LZ reference outside written output')
                        for _ in range(length):
                            write -= 1
                            output[write] = output[write+displacement]
                    else:
                        if source <= prefix:
                            raise InvalidCompression('Truncated backwards-LZ literal')
                        source -= 1
                        write -= 1
                        output[write] = data[source]
            if source != prefix:
                raise InvalidCompression('Unused bytes in backwards-LZ stream')
        except InvalidCompression:
            valid = False
        if valid:
            results.append(bytes(output)+data[end:])
    if len(results) != 1:
        raise InvalidCompression('Invalid or ambiguous backwards-LZ stream')
    return results[0]
