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
| Store password | `android` (default; override with `GALAXY_KEYSTORE_PASS`) |
| Key alias | `androiddebugkey` |

The fingerprint is public information - it is what Android compares - and the
packager pins it in `package_mobile_apk.py` (`PROJECT_KEY_SHA256`).

The keystore itself is **not** in the repository: it was committed once, which
put the private key in public history, and it has since been removed and added
to `.gitignore`.  Keep the copy you have safe; treat it as the identity of the
app.  Rotating it is possible, but it forces every existing player through a
one-time uninstall.

## Local builds

Put the key at `out/release.keystore` (`out/` is git-ignored) and the packager
finds it:

```bash
cp /path/to/release.keystore out/release.keystore
python package_mobile_apk.py
```

`out/debug.keystore` and the old `platform/mobile/release.keystore` location are
still accepted so existing checkouts keep working.

## CI releases

The release workflow decodes the key from repository secrets:

| Secret | Value |
| --- | --- |
| `GALAXY_KEYSTORE_B64` | `base64 -w0 out/release.keystore` |
| `GALAXY_KEYSTORE_PASS` | the store password (`android` for the project key) |

Set them under *Settings -> Secrets and variables -> Actions*.  Without them the
release build **fails** instead of publishing an APK that nobody could install
over their current one - that is the whole point of the change.

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
