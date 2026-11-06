"""Reads guest memory dumps (F2 with BLOCKPLANET_DUMP_DIR): the 8 MB are kept
as 32-bit words in host order; load() returns big-endian bytes indexed by
address & 0x7FFFFF."""
import struct
import numpy as np


def load(path):
    return np.fromfile(path, dtype="<u4").astype(">u4").tobytes()


def u32(data, address):
    return struct.unpack(">I", data[address & 0x7FFFFF:(address & 0x7FFFFF) + 4])[0]


def s16(data, address):
    return struct.unpack(">h", data[address & 0x7FFFFF:(address & 0x7FFFFF) + 2])[0]
