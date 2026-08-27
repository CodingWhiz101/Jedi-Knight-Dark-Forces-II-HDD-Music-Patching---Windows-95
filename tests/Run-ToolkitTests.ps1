[CmdletBinding()]
param([string]$Source,[string]$PcmCache)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Import-Module (Join-Path $root 'tools\Toolkit.psm1') -Force
$passed=0;$failed=0
function Check([bool]$condition,[string]$name){if($condition){Write-Host "PASS  $name";$script:passed++}else{Write-Host "FAIL  $name";$script:failed++}}
$one=[Text.Encoding]::ASCII.GetBytes('xxWINMM.dllyy')
Check (@(Find-ByteSequence $one ([Text.Encoding]::ASCII.GetBytes('WINMM.dll'))).Count-eq1) 'single byte-sequence match'
$two=[Text.Encoding]::ASCII.GetBytes('WINMM.dll-WINMM.dll')
Check (@(Find-ByteSequence $two ([Text.Encoding]::ASCII.GetBytes('WINMM.dll'))).Count-eq2) 'duplicate byte-sequence detection'
$rejected=$false;try{Assert-SafeStagePath 'C:\outside' 'C:\target'}catch{$rejected=$true};Check $rejected 'unsafe staging path rejected'
$rejected=$false;try{Resolve-Ffmpeg 'Z:\definitely-missing\ffmpeg.exe'}catch{$rejected=$true};Check $rejected 'missing explicit FFmpeg rejected'
if($Source){
    $supported=Assert-SupportedSource $Source;Check ([bool]$supported) 'supplied GOG source recognized'
    & python (Join-Path $root 'tools\convert_music.py') --source (Join-Path $supported 'MUSIC') --validate-only
    Check ($LASTEXITCODE-eq0) 'all source OGG files structurally valid'
    $temp=Join-Path ([IO.Path]::GetTempPath()) ("jkdf2-test-"+[Guid]::NewGuid().ToString('N')+'.exe')
    try{Copy-Item -LiteralPath (Join-Path $supported 'JK.EXE') -Destination $temp;$b=[IO.File]::ReadAllBytes($temp);$b[100]=$b[100]-bxor1;[IO.File]::WriteAllBytes($temp,$b);$rejected=$false;try{Convert-JkExecutable $temp ($temp+'.out')}catch{$rejected=$true};Check $rejected 'unknown executable hash rejected'}finally{Remove-Item -LiteralPath ($temp+'*') -Force -ErrorAction SilentlyContinue}
}
if($Source-and$PcmCache){
    $target=Join-Path ([IO.Path]::GetTempPath()) ("jkdf2-collision-"+[Guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Path $target|Out-Null;[IO.File]::WriteAllText((Join-Path $target 'keep.txt'),'keep')
    try{$rejected=$false;try{& (Join-Path $root 'Prepare-Win95Install.ps1') -Source $Source -Output $target -PcmCache $PcmCache}catch{$rejected=$true};Check ($rejected-and(Test-Path (Join-Path $target 'keep.txt'))) 'nonempty output rejected without mutation'}finally{Remove-Item -LiteralPath $target -Recurse -Force}
}
Write-Host "$passed passed, $failed failed";if($failed){exit 1}
