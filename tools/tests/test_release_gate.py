import unittest

from tools.verify_release_apk import check_entries, parse_debuggable, parse_signer

GOOD = [
    "AndroidManifest.xml",
    "classes.dex",
    "assets/galaxy_shader_manifest.bin",
    "lib/arm64-v8a/libgame.so",
    "lib/arm64-v8a/libmain.so",
    "lib/arm64-v8a/libreserve.so",
    "lib/arm64-v8a/libinstaller.so",
    "lib/arm64-v8a/wrap.sh",
]


class TestReleaseGate(unittest.TestCase):
    def test_complete_apk_has_no_problems(self):
        self.assertEqual(check_entries(GOOD), [])

    def test_missing_native_library_is_reported(self):
        names = [n for n in GOOD if not n.endswith("libgame.so")]
        problems = check_entries(names)
        self.assertEqual(problems, ["missing lib/arm64-v8a/libgame.so"])

    def test_duplicate_entry_is_reported(self):
        problems = check_entries(GOOD + ["assets/galaxy_shader_manifest.bin"])
        self.assertEqual(problems, ["assets/galaxy_shader_manifest.bin appears 2 times"])

    def test_unexpected_abi_is_reported(self):
        problems = check_entries(GOOD + ["lib/x86_64/libgame.so"])
        self.assertEqual(problems, ["unexpected ABI directory lib/x86_64"])

    def test_signer_fingerprint_is_normalized(self):
        text = ("Signer #1 certificate SHA-256 digest: "
                "18:83:cd:60:0a:ac:d5:62:85:13:e2:3c:e6:46:f4:c8:"
                "ed:19:c0:40:a1:45:75:fb:c9:22:d3:3f:66:86:fe:ff")
        self.assertEqual(parse_signer(text),
                         "1883cd600aacd5628513e23ce646f4c8ed19c040a14575fbc922d33f6686feff")
        self.assertEqual(parse_signer("no certificate here"), "")

    def test_debuggable_flag_parsing(self):
        self.assertTrue(parse_debuggable('  A: android:debuggable(0x0101000f)=true\n'))
        self.assertFalse(parse_debuggable('  A: android:debuggable(0x0101000f)=false\n'))
        self.assertIsNone(parse_debuggable("  A: android:label(0x01010001)=Galaxy\n"))


if __name__ == "__main__":
    unittest.main()
