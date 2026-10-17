"""Deterministic checks on the generated GX vertex shaders.

The generator is plain string building (platform/src/gx/gl_shadergen.cpp), so
the part that decides what a lit vertex computes can be checked on the host: a
small probe is compiled against the real generator and the emitted GLSL is
inspected.  No GPU and no Android toolchain are involved.

Skipped, not silently passed, when no C++ compiler is available.
"""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
GX = ROOT / 'platform' / 'src' / 'gx'
PROBE_SRC = Path(__file__).resolve().parent / 'shadergen_probe.cpp'
GENERATOR_SRC = GX / 'gl_shadergen.cpp'

# XF channel control (GXLight.c: GXSetChanCtrl).  Bit 1 enables the channel,
# bit 2 includes GX_LIGHT0, bits 7-8 are the diffuse function, bit 9 is
# "attenuation enable" and bit 10 is "attenuation select".
ENABLE = 1 << 1
LIGHT0 = 1 << 2
DIFFUSE_CLAMP = 2 << 7
ATTEN_ENABLE = 1 << 9
ATTEN_SELECT = 1 << 10

AF_NONE = ENABLE | LIGHT0 | DIFFUSE_CLAMP
AF_SPEC = AF_NONE | ATTEN_ENABLE
AF_SPOT = AF_NONE | ATTEN_ENABLE | ATTEN_SELECT
UNLIT = LIGHT0 | DIFFUSE_CLAMP  # channel not enabled: material colour only


def find_compiler():
    for name in ('c++', 'g++', 'clang++'):
        path = shutil.which(name)
        if path:
            return path
    return None


@unittest.skipUnless(GENERATOR_SRC.is_file() and PROBE_SRC.is_file(), 'shader generator sources not present')
class ShaderGeneratorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.compiler = find_compiler()
        if not cls.compiler:
            raise unittest.SkipTest('no C++ compiler on PATH (c++, g++ or clang++)')
        cls.tmp = tempfile.TemporaryDirectory()
        cls.probe = Path(cls.tmp.name) / 'shadergen_probe'
        cmd = [cls.compiler, '-std=c++17', '-I', str(GX), '-I', str(ROOT / 'platform' / 'include'),
               str(PROBE_SRC), str(GENERATOR_SRC), '-o', str(cls.probe)]
        built = subprocess.run(cmd, capture_output=True, text=True)
        if built.returncode != 0:
            cls.tmp.cleanup()
            raise AssertionError('the shader generator no longer builds on the host:\n' + built.stderr)

    @classmethod
    def tearDownClass(cls):
        if getattr(cls, 'tmp', None):
            cls.tmp.cleanup()

    def shader(self, ctrl, alpha=None):
        cmd = [str(self.probe), hex(ctrl)] + ([hex(alpha)] if alpha is not None else [])
        out = subprocess.run(cmd, capture_output=True, text=True)
        self.assertEqual(out.returncode, 0, out.stderr)
        return out.stdout

    def test_af_none_does_not_attenuate(self):
        """GX_AF_NONE leaves lighting unattenuated."""
        src = self.shader(AF_NONE)
        self.assertIn('float att = 1.0;', src)
        self.assertNotIn('distDenom', src)
        self.assertNotIn('att = 0.0;', src)

    def test_af_spec_attenuates_specular(self):
        """GX_AF_SPEC computes specular attenuation using dot(nrm, dv)."""
        src = self.shader(AF_SPEC)
        self.assertIn('dot(nrm, dv)', src)
        self.assertIn('distDenom', src)

    def test_af_spot_still_attenuates(self):
        """Spot and distance attenuation keep working for GX_AF_SPOT/GX_AF_DIST."""
        src = self.shader(AF_SPOT)
        self.assertIn('max(0.0, dot(ldir, dv))', src)
        self.assertIn('distDenom', src)

    def test_diffuse_is_never_gated_on_the_specular_angle(self):
        """AF_NONE and AF_SPOT do not gate on the specular angle."""
        for ctrl in (AF_NONE, AF_SPOT):
            with self.subTest(ctrl=hex(ctrl)):
                self.assertNotIn('dot(nrm, dv)', self.shader(ctrl))

    def test_disabled_channel_falls_back_to_the_material_colour(self):
        src = self.shader(UNLIT)
        self.assertIn('v_clr0.rgb = matColor[0].rgb;', src)
        self.assertNotIn('lit = ambColor', src)

    def test_second_channel_is_the_vertex_colour_when_only_one_is_lit(self):
        """numChans == 1: channel 1 passes the vertex colour through unlit."""
        src = self.shader(AF_SPEC)
        self.assertIn('v_clr1 = vclr1;', src)
        self.assertNotIn('v_clr1.rgb = matColor[1].rgb;', src)


if __name__ == '__main__':
    unittest.main()
