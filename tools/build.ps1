param([ValidateSet('Debug','Release')][string]$Configuration='Release', [int]$Jobs=2)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
$cmakeCommand=Get-Command cmake -ErrorAction SilentlyContinue
if($cmakeCommand) {$cmakePath=$cmakeCommand.Source} else {
    $vswherePath="${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $installationPath=& $vswherePath -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    $cmakePath=Join-Path $installationPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
}
if(!(Test-Path -LiteralPath $cmakePath)) {throw 'Install Visual Studio 2022 C++ Build Tools with CMake and the Windows SDK.'}
Push-Location $projectRoot
try {
    & python (Join-Path $PSScriptRoot 'run_cmake.py') --preset windows
    if($LASTEXITCODE -ne 0) {throw 'CMake configure failed'}
    & python (Join-Path $PSScriptRoot 'run_cmake.py') --build --preset $Configuration.ToLower() --parallel $Jobs
    if($LASTEXITCODE -ne 0) {throw 'Compilation failed'}
    & python (Join-Path $PSScriptRoot 'run_cmake.py') --ctest --preset $Configuration.ToLower()
    if($LASTEXITCODE -ne 0) {throw 'Tests failed'}
} finally {Pop-Location}
