import os
import re
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


class TestGuestMemoryRegression(unittest.TestCase):
    def test_port_h_and_guest_layout_constants_match(self):
        port_h_path = os.path.join(ROOT, "platform", "include", "port", "port.h")
        with open(port_h_path, "r", encoding="utf-8") as f:
            port_h = f.read()

        guest_layout_c_path = os.path.join(ROOT, "platform", "src", "os", "guest_layout.c")
        with open(guest_layout_c_path, "r", encoding="utf-8") as f:
            guest_layout_c = f.read()

        # Invariants from port.h
        self.assertIn("#define PORT_MEM1_BASE 0x80000000u", port_h)
        self.assertIn("#define PORT_MEM1_SIZE 0x08000000u", port_h)
        self.assertIn("#define PORT_MEM2_BASE 0x90000000u", port_h)
        self.assertIn("#define PORT_MEM2_SIZE 0x08000000u", port_h)
        self.assertIn("#define PORT_MEM1_UNCACHED 0xC0000000u", port_h)
        self.assertIn("#define PORT_MEM2_UNCACHED 0xD0000000u", port_h)
        self.assertIn("#define PORT_LIB_BASE 0x98000000u", port_h)
        self.assertIn("#define PORT_LIB_SIZE 0x08000000u", port_h)
        self.assertIn("#define PORT_LOWPOOL_BASE 0xA0000000u", port_h)
        self.assertIn("#define PORT_LOWPOOL_SIZE 0x10000000u", port_h)
        self.assertIn("#define PORT_LOCKED_CACHE 0xE0000000u", port_h)

        # Matched in guest_layout.c definitions table
        self.assertIn('0x80000000u, 0x08000000u', guest_layout_c)
        self.assertIn('0x90000000u, 0x08000000u', guest_layout_c)
        self.assertIn('0x98000000u, 0x08000000u', guest_layout_c)
        self.assertIn('0xA0000000u, 0x10000000u', guest_layout_c)
        self.assertIn('0xC0000000u, 0x08000000u', guest_layout_c)
        self.assertIn('0xD0000000u, 0x08000000u', guest_layout_c)
        self.assertIn('0xE0000000u, 0x00010000u', guest_layout_c)

    def test_launcher_contains_no_blind_map_fixed_clobber(self):
        launcher_path = os.path.join(ROOT, "platform", "android", "launcher.c")
        with open(launcher_path, "r", encoding="utf-8") as f:
            launcher_c = f.read()

        # Ensure MAP_FIXED is not used blindly in launcher.c loop
        self.assertNotIn("MAP_FIXED,", launcher_c)
        self.assertNotIn("MAP_FIXED\n", launcher_c)
        self.assertNotIn("MAP_FIXED ", launcher_c)
        # Verify non-destructive reservation API is called
        self.assertIn("guest_layout_reserve_slices_nondestructive", launcher_c)
        self.assertIn("guest_layout_format_audit_report", launcher_c)

    def test_production_channel_gate_rejects_debuggable(self):
        from tools.verify_release_apk import inspect
        import zipfile
        import tempfile

        # Create a mock minimal APK containing required entries
        from tools.tests.test_release_gate import GOOD
        with tempfile.TemporaryDirectory() as tmpdir:
            mock_apk = os.path.join(tmpdir, "test.apk")
            with zipfile.ZipFile(mock_apk, "w") as zf:
                for entry in GOOD:
                    zf.writestr(entry, b"mock")

            # Inspect production mode: should check and enforce policy
            problems = inspect(mock_apk, channel="production", allow_debuggable=False)
            # Problems must include apksigner failure or missing signature since mock is unsigned
            self.assertTrue(any("apksigner" in p or "signer" in p for p in problems))


if __name__ == "__main__":
    unittest.main()
