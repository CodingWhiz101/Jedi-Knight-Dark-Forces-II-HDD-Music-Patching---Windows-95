@echo off
if not exist PCMLOGS mkdir PCMLOGS
if exist pcmcd_debug.log copy /y pcmcd_debug.log PCMLOGS\pcmcd_debug.log >nul
if exist pcmcd_error.log copy /y pcmcd_error.log PCMLOGS\pcmcd_error.log >nul
if exist WIN95_BUILD.TXT copy /y WIN95_BUILD.TXT PCMLOGS\WIN95_BUILD.TXT >nul
echo Current PCM/CDDA logs collected in PCMLOGS.
pause
