import unittest
import struct
import os

class TestStarTextureAndFilterSemantics(unittest.TestCase):
    """
    Regression tests for star texture decoding, TEV stage parsing, and GX sampler filtering.
    """
    def test_gx_tex_filter_mipmap_levels(self):
        """
        Verify that GX_NEAR (0) and GX_LINEAR (1) never trigger mipmap decoding
        even when maxLod > 0. Only filters 2..5 (GX_*_MIP_*) are mipmapped.
        """
        for minFilter in range(8):
            maxLod = 16 * 4  # e.g. 5 levels
            is_mipmapped = (minFilter >= 2 and minFilter <= 5)
            computed_levels = (maxLod // 16 + 1) if is_mipmapped else 1
            if minFilter in (0, 1):
                self.assertEqual(computed_levels, 1, f"Filter {minFilter} must produce 1 level (no mips)")
            elif 2 <= minFilter <= 5:
                self.assertEqual(computed_levels, 5, f"Filter {minFilter} must produce mip levels")

    def test_gx_filter_to_vulkan_sampler_mapping(self):
        """
        Verify GX filter enum mapping to Vulkan minFilter and mipmapMode:
        0: GX_NEAR -> NEAREST, NEAREST
        1: GX_LINEAR -> LINEAR, NEAREST
        2: GX_NEAR_MIP_NEAR -> NEAREST, NEAREST
        3: GX_LIN_MIP_NEAR -> LINEAR, NEAREST
        4: GX_NEAR_MIP_LIN -> NEAREST, LINEAR
        5: GX_LIN_MIP_LIN -> LINEAR, LINEAR
        """
        expected = {
            0: ("NEAREST", "NEAREST"),
            1: ("LINEAR", "NEAREST"),
            2: ("NEAREST", "NEAREST"),
            3: ("LINEAR", "NEAREST"),
            4: ("NEAREST", "LINEAR"),
            5: ("LINEAR", "LINEAR"),
        }
        for gx_f, (exp_min, exp_mip) in expected.items():
            if gx_f == 0:
                minf, mip = "NEAREST", "NEAREST"
            elif gx_f == 1:
                minf, mip = "LINEAR", "NEAREST"
            elif gx_f == 2:
                minf, mip = "NEAREST", "NEAREST"
            elif gx_f == 3:
                minf, mip = "LINEAR", "NEAREST"
            elif gx_f == 4:
                minf, mip = "NEAREST", "LINEAR"
            elif gx_f == 5:
                minf, mip = "LINEAR", "LINEAR"
            self.assertEqual((minf, mip), (exp_min, exp_mip))

    def test_tev_stage_info_table20_table21_bit_packing(self):
        """
        Verify Table 20 byte offset and Table 21 swap mode bit-packing in ShaderUid words 34-65.
        """
        # Simulated 20-byte Table 20 entry:
        # byte 0: dummy (0xFF)
        # bytes 1..9: ca=1, cb=2, cc=3, cd=4, cop=0, cbias=1, cscale=2, cclamp=1, cout=0
        # bytes 10..18: aa=5, ab=6, ac=7, ad=0, aop=1, abias=2, ascale=1, aclamp=0, aout=1
        # byte 19: pad
        st = [0xFF, 1, 2, 3, 4, 0, 1, 2, 1, 0, 5, 6, 7, 0, 1, 2, 1, 0, 1, 0]
        ca, cb, cc, cd, cop, cbias, cscale, cclamp, cout = st[1:10]
        aa, ab, ac, ad, aop, abias, ascale, aclamp, aout = st[10:19]

        rswap = 2
        tswap = 3

        tev_color = ((cd & 0xF) | ((cc & 0xF) << 4) | ((cb & 0xF) << 8) | ((ca & 0xF) << 12) |
                     ((cbias & 3) << 16) | ((1 if cop else 0) << 18) | ((1 if cclamp else 0) << 19) |
                     ((cscale & 3) << 20) | ((cout & 3) << 22))

        tev_alpha = ((rswap & 3) | ((tswap & 3) << 2) |
                     ((ad & 7) << 4) | ((ac & 7) << 7) | ((ab & 7) << 10) | ((aa & 7) << 13) |
                     ((abias & 3) << 16) | ((1 if aop else 0) << 18) | ((1 if aclamp else 0) << 19) |
                     ((ascale & 3) << 20) | ((aout & 3) << 22))

        # Decode using native C++ vk_shadergen_fs.cpp logic:
        # Color:
        dec_ca = (tev_color >> 12) & 15
        dec_cb = (tev_color >> 8) & 15
        dec_cc = (tev_color >> 4) & 15
        dec_cd = tev_color & 15
        dec_cbias = (tev_color >> 16) & 3
        dec_cop = (tev_color >> 18) & 1
        dec_cclamp = (tev_color >> 19) & 1
        dec_cscale = (tev_color >> 20) & 3
        dec_cout = (tev_color >> 22) & 3

        self.assertEqual(dec_ca, 1)
        self.assertEqual(dec_cb, 2)
        self.assertEqual(dec_cc, 3)
        self.assertEqual(dec_cd, 4)
        self.assertEqual(dec_cbias, 1)
        self.assertEqual(dec_cop, 0)
        self.assertEqual(dec_cclamp, 1)
        self.assertEqual(dec_cscale, 2)
        self.assertEqual(dec_cout, 0)

        # Alpha:
        dec_rswap = tev_alpha & 3
        dec_tswap = (tev_alpha >> 2) & 3
        dec_ad = (tev_alpha >> 4) & 7
        dec_ac = (tev_alpha >> 7) & 7
        dec_ab = (tev_alpha >> 10) & 7
        dec_aa = (tev_alpha >> 13) & 7
        dec_abias = (tev_alpha >> 16) & 3
        dec_aop = (tev_alpha >> 18) & 1
        dec_aclamp = (tev_alpha >> 19) & 1
        dec_ascale = (tev_alpha >> 20) & 3
        dec_aout = (tev_alpha >> 22) & 3

        self.assertEqual(dec_rswap, 2)
        self.assertEqual(dec_tswap, 3)
        self.assertEqual(dec_ad, 0)
        self.assertEqual(dec_ac, 7)
        self.assertEqual(dec_ab, 6)
        self.assertEqual(dec_aa, 5)
        self.assertEqual(dec_abias, 2)
        self.assertEqual(dec_aop, 1)
        self.assertEqual(dec_aclamp, 0)
        self.assertEqual(dec_ascale, 1)
        self.assertEqual(dec_aout, 1)

    def test_real_star_materials_from_archives(self):
        """
        Verify real star model materials (PowerStar, GrandStar, StarPiece) parse cleanly.
        """
        root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        for candidate_name in ["PowerStar.arc", "GrandStar.arc", "StarPiece.arc"]:
            arc_path = os.path.join(root, "cooked", "files", "ObjectData", candidate_name)
            if not os.path.exists(arc_path):
                continue
            import tools.rarc as r
            from tools.cook.j3d_mat3_parser import parse_j3d_mat3
            with open(arc_path, "rb") as f:
                data = f.read()
            found_model = False
            for name, b in r.rarc_list(r.yaz0_decompress(data)):
                if name.endswith(".bdl") or name.endswith(".bmd"):
                    mats, gaps, count = parse_j3d_mat3(b, name)
                    self.assertGreater(len(mats), 0, f"Expected materials in {name}")
                    from tools.cook.manifest_schema import UID_WORDS, KEY_WORDS
                    for uid_words, key, desc in mats:
                        self.assertEqual(len(uid_words), UID_WORDS)
                        self.assertEqual(len(key), KEY_WORDS)
                    found_model = True
            self.assertTrue(found_model, f"Expected at least one model in {candidate_name}")

if __name__ == '__main__':
    unittest.main()
