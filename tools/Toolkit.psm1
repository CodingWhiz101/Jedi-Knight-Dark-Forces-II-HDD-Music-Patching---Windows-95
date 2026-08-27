Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

$script:ToolkitVersion = '1.0.0'
$script:SupportedJkSha256 = '12E8C8C3C078E7538733175F93DAFA62E309973E814D8ECB975F04C3A57EFE44'
$script:PatchedJkSha256 = '6C00D0E541618F01E0AB2EB1BEAD7DA00A718EF445BE98536725E96F91588BCC'
$script:QuietDllSha256 = '799E72113FCA8794E347A84F6C35117E2EBA1C33C7DF797A809E48AAF1D15CDB'
$script:DebugDllSha256 = 'B2BB644709F98ED47A9AB24E7DC80FD51D9DA9812D94F0BF0C58AD1EB36001A6'
$script:DiscTestSha256 = 'C2D6DB45AA8E7E68F374324A23D22FD6FFE2061A02475F7C7C616F8964F755B5'
$script:Tracks = @(12,13,14,15,16,17,18,22,23,24,25,26,27,28,29,30,31,32)

function Get-JkToolkitConstants {
    [pscustomobject]@{
        Version = $script:ToolkitVersion
        SupportedJkSha256 = $script:SupportedJkSha256
        PatchedJkSha256 = $script:PatchedJkSha256
        QuietDllSha256 = $script:QuietDllSha256
        DebugDllSha256 = $script:DebugDllSha256
        DiscTestSha256 = $script:DiscTestSha256
        Tracks = @($script:Tracks)
    }
}

function Get-Sha256([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "File is missing: $Path" }
    (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToUpperInvariant()
}

function Get-DirectoryDigest([string]$Path) {
    $root = (Resolve-Path -LiteralPath $Path).Path
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        foreach ($file in Get-ChildItem -LiteralPath $root -Recurse -File | Sort-Object FullName) {
            $relative = $file.FullName.Substring($root.Length).TrimStart('\')
            $fileHash = (Get-Sha256 $file.FullName).ToLowerInvariant()
            $line = [Text.Encoding]::UTF8.GetBytes("$relative`t$($file.Length)`t$fileHash`n")
            [void]$sha.TransformBlock($line, 0, $line.Length, $line, 0)
        }
        [void]$sha.TransformFinalBlock([byte[]]::new(0), 0, 0)
        (($sha.Hash | ForEach-Object { $_.ToString('x2') }) -join '')
    } finally { $sha.Dispose() }
}

function Find-ByteSequence([byte[]]$Data, [byte[]]$Pattern) {
    $matches = New-Object System.Collections.Generic.List[int]
    for ($i = 0; $i -le $Data.Length - $Pattern.Length; $i++) {
        $same = $true
        for ($j = 0; $j -lt $Pattern.Length; $j++) {
            if ($Data[$i + $j] -ne $Pattern[$j]) { $same = $false; break }
        }
        if ($same) { $matches.Add($i) }
    }
    @($matches)
}

function Convert-JkExecutable([string]$Source, [string]$Destination) {
    if ((Get-Sha256 $Source) -ne $script:SupportedJkSha256) {
        throw "Unsupported JK.EXE. This v1 toolkit accepts SHA-256 $($script:SupportedJkSha256) only."
    }
    $bytes = [IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $Source))
    $old = [Text.Encoding]::ASCII.GetBytes('WINMM.dll')
    $new = [Text.Encoding]::ASCII.GetBytes('WINCD.dll')
    $matches = @(Find-ByteSequence $bytes $old)
    if ($matches.Count -ne 1) { throw "Expected exactly one WINMM.dll import string; found $($matches.Count)." }
    $offset = $matches[0]
    for ($i = 0; $i -lt $new.Length; $i++) { $bytes[$offset + $i] = $new[$i] }
    [IO.File]::WriteAllBytes($Destination, $bytes)
    if ((Get-Sha256 $Destination) -ne $script:PatchedJkSha256) {
        throw 'Patched JK.EXE hash is not the hardware-approved result.'
    }
    $offset
}

function Assert-SupportedSource([string]$Source) {
    $sourcePath = [IO.Path]::GetFullPath($Source.Trim('"'))
    if (-not (Test-Path -LiteralPath $sourcePath -PathType Container)) { throw "GOG source directory is missing: $sourcePath" }
    foreach ($item in @('JK.EXE','SMACKW32.DLL','Controls','Episode','Resource','MUSIC')) {
        if (-not (Test-Path -LiteralPath (Join-Path $sourcePath $item))) { throw "Required GOG item is missing: $item" }
    }
    if ((Get-Sha256 (Join-Path $sourcePath 'JK.EXE')) -ne $script:SupportedJkSha256) {
        throw "Unsupported JK.EXE. This v1 toolkit accepts SHA-256 $($script:SupportedJkSha256) only."
    }
    $actual = @(Get-ChildItem -LiteralPath (Join-Path $sourcePath 'MUSIC') -File -Filter '*.ogg' | ForEach-Object { $_.BaseName } | Sort-Object)
    $expected = @($script:Tracks | ForEach-Object { "Track$_" } | Sort-Object)
    if (($actual -join '|') -ne ($expected -join '|')) {
        throw "MUSIC must contain exactly the 18 supported GOG tracks: $($expected -join ', ')."
    }
    $sourcePath
}

function Resolve-Ffmpeg([string]$ExplicitPath) {
    if ($ExplicitPath) {
        $candidate = [IO.Path]::GetFullPath($ExplicitPath.Trim('"'))
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { return $candidate }
        throw "FFmpeg was not found at: $candidate"
    }
    if ($env:PCMCD_FFMPEG) {
        $candidate = [IO.Path]::GetFullPath($env:PCMCD_FFMPEG.Trim('"'))
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { return $candidate }
        throw "PCMCD_FFMPEG points to a missing file: $candidate"
    }
    $command = Get-Command ffmpeg.exe -ErrorAction SilentlyContinue
    if (-not $command) { $command = Get-Command ffmpeg -ErrorAction SilentlyContinue }
    if ($command) { return $command.Source }
    throw 'FFmpeg was not found. Supply -Ffmpeg, set PCMCD_FFMPEG, or add ffmpeg.exe to PATH. It is never downloaded automatically.'
}

function Assert-SafeStagePath([string]$Stage, [string]$Parent) {
    $stagePath = [IO.Path]::GetFullPath($Stage)
    $parentPath = [IO.Path]::GetFullPath($Parent).TrimEnd('\') + '\'
    if (-not $stagePath.StartsWith($parentPath, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Unsafe staging path outside output parent: $stagePath"
    }
    if ([IO.Path]::GetFileName($stagePath) -notmatch '\.partial-[0-9]+-[0-9a-f]+$') {
        throw "Unsafe staging path name: $stagePath"
    }
}

Export-ModuleMember -Function Get-JkToolkitConstants,Get-Sha256,Get-DirectoryDigest,Find-ByteSequence,Convert-JkExecutable,Assert-SupportedSource,Resolve-Ffmpeg,Assert-SafeStagePath
