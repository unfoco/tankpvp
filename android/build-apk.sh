#!/bin/bash
# Packages build/android/arm64-v8a/release/libmain.so into an installable debug-signed APK.
# Requires: Android SDK build-tools + platform (installed via sdkmanager), a JDK, and a prior
#   xmake f -p android -a arm64-v8a --ndk=... --ndk_sdkver=28 && xmake
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SDK="${ANDROID_SDK_ROOT:-$HOME/Library/Android/sdk}"
# build-tools 34 dexer/signer require a Java 17-era JRE (Java 21+ breaks d8 internals)
if [ -d /opt/homebrew/opt/openjdk@17 ]; then
    export JAVA_HOME=/opt/homebrew/opt/openjdk@17
    export PATH="$JAVA_HOME/bin:$PATH"
fi
BT="$SDK/build-tools/34.0.0"
PLATFORM="$SDK/platforms/android-34/android.jar"
OUT="$ROOT/build/apk"
SO="$ROOT/build/android/arm64-v8a/release/libmain.so"

[ -f "$SO" ] || { echo "libmain.so not found; run the xmake android build first"; exit 1; }

rm -rf "$OUT"
mkdir -p "$OUT/staging/lib/arm64-v8a" "$OUT/staging/assets" "$OUT/classes"

# 1. Native library
cp "$SO" "$OUT/staging/lib/arm64-v8a/libmain.so"

# 2. Game content: asset/ and mods/ bundled as APK assets, plus a file list the
#    engine reads at first launch to extract them to internal storage.
(cd "$ROOT" && find asset mods -type f ! -name ".DS_Store" | LC_ALL=C sort) > "$OUT/asset_list.txt"
while IFS= read -r f; do
    mkdir -p "$OUT/staging/assets/$(dirname "$f")"
    cp "$ROOT/$f" "$OUT/staging/assets/$f"
done < "$OUT/asset_list.txt"
cp "$OUT/asset_list.txt" "$OUT/staging/assets/embrik_assets.txt"

# 3. Java: SDL glue + EmbrikActivity
find "$ROOT/android/java" -name "*.java" > "$OUT/java_sources.txt"
javac --release 11 -classpath "$PLATFORM" -d "$OUT/classes" @"$OUT/java_sources.txt" 2> >(grep -v "deprecat" >&2 || true)
(cd "$OUT/classes" && jar cf "$OUT/classes.jar" .)
"$BT/d8" --release --lib "$PLATFORM" --output "$OUT" "$OUT/classes.jar"

# 4. Package: manifest via aapt2, then add dex, native lib, and assets
"$BT/aapt2" link -o "$OUT/base.apk" --manifest "$ROOT/android/AndroidManifest.xml" -I "$PLATFORM"
cd "$OUT/staging"
cp "$OUT/base.apk" "$OUT/unaligned.apk"
cp "$OUT/classes.dex" .
zip -q "$OUT/unaligned.apk" classes.dex
zip -q -r "$OUT/unaligned.apk" lib assets

# 5. Align + debug-sign
"$BT/zipalign" -f -p 4 "$OUT/unaligned.apk" "$OUT/aligned.apk"
KEYSTORE="$HOME/.android/debug.keystore"
if [ ! -f "$KEYSTORE" ]; then
    mkdir -p "$HOME/.android"
    keytool -genkeypair -keystore "$KEYSTORE" -storepass android -keypass android \
        -alias androiddebugkey -dname "CN=Android Debug,O=Android,C=US" \
        -keyalg RSA -keysize 2048 -validity 10000
fi
"$BT/apksigner" sign --ks "$KEYSTORE" --ks-pass pass:android --ks-key-alias androiddebugkey \
    --out "$OUT/embrik.apk" "$OUT/aligned.apk"

echo "APK: $OUT/embrik.apk ($(du -h "$OUT/embrik.apk" | cut -f1 | tr -d ' '))"
