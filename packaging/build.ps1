# Throne - full Windows x64 build in one script (same approach as MCP-app/packaging).
#
#   powershell -ExecutionPolicy Bypass -File packaging\build.ps1
#   powershell -ExecutionPolicy Bypass -File packaging\build.ps1 -Version 1.3.0
#   powershell -ExecutionPolicy Bypass -File packaging\build.ps1 -Clean -SkipInstaller
#
# Steps: web dashboard -> protoc + Go plugins -> Go core (script/build_go.sh) -> Qt + OpenSSL
#        -> MSVC environment -> CMake/Ninja -> deployment\windows-amd64 -> portable zip
#        -> Setup.exe (script\windows_installer.iss, x64 only, if Inno Setup 6 is installed).
#
# Needs: Visual Studio 2022 with the C++ workload (it brings cmake and ninja), Go, Git for Windows, 7-Zip.
# The repository path must not contain spaces (script/build_go.sh does not quote $DEST).
# Output: deployment\windows-amd64\  deployment\Throne-<ver>-windows64.zip  deployment\Throne-<ver>-windows64-setup.exe
# On a compile error the errors are printed at the end (and as GitHub annotations in CI); full log: build\ninja.log
#
# ASCII only on purpose: Windows PowerShell 5.1 reads BOM-less scripts as ANSI.

param(
    [string]$Version = "0.0.0",       # "1.3.0", "v1.3.0" or "1.3.0-dev.7"
    [string]$QtVersion = "6.12.0",    # release of throneproj/buildqt
    [string]$ProtocVersion = "36.2",
    [switch]$SkipInstaller,           # do not build Setup.exe even if Inno Setup is installed
    [switch]$Clean                    # delete build\ and deployment\ first
)

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$Root = Split-Path -Parent $PSScriptRoot
Set-Location $Root

$Arch = "x64"
$GoArch = "amd64"
$Dest = Join-Path $Root "deployment\windows-amd64"
$Tools = Join-Path $Root "build-tools"
$Downloads = Join-Path $Tools "downloads"
$Build = Join-Path $Root "build"

function Step($text) { Write-Host "`n==> $text" -ForegroundColor Cyan }
function Check($what) { if ($LASTEXITCODE -ne 0) { throw "$what failed (exit code $LASTEXITCODE)" } }
function Slash($path) { return ($path -replace "\\", "/") }

function Download($Url, $OutFile) {
    if (Test-Path $OutFile) { Write-Host "Cached: $OutFile"; return }
    Write-Host "Downloading $Url"
    $part = "$OutFile.part"
    Invoke-WebRequest -Uri $Url -OutFile $part -UseBasicParsing
    Move-Item -Force $part $OutFile
}

function Find-Tool($Name, $Candidates, $Hint) {
    $cmd = Get-Command $Name -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    $found = $Candidates | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
    if (-not $found) { throw "$Name not found. $Hint" }
    return $found
}

# ------------------------------------------------------------------ 0. version and tools
$Version = $Version.TrimStart("v")
if ($Version -notmatch '^(\d+)(?:\.(\d+))?(?:\.(\d+))?') { throw "Version '$Version' must start with digits, e.g. 1.3.0" }
$Major = $Matches[1]
$Minor = if ($Matches[2]) { $Matches[2] } else { "0" }
$Patch = if ($Matches[3]) { $Matches[3] } else { "0" }
Write-Host "Throne $Version (windows-amd64, Qt $QtVersion)" -ForegroundColor White

Find-Tool "go" @() "Install Go (the version from build.yml, GO_VERSION)." | Out-Null
$Git = Find-Tool "git" @() "Install Git for Windows: winget install Git.Git"
# Not 'bash' from PATH: on Windows that can be the WSL launcher.
$Bash = @(
    (Join-Path (Split-Path (Split-Path $Git)) "bin\bash.exe"),
    "$env:ProgramFiles\Git\bin\bash.exe",
    "${env:ProgramFiles(x86)}\Git\bin\bash.exe"
) | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $Bash) { throw "Git Bash (Git\bin\bash.exe) not found. Install Git for Windows." }
$SevenZip = Find-Tool "7z" @("$env:ProgramFiles\7-Zip\7z.exe") "Install 7-Zip: winget install 7zip.7zip"

if ($Clean) {
    Step "Clean"
    foreach ($d in @($Build, (Join-Path $Root "deployment"))) { if (Test-Path $d) { Remove-Item -Recurse -Force $d } }
}
New-Item -ItemType Directory -Force -Path $Downloads, $Build | Out-Null

# ------------------------------------------------------------------ 1. web dashboard
Step "Web dashboard (SagerNet/sing-box-dashboard, gh-pages)"
$DashDir = Join-Path $Root "res\dashboard"
$DashIndex = Join-Path $DashDir "index.html"
if (-not (Test-Path $DashIndex)) {
    $DashSrc = Join-Path $Tools "dashboard-src"
    if (Test-Path $DashSrc) { Remove-Item -Recurse -Force $DashSrc }
    & $Git clone --quiet --depth 1 --branch gh-pages https://github.com/SagerNet/sing-box-dashboard.git $DashSrc
    Check "git clone sing-box-dashboard"
    Remove-Item -Recurse -Force (Join-Path $DashSrc ".git")
    New-Item -ItemType Directory -Force -Path $DashDir | Out-Null
    Copy-Item -Path (Join-Path $DashSrc "*") -Destination $DashDir -Recurse -Force
}
# cmake globs res/dashboard at configure time and silently ships nothing if it is empty.
if (-not (Test-Path $DashIndex)) { throw "res\dashboard\index.html is missing" }
Write-Host "OK"

# ------------------------------------------------------------------ 2. protoc + Go plugins
Step "protoc $ProtocVersion and Go plugins"
$ProtocZip = Join-Path $Downloads "protoc-$ProtocVersion-win64.zip"
Download "https://github.com/protocolbuffers/protobuf/releases/download/v$ProtocVersion/protoc-$ProtocVersion-win64.zip" $ProtocZip
$ProtocDir = Join-Path $Tools "protoc-$ProtocVersion"
if (-not (Test-Path (Join-Path $ProtocDir "bin\protoc.exe"))) { Expand-Archive -Path $ProtocZip -DestinationPath $ProtocDir -Force }
$GoBin = Join-Path ((& go env GOPATH) | Select-Object -First 1) "bin"
$env:PATH = "$ProtocDir\bin;$GoBin;$env:PATH"
& go install google.golang.org/protobuf/cmd/protoc-gen-go@v1.36.12
Check "go install protoc-gen-go"
& go install google.golang.org/grpc/cmd/protoc-gen-go-grpc@v1.6.2
Check "go install protoc-gen-go-grpc"
& protoc --version
Check "protoc --version"

# ------------------------------------------------------------------ 3. Go core
Step "Go core (script/build_go.sh, windows/$GoArch)"
$env:GOOS = "windows"
$env:GOARCH = $GoArch
$env:DEST = Slash $Dest
try {
    & $Bash "script/build_go.sh"
    Check "script/build_go.sh"
} finally {
    Remove-Item Env:GOOS, Env:GOARCH -ErrorAction SilentlyContinue
}

# ------------------------------------------------------------------ 4. Qt + OpenSSL
Step "Qt $QtVersion and OpenSSL ($Arch)"
$QtArchive = Join-Path $Downloads "Qt_${QtVersion}_$Arch.7z"
Download "https://github.com/throneproj/buildqt/releases/download/Qt_$QtVersion/Qt_${QtVersion}_$Arch.7z" $QtArchive
$SslArchive = Join-Path $Downloads "openssl_$Arch.7z"
Download "https://github.com/throneproj/env_windows_legacy/releases/download/latest/openssl_$Arch.7z" $SslArchive
$QtDir = Join-Path $Tools "qt-$QtVersion-$Arch"
if (-not (Test-Path (Join-Path $QtDir "Qt\lib\cmake"))) {
    & $SevenZip x $QtArchive "-o$QtDir" -y | Out-Null
    Check "7z x Qt"
}
$SslDir = Join-Path $Tools "openssl-$Arch"
if (-not (Test-Path (Join-Path $SslDir "openssl"))) {
    & $SevenZip x $SslArchive "-o$SslDir" -y | Out-Null
    Check "7z x OpenSSL"
}
$env:CMAKE_PREFIX_PATH = Slash (Join-Path $QtDir "Qt\lib\cmake")
$env:OPENSSL_ROOT_DIR = Slash (Join-Path $SslDir "openssl")
Write-Host "CMAKE_PREFIX_PATH=$env:CMAKE_PREFIX_PATH"
Write-Host "OPENSSL_ROOT_DIR=$env:OPENSSL_ROOT_DIR"

# ------------------------------------------------------------------ 5. MSVC environment
Step "MSVC environment (vcvarsall $Arch)"
$Vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $Vswhere)) { throw "Visual Studio not found (no vswhere.exe). Install VS 2022 with 'Desktop development with C++'." }
$Vs = & $Vswhere -latest -products * -prerelease -property installationPath | Select-Object -First 1
if (-not $Vs) { throw "Visual Studio not found" }
# A wrapper script keeps the quoted path away from PowerShell's cmd.exe argument quoting.
$VcBat = Join-Path $env:TEMP "throne-vcvars.cmd"
Set-Content -LiteralPath $VcBat -Encoding ascii -Value "@call `"$Vs\VC\Auxiliary\Build\vcvarsall.bat`" $Arch >nul && set"
$VcVars = & $VcBat
# vcvarsall exits 0 even when it rejects the architecture.
if (-not ($VcVars -ceq "VSCMD_ARG_TGT_ARCH=$Arch")) { throw "vcvarsall.bat $Arch did not set up the $Arch tools" }
foreach ($line in $VcVars) {
    $name, $value = $line -split "=", 2
    if ($name -and $null -ne $value) { [Environment]::SetEnvironmentVariable($name, $value, "Process") }
}
Write-Host "Visual Studio: $Vs"
& cmake --version | Select-Object -First 1
& ninja --version

# ------------------------------------------------------------------ 6. CMake + Ninja
Step "CMake + Ninja (RelWithDebInfo)"
$Srslist = Join-Path $Build "srslist.h"
if (Test-Path $Srslist) { Remove-Item $Srslist }
Download "https://raw.githubusercontent.com/throneproj/routeprofiles/rule-set/srslist.h" $Srslist
$env:INPUT_VERSION = $Version
& cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
Check "cmake configure"

$Log = Join-Path $Build "ninja.log"
$saved = $ErrorActionPreference
$ErrorActionPreference = "Continue"
& ninja -C build 2>&1 | ForEach-Object { "$_" } | Tee-Object -FilePath $Log
$NinjaCode = $LASTEXITCODE
$ErrorActionPreference = $saved
if ($NinjaCode -ne 0) {
    $errors = Select-String -Path $Log -Pattern "error C\d+|error LNK\d+|fatal error|: error|FAILED:" | Select-Object -First 50
    Write-Host "`nCompile errors (full log: build\ninja.log):" -ForegroundColor Red
    foreach ($e in $errors) {
        Write-Host "  $($e.Line)" -ForegroundColor Red
        # Annotations show up on the run page, readable without opening the log.
        if ($env:GITHUB_ACTIONS -eq "true") { Write-Host ("::error title=Compile error::" + ($e.Line -replace "%", "%25")) }
    }
    throw "ninja failed (exit code $NinjaCode)"
}

# ------------------------------------------------------------------ 7. deployment
Step "Deployment (deployment\windows-amd64)"
$Exe = Join-Path $Build "Throne.exe"
if (-not (Test-Path $Exe)) { throw "build\Throne.exe was not produced" }
New-Item -ItemType Directory -Force -Path $Dest | Out-Null
Copy-Item $Exe $Dest -Force
$Pdb = Join-Path $Build "Throne.pdb"
if (Test-Path $Pdb) { Copy-Item $Pdb $Dest -Force }
Get-ChildItem $Dest -File | ForEach-Object { Write-Host ("  {0}  ({1:N1} MB)" -f $_.Name, ($_.Length / 1MB)) }

# ------------------------------------------------------------------ 8. portable zip
Step "Portable zip"
$StageRoot = Join-Path $Build "package"
if (Test-Path $StageRoot) { Remove-Item -Recurse -Force $StageRoot }
$Stage = Join-Path $StageRoot "Throne"
New-Item -ItemType Directory -Force -Path $Stage | Out-Null
Get-ChildItem $Dest | Where-Object { $_.Extension -ne ".pdb" } | Copy-Item -Destination $Stage -Recurse -Force
$Zip = Join-Path $Root "deployment\Throne-$Version-windows64.zip"
if (Test-Path $Zip) { Remove-Item $Zip }
Compress-Archive -Path $Stage -DestinationPath $Zip
Write-Host $Zip

# ------------------------------------------------------------------ 9. installer
if (-not $SkipInstaller) {
    Step "Installer (Inno Setup 6, x64 only)"
    $Iscc = (Get-Command "iscc.exe" -ErrorAction SilentlyContinue).Source
    if (-not $Iscc) {
        $Iscc = @(
            "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
            "$env:ProgramFiles\Inno Setup 6\ISCC.exe",
            "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe"
        ) | Where-Object { Test-Path $_ } | Select-Object -First 1
    }
    if ($Iscc) {
        # script\windows_installer.iss packs all four Windows builds; keep only windows-amd64.
        # The copy sits one level below the root too, so its SourceDir=.. still points at the repository.
        $Skip = @("deployment\windowslegacy-amd64\", "deployment\windows-arm64\", "deployment\windowslegacy-386\")
        $Lines = [IO.File]::ReadAllLines((Join-Path $Root "script\windows_installer.iss")) |
            Where-Object { $l = $_; -not ($Skip | Where-Object { $l.Contains($_) }) }
        $Iss = Join-Path $Build "installer-x64.iss"
        [IO.File]::WriteAllLines($Iss, [string[]]$Lines, (New-Object Text.UTF8Encoding $true))
        & $Iscc "/DAppVersion=$Version" "/DAppVersionMajor=$Major" "/DAppVersionMinor=$Minor" "/DAppVersionPatch=$Patch" "/DAppVersionBuild=0" "/FThrone-$Version-windows64-setup" $Iss
        Check "Inno Setup"
    } else {
        Write-Host "Inno Setup 6 not found - Setup.exe skipped. Install it: winget install --id JRSoftware.InnoSetup" -ForegroundColor Yellow
    }
}

Step "Done"
Get-ChildItem (Join-Path $Root "deployment") -File | ForEach-Object { Write-Host ("  {0}  ({1:N1} MB)" -f $_.Name, ($_.Length / 1MB)) }
Write-Host "  deployment\windows-amd64\Throne.exe"
