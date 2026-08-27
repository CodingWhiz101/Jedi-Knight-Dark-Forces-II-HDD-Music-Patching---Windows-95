#ifndef PCMCD_H
#define PCMCD_H

/* Open Watcom's native Win32 libraries use direct stdcall import stubs.
 * Function dllimport is optional; exports are declared in exports.lnk. */
#if defined(__WATCOMC__)
#define __declspec(x)
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>

#define PCMCD_MAGIC_DEVICE ((MCIDEVICEID)0xCDDA)
#define PCMCD_LOG_BUFFER 65536UL
#define PCMCD_TRACK_COUNT 18
#define PCMCD_MAX_SEGMENTS 18
#define PCMCD_CHANNELS 2
#define PCMCD_BITS 16
#define PCMCD_BLOCK_ALIGN 4

typedef struct TrackFile {
    BYTE disc;
    BYTE physical;
    BYTE logical;
    HANDLE handle;
    DWORD data_offset;
    DWORD data_bytes;
    DWORD frames;
} TrackFile;

typedef struct PcmSegment {
    BYTE track;
    DWORD first_frame;
    DWORD end_frame;
} PcmSegment;

typedef struct PlaybackPlan {
    PcmSegment segment[PCMCD_MAX_SEGMENTS];
    int count;
    DWORD total_frames;
    DWORD sample_rate;
    BYTE endpoint_track;
    DWORD endpoint_frame;
} PlaybackPlan;

typedef struct PlaybackProgram {
    PlaybackPlan initial;
    PlaybackPlan repeat;
    int retail_repeat;
} PlaybackProgram;

extern char g_dll_dir[MAX_PATH];
extern volatile LONG g_buffer_fills;
extern volatile LONG g_bytes_read;
extern volatile LONG g_underruns;
extern volatile LONG g_recoveries;
extern volatile LONG g_backend_errors;
extern volatile LONG g_sched_gaps;
extern volatile LONG g_initial_completions;
extern volatile LONG g_provisional_cycles;
extern volatile LONG g_adopted_plays;

void pc_log_debug(const char *fmt, ...);
void pc_log_error(const char *operation, DWORD code, const char *detail);
void pc_flush_log(void);

DWORD source_sample_rate(void);
int source_read(void *buffer, DWORD bytes);
int source_seek(int phase, DWORD relative_frame);
DWORD source_phase_frames(int phase);
void source_describe_position(int phase, DWORD relative_frame,
                              DWORD *logical, DWORD *offset);

int backend_play(HWND window);
void backend_stop(void);
void backend_close(void);
int backend_is_playing(void);
int backend_has_failed(void);
DWORD backend_played_frames(void);
int backend_playback_phase(void);
DWORD backend_completion_generation(void);
DWORD backend_repeat_cycles(void);
void backend_set_volume(DWORD volume);
void backend_log_caps(void);

#endif
