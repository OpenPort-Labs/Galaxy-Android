# Signing Galaxy Android

Android only installs an APK over an existing one when both are signed with the
**same certificate**.  Anything else fails with
`INSTALL_FAILED_UPDATE_INCOMPATIBLE` and leaves the player with no choice but to
uninstall first, losing the extracted disc and the saves
([#35](https://github.com/OpenPort-Labs/Galaxy-Android/issues/35),
[#36](https://github.com/OpenPort-Labs/Galaxy-Android/issues/36)).

Every Galaxy Android build therefore has to be signed with the one project key.

## The project key

| | |
| --- | --- |
| Subject | `CN=GalaxyAndroid Debug, O=Local, C=US` |
| SHA-256 | `1883CD600AACD5628513E23CE646F4C8ED19C040A14575FBC922D33F6686FEFF` |
| Store password | Set via `GALAXY_KEYSTORE_PASS` (never hard-coded in repo) |
| Key alias | `androiddebugkey` |

The fingerprint is public information - it is what Android compares - and the
packager pins it in `package_mobile_apk.py` (`PROJECT_KEY_SHA256`).

The keystore itself is **not** in the repository: it was committed once, which
put the private key in public history, and it has since been removed and added
to `.gitignore`.  Keep the copy you have safe; treat it as the identity of the
app.  Rotating it is possible, but it forces every existing player through a
one-time uninstall.

## Build channels

Packaging and release verification distinguish between two channels:

1. **Tester channel (`--channel tester` / `GALAXY_RELEASE_CHANNEL=tester`)**:
   - Permits `android:debuggable="true"` when opt-in (`GALAXY_ALLOW_DEBUGGABLE=1`).
   - Required on Android for `lib/<abi>/wrap.sh` to preload `libreserve.so` (which
     reserves the 32-bit GameCube memory space before ART initializes).
   - Tester APKs are sideload-only artifacts and marked as pre-releases on GitHub.
   - For local development with a local debug keystore, defaults to `android` only
     when building on the tester channel.

2. **Production channel (`--channel production` / `GALAXY_RELEASE_CHANNEL=production`)**:
   - Strictly forbids `android:debuggable="true"`.
   - Requires explicit `GALAXY_KEYSTORE_PASS` via environment; missing credentials
     immediately fail rather than falling back to defaults.
   - Forbids `GALAXY_ALLOW_NEW_KEY=1` throwaway keys.
   - Rejects any APK that fails certificate verification or entry validation.

## Local builds

Put the key at `out/release.keystore` (`out/` is git-ignored) and the packager
finds it:

```bash
cp /path/to/release.keystore out/release.keystore
export GALAXY_KEYSTORE_PASS="your-store-pass"
python package_mobile_apk.py --channel tester
```

`out/debug.keystore` and the old `platform/mobile/release.keystore` location are
still accepted so existing checkouts keep working.

## CI releases

The release workflow decodes the key from repository secrets:

| Secret | Value |
| --- | --- |
| `GALAXY_KEYSTORE_B64` | `base64 -w0 out/release.keystore` |
| `GALAXY_KEYSTORE_PASS` | the store password (provided securely via environment) |

Set them under *Settings -> Secrets and variables -> Actions*.  Credentials are
passed to `apksigner` and `keytool` through environment variables (`env:` and
`-storepass:env`), never through exposed command-line arguments.

Without valid credentials, release packaging fails immediately instead of
publishing an insecure or untrusted artifact.

## What the packager enforces

`package_mobile_apk.py` resolves a key from, in order,
`GALAXY_KEYSTORE_B64`, `GALAXY_KEYSTORE`, `out/release.keystore`,
`out/debug.keystore`, `platform/mobile/release.keystore`, then:

1. checks the keystore's certificate against the pinned fingerprint **before**
   spending time on a build;
2. signs the APK;
3. reads the certificate back out of the finished APK with `apksigner verify`
   and refuses to hand it over if it is not the project certificate.

`GALAXY_ALLOW_NEW_KEY=1` generates a throwaway `out/dev.keystore` for
experimentation.  Such an APK is not an update of anything, so the fingerprint
check is skipped for it, with a warning.

`GALAXY_EXPECTED_SIGNER_SHA256` overrides the pin for a fork that ships under a
different certificate.

## Verifying an APK by hand

```bash
apksigner verify --print-certs out-mobile/GalaxyAndroid.apk
adb install -r out-mobile/GalaxyAndroid.apk   # must succeed, without uninstalling
```
