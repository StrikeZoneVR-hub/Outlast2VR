param(
    [Parameter(Mandatory=$true)][int]$Phase,
    [Parameter(Mandatory=$true)][string]$Name
)

$ErrorActionPreference='Stop'
$root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$id="p$Phase"
$slug=($Name.ToLowerInvariant() -replace '[^a-z0-9]+','_').Trim('_')

if(-not $slug){throw 'Invalid phase name.'}

$h=Join-Path $root ("src\{0}_{1}.h" -f $id,$slug)
$inl=Join-Path $root ("src\{0}_{1}.inl" -f $id,$slug)
$doc=Join-Path $root ("docs\phases\P{0}_{1}.md" -f $Phase,$slug)

foreach($p in @($h,$inl,$doc)){
    if(Test-Path $p){throw "Refusing to overwrite existing file: $p"}
}

New-Item -ItemType Directory -Path (Split-Path $doc -Parent) -Force|Out-Null

@"
#pragma once

// P$Phase - $Name
// Add declarations for the new phase here.

namespace outlast2vr::$id {
}
"@ | Set-Content -LiteralPath $h -Encoding UTF8

@"
// P$Phase - $Name
// Add experimental implementation here.
// Wire it into the existing runtime deliberately after reviewing the P44 path.

#include "$id`_$slug.h"

namespace outlast2vr::$id {
}
"@ | Set-Content -LiteralPath $inl -Encoding UTF8

@"
# P$Phase - $Name

## Goal

TODO

## Suggested build marker

OUTLAST2VR-P$Phase-$($slug.ToUpperInvariant())

## Test results

- [ ] Builds
- [ ] Existing tests pass
- [ ] Main menu works
- [ ] Gameplay loads
- [ ] Stereo works
- [ ] Head tracking works
- [ ] Camera/HUD checked
- [ ] Night vision checked
- [ ] Keyboard/mouse checked
- [ ] Gamepad checked
- [ ] Regressions documented
"@ | Set-Content -LiteralPath $doc -Encoding UTF8

Write-Host ''
Write-Host "Created P$Phase scaffold." -ForegroundColor Green
Write-Host $h
Write-Host $inl
Write-Host $doc