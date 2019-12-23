
# Windows build script for PowerShell

$ErrorActionPreference = "Stop"

Import-Module ".\.ldrelease\helpers.psm1" -Force

SetupVSToolsEnv -architecture amd64

DownloadAndUnzip -url "https://curl.haxx.se/download/curl-7.59.0.zip" -filename "curl.zip"
DownloadAndUnzip -url "https://ftp.pcre.org/pub/pcre/pcre-8.43.zip" -filename "pcre.zip"

Write-Host
Write-Host Building curl
Push-Location
cd curl-7.59.0/winbuild
ExecuteOrFail { nmake /f Makefile.vc mode=static }
Pop-Location

Write-Host
Write-Host Building PCRE
Push-Location
cd pcre-8.43
New-Item -ItemType Directory -Force -Path .\build
cd build
ExecuteOrFail { cmake -G "Visual Studio 16 2019" -A x64 .. }
ExecuteOrFail { cmake --build . }
Pop-Location

Write-Host
Write-Host Building SDK
Push-Location
New-Item -ItemType Directory -Force -Path .\build
cd build
ExecuteOrFail { cmake -G "Visual Studio 16 2019" -A x64 .. }
ExecuteOrFail { cmake --build . }
Pop-Location
