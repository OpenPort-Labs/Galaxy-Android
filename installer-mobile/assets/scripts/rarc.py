#!/usr/bin/env python3
"""Minimal Yaz0 + RARC reader used for surveying and cooking game data."""
import ctypes
import os
import struct

_dll = None
_dll_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'cook', 'native', 'yaz0.dll')
if os.path.exists(_dll_path):
    _dll = ctypes.CDLL(_dll_path)
    _dll.yaz0_decode.restype = ctypes.c_longlong
    _dll.yaz0_decode.argtypes = [ctypes.c_char_p, ctypes.c_size_t, ctypes.c_char_p, ctypes.c_size_t]


def yaz0_decompress(data):
    if data[:4] != b'Yaz0':
        return data
    if _dll is not None:
        size = struct.unpack_from('>I', data, 4)[0]
        out = ctypes.create_string_buffer(size)
        n = _dll.yaz0_decode(bytes(data), len(data), out, size)
        if n != size:
            raise ValueError('bad Yaz0 stream')
        return out.raw
    size = struct.unpack_from('>I', data, 4)[0]
    out = bytearray(size)
    src = 16
    dst = 0
    while dst < size:
        code = data[src]
        src += 1
        for bit in range(8):
            if dst >= size:
                break
            if code & (0x80 >> bit):
                out[dst] = data[src]
                dst += 1
                src += 1
            else:
                b1 = data[src]
                b2 = data[src + 1]
                src += 2
                dist = ((b1 & 0x0F) << 8) | b2
                copy_src = dst - dist - 1
                n = b1 >> 4
                if n == 0:
                    n = data[src] + 0x12
                    src += 1
                else:
                    n += 2
                for _ in range(n):
                    out[dst] = out[copy_src]
                    dst += 1
                    copy_src += 1
    return bytes(out)


class RarcFile:
    def __init__(self, name, data, is_dir=False):
        self.name = name
        self.data = data
        self.is_dir = is_dir


def rarc_list(data):
    """Yields (path, bytes) for every file in a RARC archive."""
    if data[:4] != b'RARC':
        raise ValueError('not RARC')
    data_off = struct.unpack_from('>I', data, 0x0C)[0] + 0x20
    info = 0x20
    num_nodes, node_off, num_entries, entry_off, str_size, str_off = struct.unpack_from('>IIIIII', data, info)
    node_off += 0x20
    entry_off += 0x20
    str_off += 0x20

    def cstr(off):
        end = data.index(b'\0', str_off + off)
        return data[str_off + off:end].decode('shift_jis', errors='replace')

    nodes = []
    for i in range(num_nodes):
        ident, name_ofs, name_hash, n_ent, first = struct.unpack_from('>IIHHI', data, node_off + i * 0x10)
        nodes.append((cstr(name_ofs), n_ent, first))

    def walk(node_idx, prefix):
        name, n_ent, first = nodes[node_idx]
        for e in range(first, first + n_ent):
            fid, h, flags_type, name_ofs, d_off, d_size = struct.unpack_from('>HHHHII', data, entry_off + e * 0x14)
            ename = cstr(name_ofs)
            ftype = flags_type >> 8
            if ftype & 0x02:  # directory
                if ename in ('.', '..'):
                    continue
                yield from walk(d_off, prefix + ename + '/')
            else:
                yield prefix + ename, data[data_off + d_off:data_off + d_off + d_size]

    yield from walk(0, nodes[0][0] + '/')
