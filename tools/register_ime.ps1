param(
    [ValidateSet('Register','Unregister','Status')]
    [string]$Action='Status',
    [ValidateSet('Debug','Release')]
    [string]$Configuration='Release',
    [string]$ArtifactDirectory
)

$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
if($ArtifactDirectory) {$artifactDirectory=[IO.Path]::GetFullPath($ArtifactDirectory)}
elseif(Test-Path -LiteralPath (Join-Path $projectRoot 'bin\taiwan_bopomofo.dll')) {$artifactDirectory=Join-Path $projectRoot 'bin'}
else {$artifactDirectory=Join-Path $projectRoot "build\windows\src\windows_tsf\$Configuration"}
$dll=Join-Path $artifactDirectory 'taiwan_bopomofo.dll'
$probe=Join-Path $artifactDirectory 'ime_profile_probe.exe'
$regsvr=Join-Path ([Environment]::SystemDirectory) 'regsvr32.exe'

if(!(Test-Path -LiteralPath $probe)) {throw "Profile probe not found: $probe. Build the $Configuration preset first."}

if($Action -eq 'Status') {
    & $probe
    exit $LASTEXITCODE
}

$principal=[Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
if(!$principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw "TSF COM registration is machine-wide. Reopen PowerShell as Administrator, then run: .\tools\register_ime.ps1 -Action $Action -Configuration $Configuration"
}
if(!(Test-Path -LiteralPath $dll)) {throw "IME DLL not found: $dll. Build the $Configuration preset first."}

if($Action -eq 'Register') {
    $registration=Start-Process -FilePath $regsvr -ArgumentList @('/s', ('"' + $dll + '"')) -Wait -PassThru -WindowStyle Hidden
    if($registration.ExitCode -ne 0) {throw "regsvr32 registration failed with exit code $($registration.ExitCode)."}
    & $probe
    if($LASTEXITCODE -ne 0) {throw 'Registration returned success, but the COM server/profile probe failed.'}
    Write-Host 'Taiwan Bopomofo is registered and enabled for the current user.'
    Write-Host 'Open a non-elevated Notepad window, select Taiwan Bopomofo with Win+Space, and follow docs/compatibility.md.'
    exit 0
}

$registration=Start-Process -FilePath $regsvr -ArgumentList @('/u', '/s', ('"' + $dll + '"')) -Wait -PassThru -WindowStyle Hidden
if($registration.ExitCode -ne 0) {throw "regsvr32 unregistration failed with exit code $($registration.ExitCode)."}
& $probe --expect-absent
if($LASTEXITCODE -ne 0) {throw 'Unregistration returned success, but a clean absent profile could not be verified.'}
Write-Host 'Taiwan Bopomofo was unregistered.'
exit 0
