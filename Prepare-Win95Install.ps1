[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$Source,
    [Parameter(Mandatory=$true)][string]$Output,
    [ValidateSet(22050,44100)][int]$Rate = 22050,
    [string]$Ffmpeg,
    [string]$PcmCache,
    [switch]$IncludePlayerData
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $MyInvocation.MyCommand.Path
Import-Module (Join-Path $repo 'tools\Toolkit.psm1') -Force
$constants = Get-JkToolkitConstants
$sourcePath = Assert-SupportedSource $Source
& python (Join-Path $repo 'tools\convert_music.py') --source (Join-Path $sourcePath 'MUSIC') --validate-only
if ($LASTEXITCODE -ne 0) { throw "GOG soundtrack validation failed with exit code $LASTEXITCODE." }
$outputPath = [IO.Path]::GetFullPath($Output.Trim('"')).TrimEnd('\')
$outputParent = Split-Path -Parent $outputPath
if (-not $outputParent) { throw 'Output must have a parent directory.' }
if (-not (Test-Path -LiteralPath $outputParent)) { New-Item -ItemType Directory -Path $outputParent | Out-Null }
if (Test-Path -LiteralPath $outputPath) {
    if (-not (Test-Path -LiteralPath $outputPath -PathType Container)) { throw "Output already exists and is not a directory: $outputPath" }
    if (@(Get-ChildItem -LiteralPath $outputPath -Force).Count -ne 0) { throw "Output directory is not empty: $outputPath" }
    Remove-Item -LiteralPath $outputPath
}

$pcmBytes = if ($Rate -eq 22050) { [int64]240984744 } else { [int64]481969488 }
$copyBytes = [int64]0
foreach ($name in @('Controls','Episode','Resource')) {
    $copyBytes += [int64](Get-ChildItem -LiteralPath (Join-Path $sourcePath $name) -Recurse -File | Measure-Object Length -Sum).Sum
}
$required = $copyBytes + $pcmBytes + 64MB
$drive = [IO.DriveInfo]::new([IO.Path]::GetPathRoot($outputParent))
if ($drive.AvailableFreeSpace -lt $required) {
    throw ('Insufficient free space. Need at least {0:N0} bytes; only {1:N0} are available.' -f $required,$drive.AvailableFreeSpace)
}

$stage = "$outputPath.partial-$PID-$([Guid]::NewGuid().ToString('N'))"
Assert-SafeStagePath $stage $outputParent
$promoted = $false
try {
    New-Item -ItemType Directory -Path $stage | Out-Null
    foreach ($name in @('Controls','Episode','Resource')) {
        Copy-Item -LiteralPath (Join-Path $sourcePath $name) -Destination $stage -Recurse
    }
    foreach ($name in @('SMACKW32.DLL','Jedi.doc','Jedi.txt','README.TXT','UPDATE.TXT')) {
        $item = Join-Path $sourcePath $name
        if (Test-Path -LiteralPath $item -PathType Leaf) { Copy-Item -LiteralPath $item -Destination $stage }
    }
    $playerOut = Join-Path $stage 'player'
    if ($IncludePlayerData -and (Test-Path -LiteralPath (Join-Path $sourcePath 'player'))) {
        Copy-Item -LiteralPath (Join-Path $sourcePath 'player') -Destination $stage -Recurse
    } else {
        New-Item -ItemType Directory -Path $playerOut | Out-Null
        [IO.File]::WriteAllBytes((Join-Path $playerOut 'dummy.txt'), [byte[]]::new(0))
    }

    Copy-Item -LiteralPath (Join-Path $sourcePath 'JK.EXE') -Destination (Join-Path $stage 'JK.EXE.bak')
    $patchOffset = Convert-JkExecutable (Join-Path $sourcePath 'JK.EXE') (Join-Path $stage 'JK.EXE')

    foreach ($name in @('wincd_pcm.dll','wincd_pcm_debug.dll','DISC_TEST.EXE')) {
        Copy-Item -LiteralPath (Join-Path $repo "bin\win95\$name") -Destination $stage
    }
    Copy-Item -LiteralPath (Join-Path $repo 'bin\win95\wincd_pcm.dll') -Destination (Join-Path $stage 'wincd.dll')
    Get-ChildItem -LiteralPath (Join-Path $repo 'payload') -File | Copy-Item -Destination $stage

    $musicOut = Join-Path $stage 'MUSIC_PCM'
    if ($PcmCache) {
        $cachePath = [IO.Path]::GetFullPath($PcmCache.Trim('"'))
        if (-not (Test-Path -LiteralPath (Join-Path $cachePath 'MANIFEST.TXT') -PathType Leaf)) { throw "PCM cache has no MANIFEST.TXT: $cachePath" }
        Copy-Item -LiteralPath $cachePath -Destination $musicOut -Recurse
        $musicMethod = 'validated-cache'
    } else {
        $ffmpegPath = Resolve-Ffmpeg $Ffmpeg
        & python (Join-Path $repo 'tools\convert_music.py') --source (Join-Path $sourcePath 'MUSIC') --output $musicOut --rate $Rate --ffmpeg $ffmpegPath
        if ($LASTEXITCODE -ne 0) { throw "Music conversion failed with exit code $LASTEXITCODE." }
        $musicMethod = 'ffmpeg-conversion'
    }

    $pcmManifestPath = Join-Path $musicOut 'MANIFEST.TXT'
    $pcmManifestSha = Get-Sha256 $pcmManifestPath
    $ffmpegVersion = ''
    $ffmpegSha = ''
    foreach ($line in [IO.File]::ReadAllLines($pcmManifestPath)) {
        if ($line.StartsWith("FFmpeg version`t")) { $ffmpegVersion = $line.Substring(15) }
        if ($line.StartsWith("FFmpeg SHA-256`t")) { $ffmpegSha = $line.Substring(15).ToUpperInvariant() }
    }
    if (-not $ffmpegVersion -or $ffmpegSha -notmatch '^[0-9A-F]{64}$') { throw 'PCM manifest lacks a valid FFmpeg identity.' }

    $manifestLines = @(
        'Jedi Knight Windows 95 build manifest',
        "toolkit_version=$($constants.Version)",
        "created_utc=$([DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ'))",
        "source_jk_sha256=$($constants.SupportedJkSha256)",
        "patched_jk_sha256=$($constants.PatchedJkSha256)",
        "patch_offset=0x$($patchOffset.ToString('X8'))",
        "sample_rate=$Rate",
        "music_method=$musicMethod",
        "pcm_manifest_sha256=$pcmManifestSha",
        "ffmpeg_version=$ffmpegVersion",
        "ffmpeg_sha256=$ffmpegSha",
        "include_player_data=$([int][bool]$IncludePlayerData)",
        "quiet_dll_sha256=$($constants.QuietDllSha256)",
        "debug_dll_sha256=$($constants.DebugDllSha256)",
        "disc_test_sha256=$($constants.DiscTestSha256)",
        'disc_1=logical 12-18; retail physical 2-8',
        'disc_2=logical 22-32; retail physical 2-12',
        'invalid_gap=19-21'
    )
    [IO.File]::WriteAllText((Join-Path $stage 'WIN95_BUILD.TXT'), (($manifestLines -join "`r`n") + "`r`n"), [Text.Encoding]::ASCII)

    & (Join-Path $repo 'Test-Win95Install.ps1') -Path $stage -Source $sourcePath -ExpectedRate $Rate -RequireQuiet
    if ($LASTEXITCODE -ne 0) { throw 'Staged installation validation failed.' }
    Move-Item -LiteralPath $stage -Destination $outputPath
    $promoted = $true
    & (Join-Path $repo 'Test-Win95Install.ps1') -Path $outputPath -Source $sourcePath -ExpectedRate $Rate -RequireQuiet
    if ($LASTEXITCODE -ne 0) { throw 'Promoted installation validation failed.' }
    Write-Host "Win95 installation prepared successfully: $outputPath"
} finally {
    if (-not $promoted -and (Test-Path -LiteralPath $stage)) {
        Assert-SafeStagePath $stage $outputParent
        Remove-Item -LiteralPath $stage -Recurse -Force
    }
}
