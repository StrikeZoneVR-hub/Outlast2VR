param(
    [string]$BuildDir = 'build',
    [Parameter(Mandatory=$true)]
    [string]$OpenXRLoaderPath,
    [string]$OutputDir = 'release-assets'
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$buildRoot = [IO.Path]::GetFullPath((Join-Path $repo $BuildDir))
$outputRoot = [IO.Path]::GetFullPath((Join-Path $repo $OutputDir))
$packageName = 'Outlast2VR-Beta1.1-RC1-Windows-x64'
$stage = Join-Path $outputRoot $packageName
$archive = Join-Path $outputRoot ($packageName + '.zip')
$archiveHashFile = Join-Path $outputRoot ($packageName + '.sha256.txt')

$runtime = @(
    (Join-Path $buildRoot 'bin\dinput8.dll'),
    (Join-Path $buildRoot 'bin\Outlast2VR.exe')
)
if (-not (Test-Path -LiteralPath $runtime[0])) {
    $runtime[0] = Join-Path $buildRoot 'bin\RelWithDebInfo\dinput8.dll'
}
if (-not (Test-Path -LiteralPath $runtime[1])) {
    $runtime[1] = Join-Path $buildRoot 'bin\RelWithDebInfo\Outlast2VR.exe'
}
foreach ($file in $runtime) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Missing build output: $file" }
}

$loader = (Resolve-Path -LiteralPath $OpenXRLoaderPath).Path
$expectedLoaderHash = 'AC26A322DCDA7B2C7229F8860EEAA4B6A28E62015511D14E2ABC4643AAD0C24A'
if ((Get-FileHash -LiteralPath $loader -Algorithm SHA256).Hash -ne $expectedLoaderHash) {
    throw 'OpenXR loader does not match the verified 1.1.58 release binary.'
}

$dllText = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($runtime[0]))
if (-not $dllText.Contains('OUTLAST2VR-BETA11-COMPAT-PF18-20260914')) {
    throw 'dinput8.dll does not contain the expected PF18 build ID.'
}

New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
if (Test-Path -LiteralPath $stage) {
    $resolvedStage = (Resolve-Path -LiteralPath $stage).Path
    if ((Split-Path -Parent $resolvedStage) -ne $outputRoot -or (Split-Path -Leaf $resolvedStage) -ne $packageName) {
        throw "Refusing to replace unexpected staging path: $resolvedStage"
    }
    Remove-Item -LiteralPath $resolvedStage -Recurse -Force
}
foreach ($old in @($archive,$archiveHashFile)) {
    if (Test-Path -LiteralPath $old -PathType Leaf) { Remove-Item -LiteralPath $old -Force }
}
New-Item -ItemType Directory -Path $stage | Out-Null

$copies = @{
    $runtime[0] = 'dinput8.dll'
    $runtime[1] = 'Outlast2VR.exe'
    $loader = 'openxr_loader.dll'
}
foreach ($entry in $copies.GetEnumerator()) {
    Copy-Item -LiteralPath $entry.Key -Destination (Join-Path $stage $entry.Value)
}
foreach ($name in @(
    'outlast2_vr_p32.ini','outlast2_vr_p35.ini','outlast2_vr_p37.ini',
    'Install.ps1','INSTALL.md','README.md','RELEASE_NOTES.md','DEVELOPMENT.md',
    'GITHUB_SETUP.md','CONTRIBUTING.md','LICENSE','THIRD_PARTY_NOTICES.md',
    'OPENXR_LICENSE.md'
)) {
    Copy-Item -LiteralPath (Join-Path $repo $name) -Destination (Join-Path $stage $name)
}

$lines = Get-ChildItem -LiteralPath $stage -File | Sort-Object Name | ForEach-Object {
    '{0}  {1}' -f (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash,$_.Name
}
Set-Content -LiteralPath (Join-Path $stage 'SHA256SUMS.txt') -Value $lines -Encoding ascii
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $archive -CompressionLevel Optimal
$archiveHash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash
Set-Content -LiteralPath $archiveHashFile -Value ($archiveHash + '  ' + (Split-Path -Leaf $archive)) -Encoding ascii

Write-Output "Release ZIP: $archive"
Write-Output "SHA-256: $archiveHash"
Write-Output "Checksum file: $archiveHashFile"
