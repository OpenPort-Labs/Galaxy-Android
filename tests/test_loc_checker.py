import os
import tempfile
import unittest

from tools.check_loc import is_excluded, count_file_lines, check_loc

class TestLocChecker(unittest.TestCase):
    def test_extension_and_boundary_handling(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            # 1. File with exactly 300 lines
            f300 = os.path.join(tmpdir, "valid_300.cpp")
            with open(f300, "w") as fp:
                fp.write("\n" * 300)

            # 2. File with 301 lines (violates <= 300)
            f301 = os.path.join(tmpdir, "invalid_301.py")
            with open(f301, "w") as fp:
                fp.write("\n" * 301)

            # 3. File with non-code extension (e.g. .txt, .md, .bin)
            ftxt = os.path.join(tmpdir, "notes.txt")
            with open(ftxt, "w") as fp:
                fp.write("\n" * 500)

            files, violations = check_loc(tmpdir, max_lines=300)
            file_names = [os.path.basename(f) for f in files]

            self.assertIn("valid_300.cpp", file_names)
            self.assertIn("invalid_301.py", file_names)
            self.assertNotIn("notes.txt", file_names)

            self.assertEqual(len(violations), 1)
            self.assertEqual(violations[0][0], 301)
            self.assertEqual(os.path.basename(violations[0][1]), "invalid_301.py")

    def test_exclusion_directories(self):
        self.assertTrue(is_excluded("/repo/.git/config"))
        self.assertTrue(is_excluded("/repo/build-mobile/test.cpp"))
        self.assertTrue(is_excluded("/repo/decomp/src/main.cpp"))
        self.assertTrue(is_excluded("/repo/third_party/lib/foo.c"))
        self.assertTrue(is_excluded("/repo/installer-mobile/vendor/python/py.h"))
        self.assertTrue(is_excluded("/repo/tools/THPConv/src/main.c"))

        self.assertFalse(is_excluded("/repo/platform/src/gx/vk/vk_render.cpp"))
        self.assertFalse(is_excluded("/repo/platform/mobile/ui/TouchInputHandler.cpp"))
        self.assertFalse(is_excluded("/repo/tools/check_loc.py"))

    def test_line_counting_accuracy(self):
        with tempfile.NamedTemporaryFile("w+", delete=False) as fp:
            fp.write("line 1\n\nline 3\n// comment\n")
            fp.flush()
            path = fp.name

        try:
            self.assertEqual(count_file_lines(path), 4)
        finally:
            os.remove(path)

    def test_repo_compliance(self):
        root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        files, violations = check_loc(root, max_lines=300)
        self.assertEqual(len(violations), 0, f"Repository has LOC violations: {violations}")
        self.assertGreater(len(files), 100)

if __name__ == "__main__":
    unittest.main()
