param([string]$OutputDirectory, [switch]$BuildVerifier)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $repo 'dist\LoosePakFix' }
$out = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $out | Out-Null
$commit = (& git -C $repo rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Cannot identify source commit' }
$dirty = [bool](& git -C $repo status --porcelain)
$buildId = 'dev-g' + $commit.Substring(0,7)
@"
#define TCHIA_BUILD_ID "$buildId"
#define TCHIA_SOURCE_COMMIT "$commit"
"@ | Set-Content -LiteralPath (Join-Path $out 'build_version.h') -Encoding ascii
$vswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
$vs = (& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath).Trim()
if (-not $vs) { throw 'MSVC x64 tools not found' }
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
$batch = Join-Path $out 'build.cmd'
@"
@echo off
call "$vcvars" >nul
if errorlevel 1 exit /b 1
cd /d "$out"
ml64 /nologo /c /Fo"forward.obj" "$PSScriptRoot\forward.asm"
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /O2 /MT /GS /W4 /WX /EHsc /I"$out" /LD "$PSScriptRoot\patch.cpp" "$PSScriptRoot\proxy.cpp" forward.obj /Fe:X3DAudio1_7.dll /link /DEF:"$PSScriptRoot\exports.def" /IMPLIB:X3DAudio1_7.lib
if errorlevel 1 exit /b 1
if not "%~1"=="verify" exit /b 0
cl /nologo /std:c++17 /O2 /MT /GS /W4 /WX /EHsc "$PSScriptRoot\verify.cpp" "$PSScriptRoot\patch.cpp" /Fe:verify.exe
exit /b %errorlevel%
"@ | Set-Content -LiteralPath $batch -Encoding ascii
$mode = if ($BuildVerifier) { 'verify' } else { 'dll' }
& $env:ComSpec /d /c $batch $mode
if ($LASTEXITCODE -ne 0) { throw "Native build failed: $LASTEXITCODE" }
@{ build_id=$buildId; commit=$commit; dirty=$dirty; dll_sha256=(Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $out 'X3DAudio1_7.dll')).Hash.ToLowerInvariant() } |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $out 'build-report.json') -Encoding utf8
