$ErrorActionPreference = "Stop"

$repo = Get-Location
$sdk = "C:\Users\carlo\AppData\Local\Android\Sdk"
$bt = "C:\Users\carlo\AppData\Local\Android\Sdk\build-tools\37.0.0"
$platform = "C:\Users\carlo\AppData\Local\Android\Sdk\platforms\android-37.0"
$ndk = "C:\Users\carlo\AppData\Local\Android\Sdk\ndk\28.2.13676358"
$strip = "$ndk\toolchains\llvm\prebuilt\windows-x86_64\bin\llvm-strip.exe"
$python = "$ndk\toolchains\llvm\prebuilt\windows-x86_64\python3\python.exe"
$env:JAVA_HOME = "C:\Program Files\Java\jdk-26.0.2"
$env:PATH = "C:\Program Files\Java\jdk-26.0.2\bin;$bt;" + $env:PATH

$build = "build-mobile"
$out = "out-mobile"
$keystore = "out\debug.keystore"

if (-not (Test-Path "$out\apk\lib\arm64-v8a")) {
    New-Item -ItemType Directory -Force -Path "$out\apk\lib\arm64-v8a" | Out-Null
}

# Copy and strip wrap.sh (ensure LF endings)
$wrapContent = [System.IO.File]::ReadAllText("platform\android\wrap.sh").Replace("`r`n", "`n")
[System.IO.File]::WriteAllText("$out\apk\lib\arm64-v8a\wrap.sh", $wrapContent, (New-Object System.Text.UTF8Encoding $false))

# Strip native libraries
foreach ($lib in @("libgame.so", "libmain.so", "libreserve.so")) {
    $src = "$build\$lib"
    $dst = "$out\apk\lib\arm64-v8a\$lib"
    & $strip --strip-unneeded -o $dst $src
}

# Compile resources
if (Test-Path "$out\res.zip") { Remove-Item "$out\res.zip" -Force }
& "$bt\aapt2.exe" compile --dir platform/android/res -o "$out\res.zip"

function Build-ApkFlavor {
    param(
        [string]$Manifest,
        [string]$OutApk,
        [string]$Label
    )

    Write-Host "--- Packaging $Label ---"
    if (Test-Path "$out\base.apk") { Remove-Item "$out\base.apk" -Force }
    if (Test-Path "$out\aligned.apk") { Remove-Item "$out\aligned.apk" -Force }

    & "$bt\aapt2.exe" link -o "$out\base.apk" --manifest $Manifest -R "$out\res.zip" --auto-add-overlay `
      -I "$platform\android.jar" --min-sdk-version 30 --target-sdk-version 34

    # Add native libs to base.apk
    $pyCode = @"
import zipfile, os
z = zipfile.ZipFile('$out/base.apk'.replace('\\', '/'), 'a', zipfile.ZIP_DEFLATED)
for root, _, files in os.walk('$out/apk/lib'.replace('\\', '/')):
    for f in files:
        full_p = os.path.join(root, f)
        rel_p = os.path.relpath(full_p, '$out/apk'.replace('\\', '/')).replace('\\', '/')
        z.write(full_p, rel_p)
z.close()
"@
    & $python -c $pyCode

    # 16KB page alignment
    & "$bt\zipalign.exe" -f -p 16 "$out\base.apk" "$out\aligned.apk"

    # Sign APK
    cmd.exe /c "`"$bt\apksigner.bat`" sign --ks `"$keystore`" --ks-pass pass:android --key-pass pass:android --out `"$OutApk`" `"$out\aligned.apk`""

    Remove-Item "$out\base.apk" -Force
    Remove-Item "$out\aligned.apk" -Force

    $apkInfo = Get-Item $OutApk
    Write-Host "Successfully packaged ${Label}: $OutApk ($($apkInfo.Length) bytes)"
}

Build-ApkFlavor -Manifest "platform\mobile\AndroidManifest.xml" -OutApk "$out\GalaxyMobile.apk" -Label "GalaxyMobile (Vanilla)"
Build-ApkFlavor -Manifest "platform\mobile\AndroidManifest_plus.xml" -OutApk "$out\GalaxyMobilePlus.apk" -Label "GalaxyMobile+ (Mods Enabled)"

if (Test-Path "$out\res.zip") { Remove-Item "$out\res.zip" -Force }
Write-Host "=== Both APKs built and signed successfully! ==="
