"""Native Bink decoding without any original DLL; private movie test is optional."""
import ctypes
import hashlib
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
class NativeBinkTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp=tempfile.TemporaryDirectory();so=Path(cls.tmp.name)/'bink.so'
        flags=subprocess.check_output(['pkg-config','--cflags','--libs','libavformat','libavcodec','libavutil','libswresample','libswscale'],text=True).split()
        subprocess.run(['cc','-shared','-fPIC','-O2','-DLB_HAVE_FFMPEG',str(ROOT/'runtime/bink_native.c'),'-o',str(so),*flags],check=True)
        cls.lib=ctypes.CDLL(str(so));cls.lib.native_bink_open.argtypes=[ctypes.c_char_p,ctypes.c_char_p,ctypes.c_size_t];cls.lib.native_bink_open.restype=ctypes.c_void_p
        for name in ['close','decode','width','height','frames','rate','rate_div','pixels','audio_rate']:
            getattr(cls.lib,'native_bink_'+name).argtypes=[ctypes.c_void_p]
        cls.lib.native_bink_pixels.restype=ctypes.c_void_p
        cls.lib.native_bink_audio.argtypes=[ctypes.c_void_p,ctypes.POINTER(ctypes.c_size_t)];cls.lib.native_bink_audio.restype=ctypes.POINTER(ctypes.c_float)
        cls.lib.native_bink_copy_bgra.argtypes=[ctypes.c_void_p,ctypes.c_size_t,ctypes.c_int,*([ctypes.c_uint32]*3),ctypes.c_void_p,*([ctypes.c_uint32]*6)]
    @classmethod
    def tearDownClass(cls):cls.tmp.cleanup()
    def test_required_decoders_and_demuxer_available(self):
        self.assertEqual(self.lib.native_bink_available(),1)
    def test_rejects_missing_truncated_and_non_bink(self):
        error=ctypes.create_string_buffer(256)
        for i,data in enumerate([b'',b'BIKi',b'not a movie'*4,b'BIKi'+bytes(32)]):
            p=Path(self.tmp.name)/('bad'+str(i));p.write_bytes(data)
            self.assertFalse(self.lib.native_bink_open(os.fsencode(p),error,len(error)))
            self.assertTrue(error.value)
        self.assertFalse(self.lib.native_bink_open(b'/nonexistent-skydream-movie.bik',error,len(error)))
    def test_partial_tile_clips_and_preserves_padding(self):
        pixels=ctypes.create_string_buffer(bytes(range(48)),48) # 4x3 BGRA
        dest=ctypes.create_string_buffer(bytes([255]*80),80) # 4 rows, 20-byte stride
        self.assertEqual(self.lib.native_bink_copy_bgra(dest,80,20,4,1,1,pixels,4,3,2,2,256,256),1)
        expected=bytearray([255]*80);expected[24:32]=bytes(range(40,48))
        self.assertEqual(dest.raw,bytes(expected))
        self.assertEqual(self.lib.native_bink_copy_bgra(dest,79,20,4,1,1,pixels,4,3,2,2,256,256),0)
        self.assertEqual(dest.raw,bytes(expected))
    @unittest.skipUnless(os.environ.get('SKYDREAM_BINK_TEST_MOVIE'),'set SKYDREAM_BINK_TEST_MOVIE to a privately owned Bink movie')
    def test_private_movie_matches_ffmpeg_frames_and_has_audio(self):
        movie=os.environ['SKYDREAM_BINK_TEST_MOVIE'];error=ctypes.create_string_buffer(256)
        b=self.lib.native_bink_open(os.fsencode(movie),error,len(error));self.assertTrue(b,error.value)
        try:
            width=self.lib.native_bink_width(b);height=self.lib.native_bink_height(b);frames=self.lib.native_bink_frames(b)
            self.assertGreater(width,0);self.assertGreater(height,0);self.assertGreater(frames,0)
            hashes=[];saved_pixels={};max_video_error=0;audio_samples=0;audio_peak=0.0;audio_hash=hashlib.sha256()
            for i in range(frames):
                self.assertEqual(self.lib.native_bink_decode(b),1,'frame '+str(i+1))
                if i in [0,29,89]:
                    saved_pixels[i]=ctypes.string_at(self.lib.native_bink_pixels(b),width*height*4)
                    hashes.append((i,hashlib.sha256(saved_pixels[i]).hexdigest()))
                count=ctypes.c_size_t();pcm=self.lib.native_bink_audio(b,ctypes.byref(count));audio_samples+=count.value
                if count.value:
                    audio_peak=max(audio_peak,max(abs(pcm[j]) for j in range(count.value)))
                    audio_hash.update(ctypes.string_at(pcm,count.value*4))
            self.assertEqual(self.lib.native_bink_decode(b),0)
            if self.lib.native_bink_audio_rate(b)>0:
                self.assertGreater(audio_samples,0);self.assertGreater(audio_peak,0.001)
                raw_audio=subprocess.check_output(['ffmpeg','-v','error','-i',movie,'-map','0:a:0','-f','f32le','-ac','2','-ar',str(self.lib.native_bink_audio_rate(b)),'-'])
                self.assertEqual(hashlib.sha256(raw_audio).hexdigest(),audio_hash.hexdigest())
            # Compare frame hashes to the independently driven FFmpeg CLI.
            for i,digest in hashes:
                raw=subprocess.check_output(['ffmpeg','-v','error','-i',movie,'-vf',f'select=eq(n\\,{i})','-frames:v','1','-f','rawvideo','-pix_fmt','bgra','-'])
                self.assertEqual(len(raw),width*height*4)
                if hashlib.sha256(raw).hexdigest()!=digest:
                    # FFmpeg's scalar and SIMD YUV->RGB paths can differ by
                    # two quantization levels. Reject larger visual errors;
                    # decoded PCM above still must match byte for byte.
                    error=max(abs(x-y) for x,y in zip(raw,saved_pixels[i]))
                    max_video_error=max(max_video_error,error)
                    self.assertLessEqual(error,2,'RGB conversion at frame '+str(i+1))
            print(f'Bink decoded {frames} frames {width}x{height}, {audio_samples} stereo float samples, peak {audio_peak:.6f}; RGB max error {max_video_error}, audio matches FFmpeg byte for byte')
        finally:self.lib.native_bink_close(b)
if __name__=='__main__':unittest.main()
