#!/usr/bin/env python3
"""Append an Attachments element to the end of an FFmpeg-muxed Matroska file
(after Clusters and Cues) and add a SeekHead entry for it, so a demuxer can
only reach it through the SeekHead. Offsets of everything else are kept: the
SeekHead is rewritten inside the space FFmpeg reserved for it (SeekHead + Void).

usage: mkv_attach_at_end.py in.mkv out.mkv attachment.bin
"""
import sys

ID_SEGMENT, ID_SEEKHEAD, ID_VOID = 0x18538067, 0x114D9B74, 0xEC
ID_SEEK, ID_SEEKID, ID_SEEKPOS = 0x4DBB, 0x53AB, 0x53AC
ID_ATTACHMENTS, ID_ATTACHEDFILE = 0x1941A469, 0x61A7
ID_FILENAME, ID_MIME, ID_DATA, ID_UID = 0x466E, 0x4660, 0x465C, 0x46AE


def vint(buf, pos, keep_marker):
    first = buf[pos]
    n = 1
    while not first & (0x80 >> (n - 1)):
        n += 1
    val = first if keep_marker else first & (0xFF >> n)
    for b in buf[pos + 1:pos + n]:
        val = val << 8 | b
    return val, pos + n


def elem(buf, pos):  # -> id, data_start, data_size
    eid, pos = vint(buf, pos, True)
    size, pos = vint(buf, pos, False)
    return eid, pos, size


def enc_id(eid):
    return eid.to_bytes((eid.bit_length() + 7) // 8, "big")


def enc_size(n, width=None):
    width = width or next(w for w in range(1, 9) if n < (1 << 7 * w) - 1)
    return ((1 << 7 * width) | n).to_bytes(width, "big")


def mk(eid, payload):
    return enc_id(eid) + enc_size(len(payload)) + payload


def uint(eid, n):
    return mk(eid, n.to_bytes(max(1, (n.bit_length() + 7) // 8), "big"))


def main(src, dst, attachment):
    buf = bytearray(open(src, "rb").read())
    _, pos, n = elem(buf, 0)                       # EBML header
    pos += n
    seg_id, seg_data, seg_size = elem(buf, pos)
    assert seg_id == ID_SEGMENT
    size_field = (pos + len(enc_id(ID_SEGMENT)), seg_data)

    sh_id, sh_data, sh_size = elem(buf, seg_data)
    assert sh_id == ID_SEEKHEAD, "SeekHead must be the first Segment child"
    void_id, void_data, void_size = elem(buf, sh_data + sh_size)
    assert void_id == ID_VOID, "no reserved space after the SeekHead"
    region = (seg_data, void_data + void_size)

    seeks, p = [], sh_data
    while p < sh_data + sh_size:
        eid, d, n = elem(buf, p)
        if eid == ID_SEEK:
            seeks.append(bytes(buf[p:d + n]))
        p = d + n                                  # drops the CRC-32
    attach_pos = len(buf) - seg_data
    seeks.append(mk(ID_SEEK, mk(ID_SEEKID, enc_id(ID_ATTACHMENTS)) + uint(ID_SEEKPOS, attach_pos)))
    head = mk(ID_SEEKHEAD, b"".join(seeks))
    rest = region[1] - region[0] - len(head)
    assert rest >= 9, "no room for one more SeekHead entry"
    head += enc_id(ID_VOID) + enc_size(rest - 9, 8) + bytes(rest - 9)
    buf[region[0]:region[1]] = head

    data = open(attachment, "rb").read()
    att = mk(ID_ATTACHEDFILE, mk(ID_FILENAME, b"end.ttf") +
             mk(ID_MIME, b"application/x-truetype-font") +
             mk(ID_DATA, data) + uint(ID_UID, 0x5A))
    buf += enc_id(ID_ATTACHMENTS) + enc_size(len(att), 8) + att

    width = size_field[1] - size_field[0]
    if seg_size != (1 << 7 * width) - 1:           # known size: grow it
        buf[size_field[0]:size_field[1]] = enc_size(len(buf) - seg_data, width)
    open(dst, "wb").write(buf)


if __name__ == "__main__":
    main(*sys.argv[1:])
