param(
    [Parameter(Mandatory=$true)]
    [string]$GameDir,
    [string]$BackupRoot = ''
)

$ErrorActionPreference = 'Stop'
$gamePath = (Resolve-Path -LiteralPath $GameDir).Path
if (-not (Test-Path -LiteralPath (Join-Path $gamePath 'Outlast2.exe') -PathType Leaf)) {
    throw 'GameDir must be the Outlast 2 Binaries\Win64 folder containing Outlast2.exe.'
}
if (Get-Process -Name Outlast2,Outlast2VR -ErrorAction SilentlyContinue) {
    throw 'Close Outlast 2 and the Outlast 2 VR launcher before installing.'
}

$files = @(
    'dinput8.dll','openxr_loader.dll','Outlast2VR.exe',
    'outlast2_vr_p32.ini','outlast2_vr_p35.ini','outlast2_vr_p37.ini',
    'INSTALL.md','README.md','RELEASE_NOTES.md','LICENSE','THIRD_PARTY_NOTICES.md',
    'OPENXR_LICENSE.md'
)
foreach ($file in $files) {
    if (-not (Test-Path -LiteralPath (Join-Path $PSScriptRoot $file) -PathType Leaf)) {
        throw "Release package is incomplete: $file"
    }
}

if (-not $BackupRoot) {
    $BackupRoot = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'Outlast2VR-install-backups'
}
$backupRoot = [IO.Path]::GetFullPath($BackupRoot)
$backup = Join-Path $backupRoot ('before-beta1-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $backup -Force | Out-Null

foreach ($file in $files) {
    $target = Join-Path $gamePath $file
    if (Test-Path -LiteralPath $target -PathType Leaf) {
        Copy-Item -LiteralPath $target -Destination (Join-Path $backup $file)
    }
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $file) -Destination $target -Force
    $sourceHash = (Get-FileHash -LiteralPath (Join-Path $PSScriptRoot $file) -Algorithm SHA256).Hash
    $targetHash = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash
    if ($sourceHash -ne $targetHash) { throw "Installed hash did not match: $file" }
}

Write-Output "Outlast 2 VR Beta 1 installed to: $gamePath"
Write-Output "Previous mod files backed up to: $backup"
Write-Output 'Launch Outlast2VR.exe and choose PLAY VR.'
