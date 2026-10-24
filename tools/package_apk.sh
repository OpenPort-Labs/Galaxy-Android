#!/bin/bash
# Packages build-android/*.so and the app's pictures (platform/android/res)
# into a signed APK (no Gradle needed): out/GalaxyQuest.apk.  Run
# ./build_android.sh first.
# Needs the Android SDK (build-tools and platforms/android-36 or another
# android-3x) and a JDK; see tools/env.sh for how they are found.
# The APK is signed with out/debug.keystore, made on first use: keep it, as
# an update installs over an earlier APK only when both carry the same key.
set -e
cd "$(dirname "$0")/.."
source tools/env.sh

SDK=$(find_sdk) || { echo "Android SDK not found: set ANDROID_HOME" >&2; exit 1; }
BT=$(ls -d "$SDK"/build-tools/* 2> /dev/null | sort -V | tail -1)
[ -n "$BT" ] || { echo "no build-tools in $SDK" >&2; exit 1; }
PLATFORM=$(ls -d "$SDK"/platforms/android-3[2-9] "$SDK"/platforms/android-[4-9][0-9] 2> /dev/null | sort -V | tail -1)
[ -n "$PLATFORM" ] || { echo "no platforms/android-32 or newer in $SDK" >&2; exit 1; }
# A JDK: JAVA_HOME, then Android Studio's own.
for j in "$JAVA_HOME" "/c/Program Files/Android/Android Studio/jbr" "/Applications/Android Studio.app/Contents/jbr/Contents/Home" \
  "/opt/android-studio/jbr"; do
  if [ -n "$j" ] && [ -x "$j/bin/keytool$EXE" ]; then
    export JAVA_HOME=$(native_path "$j")
    KEYTOOL="$j/bin/keytool$EXE"
    break
  fi
done
KEYTOOL=${KEYTOOL:-$(command -v keytool)} || { echo "no JDK found: set JAVA_HOME" >&2; exit 1; }
STRIP=$(ndk_llvm_bin)/llvm-strip$EXE
OPENXR=third_party/openxr/prefab/modules/openxr_loader/libs/android.arm64-v8a/libopenxr_loader.so
OUT=out
rm -rf $OUT/apk
mkdir -p $OUT/apk/lib/arm64-v8a

KEYSTORE=$OUT/debug.keystore
if [ ! -f $KEYSTORE ]; then
  "$KEYTOOL" -genkeypair -keystore $KEYSTORE -storepass android -keypass android -alias androiddebugkey \
    -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=PetariVR Debug,O=Local,C=US" > /dev/null
fi

GALAXY_KEYSTORE_PASS="${GALAXY_KEYSTORE_PASS:-android}"
export GALAXY_KEYSTORE_PASS
py3 - "$KEYSTORE" << 'PY'
import sys
from tools.apk_env import check_signer, keystore_fingerprint
problem = check_signer(keystore_fingerprint(sys.argv[1]), channel='tester')
if problem:
    sys.exit('REFUSING TO SIGN: ' + problem)
PY

rm -f $OUT/res.zip
"$BT/aapt2$EXE" compile --dir platform/android/res -o $OUT/res.zip
"$BT/aapt2$EXE" link -o $OUT/base.apk --manifest platform/android/AndroidManifest.xml -R $OUT/res.zip --auto-add-overlay \
  -I "$(native_path "$PLATFORM/android.jar")" --min-sdk-version 32 --target-sdk-version 34

# Android runs wrap.sh (debuggable APKs only); a CR would break its #! line.
tr -d '\r' < platform/android/wrap.sh > $OUT/apk/lib/arm64-v8a/wrap.sh
for lib in libgame.so libmain.so libreserve.so; do
  "$STRIP" --strip-unneeded -o $OUT/apk/lib/arm64-v8a/$lib build-android/$lib
done
cp $OPENXR $OUT/apk/lib/arm64-v8a/

(cd $OUT/apk && py3 - << 'PY'
import zipfile, os
z = zipfile.ZipFile('../base.apk', 'a', zipfile.ZIP_DEFLATED)
for root, _, files in os.walk('lib'):
    for f in files:
        p = os.path.join(root, f).replace('\\', '/')
        z.write(p, p)
z.close()
PY
)

"$BT/zipalign$EXE" -f -p 16 $OUT/base.apk $OUT/aligned.apk
if [ -n "$IS_WINDOWS" ]; then
  cmd //c "$(cygpath -w "$BT/apksigner.bat")" sign --ks "$(cygpath -w $KEYSTORE)" --ks-pass env:GALAXY_KEYSTORE_PASS \
    --out "$(cygpath -w $OUT/GalaxyQuest.apk)" "$(cygpath -w $OUT/aligned.apk)"
else
  "$BT/apksigner" sign --ks $KEYSTORE --ks-pass env:GALAXY_KEYSTORE_PASS --out $OUT/GalaxyQuest.apk $OUT/aligned.apk
fi
rm -f $OUT/base.apk $OUT/aligned.apk $OUT/res.zip
echo "built $OUT/GalaxyQuest.apk ($(wc -c < $OUT/GalaxyQuest.apk) bytes)"
# An incremental install only unpacks the .so files, not wrap.sh.
echo "install with: adb install --no-incremental -r $OUT/GalaxyQuest.apk"
