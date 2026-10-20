import os
import subprocess
import tempfile
import unittest

from tools.check_include_case import check


def _make_repo(root, files):
    subprocess.run(["git", "init", "-q", root], check=True)
    for rel, text in files.items():
        path = os.path.join(root, rel)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8") as fp:
            fp.write(text)
    subprocess.run(["git", "-C", root, "add", "-A"], check=True)
    subprocess.run(
        ["git", "-C", root, "-c", "user.email=ci@example.com", "-c", "user.name=ci",
         "commit", "-qm", "test"],
        check=True,
    )


class TestIncludeCase(unittest.TestCase):
    def test_repository_includes_match_tracked_names(self):
        # The release build checks out case-sensitively on Linux; a local
        # Windows build cannot notice a mismatch, so this asserts the tree.
        problems = check(verbose=False)
        self.assertEqual(problems, [], f"#include case mismatches: {problems}")

    def test_mismatch_is_reported_with_the_tracked_spelling(self):
        with tempfile.TemporaryDirectory() as root:
            _make_repo(root, {
                "platform/src/Foo.hpp": "#pragma once\n",
                "platform/src/use.cpp": '#include "foo.hpp"\n',
            })
            problems = check(root, verbose=False)
            self.assertEqual(len(problems), 1, problems)
            source, include, tracked = problems[0]
            self.assertEqual(source, "platform/src/use.cpp")
            self.assertEqual(include, "foo.hpp")
            self.assertEqual(tracked, "platform/src/Foo.hpp")

    def test_exact_spelling_is_accepted(self):
        with tempfile.TemporaryDirectory() as root:
            _make_repo(root, {
                "platform/src/Foo.hpp": "#pragma once\n",
                "platform/src/use.cpp": '#include "Foo.hpp"\n',
            })
            self.assertEqual(check(root, verbose=False), [])

    def test_nested_header_mismatch_is_reached_transitively(self):
        with tempfile.TemporaryDirectory() as root:
            _make_repo(root, {
                "decomp/libs/JSystem/include/JSystem/Deep.hpp": '#include "JSystem/other.hpp"\n',
                "decomp/libs/JSystem/include/JSystem/Other.hpp": "#pragma once\n",
                "platform/src/use.cpp": '#include "JSystem/Deep.hpp"\n',
            })
            problems = check(root, verbose=False)
            self.assertEqual(len(problems), 1, problems)
            self.assertEqual(problems[0][0], "decomp/libs/JSystem/include/JSystem/Deep.hpp")
            self.assertEqual(problems[0][2], "decomp/libs/JSystem/include/JSystem/Other.hpp")

    def test_uncompiled_reference_code_is_not_checked(self):
        with tempfile.TemporaryDirectory() as root:
            _make_repo(root, {
                "decomp/libs/RVL_SDK/include/revolution/nand.h": "#pragma once\n",
                "decomp/src/RVLFaceLib/RFL_NANDLoader.c": '#include "revolution/NAND.h"\n',
            })
            self.assertEqual(check(root, verbose=False), [])


if __name__ == "__main__":
    unittest.main()
