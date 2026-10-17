# Source Audit — Galaxy Android

Date: 2026-10-16 · Scope: whole repository (hand-written port + tooling)

## Scope and method

The repository has two very different kinds of code:

- `decomp/` (~2,300 C/C++ files) — reconstructed Super Mario Galaxy game code and
  SDK sources, carried over from the SMGCommunity/Petari and GalaxyQuest
  projects. This audit treats it as third-party derived and reviews it only where
  the port layer substitutes or wraps it.
- Everything else is this project's own code and was reviewed line by line:
  `platform/` (native port layer, Android launcher shims, mobile UI, renderers),
  `installer-mobile/` (JNI disc extractor, AES/Yaz0, embedded-Python bridge,
  cook scripts), `tools/` (cook pipeline, desktop installer, tests), the APK
  packaging scripts, and the CI workflows.

Checks performed: manual review of the files above, pattern scans for unsafe
string/memory functions, secrets and keystores, untracked-vs-committed file
comparison, and execution of the existing test suite.

Legend: **Critical** / **High** / **Medium** / **Low** / **Info**.

## Findings

### High — Signing keystore committed to a public repository

- `platform/mobile/release.keystore` is tracked in git
  (`git ls-files` lists it).
- `package_mobile_apk.py:72` signs release APKs with it and supplies the
  password inline (`--ks-pass pass:android`, `--key-pass pass:android`), so the
  private key and its passphrase are both public.

Impact: anyone can produce an APK signed with the project's key. Android accepts
a replacement for an installed app only if signed by the same key, so a third
party could publish an "update" that existing users' devices would accept, and
could impersonate official releases. Even for a sideloaded, GPL app this is a
supply-chain compromise.

Recommendation: remove the keystore from git and add `*.keystore`/`*.jks` to
`.gitignore`; generate a fresh key locally and load the password from an
environment variable or a secrets store; rotate the published key. Note that the
keystore is now in history and must be considered compromised regardless.

### High — Shimmed release build shipped `android:debuggable="true"`

- `platform/mobile/AndroidManifest.xml` sets `android:debuggable="true"` (and
  `extractNativeLibs="true"`), which the same file explains is required for
  `wrap.sh` to preload `libreserve.so`.

Impact: the shipped APK runs with the debug flag set. Any process can attach a
debugger, and `run-as com.openport.galaxyandroid` can read the app-private data
directory. Google Play and most stores reject debuggable apps outright.

Recommendation: keep the debug flag only for internal/dev builds and enable
`wrap.sh` preloading through a non-debuggable path, or accept and document the
trade-off explicitly and never publish this APK through a store. At minimum,
add a release-build check that fails packaging when `debuggable="true"`.

### Medium — Out-of-bounds reads and path traversal in the disc extractor

`installer-mobile/jni/wii_extractor.cpp`, `extractWiiDisc()`:

- The FST is trusted after only a size check. `numEntries` is read from the file
  (`read_be32(fstData.data() + 8)`) and then used to form
  `stringTable = fstData.data() + numEntries * 12` with no check that
  `numEntries * 12 <= fstSize`. The multiplication is 32-bit and can also wrap.
  The loop then walks `fstData.data() + i * 12` for `i < numEntries`.
- Filenames are dereferenced with no bound on the name offset:
  `const char* name = stringTable + nameOff;` (`nameOff` up to `0xFFFFFF`) and
  copied into a `std::string`, which reads until a NUL — potentially far outside
  the buffer.
- `extractFileToDisk()` builds the destination as `filesDir + "/" + filePath`
  from FST names with no `..`/separator sanitisation.

Impact: opening a malformed or maliciously crafted disc image (`.iso`/`.wbfs`)
can cause out-of-bounds reads (crash, or leaking surrounding heap bytes into
filenames/logs) and can write files outside the chosen output directory.
Severity is bounded because the input is a locally selected file, not remote
data.

Recommendation: validate `fstSize >= 12` and `numEntries * 12 <= fstSize`
(64-bit arithmetic) before use; bound every `nameOff` against the string-table
length and reject entries that overflow it; reject FST names containing `/`,
`\`, or `..` and confirm each resolved output path stays under `filesDir`.

### Medium — Python source injection through concatenated paths

`installer-mobile/jni/py_bridge.cpp`, `runCooker()` builds the embedded Python
program by concatenating `scriptsDir`, `inputDir` and `outputDir` straight into
Python string literals:

```cpp
"sys.path.insert(0, '" + std::string(scriptsDir) + "')\n"
...
"data_dir='" + std::string(inputDir) + "',\n"
```

A path containing a single quote or newline breaks out of the literal and
executes arbitrary Python in the app process. The values originate in Java
(`MainActivity`) and are normally app-internal paths, so exploitation needs an
unusual storage layout — but the code is also simply fragile for any path with
an apostrophe or, for `sys.path`, a Windows-style backslash.

Recommendation: pass the paths as arguments instead of interpolating them into
source — e.g. `PyRun_SimpleString` with a fixed script that reads `sys.argv`, or
set them via `PySys_SetArgvEx` / a Python dict — or at minimum escape embedded
quotes and backslashes.

### Low — FST name offsets not validated in the engine's own loader

`platform/src/dvd/dvd.cpp`, `loadFst()` masks `nameOff` from each entry
(`be32(e) & 0x00FFFFFF`) and dereferences `strings + nameOff` with no bound
against the string-table size, and `findChild()`/`DVDConvertPathToEntrynum()`
index `sFst` with `next`/`parent` values read from the file without clamping to
`sFst.size()`. `readFst()` validates only that `count * 12 < size`. The same
class of issue as the extractor above, in the code that loads `sys/fst.bin`.

Impact: a corrupted or hand-modified game data tree can trigger out-of-bounds
reads or reads past the vector during boot. Not remotely reachable.

Recommendation: validate `nameOff < stringsSize` and clamp directory
`next`/`parent` to `sFst.size()` when building the table.

### Low — `strcpy` into a path buffer that may have been truncated

`platform/android/launcher.c:96`:

```c
snprintf(path, sizeof(path), "%s", info.dli_fname);
char* slash = strrchr(path, '/');
...
strcpy(slash + 1, "libgame.so");
```

`snprintf` can fill the whole 512-byte buffer; if `slash` lands near the end,
`strcpy(slash + 1, "libgame.so")` writes up to 10 bytes past the buffer. Only
reachable with a pathologically long installed library path.

Recommendation: use `snprintf` for the final copy as well, or check the
remaining space before writing.

### Low — Downloaded platform-tools not integrity-checked

`tools/build_installer.py:109` downloads Google's `platform-tools` zip over
HTTPS and extracts it with no checksum or signature verification (the SHA-256 it
computes at line 169 covers the produced installer, not the download). The
extraction itself is well defended (path-traversal and symlink checks, covered
by `tools/tests/test_installer.py`).

Recommendation: verify a pinned SHA-256 of the archive before extracting.

### Low — CI actions pinned to tags, not commit SHAs

`.github/workflows/release.yml` and `desktop-installers.yml` use
`actions/checkout@v4`, `actions/setup-java@v4`, `nttld/setup-ndk@v1`,
`softprops/action-gh-release@v2`, etc. Tag refs are mutable, so a compromised
third-party action can run in a workflow that has `contents: write` and signs
release artifacts. Recommendation: pin every action to a full commit SHA (and
scope `permissions` per job).

### Low — Robustness gaps in the JNI and Java layers

- `py_bridge.cpp`: `GetStringUTFChars` results are used without a null check
  (`isWiiDisc`, `extractDisc`, `runCooker`); constructing `std::string` from a
  null pointer is undefined behaviour on allocation failure. `GetMethodID`
  results for the callback are also unchecked before `CallVoidMethod`/`CallIntMethod`.
- `platform/mobile/src/com/openport/galaxyandroid/MainActivity.java`: the
  background cooking thread reads `chkIncludeMovies.isChecked()` off the UI
  thread; view access must happen on the main thread. `getPathFromUri()` falls
  back to matching a display name under `Downloads`/external storage, which can
  resolve to a different file than the one the user picked.
- `AndroidManifest.xml`: both `MainActivity` and `GameActivity` are
  `android:exported="true"` with no permission. Launching them from another app
  is largely benign here but is unnecessary surface.

### Info — `THPVideoDecode` and I8 tile writes assume well-formed movie data

`platform/src/sdk/thp_video.cpp` bounds the scan with a fixed
`end = file + (4 MiB)` window rather than a real buffer length, and
`storeBlock()` bounds rows by `height` but not columns by `width`. For sizes
that are not 16/8-aligned, a block can be written a tile past the plane. THP
video is fixed-size game data, so this is not currently reachable, but the
bounds are heuristic rather than structural.

## Positive observations

- The memory model (`platform/src/os/os_mem.cpp`) is careful: the 32-bit game
  window is reserved up front, `mmap` failures and misplacements are fatal
  rather than ignored, and uncached mirrors alias the same pages.
- The thread/interrupt layer (`platform/src/os/os_thread.cpp`) is a faithful
  port of the SDK scheduler with clearly documented single-CPU ownership,
  thread-local state, and interrupt delivery only at safe points.
- The textures decoder (`platform/src/gx/tex_decode.cpp`) and the Yaz0 decoder
  (`installer-mobile/jni/yaz0.c`) are properly bounds-checked, including the
  CMPR sub-block edge cases and the Yaz0 back-reference distance.
- The desktop installer separates UI from work threads, validates device
  identity and free space before copying, cleans up its own temporary
  workspaces, and rejects zip path traversal and symlinks
  (`tools/build_installer.py`, `tools/tests/test_installer.py`).
- Sample decoders cope with corrupt input by clamping rather than reading out of
  range in their hot paths (`thp_video.cpp` clamps IDCT output and caps
  `kNaturalOrder`).

## Verification performed

- `python -m unittest discover -s tools/tests -p 'test_installer.py'` → 15 tests,
  all pass.
- `git ls-files` used to confirm the keystore is tracked and to separate
  committed files from ignored artifacts.

## Limitations

- `decomp/` (reconstructed game and SDK sources) was not reviewed line by line;
  it is treated as third-party derived code.
- No Android device or NDK toolchain was used, so no build, run, or dynamic
  analysis was performed; findings are from static review.
- The large renderers (`vk_renderer.cpp`, `gl_renderer.cpp`, `gx_recorder.cpp`,
  `mobile_ui.cpp`, `vr_game.cpp`) were scanned for patterns and spot-checked but
  not fully read; a dedicated GPU/UI review is still worthwhile.
