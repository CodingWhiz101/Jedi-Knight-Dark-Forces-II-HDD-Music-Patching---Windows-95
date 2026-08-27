[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$Path,
    [Parameter(Mandatory=$true)][string]$Source,
    [ValidateSet(22050,44100)][int]$ExpectedRate = 22050,
    [switch]$RequireQuiet
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $MyInvocation.MyCommand.Path
Import-Module (Join-Path $repo 'tools\Toolkit.psm1') -Force
$constants = Get-JkToolkitConstants
$failures = 0
function Result([bool]$Pass, [string]$Message) {
    if ($Pass) { Write-Host "PASS  $Message" }
    else { Write-Host "FAIL  $Message"; $script:failures++ }
}
function U16([byte[]]$Bytes,[int]$At) { [BitConverter]::ToUInt16($Bytes,$At) }
function U32([byte[]]$Bytes,[int]$At) { [BitConverter]::ToUInt32($Bytes,$At) }

try {
    $install = [IO.Path]::GetFullPath($Path.Trim('"'))
    $sourcePath = Assert-SupportedSource $Source
    Result (Test-Path -LiteralPath $install -PathType Container) "install directory exists"
    if (-not (Test-Path -LiteralPath $install -PathType Container)) { exit 1 }

    foreach ($name in @('JK.EXE','JK.EXE.bak','SMACKW32.DLL','wincd.dll','wincd_pcm.dll','wincd_pcm_debug.dll','DISC_TEST.EXE','WIN95_BUILD.TXT','MUSIC_PCM','Controls','Episode','Resource','player')) {
        Result (Test-Path -LiteralPath (Join-Path $install $name)) "$name present"
    }

    Result ((Get-Sha256 (Join-Path $install 'JK.EXE.bak')) -eq $constants.SupportedJkSha256) 'JK.EXE.bak is the supported stock executable'
    Result ((Get-Sha256 (Join-Path $install 'JK.EXE')) -eq $constants.PatchedJkSha256) 'JK.EXE is the approved WINCD-patched executable'
    $stockBytes = [IO.File]::ReadAllBytes((Join-Path $install 'JK.EXE.bak'))
    $patchedBytes = [IO.File]::ReadAllBytes((Join-Path $install 'JK.EXE'))
    $diffs = @()
    if ($stockBytes.Length -eq $patchedBytes.Length) {
        for ($i=0;$i -lt $stockBytes.Length;$i++) { if ($stockBytes[$i] -ne $patchedBytes[$i]) { $diffs += $i } }
    }
    Result (($diffs.Count -eq 2) -and ($diffs[0] -eq 0x151091) -and ($diffs[1] -eq 0x151092)) 'JK.EXE differs from backup at only the two approved import bytes'
    Result (@(Find-ByteSequence $patchedBytes ([Text.Encoding]::ASCII.GetBytes('WINCD.dll'))).Count -eq 1) 'JK.EXE imports WINCD.dll exactly once'

    Result ((Get-Sha256 (Join-Path $install 'wincd_pcm.dll')) -eq $constants.QuietDllSha256) 'quiet DLL hash approved'
    Result ((Get-Sha256 (Join-Path $install 'wincd_pcm_debug.dll')) -eq $constants.DebugDllSha256) 'debug DLL hash approved'
    Result ((Get-Sha256 (Join-Path $install 'DISC_TEST.EXE')) -eq $constants.DiscTestSha256) 'disc test hash approved'
    $activeHash = Get-Sha256 (Join-Path $install 'wincd.dll')
    $activeApproved = $activeHash -in @($constants.QuietDllSha256,$constants.DebugDllSha256)
    Result $activeApproved 'active wincd.dll is an approved PCM build'
    if ($RequireQuiet) { Result ($activeHash -eq $constants.QuietDllSha256) 'quiet DLL is active' }

    foreach ($dir in @('Controls','Episode','Resource')) {
        $sourceDigest = Get-DirectoryDigest (Join-Path $sourcePath $dir)
        $installDigest = Get-DirectoryDigest (Join-Path $install $dir)
        Result ($sourceDigest -eq $installDigest) "$dir copied byte-for-byte"
    }
    Result ((Get-Sha256 (Join-Path $install 'SMACKW32.DLL')) -eq (Get-Sha256 (Join-Path $sourcePath 'SMACKW32.DLL'))) 'SMACKW32.DLL copied byte-for-byte'

    $forbidden = @('winmm.dll','libogg-0.dll','libvorbis-0.dll','libvorbisfile-3.dll','GOGLauncher.exe','JKStart.exe','game.sdb','directplay.cmd','unins000.exe','MUSIC')
    foreach ($name in $forbidden) { Result (-not (Test-Path -LiteralPath (Join-Path $install $name))) "forbidden GOG item absent: $name" }
    $gogMetadata = @(Get-ChildItem -LiteralPath $install -Force | Where-Object { $_.Name -like 'goggame-*' -or $_.Name -eq 'goglog.ini' })
    Result ($gogMetadata.Count -eq 0) 'GOG launcher metadata absent'

    $buildValues = @{}
    foreach ($line in [IO.File]::ReadAllLines((Join-Path $install 'WIN95_BUILD.TXT'))) {
        if ($line -match '^([^=]+)=(.*)$') { $buildValues[$matches[1]] = $matches[2] }
    }
    Result ($buildValues['toolkit_version'] -eq $constants.Version) 'build manifest toolkit version'
    Result ([int]$buildValues['sample_rate'] -eq $ExpectedRate) 'build manifest sample rate'

    $music = Join-Path $install 'MUSIC_PCM'
    $musicManifest = Join-Path $music 'MANIFEST.TXT'
    Result (Test-Path -LiteralPath $musicManifest -PathType Leaf) 'PCM manifest present'
    Result ($buildValues['pcm_manifest_sha256'] -eq (Get-Sha256 $musicManifest)) 'build manifest identifies the PCM manifest'
    Result ($buildValues['ffmpeg_sha256'] -match '^[0-9A-F]{64}$') 'build manifest records FFmpeg SHA-256'
    Result ([bool]$buildValues['ffmpeg_version']) 'build manifest records FFmpeg version'
    $rows = @{}
    $header = $false
    if (Test-Path -LiteralPath $musicManifest -PathType Leaf) {
        foreach ($line in [IO.File]::ReadAllLines($musicManifest)) {
            if ($line.StartsWith("logical`t")) { $header=$true; continue }
            if ($header -and $line.Trim()) {
                $fields=$line.Split("`t")
                if ($fields.Count -eq 11) { $rows[[int]$fields[0]]=$fields }
            }
        }
    }
    Result ($rows.Count -eq 18) 'PCM manifest contains exactly 18 tracks'
    foreach ($track in $constants.Tracks) {
        $wave = Join-Path $music "Track$track.wav"
        if (-not (Test-Path -LiteralPath $wave -PathType Leaf)) { Result $false "Track$track.wav present"; continue }
        Result $true "Track$track.wav present"
        if (-not $rows.ContainsKey([int]$track)) { Result $false "Track$track manifest row"; continue }
        $row=$rows[[int]$track]
        $bytes=New-Object byte[] 44
        $stream=[IO.File]::OpenRead($wave)
        try { $read=$stream.Read($bytes,0,44); $length=$stream.Length } finally { $stream.Dispose() }
        $canonical = $read -eq 44 -and [Text.Encoding]::ASCII.GetString($bytes,0,4) -eq 'RIFF' -and [Text.Encoding]::ASCII.GetString($bytes,8,4) -eq 'WAVE' -and [Text.Encoding]::ASCII.GetString($bytes,12,4) -eq 'fmt ' -and (U16 $bytes 20) -eq 1 -and (U16 $bytes 22) -eq 2 -and (U32 $bytes 24) -eq $ExpectedRate -and (U16 $bytes 32) -eq 4 -and (U16 $bytes 34) -eq 16 -and [Text.Encoding]::ASCII.GetString($bytes,36,4) -eq 'data'
        Result $canonical "Track$track canonical PCM format"
        $dataBytes=[int64](U32 $bytes 40)
        Result (($dataBytes -eq [int64]$row[10]) -and ($length -eq 44+$dataBytes)) "Track$track frame-aligned data length"
        Result ((Get-Sha256 $wave) -eq $row[4].ToUpperInvariant()) "Track$track WAV hash matches manifest"
        Result ((Get-Sha256 (Join-Path $sourcePath "MUSIC\Track$track.ogg")) -eq $row[3].ToUpperInvariant()) "Track$track source hash matches manifest"
        Result ([int]$row[7] -eq $ExpectedRate) "Track$track manifest rate"
        $disc = if ($track -le 18) { 1 } else { 2 }
        $physical = if ($disc -eq 1) { $track - 10 } else { $track - 20 }
        Result (([int]$row[1] -eq $disc) -and ([int]$row[2] -eq $physical)) "Track$track retail mapping"
    }
    foreach ($gap in 19..21) { Result (-not (Test-Path -LiteralPath (Join-Path $music "Track$gap.wav"))) "invalid Track$gap.wav absent" }

    $includePlayer = [int]$buildValues['include_player_data']
    if ($includePlayer -eq 0) {
        $playerFiles = @(Get-ChildItem -LiteralPath (Join-Path $install 'player') -Recurse -File)
        Result (($playerFiles.Count -eq 1) -and ($playerFiles[0].Name -eq 'dummy.txt')) 'player data excluded by default'
    } else {
        Result ((Get-DirectoryDigest (Join-Path $install 'player')) -eq (Get-DirectoryDigest (Join-Path $sourcePath 'player'))) 'requested player data copied byte-for-byte'
    }

    if ($failures -eq 0) { Write-Host 'PASS  COMPLETE WIN95 INSTALL VALIDATION PASSED'; exit 0 }
    Write-Host "FAIL  $failures validation check(s) failed"; exit 1
} catch {
    Write-Host "FAIL  $($_.Exception.Message)"
    exit 1
}
