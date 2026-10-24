import io
import os
import unittest
import zipfile
from unittest import mock

from tools.apk_env import COMPROMISED_KEY_SHA256, check_signer
from tools.verify_release_apk import (
    check_contents, check_entries, parse_debuggable, parse_signer,
    tester_debuggable_opt_in as _debuggable_opt_in,
)

NEW_SIGNER = "aa" * 32

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


def elf64(machine=183):
    header = bytearray(64)
    header[0:4] = b"\x7fELF"
    header[4] = 2
    header[5] = 1
    header[18:20] = machine.to_bytes(2, "little")
    return bytes(header)


def make_apk(**overrides):
    payload = {
        "AndroidManifest.xml": b"\x03\x00\x08\x00",
        "classes.dex": b"dex\n035\x00" + b"\x00" * 64,
        "assets/galaxy_shader_manifest.bin": b"manifest",
        "lib/arm64-v8a/libgame.so": elf64(),
        "lib/arm64-v8a/libmain.so": elf64(),
        "lib/arm64-v8a/libreserve.so": elf64(),
        "lib/arm64-v8a/libinstaller.so": elf64(),
        "lib/arm64-v8a/wrap.sh": b"#!/system/bin/sh\nexec \"$0\"\n",
    }
    for name, data in overrides.items():
        if data is None:
            payload.pop(name, None)
        else:
            payload[name] = data
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, "w") as archive:
        for name, data in payload.items():
            archive.writestr(name, data)
    buffer.seek(0)
    return zipfile.ZipFile(buffer)


class TestReleaseGate(unittest.TestCase):
    def test_complete_apk_has_no_problems(self):
        self.assertEqual(check_entries(GOOD), [])

    def test_complete_payload_has_no_problems(self):
        with make_apk() as apk:
            self.assertEqual(check_contents(apk), [])

    def test_empty_native_library_is_reported(self):
        with make_apk(**{"lib/arm64-v8a/libgame.so": b""}) as apk:
            self.assertEqual(check_contents(apk), [
                "lib/arm64-v8a/libgame.so is empty",
                "lib/arm64-v8a/libgame.so is not a little-endian 64-bit ELF",
            ])

    def test_foreign_architecture_is_reported(self):
        with make_apk(**{"lib/arm64-v8a/libgame.so": elf64(machine=62)}) as apk:
            self.assertEqual(check_contents(apk), ["lib/arm64-v8a/libgame.so is not an AArch64 library"])

    def test_32bit_elf_is_reported(self):
        truncated = bytearray(elf64())
        truncated[4] = 1
        with make_apk(**{"lib/arm64-v8a/libgame.so": bytes(truncated)}) as apk:
            self.assertEqual(check_contents(apk), [
                "lib/arm64-v8a/libgame.so is not a little-endian 64-bit ELF"])

    def test_crlf_wrap_script_is_reported(self):
        with make_apk(**{"lib/arm64-v8a/wrap.sh": b"#!/system/bin/sh\r\nexec \"$0\"\r\n"}) as apk:
            self.assertEqual(check_contents(apk), ["lib/arm64-v8a/wrap.sh has CRLF line endings"])

    def test_wrap_script_without_shebang_is_reported(self):
        with make_apk(**{"lib/arm64-v8a/wrap.sh": b"exec \"$0\"\n"}) as apk:
            self.assertEqual(check_contents(apk), ["lib/arm64-v8a/wrap.sh is not a shebang script"])

    def test_bad_classes_dex_is_reported(self):
        with make_apk(**{"classes.dex": b"PK\x03\x04" + b"\x00" * 64}) as apk:
            self.assertEqual(check_contents(apk), ["classes.dex is not a Dalvik executable"])

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
                "aa:aa:aa:aa:aa:aa:aa:aa:aa:aa:aa:aa:aa:aa:aa:aa:"
                "aa:aa:aa:aa:aa:aa:aa:aa:aa:aa:aa:aa:aa:aa:aa:aa")
        self.assertEqual(parse_signer(text), NEW_SIGNER)
        self.assertEqual(parse_signer("no certificate here"), "")

    def test_compromised_key_is_rejected_on_every_channel(self):
        for channel in ("tester", "production"):
            problem = check_signer(COMPROMISED_KEY_SHA256, channel=channel)
            self.assertIn("repository history", problem or "", channel)

    def test_production_requires_a_pinned_signer(self):
        with mock.patch.dict(os.environ, {}, clear=False):
            os.environ.pop("GALAXY_EXPECTED_SIGNER_SHA256", None)
            self.assertIsNotNone(check_signer(NEW_SIGNER, channel="production"))
            self.assertIsNone(check_signer(NEW_SIGNER, channel="tester"))

    def test_pinned_signer_mismatch_is_rejected(self):
        with mock.patch.dict(os.environ, {"GALAXY_EXPECTED_SIGNER_SHA256": NEW_SIGNER}):
            self.assertIsNone(check_signer(NEW_SIGNER, channel="production"))
            self.assertIsNone(check_signer(NEW_SIGNER, channel="tester"))
            self.assertIsNotNone(check_signer("bb" * 32, channel="tester"))

    def test_tester_debuggable_requires_explicit_opt_in(self):
        with mock.patch.dict(os.environ, {}, clear=False):
            os.environ.pop("GALAXY_ALLOW_DEBUGGABLE", None)
            self.assertFalse(_debuggable_opt_in())
        with mock.patch.dict(os.environ, {"GALAXY_ALLOW_DEBUGGABLE": "1"}):
            self.assertTrue(_debuggable_opt_in())

    def test_debuggable_flag_parsing(self):
        self.assertTrue(parse_debuggable('  A: android:debuggable(0x0101000f)=true\n'))
        self.assertFalse(parse_debuggable('  A: android:debuggable(0x0101000f)=false\n'))
        self.assertIsNone(parse_debuggable("  A: android:label(0x01010001)=Galaxy\n"))

    def test_resolve_keystore_pass_production_rejects_empty(self):
        from tools.apk_env import resolve_keystore_pass
        import os
        old = os.environ.get("GALAXY_KEYSTORE_PASS")
        try:
            if "GALAXY_KEYSTORE_PASS" in os.environ:
                del os.environ["GALAXY_KEYSTORE_PASS"]
            with self.assertRaises(SystemExit) as ctx:
                resolve_keystore_pass(channel="production")
            self.assertIn("GALAXY_KEYSTORE_PASS", str(ctx.exception))
        finally:
            if old is not None:
                os.environ["GALAXY_KEYSTORE_PASS"] = old

    def test_resolve_keystore_pass_production_accepts_secret(self):
        from tools.apk_env import resolve_keystore_pass
        import os
        old = os.environ.get("GALAXY_KEYSTORE_PASS")
        try:
            os.environ["GALAXY_KEYSTORE_PASS"] = "secure_custom_pass"
            self.assertEqual(resolve_keystore_pass(channel="production"), "secure_custom_pass")
        finally:
            if old is not None:
                os.environ["GALAXY_KEYSTORE_PASS"] = old
            else:
                os.environ.pop("GALAXY_KEYSTORE_PASS", None)

    def test_resolve_keystore_pass_tester_fallback(self):
        from tools.apk_env import resolve_keystore_pass
        import os
        old = os.environ.get("GALAXY_KEYSTORE_PASS")
        try:
            if "GALAXY_KEYSTORE_PASS" in os.environ:
                del os.environ["GALAXY_KEYSTORE_PASS"]
            self.assertEqual(resolve_keystore_pass(channel="tester"), "android")
        finally:
            if old is not None:
                os.environ["GALAXY_KEYSTORE_PASS"] = old

    def test_production_mode_rejects_debuggable_even_with_allow_var(self):
        import tools.verify_release_apk as vr
        with mock.patch.dict(os.environ, {"GALAXY_EXPECTED_SIGNER_SHA256": NEW_SIGNER}), \
             mock.patch.object(vr, "run") as mock_run, mock.patch("zipfile.ZipFile"):
            mock_run.side_effect = [
                mock.Mock(returncode=0, stdout="certificate SHA-256 digest: " + NEW_SIGNER, stderr=""),
                mock.Mock(returncode=0, stdout='  A: android:debuggable(0x0101000f)=true\n', stderr=""),
                mock.Mock(returncode=0, stdout="", stderr=""),
            ]
            with mock.patch.object(vr, "check_entries", return_value=[]), \
                 mock.patch.object(vr, "check_contents", return_value=[]):
                problems = vr.inspect("mock.apk", channel="production", allow_debuggable=True)
                self.assertTrue(any("forbidden in production" in p for p in problems))

    def test_tester_mode_permits_debuggable_with_optin(self):
        import tools.verify_release_apk as vr
        with mock.patch.dict(os.environ, {"GALAXY_EXPECTED_SIGNER_SHA256": NEW_SIGNER}), \
             mock.patch.object(vr, "run") as mock_run, mock.patch("zipfile.ZipFile"):
            mock_run.side_effect = [
                mock.Mock(returncode=0, stdout="certificate SHA-256 digest: " + NEW_SIGNER, stderr=""),
                mock.Mock(returncode=0, stdout='  A: android:debuggable(0x0101000f)=true\n', stderr=""),
                mock.Mock(returncode=0, stdout="", stderr=""),
            ]
            with mock.patch.object(vr, "check_entries", return_value=[]), \
                 mock.patch.object(vr, "check_contents", return_value=[]):
                problems = vr.inspect("mock.apk", channel="tester", allow_debuggable=True)
                self.assertEqual(problems, [])

    def test_release_gate_rejects_compromised_signer_and_unpinned_production(self):
        import tools.verify_release_apk as vr
        for channel, fingerprint, pin in (
                ("tester", COMPROMISED_KEY_SHA256, NEW_SIGNER),
                ("production", NEW_SIGNER, None)):
            env = {"GALAXY_EXPECTED_SIGNER_SHA256": pin} if pin else {}
            with mock.patch.dict(os.environ, env, clear=False), \
                 mock.patch.object(vr, "run") as mock_run, mock.patch("zipfile.ZipFile"):
                if not pin:
                    os.environ.pop("GALAXY_EXPECTED_SIGNER_SHA256", None)
                mock_run.side_effect = [
                    mock.Mock(returncode=0, stdout="certificate SHA-256 digest: " + fingerprint, stderr=""),
                    mock.Mock(returncode=0, stdout="", stderr=""),
                    mock.Mock(returncode=0, stdout="", stderr=""),
                ]
                with mock.patch.object(vr, "check_entries", return_value=[]), \
                     mock.patch.object(vr, "check_contents", return_value=[]):
                    problems = vr.inspect("mock.apk", channel=channel, allow_debuggable=False)
                self.assertTrue(problems, channel)


if __name__ == "__main__":
    unittest.main()
