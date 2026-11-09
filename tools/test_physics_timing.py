# SPDX-License-Identifier: GPL-3.0-only
"""Realtime physics limits, including sub-millisecond and changing frame times."""
import ctypes as C
from pathlib import Path
import math
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
class PhysicsTiming(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        so = Path(cls.tmp.name) / 'timing.so'
        subprocess.run(['cc', '-shared', '-fPIC', '-O2', '-Wall', '-Wextra', '-Werror',
                        str(ROOT/'runtime/physics_timing.c'), '-lm', '-o', str(so)], check=True)
        cls.lib = C.CDLL(str(so))
        cls.fn = cls.lib.skydream_physics_limits
        cls.fn.argtypes = [C.c_double, C.POINTER(C.c_float), C.POINTER(C.c_float)]
        cls.fn.restype = C.c_int
    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()
    def test_realtime_intervals_without_refresh_or_fps_ceiling(self):
        for fps in [15, 30, 60, 75, 100, 120, 144, 240, 1000, 2000, 10000, 1000000]:
            n, c = C.c_float(), C.c_float()
            self.assertEqual(self.fn(1/fps, C.byref(n), C.byref(c)), 1)
            self.assertAlmostEqual(n.value, 1/max(60, fps), delta=1e-8/max(1,fps/60))
            self.assertAlmostEqual(c.value, 1/(max(60, fps)-30), delta=1e-8/max(1,fps/60))
            self.assertGreater(n.value, 0)
        # The old fixed 1/60 budget is 33.3 times a 2000-Hz frame duration.
        self.assertGreater((1/60)/(1/2000), 33)
    def test_no_smoothing_lag_after_sharp_rate_changes(self):
        n, c = C.c_float(), C.c_float()
        for seconds in [1/100, 1/2000, 1/30, 1/240, 1/10000, 0.7, 1/2000]:
            self.assertEqual(self.fn(seconds, C.byref(n), C.byref(c)), 1)
            self.assertLessEqual(n.value, min(seconds, 1/60)*1.000001)
    def test_invalid_values_do_not_destroy_last_valid_settings(self):
        for seconds in [0, -1, float('nan'), float('inf'), -float('inf'), 1e-300]:
            n, c = C.c_float(0.01), C.c_float(0.02)
            before = n.value, c.value
            self.assertEqual(self.fn(seconds, C.byref(n), C.byref(c)), 0)
            self.assertEqual((n.value,c.value), before)


class FixedPhysicsClock(unittest.TestCase):
    class Clock(C.Structure):
        _fields_ = [('remainder', C.c_double), ('discarded', C.c_double), ('ticks', C.c_uint64)]
    class Batch(C.Structure):
        _fields_ = [('steps', C.c_uint), ('seconds', C.c_double), ('alpha', C.c_double)]
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        so = Path(cls.tmp.name)/'fixed.so'
        subprocess.run(['cc','-shared','-fPIC','-O2','-Wall','-Wextra','-Werror',
                        str(ROOT/'runtime/physics_timing.c'),'-lm','-o',str(so)],check=True)
        cls.lib=C.CDLL(str(so));cls.fn=cls.lib.skydream_physics_advance
        cls.fn.argtypes=[C.POINTER(cls.Clock),C.c_double,C.c_double,C.c_int]
        cls.fn.restype=cls.Batch
    @classmethod
    def tearDownClass(cls):cls.tmp.cleanup()
    def test_render_rate_does_not_change_simulated_ticks(self):
        for fps in [15,30,60,100,144,240,1000,2000,10000]:
            clock=self.Clock();steps=0
            for _ in range(fps*2):
                batch=self.fn(C.byref(clock),1/fps,1,1)
                self.assertLessEqual(batch.steps,8)
                self.assertAlmostEqual(batch.seconds,batch.steps/60)
                self.assertGreaterEqual(batch.alpha,0);self.assertLessEqual(batch.alpha,1)
                steps+=batch.steps
            self.assertEqual(steps,120,fps);self.assertEqual(clock.ticks,120)
            self.assertAlmostEqual(clock.remainder,0,places=10)
            self.assertEqual(clock.discarded,0)
    def test_pause_slow_motion_and_private_timeline_state(self):
        a,b=self.Clock(),self.Clock()
        self.fn(C.byref(a),1/120,1,1)
        remainder=a.remainder
        self.assertEqual(self.fn(C.byref(a),100,1,0).steps,0)
        self.assertEqual(a.remainder,remainder)
        self.assertEqual(a.discarded,0)
        for _ in range(2000):self.fn(C.byref(b),0.0005,0.1,1)
        self.assertEqual(b.ticks,6);self.assertEqual(a.ticks,0)
        self.assertEqual(self.fn(C.byref(a),1/120,1,1).steps,1)
        self.assertEqual(self.fn(C.byref(b),1,0,1).steps,0)
    def test_stall_bounded_work_and_time_accounting(self):
        clock=self.Clock()
        first=self.fn(C.byref(clock),2,1,1)
        self.assertEqual(first.steps,8)
        self.assertAlmostEqual(clock.discarded,1.75)
        second=self.fn(C.byref(clock),0,1,1)
        self.assertEqual(second.steps,7)
        self.assertAlmostEqual(first.seconds+second.seconds+clock.remainder+clock.discarded,2)
        self.assertAlmostEqual(clock.remainder,0,places=10)
    def test_invalid_inputs_preserve_state(self):
        clock=self.Clock();self.fn(C.byref(clock),0.005,1,1)
        before=bytes(clock)
        for elapsed,scale in [(float('nan'),1),(-1,1),(1,float('inf')),(1,-1)]:
            self.assertEqual(self.fn(C.byref(clock),elapsed,scale,1).steps,0)
            self.assertEqual(bytes(clock),before)

if __name__ == '__main__':
    unittest.main()
