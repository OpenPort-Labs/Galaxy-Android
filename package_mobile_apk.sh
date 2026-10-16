#!/bin/bash
# Packages build-mobile/*.so and resources into signed mobile APKs:
# out-mobile/GalaxyMobile.apk (Vanilla - no mods)
# out-mobile/GalaxyMobilePlus.apk (Plus - all mods included)
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

# Prepare shared native libraries
tr -d '\r' < platform/android/wrap.sh > $OUT/apk/lib/arm64-v8a/wrap.sh
for lib in libgame.so libmain.so libreserve.so; do
  "$STRIP" --strip-unneeded -o $OUT/apk/lib/arm64-v8a/$lib $BUILD/$lib
done

rm -f $OUT/res.zip
# The mobile manifest themes come from platform/mobile/res (Theme.GalaxyAndroid).
"$BT/aapt2$EXE" compile --dir platform/mobile/res -o $OUT/res.zip

package_flavor() {
  local MANIFEST="$1"
  local OUT_APK="$2"
  local LABEL="$3"

  rm -f $OUT/base.apk $OUT/aligned.apk
  "$BT/aapt2$EXE" link -o $OUT/base.apk --manifest "$MANIFEST" -R $OUT/res.zip --auto-add-overlay \
    -I "$(native_path "$PLATFORM/android.jar")" --min-sdk-version 28 --target-sdk-version 34

  (cd $OUT/apk && py3 - << 'PY'
import zipfile, os
z = zipfile.ZipFile('../base.apk', 'a')
for root, _, files in os.walk('lib'):
    for f in files:
        p = os.path.join(root, f).replace('\\', '/')
        c_type = zipfile.ZIP_STORED if f.endswith('.so') else zipfile.ZIP_DEFLATED
        z.write(p, p, compress_type=c_type)
z.close()
PY
  )

  "$BT/zipalign$EXE" -f -p 16 $OUT/base.apk $OUT/aligned.apk
  if [ -n "$IS_WINDOWS" ]; then
    APKSIGNER_BAT="$BT/apksigner.bat"
    cmd //c "$(native_path "$APKSIGNER_BAT")" sign --ks "$(native_path "$KEYSTORE")" --ks-pass pass:android --key-pass pass:android \
      --out "$(native_path "$OUT_APK")" "$(native_path "$OUT/aligned.apk")"
  else
    "$BT/apksigner" sign --ks $KEYSTORE --ks-pass pass:android --key-pass pass:android --out "$OUT_APK" $OUT/aligned.apk
  fi
  rm -f $OUT/base.apk $OUT/aligned.apk
  echo "Successfully built $LABEL: $OUT_APK ($(wc -c < "$OUT_APK") bytes)"
}

package_flavor platform/mobile/AndroidManifest.xml "$OUT/GalaxyAndroid.apk" "GalaxyAndroid (Unified Engine)"

rm -f $OUT/res.zip
echo "All APKs generated successfully in $OUT/"
