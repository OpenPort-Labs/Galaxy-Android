#!/bin/bash
# Packages build-mobile/*.so and resources into a signed mobile APK:
# out/GalaxyMobile.apk (no OpenXR dependencies).
# Run ./build_mobile.sh first.
set -e
cd "$(dirname "$0")"
source tools/env.sh

SDK=$(find_sdk) || { echo "Android SDK not found: set ANDROID_HOME" >&2; exit 1; }
BT=$(ls -d "$SDK"/build-tools/* 2> /dev/null | sort -V | tail -1)
[ -n "$BT" ] || { echo "no build-tools in $SDK" >&2; exit 1; }
PLATFORM=$(ls -d "$SDK"/platforms/android-3[2-9] "$SDK"/platforms/android-[4-9][0-9] 2> /dev/null | sort -V | tail -1)
[ -n "$PLATFORM" ] || { echo "no platforms/android-32 or newer in $SDK" >&2; exit 1; }

for j in "$JAVA_HOME" "/c/Program Files/Java/jdk-26.0.2" "/c/Program Files/Java/latest" \
  "/c/Program Files/Android/Android Studio/jbr" "/Applications/Android Studio.app/Contents/jbr/Contents/Home" \
  "/opt/android-studio/jbr"; do
  if [ -n "$j" ] && [ -x "$j/bin/keytool$EXE" ]; then
    export JAVA_HOME=$(native_path "$j")
    KEYTOOL="$j/bin/keytool$EXE"
    break
  fi
done
KEYTOOL=${KEYTOOL:-$(command -v keytool)} || { echo "no JDK found: set JAVA_HOME" >&2; exit 1; }
STRIP=$(ndk_llvm_bin)/llvm-strip$EXE
BUILD=build-mobile
OUT=out-mobile

rm -rf $OUT/apk
mkdir -p $OUT/apk/lib/arm64-v8a

KEYSTORE=out/debug.keystore
mkdir -p out
if [ ! -f "$KEYSTORE" ]; then
  "$KEYTOOL" -genkeypair -keystore "$KEYSTORE" -storepass android -keypass android -alias androiddebugkey \
    -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=GalaxyMobile Debug,O=Local,C=US" > /dev/null
fi

rm -f $OUT/res.zip
"$BT/aapt2$EXE" compile --dir platform/android/res -o $OUT/res.zip
"$BT/aapt2$EXE" link -o $OUT/base.apk --manifest platform/mobile/AndroidManifest.xml -R $OUT/res.zip --auto-add-overlay \
  -I "$(native_path "$PLATFORM/android.jar")" --min-sdk-version 32 --target-sdk-version 34

# Android runs wrap.sh (debuggable APKs only); strip CRs
tr -d '\r' < platform/android/wrap.sh > $OUT/apk/lib/arm64-v8a/wrap.sh
for lib in libgame.so libmain.so libreserve.so; do
  "$STRIP" --strip-unneeded -o $OUT/apk/lib/arm64-v8a/$lib $BUILD/$lib
done

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
  # On Windows, run apksigner.bat or apksigner.jar
  APKSIGNER_BAT="$BT/apksigner.bat"
  cmd //c "$(native_path "$APKSIGNER_BAT")" sign --ks "$(native_path "$KEYSTORE")" --ks-pass pass:android --key-pass pass:android \
    --out "$(native_path "$OUT/GalaxyMobile.apk")" "$(native_path "$OUT/aligned.apk")"
else
  "$BT/apksigner" sign --ks $KEYSTORE --ks-pass pass:android --key-pass pass:android --out $OUT/GalaxyMobile.apk $OUT/aligned.apk
fi

rm -f $OUT/base.apk $OUT/aligned.apk $OUT/res.zip
echo "Successfully built $OUT/GalaxyMobile.apk ($(wc -c < $OUT/GalaxyMobile.apk) bytes)"
echo "To install on connected device: adb install --no-incremental -r $OUT/GalaxyMobile.apk"
