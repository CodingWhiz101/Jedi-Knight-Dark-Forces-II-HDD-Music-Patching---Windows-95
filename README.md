NOTE: Most of these tools were vibe-coded; I am not a programmer, simply a gamer that enjoys getting old games working on old hardware. For those of you who do not have disc version of this game, this patch is intended to get your GOG data (particularly the in-game music that is expected from a physical disc) fully working from your old PC's HDD.


# Play Jedi Knight on Windows 95 without the game CDs

This toolkit converts the GOG version of *Star Wars Jedi Knight: Dark Forces II* into a folder you can copy to a real Windows 95 computer.

It is intended for people whose original CD drive or game CDs are unavailable. It makes the GOG soundtrack work as continuous in-game music using the computer's hard drive.

You do **not** need to know how to program. You do **not** need to compile anything.

## Before you begin

You will need:

1. A Windows 10 or Windows 11 computer on which the GOG version of Jedi Knight is already installed.
2. Your own legally purchased GOG copy of the game.
3. Python 3 installed on the modern computer.
4. FFmpeg downloaded or installed on the modern computer.
5. About 1 GB of free space for the converted game.
6. A way to transfer an approximately 870 MB folder to the Windows 95 computer, such as a compatible USB drive.

The conversion does not alter or delete your GOG installation. It creates a separate Windows 95 version in a new folder.

## What are Python and FFmpeg?

Python runs the conversion helper included with this toolkit. FFmpeg converts the GOG `.ogg` music files into simple `.wav` files that Windows 95 can play efficiently.

Both are used only on the modern computer. Neither one is copied to Windows 95.

### Installing Python

Download the current Python 3 Windows installer from the official Python website. When the installer opens, enable the option labeled **Add Python to PATH**, then complete the installation.

To check it:

1. Open the Start menu.
2. Type `PowerShell` and open Windows PowerShell.
3. Type the following and press Enter:

```powershell
python --version
```

If it prints a Python version number, Python is ready.

### Finding FFmpeg

Download a Windows build of FFmpeg and extract it. Inside the extracted folder, find:

```text
bin\ffmpeg.exe
```

Remember where that file is. You can pass its full location to the conversion command, so you do not need to understand or modify the Windows `PATH` setting.

## Step 1: Download this toolkit

On the GitHub page, open **Releases**, download `jkdf2-win95-1.0.0.zip`, and extract it to an ordinary folder such as:

```text
C:\Users\YourName\Desktop\jkdf2-win95-1.0.0
```

Do not run the conversion from inside the ZIP file. Extract it first.

## Step 2: Find your GOG game folder

The folder must contain `JK.EXE` and a `MUSIC` folder. Common locations include:

```text
C:\GOG Games\Star Wars Jedi Knight - Dark Forces 2
```

or:

```text
C:\Program Files (x86)\GOG Galaxy\Games\Star Wars Jedi Knight - Dark Forces 2
```

If you are unsure, find Jedi Knight in GOG Galaxy, open its installation or management menu, and choose the option that shows the installation folder.

Open that folder in File Explorer and confirm that you can see both `JK.EXE` and the `MUSIC` directory.

## Step 3: Choose where the converted game will go

Choose a new folder that does not already contain files. For example:

```text
C:\Users\YourName\Desktop\DARKF2_95
```

The toolkit deliberately refuses to overwrite a nonempty folder.

## Step 4: Open PowerShell in the toolkit folder

1. Open the extracted toolkit folder in File Explorer.
2. Click the address bar at the top of the window.
3. Type `powershell` and press Enter.

A blue PowerShell window should open in the correct folder.

## Step 5: Run the conversion

Copy the command below into PowerShell, but replace the three example paths:

- Replace the path after `-Source` with your GOG game folder.
- Replace the path after `-Output` with the new folder you chose.
- Replace the path after `-Ffmpeg` with the actual location of `ffmpeg.exe`.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\Prepare-Win95Install.ps1" -Source "C:\GOG Games\Star Wars Jedi Knight - Dark Forces 2" -Output "C:\Users\YourName\Desktop\DARKF2_95" -Ffmpeg "C:\Users\YourName\Downloads\ffmpeg\bin\ffmpeg.exe"
```

Keep the quotation marks around every path. They are important when folder names contain spaces.

The conversion normally takes a few minutes. It will:

1. Check that your GOG version is supported.
2. Check all 18 soundtrack files.
3. Copy the original Windows-era game files.
4. Save an untouched copy of the original executable as `JK.EXE.bak`.
5. Make the two-byte compatibility patch to a copy of `JK.EXE`.
6. Convert the music to Windows 95-friendly PCM files.
7. Install the tested music replacement.
8. Validate the completed folder twice.

Do not close PowerShell while it is working.

Success looks like this:

```text
PASS  COMPLETE WIN95 INSTALL VALIDATION PASSED
Win95 installation prepared successfully: C:\...\DARKF2_95
```

If you see `FAIL` or `ERROR`, do not copy a partial folder to Windows 95. Read [Common problems](#common-problems) below.

## Step 6: Copy the game to Windows 95

Copy the entire generated `DARKF2_95` folder to your transfer drive. Do not copy only the WAV files or DLLs.

On the Windows 95 computer, copy the folder to a short path without spaces. The tested example is:

```text
D:\GAMES\DARKF2
```

Your drive letter may be different. Use the actual drive containing the game.

The final Windows 95 folder should contain at least:

```text
JK.EXE
JK.EXE.bak
wincd.dll
wincd_pcm.dll
wincd_pcm_debug.dll
DISC_TEST.EXE
MUSIC_PCM\
Episode\
Resource\
```

## Step 7: Set up and test the game on Windows 95

### Write the game location to the registry

1. Click **Start**, then **Run**.
2. Type `command` and press Enter.
3. At the command prompt, switch to the game's drive if necessary. For drive D, type:

```text
D:
```

4. Change to the game directory:

```text
CD \GAMES\DARKF2
```

5. Run the registry helper using the complete path:

```text
SETJEDIREG.BAT D:\GAMES\DARKF2
```

It should report that the registry setup completed.

### Check the files

From File Explorer, double-click:

```text
VERIFY95.BAT
```

It should say that the basic file-layout checks passed.

### Test the music mapping

Double-click:

```text
DISC_TEST.EXE
```

This takes about 45 seconds and plays short samples from both retail disc mappings. It should finish with:

```text
PASSED: 0 failure(s).
```

### Start the game

Double-click `JK.EXE`. Begin a game and confirm that music and sound effects play together.

The quiet, performance-oriented music DLL is already selected. You do not need to run another selector during ordinary use.

## Optional features

### Copy your existing GOG profiles and saves

Add `-IncludePlayerData` to the end of the conversion command. Without it, the toolkit creates a clean player directory and does not copy profiles or saves.

### Reuse music that was already converted

Advanced users preparing another installation can add:

```text
-PcmCache "C:\path\to\MUSIC_PCM"
```

The cache is accepted only if every source and WAV hash matches. Normal first-time users should ignore this option.

### Use full-rate 44.1 kHz audio

The tested default is 22.05 kHz because it sounds good and reduces work for the Pentium. If different audio hardware rejects that format, add:

```text
-Rate 44100
```

Do not use this unless the default actually fails on your hardware.

## Debugging a Windows 95 problem

For normal play, keep the quiet DLL selected.

If music fails or behaves incorrectly:

1. Double-click `SELECT_DEBUG.BAT`.
2. Run the game and reproduce the problem once.
3. Exit the game normally.
4. Double-click `COLLECT_LOGS.BAT`.
5. Copy the resulting `PCMLOGS` directory back to the modern computer.
6. Double-click `RESTORE_QUIET.BAT` before normal play.

The debug build can produce `pcmcd_debug.log`. The quiet build creates `pcmcd_error.log` only after a fatal file or audio error.

## Common problems

### “Running scripts is disabled on this system”

Use the complete command shown above, including:

```text
powershell -NoProfile -ExecutionPolicy Bypass -File
```

This bypass applies only to that PowerShell process; it does not permanently change the computer's execution policy.

### “Python was not found” or “python is not recognized”

Install Python 3 and enable **Add Python to PATH** during setup. Close PowerShell, reopen it, and run `python --version` again.

### “FFmpeg was not found”

Check that the path after `-Ffmpeg` ends with the real `ffmpeg.exe` file, usually inside an extracted `bin` folder.

### “Unsupported JK.EXE”

Version 1 intentionally supports only the tested GOG executable. Its SHA-256 is:

```text
12E8C8C3C078E7538733175F93DAFA62E309973E814D8ECB975F04C3A57EFE44
```

Do not bypass this check. Open a GitHub issue and include where your copy came from and the SHA-256 reported for it. Steam, original-CD, modified, and other regional executables are not automatically interchangeable.

### “Output directory is not empty”

Choose a new folder name or move the existing folder elsewhere. The toolkit will not merge files into an existing installation.

### The game starts but has no music

Run `VERIFY95.BAT`. Confirm that:

- The whole `MUSIC_PCM` folder was copied.
- `wincd.dll` is present beside `JK.EXE`.
- GOG's `winmm.dll` and Vorbis DLLs were not copied into the Windows 95 folder.
- The music volume inside Jedi Knight is not set to zero.

Then use the debug procedure above.

### The registry helper cannot find JK.EXE

Use the exact Windows 95 path and do not add a trailing backslash. For example:

```text
SETJEDIREG.BAT D:\GAMES\DARKF2
```

## Safety and undoing the conversion

The toolkit never modifies the GOG source folder. Your stock executable is also preserved inside the generated folder as `JK.EXE.bak`.

To undo everything on the modern computer, delete only the generated output folder. Do not delete your original GOG installation.

On Windows 95, `RESTORE_QUIET.BAT` restores the approved production music DLL after diagnostic testing. It does not restore the old GOG OGG proxy, because that proxy is not suitable for the tested Windows 95/Pentium hardware.

## Technical summary

The supported GOG executable normally imports a local OGG-aware `winmm.dll`. Windows 95 handles the system WinMM library specially, so the toolkit changes that one import name to `WINCD.dll`. The patched executable differs from stock at only two bytes.

The replacement DLL uses predecoded stereo 16-bit PCM through classic DirectSound. It models the two retail soundtrack discs, keeps music running while Jedi Knight delays its MCI status checks, and forwards unrelated timer, joystick, AUX, and WinMM operations to the real system library.

Logical tracks 12–18 represent retail disc 1 tracks 2–8. Logical tracks 22–32 represent retail disc 2 tracks 2–12. Tracks 19–21 are an intentional gap and must not exist.

The supplied binaries were built with Open Watcom 2.0 for Win32/Pentium using `-5s` and PE version 4.0. The quiet and debug DLLs passed the exact-export, Win95-import, Pentium-instruction, transport, soundtrack, and hardware tests. Rebuilding is unnecessary for normal use; maintainers can read [Building and releases](docs/BUILDING.md).

## Tested hardware

The final build was tested on:

- Windows 95 OSR2
- Intel Pentium MMX 233
- Approximately 128 MB RAM
- Soundscape audio hardware using classic DirectSound

Music and effects played together through Levels 1 and 2. The accepted debug run recorded seamless looping and no DirectSound errors, read failures, underruns, recoveries, or scheduling gaps.

Other Windows 95 hardware may behave differently. Please report the computer model, processor, sound card, Windows version, and exact error message when opening an issue.

## Legal note

This repository contains original compatibility code and tools. It does not contain the game, soundtrack, FFmpeg, or GOG/LucasArts files. You must provide your own lawful GOG installation.

The original toolkit and shim code are available under the MIT License. See [Legal and provenance](docs/LEGAL.md) for details.
