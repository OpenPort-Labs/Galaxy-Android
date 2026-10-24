# Signing Galaxy Android

Android installs an update only when the old and new APKs carry the same
certificate.  A release signed with the wrong key produces
`INSTALL_FAILED_UPDATE_INCOMPATIBLE` and, worse, a key that has leaked lets
anyone else produce a malicious "update" that Android accepts.

## Incident: the former project key is compromised

The former key (`CN=GalaxyAndroid Debug, O=Local, C=US`, alias
`androiddebugkey`) was committed to this public repository together with its
store password.  It has been removed from the current tree and is git-ignored,
but it remains recoverable from history and **must be treated as public**.

| | |
| --- | --- |
| Old certificate SHA-256 | `1883CD600AACD5628513E23CE646F4C8ED19C040A14575FBC922D33F6686FEFF` |
| Status | Compromised. Never sign with it again. |

The fingerprint is public information - Android compares certificates by it - so
publishing it discloses nothing new.  The private key and its password are the
secret, and both are already in history.

`tools/apk_env.py` pins this fingerprint as `COMPROMISED_KEY_SHA256` and
`check_signer()` refuses it on **every** channel.  Removing the file from
`git` is not enough; the key itself has to be abandoned.

The old key has **not** been rotated in this checkout yet.  Until the steps
below are completed, packaging and the release workflow fail deliberately.
That is the intended fail-closed state, not a regression.

## Rotate to a new signing identity

Run these steps on a trusted machine, offline where possible.

1. Create the new key.  Choose a long, unique password and store it in a
   password manager.  Use a new alias (the old builds used
   `androiddebugkey`, but a distinct alias makes rotation visible).

   ```bash
   keytool -genkeypair -keystore release.keystore \
     -alias galaxy-release -keyalg RSA -keysize 4096 -validity 10000 \
     -dname "CN=Galaxy Android Release, O=Galaxy Android, C=US"
   ```

   Use the same password for the store and the key (the default PKCS12
   keystore requires this, and CI supplies only `GALAXY_KEYSTORE_PASS`).

2. Read its fingerprint:

   ```bash
   keytool -list -v -keystore release.keystore -alias galaxy-release
   ```

   Copy the `SHA-256:` value and remove the colons.

3. Configure CI (repository settings, never committed):

   | Name | Kind | Value |
   | --- | --- | --- |
   | `GALAXY_KEYSTORE_B64` | secret | `base64 -w0 release.keystore` |
   | `GALAXY_KEYSTORE_PASS` | secret | the new store password |
   | `GALAXY_KEY_ALIAS` | secret | `galaxy-release` |
   | `GALAXY_EXPECTED_SIGNER_SHA256` | variable | the new fingerprint |

   The release workflow refuses to build or publish while
   `GALAXY_EXPECTED_SIGNER_SHA256` or `GALAXY_KEY_ALIAS` is empty, and the
   packager refuses any certificate that does not match the pin.  It signs with
   the named alias and then reads the finished APK's certificate back.

4. Local builds use the same policy:

   ```bash
   export GALAXY_KEYSTORE=/absolute/path/to/release.keystore
   export GALAXY_KEYSTORE_PASS='...'
   export GALAXY_KEY_ALIAS=galaxy-release
   export GALAXY_EXPECTED_SIGNER_SHA256='<new fingerprint>'
   python package_mobile_apk.py --channel tester
   ```

5. Delete or archive the old keystores (`out/release.keystore`,
   `out/debug.keystore`, `platform/mobile/release.keystore`).  The tools now
   reject them by fingerprint, but keeping them invites a future mistake.

## Installing the rotated build

APKs signed with the old key cannot update APKs signed with the new key.
Existing installations need one uninstall before installing the rotated APK.
Back up anything important first: the app stores saves under
`/sdcard/GalaxyData/saves/`, which is shared storage, but uninstall and
storage changes can still remove data.  Do not accept an "update" from an
unknown source that is signed with the old fingerprint.

## Build channels

1. **Tester channel (`--channel tester` / `GALAXY_RELEASE_CHANNEL=tester`)**
   - Sideload-only pre-release artifacts.
   - Permits `android:debuggable="true"` only with `GALAXY_ALLOW_DEBUGGABLE=1`;
     Android requires a debuggable app to run `lib/<abi>/wrap.sh`, which
     preloads `libreserve.so` to reserve the game's 32-bit address window.
   - A local key is accepted only if it is not the compromised key.  Set
     `GALAXY_EXPECTED_SIGNER_SHA256` to pin it.

2. **Production channel (`--channel production`)**
   - `android:debuggable="true"` is forbidden.
   - `GALAXY_KEYSTORE_PASS` and `GALAXY_EXPECTED_SIGNER_SHA256` are required;
     there is no default password and no fallback to a debug key.
   - `GALAXY_ALLOW_NEW_KEY` is not accepted.
   - The finished APK's certificate is read back and must match the pin.
   - **Blocked today.** A non-debuggable app never executes `wrap.sh`, so
     `libreserve.so` cannot claim the guest window before ART. The launcher's
     runtime audit then reserves after ART has started; it now aborts on any
     conflict instead of overwriting memory, but no non-debuggable build has
     been proven to boot reliably on a device.  Do not publish a production
     APK until a pre-ART reservation path is proven.  Next experiment: sign a
     non-debuggable build, launch it on the test device, and capture the
     launcher audit plus `/proc/<pid>/maps` across repeated cold starts to see
     whether the window is still free at that point.

## What the packager enforces

`package_mobile_apk.py` resolves a key from, in order,
`GALAXY_KEYSTORE_B64`, `GALAXY_KEYSTORE`, `out/release.keystore`,
`out/debug.keystore`, `platform/mobile/release.keystore`, then:

1. rejects the compromised fingerprint and any certificate that does not match
   `GALAXY_EXPECTED_SIGNER_SHA256`;  the pre-check inspects the certificate of
   `GALAXY_KEY_ALIAS`, the alias that will sign, not just the first key in the
   store;
2. requires `GALAXY_KEYSTORE_PASS` and `GALAXY_EXPECTED_SIGNER_SHA256` for
   production;
3. signs the APK with `GALAXY_KEY_ALIAS`;
4. reads the certificate back with `apksigner verify` and refuses to hand over
   the APK if it does not satisfy the same policy.

No channel has a default password.  A missing key is an error unless
`GALAXY_ALLOW_NEW_KEY=1` explicitly asks for a local development key, in which
case `GALAXY_KEYSTORE_PASS` must still be supplied.

The legacy shell packagers (`package_mobile_apk.sh`, `tools/package_apk.sh`)
apply the same policy: they require an explicit password, refuse to create a
key without `GALAXY_ALLOW_NEW_KEY=1`, sign with `GALAXY_KEY_ALIAS`, and verify
the finished APK's certificate.  `installer-mobile/build.py` uses the shared
policy for the installer APK.

`tools/verify_release_apk.py` applies the policy to the artifact that is
actually uploaded, not just the one the packager saw.  Besides the signer it
rejects a debuggable manifest in production, missing or duplicate native
entries, a second ABI, empty entries, non-AArch64 libraries, a `wrap.sh` that
is not a LF-only shebang script and a `classes.dex` without the Dalvik magic.
The release workflow runs it once after packaging and again on the downloaded
artifact in the job that publishes it.

## Verifying by hand

```bash
apksigner verify --print-certs out-mobile/GalaxyAndroid.apk
adb install --no-incremental -r out-mobile/GalaxyAndroid.apk
```

The printed SHA-256 must equal `GALAXY_EXPECTED_SIGNER_SHA256` and must not be
the compromised fingerprint.  History has not been rewritten: purging the
old key from Git history would not make it secret again and would break every
existing clone.  Rotation is the remedy.
