import os
import shutil
import tempfile
import unittest

def is_safe_child_path(root_dir: str, target_file: str) -> bool:
    """Python reference implementation of CookerEngine.verifyNoSymlinksBeneath invariant."""
    try:
        root_canon = os.path.realpath(root_dir)
        target_canon = os.path.realpath(target_file)
        if not target_canon.startswith(root_canon + os.sep) and target_canon != root_canon:
            return False
        # Verify no intermediate component is a symbolic link
        cur = os.path.abspath(target_file)
        root_abs = os.path.abspath(root_dir)
        if os.path.islink(cur):
            return False
        parent = os.path.dirname(cur)
        while parent and parent.startswith(root_abs) and parent != root_abs:
            if os.path.islink(parent):
                return False
            parent = os.path.dirname(parent)
        return True
    except Exception:
        return False

class TestExtractionPathSecurity(unittest.TestCase):
    def setUp(self):
        self.test_dir = tempfile.mkdtemp(prefix="galaxy_sec_test_")
        self.root = os.path.join(self.test_dir, "dest_root")
        os.makedirs(self.root, exist_ok=True)
        self.outside_sentinel = os.path.join(self.test_dir, "outside_sentinel.txt")
        with open(self.outside_sentinel, "w", encoding="utf-8") as f:
            f.write("SENTINEL_INTEGRITY_OK")

    def tearDown(self):
        shutil.rmtree(self.test_dir, ignore_errors=True)

    def test_parent_directory_traversal_rejected(self):
        bad_paths = [
            "../outside.txt",
            "../../outside.txt",
            "files/../../outside.txt",
            "..\\outside.txt",
            "/absolute/outside.txt",
        ]
        for bad in bad_paths:
            target = os.path.join(self.root, bad)
            self.assertFalse(is_safe_child_path(self.root, target), f"Failed to reject: {bad}")

    def test_valid_nested_paths_accepted(self):
        valid_paths = [
            "sys/main.dol",
            "sys/fst.bin",
            "files/StageData/MarioFace.arc",
            "files/UsEnglish/LayoutData/HomeButton.arc",
        ]
        for valid in valid_paths:
            target = os.path.join(self.root, valid)
            self.assertTrue(is_safe_child_path(self.root, target), f"Failed to accept valid path: {valid}")

    def test_symlink_file_escape_rejected_and_sentinel_preserved(self):
        symlink_target = os.path.join(self.root, "evil_symlink.txt")
        try:
            os.symlink(self.outside_sentinel, symlink_target)
        except (OSError, NotImplementedError):
            self.skipTest("Symlink creation not supported in this user environment")

        self.assertFalse(is_safe_child_path(self.root, symlink_target))
        with open(self.outside_sentinel, "r", encoding="utf-8") as f:
            self.assertEqual(f.read(), "SENTINEL_INTEGRITY_OK")

    def test_symlink_directory_escape_rejected(self):
        outside_dir = os.path.join(self.test_dir, "outside_dir")
        os.makedirs(outside_dir, exist_ok=True)
        symlink_dir = os.path.join(self.root, "files", "evil_dir")
        os.makedirs(os.path.dirname(symlink_dir), exist_ok=True)
        try:
            os.symlink(outside_dir, symlink_dir)
        except (OSError, NotImplementedError):
            self.skipTest("Symlink creation not supported in this user environment")

        target_in_symlink = os.path.join(symlink_dir, "injected.bin")
        self.assertFalse(is_safe_child_path(self.root, target_in_symlink))

if __name__ == "__main__":
    unittest.main()
