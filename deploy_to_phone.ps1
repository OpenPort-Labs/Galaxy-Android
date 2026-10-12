$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent -Path $MyInvocation.MyCommand.Definition
if ($ScriptDir) { Set-Location $ScriptDir }
$ADB = "C:\Users\carlo\AppData\Local\Android\Sdk\platform-tools\adb.exe"

Write-Host "Checking for connected Android device..."
$devices = & $ADB devices
Write-Host ($devices -join "`n")

if ($devices -match "unauthorized") {
    Write-Host "`n[!] Device detected but UNAUTHORIZED." -ForegroundColor Yellow
    Write-Host "Please unlock your phone and tap 'Allow' on the 'Allow USB debugging?' popup screen.`n"
    exit 1
}

$ready = $devices | Where-Object { $_ -match "\bdevice\b" -and $_ -notmatch "List of devices" }
if (-not $ready) {
    Write-Host "`n[!] No Android device detected." -ForegroundColor Red
    Write-Host "Please make sure:"
    Write-Host "1. Phone is plugged into USB."
    Write-Host "2. USB Debugging is ON in Settings > Developer Options."
    Write-Host "3. Phone is unlocked.`n"
    exit 1
}

Write-Host "Installing updated GalaxyMobile.apk..."
& $ADB install --no-incremental -r out-mobile/GalaxyMobile.apk

Write-Host "Checking if MovieData already exists on device..."
$movieCheck = & $ADB shell "ls -d /sdcard/Android/data/com.galaxy.mobile/files/game/files/MovieData 2>/dev/null"
if ($movieCheck -match "MovieData") {
    Write-Host "MovieData already exists on device. Verifying PrologueA.thp..."
    $prologueCheck = & $ADB shell "ls -l /sdcard/Android/data/com.galaxy.mobile/files/game/files/MovieData/PrologueA.thp 2>/dev/null"
    Write-Host $prologueCheck
} else {
    Write-Host "Pushing MovieData (2.39 GB)..."
    & $ADB push movies.tar /data/local/tmp/petari_movies.tar
    Write-Host "Extracting MovieData into game storage..."
    & $ADB shell "mkdir -p /sdcard/Android/data/com.galaxy.mobile/files/game/files && cd /sdcard/Android/data/com.galaxy.mobile/files/game/files && tar xf /data/local/tmp/petari_movies.tar && rm /data/local/tmp/petari_movies.tar && chmod -R a+rX /sdcard/Android/data/com.galaxy.mobile/files/game/files/MovieData"
    Write-Host "MovieData extraction complete!"
}

Write-Host "Launching GalaxyMobile on device..."
& $ADB shell am start -n com.galaxy.mobile/android.app.NativeActivity
Write-Host "GalaxyMobile launched successfully!"
