[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$Root,
    [Parameter(Mandatory=$true)][string]$Package,
    [string]$ReportPath
)
$ErrorActionPreference='Stop'
$rootPath=[IO.Path]::GetFullPath($Root.Trim('"'))
$packagePath=[IO.Path]::GetFullPath($Package.Trim('"'))
if (-not $ReportPath) { $ReportPath=Join-Path $packagePath 'BINARY_VALIDATION.TXT' }
$report=New-Object System.Collections.Generic.List[string]
$failures=0
$exports=@('auxGetDevCapsA','auxGetNumDevs','auxGetVolume','auxSetVolume','joyGetDevCapsA','joyGetNumDevs','joyGetPos','joyGetPosEx','mciSendCommandA','timeGetTime')
function Record([string]$state,[string]$message){$script:report.Add(('{0,-5} {1}' -f $state,$message));if($state-eq'FAIL'){$script:failures++}}
function U16([byte[]]$b,[int]$o){[BitConverter]::ToUInt16($b,$o)}
function U32([byte[]]$b,[int]$o){[BitConverter]::ToUInt32($b,$o)}
$wdump='C:\WATCOM\binnt64\wdump.exe';if(-not(Test-Path $wdump)){$wdump='C:\WATCOM\binnt\wdump.exe'}
$wdis='C:\WATCOM\binnt64\wdis.exe';if(-not(Test-Path $wdis)){$wdis='C:\WATCOM\binnt\wdis.exe'}
$objdump='C:\msys64\mingw32\bin\objdump.exe';if(-not(Test-Path $objdump)){$objdump='C:\msys64\usr\bin\objdump.exe'}
function Validate-Pe([string]$name,[int]$subsystem,[bool]$isDll,[string[]]$objects,[string]$mapName){
    $path=Join-Path $packagePath $name
    if(-not(Test-Path $path)){Record FAIL "$name missing";return}
    $b=[IO.File]::ReadAllBytes($path);if($b.Length-lt256-or$b[0]-ne0x4d-or$b[1]-ne0x5a){Record FAIL "$name invalid MZ";return}
    $pe=[int](U32 $b 0x3c);if([Text.Encoding]::ASCII.GetString($b,$pe,4)-ne"PE`0`0"){Record FAIL "$name invalid PE";return}
    $opt=$pe+24;$machine=U16 $b ($pe+4);$chars=U16 $b ($pe+22)
    $ok=$machine-eq0x14c-and(U16 $b ($opt+40))-eq4-and(U16 $b ($opt+42))-eq0-and(U16 $b ($opt+48))-eq4-and(U16 $b ($opt+50))-eq0-and(U16 $b ($opt+68))-eq$subsystem-and((($chars-band0x2000)-ne0)-eq$isDll)
    Record $([string]$(if($ok){'PASS'}else{'FAIL'})) "$name PE i386, OS/subsystem 4.0, subsystem $subsystem, DLL=$isDll"
    Record INFO "$name SHA-256 $((Get-FileHash $path -Algorithm SHA256).Hash)"
    if(-not(Test-Path $wdump)){Record FAIL 'Open Watcom wdump missing';return}
    $dump=(& $wdump -p $path 2>&1|Out-String)
    $dlls=@([regex]::Matches($dump,'DLL name = <([^>]+)>')|ForEach-Object{$_.Groups[1].Value.ToUpperInvariant()}|Sort-Object -Unique)
    $unexpected=@($dlls|Where-Object{$_-notin@('KERNEL32.DLL','USER32.DLL')})
    Record $([string]$(if($unexpected.Count-eq0){'PASS'}else{'FAIL'})) "$name Win95 imports: $($dlls-join', ')"
    foreach($bad in @('MSVCRT.DLL','MINGW','VORBIS','InterlockedCompareExchange','SetFilePointerEx','GetFileSizeEx','InitializeCriticalSectionAndSpinCount')){if($dump-match[regex]::Escape($bad)){Record FAIL "$name forbidden import/reference $bad"}}
    if($isDll){
        $exportDump=($dump-split'Import Directory Table')[0]
        $named=@([regex]::Matches($exportDump,'(?m)^\s+\d+\s+[0-9A-Fa-f]+\s+([A-Za-z][A-Za-z0-9]+)\s*$')|ForEach-Object{$_.Groups[1].Value})
        Record $([string]$(if($named.Count-eq10-and@($named|Where-Object{$_-in$exports}).Count-eq10){'PASS'}else{'FAIL'})) "$name exact ten undecorated exports"
    }
    if(-not(Test-Path $objdump)-or-not(Test-Path $wdis)){Record FAIL "$name ISA tools missing";return}
    $args=@((Join-Path $rootPath 'tools\check_isa.py'),'--pe',$path,'--map',(Join-Path $rootPath "build\$mapName"),'--objdump',$objdump,'--wdis',$wdis)
    foreach($obj in $objects){$args+=@('--object',(Join-Path $rootPath "build\$obj"))}
    & python @args | ForEach-Object { Record INFO "$name $_" }
    Record $([string]$(if($LASTEXITCODE-eq0){'PASS'}else{'FAIL'})) "$name Pentium/MMX ISA gate"
}
Record INFO 'Open Watcom 2.0, -5s, native runtime, PE 4.0'
Validate-Pe 'wincd_pcm_debug.dll' 2 $true @('runtime_debug.obj','ds_debug.obj') 'wincd_pcm_debug.map'
Validate-Pe 'wincd_pcm.dll' 2 $true @('runtime_quiet.obj','ds_quiet.obj') 'wincd_pcm.map'
Validate-Pe 'DISC_TEST.EXE' 3 $false @('disc_test.obj') 'disc_test.map'
if($failures-eq0){Record PASS 'ALL BINARY VALIDATION GATES PASSED'}else{Record FAIL "$failures gate(s) failed"}
[IO.File]::WriteAllText($ReportPath,(($report-join"`r`n")+"`r`n"),[Text.Encoding]::ASCII)
$report|ForEach-Object{Write-Host $_}
if($failures-ne0){exit 1}
