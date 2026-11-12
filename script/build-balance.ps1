param([string]$CompilerRoot = "", [switch]$SkipTests)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if (!$CompilerRoot -and (Test-Path -LiteralPath 'C:\msys64\mingw64\bin\g++.exe')) { $CompilerRoot = 'C:\msys64\mingw64\bin' }
if ($CompilerRoot) { $env:Path = $CompilerRoot + ';' + $env:Path }
foreach ($command in @('cmake','ninja','gcc','g++')) { if (!(Get-Command $command -ErrorAction SilentlyContinue)) { throw "Missing build tool: $command" } }
$thirdParty = Join-Path $projectRoot 'third_party'
New-Item -ItemType Directory -Path $thirdParty -Force | Out-Null
$sqliteDir = Join-Path $thirdParty 'sqlite'
if (!(Test-Path -LiteralPath (Join-Path $sqliteDir 'sqlite-amalgamation-3530400\sqlite3.c'))) {
    New-Item -ItemType Directory -Path $sqliteDir -Force | Out-Null
    $archive = Join-Path $sqliteDir 'sqlite.zip'
    Invoke-WebRequest -Uri 'https://www.sqlite.org/2026/sqlite-amalgamation-3530400.zip' -OutFile $archive
    Expand-Archive -LiteralPath $archive -DestinationPath $sqliteDir -Force
}
$quickjsDir = Join-Path $thirdParty 'quickjs'
if (!(Test-Path -LiteralPath (Join-Path $quickjsDir 'quickjs.h'))) {
    $revision = '71e6367f050b42f25d8e5849425070c322705d63'
    $archive = Join-Path $thirdParty 'quickjs.zip'
    Invoke-WebRequest -Uri "https://github.com/quickjs-ng/quickjs/archive/$revision.zip" -OutFile $archive
    Expand-Archive -LiteralPath $archive -DestinationPath $thirdParty -Force
    # Both paths are fixed children of third_party; no recursive deletion is used.
    Move-Item -LiteralPath (Join-Path $thirdParty "quickjs-$revision") -Destination $quickjsDir
}
$buildDir = Join-Path $projectRoot 'build'
& cmake -S $projectRoot -B $buildDir -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_C_COMPILER=$((Get-Command gcc).Source.Replace('\','/'))" "-DCMAKE_CXX_COMPILER=$((Get-Command g++).Source.Replace('\','/'))"
if ($LASTEXITCODE) { throw 'CMake configure failed' }
& cmake --build $buildDir --parallel 4
if ($LASTEXITCODE) { throw 'Build failed' }
if (!$SkipTests) {
    & ctest --test-dir $buildDir --output-on-failure
    if ($LASTEXITCODE) { throw 'Tests failed' }
}
Write-Output (Join-Path $projectRoot 'bin\CCSwitchUsageBalance.dll')
