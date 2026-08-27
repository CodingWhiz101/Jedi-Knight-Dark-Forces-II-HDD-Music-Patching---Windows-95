[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$PcmCache)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $MyInvocation.MyCommand.Path
Import-Module (Join-Path $root 'tools\Toolkit.psm1') -Force
$constants=Get-JkToolkitConstants
& (Join-Path $root 'build.cmd')
if($LASTEXITCODE-ne0){throw 'Open Watcom build failed.'}
$out=Join-Path $root 'build\out'
& (Join-Path $root 'tools\Validate-Binaries.ps1') -Root $root -Package $out -ReportPath (Join-Path $out 'BINARY_VALIDATION.TXT')
if($LASTEXITCODE-ne0){throw 'Binary validation failed.'}
foreach($name in @('wincd_pcm.dll','wincd_pcm_debug.dll','DISC_TEST.EXE')){
    $rebuilt=[IO.File]::ReadAllBytes((Join-Path $out $name));$accepted=[IO.File]::ReadAllBytes((Join-Path $root "bin\win95\$name"))
    if($rebuilt.Length-ne$accepted.Length){throw "$name size differs from the hardware-approved binary."}
    # Open Watcom stamps the COFF TimeDateStamp at PE+8. Normalize only that
    # four-byte field; every other byte must remain identical.
    $pe=[BitConverter]::ToInt32($rebuilt,0x3c)
    for($i=0;$i-lt4;$i++){$rebuilt[$pe+8+$i]=0;$accepted[$pe+8+$i]=0}
    $sha=[Security.Cryptography.SHA256]::Create()
    try{$rebuiltHash=[Convert]::ToBase64String($sha.ComputeHash($rebuilt));$acceptedHash=[Convert]::ToBase64String($sha.ComputeHash($accepted))}finally{$sha.Dispose()}
    if($rebuiltHash-ne$acceptedHash){throw "$name code/data differs from the hardware-approved binary. Qualify it on Windows 95 before updating bin\win95."}
}
$smoke=Join-Path $root 'build\smoke'
if(Test-Path $smoke){Remove-Item -LiteralPath $smoke -Recurse -Force}
New-Item -ItemType Directory -Path $smoke|Out-Null
Copy-Item -LiteralPath (Join-Path $out 'wincd_pcm.dll'),(Join-Path $out 'wincd_pcm_debug.dll'),(Join-Path $out 'DISC_TEST.EXE') -Destination $smoke
Copy-Item -LiteralPath ([IO.Path]::GetFullPath($PcmCache.Trim('"'))) -Destination (Join-Path $smoke 'MUSIC_PCM') -Recurse
Push-Location $smoke
try{
    & .\DISC_TEST.EXE wincd_pcm_debug.dll /SELFTEST;if($LASTEXITCODE-ne0){throw 'Debug self-test failed.'}
    & .\DISC_TEST.EXE wincd_pcm.dll /SELFTEST;if($LASTEXITCODE-ne0){throw 'Quiet self-test failed.'}
    if(Test-Path '.\pcmcd_error.log'){throw 'Quiet self-test created pcmcd_error.log.'}
}finally{Pop-Location}
$releaseRoot=Join-Path $root 'release';if(Test-Path $releaseRoot){Remove-Item -LiteralPath $releaseRoot -Recurse -Force}
$stage=Join-Path $releaseRoot "jkdf2-win95-$($constants.Version)";New-Item -ItemType Directory -Path $stage|Out-Null
foreach($name in @('README.md','LICENSE','VERSION','Prepare-Win95Install.ps1','Test-Win95Install.ps1','build.cmd','Build-Release.ps1')){Copy-Item -LiteralPath (Join-Path $root $name) -Destination $stage}
foreach($dir in @('src','tools','payload','docs','bin','tests')){Copy-Item -LiteralPath (Join-Path $root $dir) -Destination $stage -Recurse}
$cacheDirs=@(Get-ChildItem -LiteralPath $stage -Recurse -Directory|Where-Object{$_.Name-eq'__pycache__'})
foreach($cacheDir in $cacheDirs){Remove-Item -LiteralPath $cacheDir.FullName -Recurse -Force}
$forbidden=@(Get-ChildItem -LiteralPath $stage -Recurse -File|Where-Object{$_.Extension-in@('.ogg','.wav','.gob','.smk','.jks','.plr','.pyc')-or$_.Name-in@('JK.EXE','JK.EXE.bak','ffmpeg.exe')})
if($forbidden.Count){throw "Release contains forbidden game/media files: $($forbidden.FullName-join', ')"}
$zip=Join-Path $releaseRoot "jkdf2-win95-$($constants.Version).zip"
Compress-Archive -LiteralPath $stage -DestinationPath $zip
$hash=(Get-FileHash $zip -Algorithm SHA256).Hash
[IO.File]::WriteAllText((Join-Path $releaseRoot 'SHA256SUMS.txt'),"$hash  $([IO.Path]::GetFileName($zip))`r`n",[Text.Encoding]::ASCII)
Write-Host "Release created: $zip"
