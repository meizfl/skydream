# SPDX-License-Identifier: GPL-3.0-only
"""WMA decoder regression: compare PCM and streaming with FFmpeg's decoder.

The fixture is an encoded synthetic tone, not a game asset. xWMA's zero-extra
configuration is also checked against user-provided .xwm files during QA.
"""
import ctypes
import os
from pathlib import Path
import shlex
import shutil
import struct
import subprocess
import tempfile
import unittest


def wave_chunks(data):
    offset = 12
    chunks = {}
    while offset + 8 <= len(data):
        name, size = struct.unpack_from('<4sI', data, offset)
        chunks[name] = data[offset + 8:offset + 8 + size]
        offset += 8 + size + (size & 1)
    return chunks


def decoder_library(directory):
    compiler = shutil.which('clang') or shutil.which('cc')
    if not compiler or not shutil.which('pkg-config') or not shutil.which('ffmpeg'):
        raise unittest.SkipTest('compiler, FFmpeg and pkg-config required')
    flags = subprocess.run(['pkg-config', '--cflags', '--libs', 'libavcodec', 'libavutil', 'libswresample'],
                           capture_output=True, text=True)
    if flags.returncode:
        raise unittest.SkipTest('FFmpeg development libraries not installed')
    runtime = Path(__file__).resolve().parent.parent / 'runtime'
    output = Path(directory) / 'libxwma-test.so'
    subprocess.run([compiler, '-O2', '-shared', '-fPIC', '-DLB_HAVE_FFMPEG',
                    str(runtime / 'xwma.c'), '-o', str(output), *shlex.split(flags.stdout)],
                   check=True, capture_output=True, timeout=30)
    lib = ctypes.CDLL(str(output))
    lib.xwma_create.argtypes = [ctypes.c_uint, *([ctypes.c_int] * 5), ctypes.c_void_p, ctypes.c_size_t]
    lib.xwma_create.restype = ctypes.c_void_p
    lib.xwma_decode.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_uint,
                               ctypes.c_int, ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(ctypes.c_uint32)]
    lib.xwma_destroy.argtypes = [ctypes.c_void_p]
    lib.xwma_reset.argtypes = [ctypes.c_void_p]
    return lib


def decode_wave(lib, wave, streaming=False):
    chunks = wave_chunks(wave)
    fmt, data = chunks[b'fmt '], chunks[b'data']
    tag, channels, rate, avg, align, bits, extra_size = struct.unpack_from('<HHIIHHH', fmt)
    extra = ctypes.create_string_buffer(fmt[18:18 + extra_size])
    decoder = lib.xwma_create(tag, channels, rate, avg, align, bits, extra, extra_size)
    if not decoder:
        raise AssertionError('decoder initialization failed')
    free = ctypes.CDLL(None).free
    free.argtypes = [ctypes.c_void_p]
    try:
        results = []
        # Reuse a source voice for a second stream after EOS.
        for _ in range(2):
            pcm = bytearray()
            stride = align if streaming else len(data)
            for offset in range(0, len(data), stride):
                block = data[offset:offset + stride]
                payload = ctypes.create_string_buffer(block)
                output, frames = ctypes.c_void_p(), ctypes.c_uint32()
                rc = lib.xwma_decode(decoder, payload, len(block), 0,
                                     int(offset + len(block) == len(data)), ctypes.byref(output), ctypes.byref(frames))
                if rc < 0:
                    raise AssertionError('decode failed: %d' % rc)
                if output.value:
                    pcm.extend(ctypes.string_at(output, frames.value * channels * 2))
                    free(output)
            results.append(bytes(pcm))
        if results[0] != results[1]:
            raise AssertionError('reusing a source voice changed decoded samples')
        return results[0]
    finally:
        lib.xwma_destroy(decoder)


class WmaDecoderTest(unittest.TestCase):
    def test_decoded_pcm_and_packet_streaming(self):
        with tempfile.TemporaryDirectory() as tmp:
            lib = decoder_library(tmp)
            for channels, rate in ((1, 44100), (2, 48000)):
                with self.subTest(channels=channels, rate=rate):
                    wav = Path(tmp) / 'tone.wav'
                    subprocess.run(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-y',
                                    '-f', 'lavfi', '-i', 'sine=frequency=440:duration=0.3',
                                    '-ar', str(rate), '-ac', str(channels), '-c:a', 'wmav2',
                                    '-b:a', '64k', '-f', 'wav', str(wav)], check=True, timeout=30)
                    reference = subprocess.run(['ffmpeg', '-hide_banner', '-loglevel', 'error',
                                                '-i', str(wav), '-f', 's16le', '-'],
                                               check=True, capture_output=True, timeout=30).stdout
                    self.assertTrue(any(reference))
                    self.assertEqual(decode_wave(lib, wav.read_bytes()), reference)
                    self.assertEqual(decode_wave(lib, wav.read_bytes(), streaming=True), reference)


if __name__ == '__main__':
    unittest.main()
