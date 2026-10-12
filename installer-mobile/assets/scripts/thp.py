"""THP movies: the player and THPAudioDecode() read the file header, the
per-frame headers and the audio record headers natively, so those become
little-endian.  Video components (JPEG byte streams) and ADPCM data are byte
oriented and stay as they are.

The file is copied and then patched in place, since movies are hundreds of
megabytes.
"""
import shutil
import struct

AUDIO_HEADER_HALFWORDS = 16 + 16 + 4  # lCoef[8][2], rCoef[8][2], lYn1/2, rYn1/2


def _swap32(fh, off, count):
    fh.seek(off)
    raw = fh.read(4 * count)
    fh.seek(off)
    fh.write(struct.pack('<%dI' % count, *struct.unpack('>%dI' % count, raw)))
    return struct.unpack('>%dI' % count, raw)


def _swap16(fh, off, count):
    fh.seek(off)
    raw = fh.read(2 * count)
    fh.seek(off)
    fh.write(struct.pack('<%dH' % count, *struct.unpack('>%dH' % count, raw)))


def cook_thp(src, dst):
    """Writes the little-endian movie to dst; returns the frame count."""
    with open(src, 'rb') as fh:
        head = fh.read(0x30)
    if head[:4] != b'THP\0' or head[4:8] != b'\x00\x01\x10\x00':
        raise ValueError('%s: not a version 1.1 THP file' % src)
    shutil.copyfile(src, dst)
    with open(dst, 'r+b') as fh:
        fields = _swap32(fh, 4, 11)  # everything after the magic (frameRate swaps like a u32)
        (_version, _buf, _maxSamples, _rate, num_frames, first_size, _data_size,
         comp_off, _offsets_off, movie_off, _final_off) = fields

        ncomp = _swap32(fh, comp_off, 1)[0]
        fh.seek(comp_off + 4)
        kinds = list(fh.read(16)[:ncomp])
        pos = comp_off + 0x14
        tracks = 1
        for kind in kinds:
            if kind == 0:    # video: xSize, ySize, videoType
                _swap32(fh, pos, 3)
                pos += 12
            elif kind == 1:  # audio: channels, frequency, samples, tracks
                tracks = _swap32(fh, pos, 4)[3]
                pos += 16
            else:
                raise ValueError('%s: unknown THP component %d' % (src, kind))

        off, size = movie_off, first_size
        for _ in range(num_frames):
            words = _swap32(fh, off, 2 + ncomp)
            next_size, comp_sizes = words[0], words[2:]
            data = off + 8 + 4 * ncomp
            for kind, comp_size in zip(kinds, comp_sizes):
                if kind == 1:
                    for t in range(tracks):
                        rec = data + comp_size * t
                        _swap32(fh, rec, 2)  # offsetNextChannel, sampleSize
                        _swap16(fh, rec + 8, AUDIO_HEADER_HALFWORDS)
                data += comp_size
            off += size
            size = next_size
    return num_frames
