#include <stdarg.h>
#include "pcmcd.h"

typedef UINT (WINAPI *PFN_AUXGETNUMDEVS)(void);
typedef MMRESULT (WINAPI *PFN_AUXGETDEVCAPSA)(UINT_PTR, LPAUXCAPSA, UINT);
typedef MMRESULT (WINAPI *PFN_AUXGETVOLUME)(UINT, LPDWORD);
typedef MMRESULT (WINAPI *PFN_AUXSETVOLUME)(UINT, DWORD);
typedef MCIERROR (WINAPI *PFN_MCISENDCOMMANDA)(MCIDEVICEID, UINT, DWORD, DWORD_PTR);
typedef UINT (WINAPI *PFN_JOYGETNUMDEVS)(void);
typedef MMRESULT (WINAPI *PFN_JOYGETDEVCAPSA)(UINT_PTR, LPJOYCAPSA, UINT);
typedef MMRESULT (WINAPI *PFN_JOYGETPOS)(UINT, LPJOYINFO);
typedef MMRESULT (WINAPI *PFN_JOYGETPOSEX)(UINT, LPJOYINFOEX);
typedef DWORD (WINAPI *PFN_TIMEGETTIME)(void);
typedef UINT (WINAPI *PFN_WAVEOUTGETNUMDEVS)(void);
typedef MMRESULT (WINAPI *PFN_WAVEOUTGETDEVCAPSA)(UINT_PTR, LPWAVEOUTCAPSA, UINT);

typedef struct TrackMap {
    BYTE disc;
    BYTE physical;
    BYTE logical;
} TrackMap;

typedef struct RetailLoopMap {
    BYTE first;
    BYTE last;
    BYTE repeat_first;
    BYTE repeat_last;
} RetailLoopMap;

static const TrackMap g_map[PCMCD_TRACK_COUNT] = {
    {1,2,12},{1,3,13},{1,4,14},{1,5,15},{1,6,16},{1,7,17},{1,8,18},
    {2,2,22},{2,3,23},{2,4,24},{2,5,25},{2,6,26},{2,7,27},
    {2,8,28},{2,9,29},{2,10,30},{2,11,31},{2,12,32}
};

/* Full-track soundtrack tuples extracted from the retail JK1.GOB.  The MCI
 * TO value is the exclusive boundary after "last". */
static const RetailLoopMap g_retail_loop[] = {
    {12,13,12,13},{13,13,12,13},{14,15,14,15},{15,15,14,15},
    {17,17,16,17},{18,18,18,18},{23,24,22,24},{24,24,22,24},
    {25,27,25,27},{26,27,25,27},{27,27,25,27},{28,28,28,28},
    {29,29,29,29},{30,30,30,30},{31,31,31,31},{32,32,32,32}
};

#define TRANSPORT_STOPPED              0
#define TRANSPORT_PLAYING              1
#define TRANSPORT_COMPLETE_PROVISIONAL 2
#define TRANSPORT_FAILED               3

char g_dll_dir[MAX_PATH];
volatile LONG g_buffer_fills;
volatile LONG g_bytes_read;
volatile LONG g_underruns;
volatile LONG g_recoveries;
volatile LONG g_backend_errors;
volatile LONG g_sched_gaps;
volatile LONG g_initial_completions;
volatile LONG g_provisional_cycles;
volatile LONG g_adopted_plays;

static HINSTANCE g_instance;
static volatile LONG g_core_state;
static HMODULE g_real_winmm;
static UINT g_real_aux_count;
static PFN_AUXGETNUMDEVS p_auxGetNumDevs;
static PFN_AUXGETDEVCAPSA p_auxGetDevCapsA;
static PFN_AUXGETVOLUME p_auxGetVolume;
static PFN_AUXSETVOLUME p_auxSetVolume;
static PFN_MCISENDCOMMANDA p_mciSendCommandA;
static PFN_JOYGETNUMDEVS p_joyGetNumDevs;
static PFN_JOYGETDEVCAPSA p_joyGetDevCapsA;
static PFN_JOYGETPOS p_joyGetPos;
static PFN_JOYGETPOSEX p_joyGetPosEx;
static PFN_TIMEGETTIME p_timeGetTime;
static PFN_WAVEOUTGETNUMDEVS p_waveOutGetNumDevs;
static PFN_WAVEOUTGETDEVCAPSA p_waveOutGetDevCapsA;

static CRITICAL_SECTION g_log_lock;
static HANDLE g_log = INVALID_HANDLE_VALUE;
static char g_log_buffer[PCMCD_LOG_BUFFER];
static DWORD g_log_used;
static DWORD g_start_tick;
static volatile LONG g_last_summary;
static volatile LONG g_time_calls;
static volatile LONG g_joy_calls;
static volatile LONG g_status_calls;
static volatile LONG g_play_calls;
static volatile LONG g_stop_calls;

static TrackFile g_track[PCMCD_TRACK_COUNT];
static DWORD g_sample_rate;
static int g_sources_open;
static PlaybackProgram g_program;
static PlaybackPlan g_transport_plan;
static int g_source_phase;
static int g_source_segment;
static DWORD g_source_frame;

static int g_mci_open;
static DWORD g_time_format = MCI_FORMAT_MSF;
static DWORD g_aux_volume = 0xFFFFFFFFUL;
static volatile LONG g_transport_state;
static DWORD g_completion_base;
static int g_endpoint_latched;
static int g_current_track = 12;
static HWND g_game_window;

static DWORD read_u32(const BYTE *p)
{
    DWORD value;
    CopyMemory(&value, p, sizeof(value));
    return value;
}

static WORD read_u16(const BYTE *p)
{
    WORD value;
    CopyMemory(&value, p, sizeof(value));
    return value;
}

static TrackFile *track_file(int logical)
{
    int i;
    for (i = 0; i < PCMCD_TRACK_COUNT; ++i)
        if ((int)g_track[i].logical == logical) return &g_track[i];
    return NULL;
}

static const TrackMap *track_map(int logical)
{
    int i;
    for (i = 0; i < PCMCD_TRACK_COUNT; ++i)
        if ((int)g_map[i].logical == logical) return &g_map[i];
    return NULL;
}

static void module_directory(void)
{
    char *p;
    DWORD n = GetModuleFileNameA(g_instance, g_dll_dir, MAX_PATH);
    if (!n || n >= MAX_PATH) {
        lstrcpyA(g_dll_dir, ".");
        return;
    }
    p = g_dll_dir + lstrlenA(g_dll_dir);
    while (p > g_dll_dir && p[-1] != '\\') --p;
    if (p > g_dll_dir) p[-1] = 0;
    else lstrcpyA(g_dll_dir, ".");
}

static FARPROC real_proc(const char *name)
{
    return g_real_winmm ? GetProcAddress(g_real_winmm, name) : NULL;
}

static void log_flush_locked(void)
{
    DWORD wrote;
    if (g_log == INVALID_HANDLE_VALUE || !g_log_used) return;
    WriteFile(g_log, g_log_buffer, g_log_used, &wrote, NULL);
    g_log_used = 0;
}

static void open_log(const char *name, DWORD disposition)
{
    char path[MAX_PATH];
    if (g_log != INVALID_HANDLE_VALUE) return;
    lstrcpyA(path, g_dll_dir);
    lstrcatA(path, "\\");
    lstrcatA(path, name);
    g_log = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                        disposition, FILE_ATTRIBUTE_NORMAL, NULL);
    if (g_log != INVALID_HANDLE_VALUE && disposition == OPEN_ALWAYS)
        SetFilePointer(g_log, 0, NULL, FILE_END);
}

static void core_init_inner(void)
{
    char path[MAX_PATH];
    InitializeCriticalSection(&g_log_lock);
    g_start_tick = GetTickCount();
    g_last_summary = (LONG)g_start_tick;
    module_directory();
#ifdef PCMCD_DEBUG
    open_log("pcmcd_debug.log", CREATE_ALWAYS);
#endif
    if (GetSystemDirectoryA(path, MAX_PATH)) {
        lstrcatA(path, "\\WINMM.DLL");
        g_real_winmm = LoadLibraryA(path);
    }
    p_auxGetNumDevs = (PFN_AUXGETNUMDEVS)real_proc("auxGetNumDevs");
    p_auxGetDevCapsA = (PFN_AUXGETDEVCAPSA)real_proc("auxGetDevCapsA");
    p_auxGetVolume = (PFN_AUXGETVOLUME)real_proc("auxGetVolume");
    p_auxSetVolume = (PFN_AUXSETVOLUME)real_proc("auxSetVolume");
    p_mciSendCommandA = (PFN_MCISENDCOMMANDA)real_proc("mciSendCommandA");
    p_joyGetNumDevs = (PFN_JOYGETNUMDEVS)real_proc("joyGetNumDevs");
    p_joyGetDevCapsA = (PFN_JOYGETDEVCAPSA)real_proc("joyGetDevCapsA");
    p_joyGetPos = (PFN_JOYGETPOS)real_proc("joyGetPos");
    p_joyGetPosEx = (PFN_JOYGETPOSEX)real_proc("joyGetPosEx");
    p_timeGetTime = (PFN_TIMEGETTIME)real_proc("timeGetTime");
    p_waveOutGetNumDevs = (PFN_WAVEOUTGETNUMDEVS)real_proc("waveOutGetNumDevs");
    p_waveOutGetDevCapsA = (PFN_WAVEOUTGETDEVCAPSA)real_proc("waveOutGetDevCapsA");
    g_real_aux_count = p_auxGetNumDevs ? p_auxGetNumDevs() : 0;
}

static void ensure_core(void)
{
    LONG prior;
    if (g_core_state == 2) return;
    prior = InterlockedExchange(&g_core_state, 1);
    if (prior == 0) {
        core_init_inner();
        InterlockedExchange(&g_core_state, 2);
#ifdef PCMCD_DEBUG
        pc_log_debug("HEADER build=pcmcd-continuous-2 mode=debug dll_dir=%s real_winmm=%08lX real_aux=%u\r\n",
                     g_dll_dir, (DWORD)g_real_winmm, g_real_aux_count);
#endif
    } else {
        while (g_core_state != 2) Sleep(0);
    }
}

void pc_flush_log(void)
{
    if (g_core_state != 2) return;
    EnterCriticalSection(&g_log_lock);
    log_flush_locked();
    LeaveCriticalSection(&g_log_lock);
}

static void append_log_line(const char *text)
{
    char line[896];
    int n = wsprintfA(line, "%08lu tid=%lu %s", GetTickCount() - g_start_tick,
                      GetCurrentThreadId(), text);
    if (n <= 0) return;
    EnterCriticalSection(&g_log_lock);
    if ((DWORD)n > PCMCD_LOG_BUFFER - g_log_used) log_flush_locked();
    if ((DWORD)n <= PCMCD_LOG_BUFFER) {
        CopyMemory(g_log_buffer + g_log_used, line, n);
        g_log_used += n;
    }
    LeaveCriticalSection(&g_log_lock);
}

void pc_log_debug(const char *fmt, ...)
{
#ifdef PCMCD_DEBUG
    char text[768];
    va_list args;
    ensure_core();
    va_start(args, fmt);
    wvsprintfA(text, fmt, args);
    va_end(args);
    append_log_line(text);
#else
    (void)fmt;
#endif
}

void pc_log_error(const char *operation, DWORD code, const char *detail)
{
    char text[768];
    InterlockedIncrement(&g_backend_errors);
    ensure_core();
    EnterCriticalSection(&g_log_lock);
#ifndef PCMCD_DEBUG
    open_log("pcmcd_error.log", OPEN_ALWAYS);
#endif
    LeaveCriticalSection(&g_log_lock);
    wsprintfA(text, "FATAL operation=%s code=%08lX detail=%s\r\n",
              operation ? operation : "unknown", code, detail ? detail : "-");
    append_log_line(text);
    pc_flush_log();
}

static void maybe_summary(DWORD now)
{
#ifdef PCMCD_DEBUG
    LONG previous = g_last_summary;
    LONG elapsed = (LONG)(now - (DWORD)previous);
    if (elapsed < 1000) return;
    g_last_summary = (LONG)now;
    pc_log_debug("SUMMARY window_ms=%ld time=%ld joy=%ld status=%ld play=%ld stop=%ld fills=%ld bytes=%ld underruns=%ld recoveries=%ld errors=%ld sched_gaps=%ld initial_complete=%ld provisional_cycles=%ld adopted=%ld phase=%d phase_frames=%lu generation=%lu\r\n",
        elapsed,
        InterlockedExchange(&g_time_calls, 0),
        InterlockedExchange(&g_joy_calls, 0),
        InterlockedExchange(&g_status_calls, 0),
        InterlockedExchange(&g_play_calls, 0),
        InterlockedExchange(&g_stop_calls, 0),
        InterlockedExchange(&g_buffer_fills, 0),
        InterlockedExchange(&g_bytes_read, 0),
        InterlockedExchange(&g_underruns, 0),
        InterlockedExchange(&g_recoveries, 0),
        InterlockedExchange(&g_backend_errors, 0),
        InterlockedExchange(&g_sched_gaps, 0),
        InterlockedExchange(&g_initial_completions, 0),
        InterlockedExchange(&g_provisional_cycles, 0),
        InterlockedExchange(&g_adopted_plays, 0),
        backend_playback_phase(), backend_played_frames(),
        backend_completion_generation());
#else
    (void)now;
#endif
}

static void log_machine(void)
{
#ifdef PCMCD_DEBUG
    OSVERSIONINFOA os;
    SYSTEM_INFO si;
    MEMORYSTATUS mem;
    UINT i, count;
    WAVEOUTCAPSA caps;
    ZeroMemory(&os, sizeof(os)); os.dwOSVersionInfoSize = sizeof(os);
    GetVersionExA(&os);
    GetSystemInfo(&si);
    ZeroMemory(&mem, sizeof(mem)); mem.dwLength = sizeof(mem);
    GlobalMemoryStatus(&mem);
    pc_log_debug("MACHINE windows=%lu.%lu build=%lu platform=%lu cpu=%lu page=%lu memory_total=%lu memory_avail=%lu\r\n",
        os.dwMajorVersion, os.dwMinorVersion, os.dwBuildNumber, os.dwPlatformId,
        si.dwProcessorType, si.dwPageSize, mem.dwTotalPhys, mem.dwAvailPhys);
    count = p_waveOutGetNumDevs ? p_waveOutGetNumDevs() : 0;
    pc_log_debug("MACHINE waveout_count=%u\r\n", count);
    for (i = 0; i < count; ++i) {
        ZeroMemory(&caps, sizeof(caps));
        if (p_waveOutGetDevCapsA && p_waveOutGetDevCapsA(i, &caps, sizeof(caps)) == MMSYSERR_NOERROR)
            pc_log_debug("MACHINE waveout=%u name=%s formats=%08lX channels=%u support=%08lX\r\n",
                i, caps.szPname, caps.dwFormats, caps.wChannels, caps.dwSupport);
    }
    backend_log_caps();
#endif
}

static void close_sources(void)
{
    int i;
    for (i = 0; i < PCMCD_TRACK_COUNT; ++i) {
        if (g_track[i].handle && g_track[i].handle != INVALID_HANDLE_VALUE)
            CloseHandle(g_track[i].handle);
        g_track[i].handle = INVALID_HANDLE_VALUE;
    }
    g_sources_open = 0;
    g_sample_rate = 0;
}

static int parse_wave(TrackFile *track, const char *path)
{
    BYTE head[12], chunk[8], fmt[40];
    DWORD got, size, pos = 12;
    int have_fmt = 0, have_data = 0;
    WORD format = 0, channels = 0, bits = 0, align = 0;
    DWORD rate = 0;
    if (!ReadFile(track->handle, head, sizeof(head), &got, NULL) || got != sizeof(head) ||
        read_u32(head) != 0x46464952UL || read_u32(head + 8) != 0x45564157UL) {
        pc_log_error("validate_wav", 0, path); return 0;
    }
    while (ReadFile(track->handle, chunk, sizeof(chunk), &got, NULL) && got == sizeof(chunk)) {
        size = read_u32(chunk + 4);
        pos += 8;
        if (read_u32(chunk) == 0x20746D66UL) {
            DWORD take = size < sizeof(fmt) ? size : sizeof(fmt);
            if (size < 16 || !ReadFile(track->handle, fmt, take, &got, NULL) || got != take) {
                pc_log_error("read_wav_fmt", GetLastError(), path); return 0;
            }
            format = read_u16(fmt); channels = read_u16(fmt + 2);
            rate = read_u32(fmt + 4); align = read_u16(fmt + 12); bits = read_u16(fmt + 14);
            if (size > take) SetFilePointer(track->handle, size - take, NULL, FILE_CURRENT);
            have_fmt = 1;
        } else if (read_u32(chunk) == 0x61746164UL) {
            track->data_offset = pos;
            track->data_bytes = size;
            have_data = 1;
            SetFilePointer(track->handle, size, NULL, FILE_CURRENT);
        } else {
            SetFilePointer(track->handle, size, NULL, FILE_CURRENT);
        }
        pos += size;
        if (size & 1) { SetFilePointer(track->handle, 1, NULL, FILE_CURRENT); ++pos; }
        if (have_fmt && have_data) break;
    }
    if (!have_fmt || !have_data || format != WAVE_FORMAT_PCM || channels != PCMCD_CHANNELS ||
        bits != PCMCD_BITS || align != PCMCD_BLOCK_ALIGN ||
        (rate != 22050UL && rate != 44100UL) || !track->data_bytes ||
        (track->data_bytes % PCMCD_BLOCK_ALIGN) != 0) {
        pc_log_error("validate_wav_format", 0, path); return 0;
    }
    if (g_sample_rate && g_sample_rate != rate) {
        pc_log_error("validate_wav_rate_mismatch", rate, path); return 0;
    }
    g_sample_rate = rate;
    track->frames = track->data_bytes / PCMCD_BLOCK_ALIGN;
    pc_log_debug("SOURCE track=%u disc=%u physical=%u rate=%lu frames=%lu bytes=%lu path=%s\r\n",
        track->logical, track->disc, track->physical, rate, track->frames, track->data_bytes, path);
    return 1;
}

static int open_sources(void)
{
    int i;
    char path[MAX_PATH];
    close_sources();
    for (i = 0; i < PCMCD_TRACK_COUNT; ++i) {
        g_track[i].disc = g_map[i].disc;
        g_track[i].physical = g_map[i].physical;
        g_track[i].logical = g_map[i].logical;
        g_track[i].handle = INVALID_HANDLE_VALUE;
        wsprintfA(path, "%s\\MUSIC_PCM\\Track%u.wav", g_dll_dir, g_track[i].logical);
        g_track[i].handle = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                                        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
        if (g_track[i].handle == INVALID_HANDLE_VALUE) {
            pc_log_error("open_track", GetLastError(), path);
            close_sources(); return 0;
        }
        if (!parse_wave(&g_track[i], path)) { close_sources(); return 0; }
    }
    g_sources_open = 1;
    return 1;
}

DWORD source_sample_rate(void) { return g_sample_rate; }

static const PlaybackPlan *phase_plan(int phase)
{
    return phase ? &g_program.repeat : &g_program.initial;
}

DWORD source_phase_frames(int phase)
{
    return phase_plan(phase)->total_frames;
}

int source_seek(int phase, DWORD relative_frame)
{
    const PlaybackPlan *plan = phase_plan(phase);
    DWORD base = 0, length;
    int i;
    if (relative_frame > plan->total_frames || !plan->count) return 0;
    g_source_phase = phase ? 1 : 0;
    for (i = 0; i < plan->count; ++i) {
        length = plan->segment[i].end_frame - plan->segment[i].first_frame;
        if (relative_frame < base + length ||
            (relative_frame == plan->total_frames && i == plan->count - 1)) {
            g_source_segment = i;
            g_source_frame = plan->segment[i].first_frame + (relative_frame - base);
            return 1;
        }
        base += length;
    }
    return 0;
}

int source_read(void *buffer, DWORD bytes)
{
    BYTE *out = (BYTE *)buffer;
    DWORD frames_wanted, available, take, got, offset;
    char detail[64];
    const PlaybackPlan *plan;
    TrackFile *track;
    const PcmSegment *segment;
    bytes -= bytes % PCMCD_BLOCK_ALIGN;
    while (bytes) {
        plan = phase_plan(g_source_phase);
        if (g_source_segment >= plan->count) {
            if (!source_seek(1, 0)) {
                pc_log_error("source_repeat_seek", 0, "empty repeat plan");
                return 0;
            }
            continue;
        }
        segment = &plan->segment[g_source_segment];
        track = track_file(segment->track);
        if (!track) {
            pc_log_error("source_track", segment->track, "missing mapped track");
            return 0;
        }
        if (g_source_frame >= segment->end_frame) {
            ++g_source_segment;
            if (g_source_segment < plan->count)
                g_source_frame = plan->segment[g_source_segment].first_frame;
            continue;
        }
        frames_wanted = bytes / PCMCD_BLOCK_ALIGN;
        available = segment->end_frame - g_source_frame;
        take = frames_wanted < available ? frames_wanted : available;
        take *= PCMCD_BLOCK_ALIGN;
        offset = track->data_offset + g_source_frame * PCMCD_BLOCK_ALIGN;
        SetLastError(NO_ERROR);
        if (SetFilePointer(track->handle, offset, NULL, FILE_BEGIN) == 0xFFFFFFFFUL &&
            GetLastError() != NO_ERROR) {
            wsprintfA(detail, "Track%u.wav PCM source seek", track->logical);
            pc_log_error("seek_track", GetLastError(), detail);
            return 0;
        }
        if (!ReadFile(track->handle, out, take, &got, NULL) || got != take) {
            wsprintfA(detail, "Track%u.wav requested=%lu read=%lu",
                      track->logical, take, got);
            pc_log_error("read_track", GetLastError(), detail);
            return 0;
        }
        out += take; bytes -= take;
        g_source_frame += take / PCMCD_BLOCK_ALIGN;
        g_bytes_read += (LONG)take;
    }
    return 1;
}

static int tmsf_offset_frames(DWORD value, DWORD *frames)
{
    DWORD minute = MCI_TMSF_MINUTE(value), second = MCI_TMSF_SECOND(value);
    DWORD cdframe = MCI_TMSF_FRAME(value);
    if (second > 59 || cdframe > 74 || !g_sample_rate || (g_sample_rate % 75) != 0) return 0;
    *frames = minute * 60UL * g_sample_rate + second * g_sample_rate +
              cdframe * (g_sample_rate / 75UL);
    return 1;
}

static int add_segment(PlaybackPlan *plan, int logical, DWORD first, DWORD end)
{
    PcmSegment *s;
    if (end <= first) return 1;
    if (plan->count >= PCMCD_MAX_SEGMENTS) return 0;
    s = &plan->segment[plan->count++];
    s->track = (BYTE)logical; s->first_frame = first; s->end_frame = end;
    plan->total_frames += end - first;
    return 1;
}

static MCIERROR build_plan(DWORD from, DWORD to, DWORD flags, PlaybackPlan *plan)
{
    int first = MCI_TMSF_TRACK(from), end_track, requested_end_track, t;
    DWORD first_offset, end_offset = 0;
    TrackFile *start = track_file(first), *end;
    BYTE disc;
    ZeroMemory(plan, sizeof(*plan));
    plan->sample_rate = g_sample_rate;
    if (!start || !tmsf_offset_frames(from, &first_offset) || first_offset >= start->frames)
        return MCIERR_OUTOFRANGE;
    disc = start->disc;
    if (!(flags & MCI_TO)) {
        end_track = disc == 1 ? 18 : 32;
        end_offset = track_file(end_track)->frames;
        plan->endpoint_track = (BYTE)end_track;
        plan->endpoint_frame = end_offset;
    } else {
        end_track = MCI_TMSF_TRACK(to);
        requested_end_track = end_track;
        if (!tmsf_offset_frames(to, &end_offset)) return MCIERR_OUTOFRANGE;
        if (end_offset == 0) {
            if ((disc == 1 && end_track == 19) || (disc == 2 && end_track == 33)) {
                end_track = disc == 1 ? 18 : 32;
                end_offset = track_file(end_track)->frames;
                plan->endpoint_track = (BYTE)end_track;
                plan->endpoint_frame = end_offset;
            } else {
                end = track_file(end_track);
                if (!end || end->disc != disc || end_track <= first) return MCIERR_OUTOFRANGE;
                plan->endpoint_track = (BYTE)requested_end_track;
                plan->endpoint_frame = 0;
                --end_track;
                end = track_file(end_track);
                if (!end || end->disc != disc) return MCIERR_OUTOFRANGE;
                end_offset = end->frames;
            }
        } else {
            end = track_file(end_track);
            if (!end || end->disc != disc || end_track < first || end_offset > end->frames)
                return MCIERR_OUTOFRANGE;
            plan->endpoint_track = (BYTE)end_track;
            plan->endpoint_frame = end_offset;
        }
    }
    for (t = first; t <= end_track; ++t) {
        TrackFile *file = track_file(t);
        DWORD a, b;
        if (!file || file->disc != disc) return MCIERR_OUTOFRANGE;
        a = t == first ? first_offset : 0;
        b = t == end_track ? end_offset : file->frames;
        if (!add_segment(plan, t, a, b)) return MCIERR_OUTOFRANGE;
    }
    if (!plan->count || !plan->total_frames) return MCIERR_OUTOFRANGE;
    return 0;
}

static int zero_tmsf_offset(DWORD value)
{
    return MCI_TMSF_MINUTE(value) == 0 && MCI_TMSF_SECOND(value) == 0 &&
           MCI_TMSF_FRAME(value) == 0;
}

static MCIERROR build_program(DWORD from, DWORD to, DWORD flags,
                              PlaybackProgram *program)
{
    MCIERROR result;
    int first, last, i;
    DWORD repeat_from, repeat_to;
    ZeroMemory(program, sizeof(*program));
    result = build_plan(from, to, flags, &program->initial);
    if (result) return result;
    program->repeat = program->initial;
    if ((flags & (MCI_FROM | MCI_TO)) != (MCI_FROM | MCI_TO) ||
        !zero_tmsf_offset(from) || !zero_tmsf_offset(to)) {
        pc_log_debug("REPEAT_PLAN generic reason=partial_or_from_only frames=%lu\r\n",
                     program->repeat.total_frames);
        return 0;
    }
    first = MCI_TMSF_TRACK(from);
    last = MCI_TMSF_TRACK(to) - 1;
    for (i = 0; i < (int)(sizeof(g_retail_loop) / sizeof(g_retail_loop[0])); ++i) {
        if (g_retail_loop[i].first == first && g_retail_loop[i].last == last) {
            repeat_from = MCI_MAKE_TMSF(g_retail_loop[i].repeat_first, 0, 0, 0);
            repeat_to = MCI_MAKE_TMSF(g_retail_loop[i].repeat_last + 1, 0, 0, 0);
            result = build_plan(repeat_from, repeat_to, MCI_FROM | MCI_TO,
                                &program->repeat);
            if (result) return result;
            program->retail_repeat = 1;
            pc_log_debug("REPEAT_PLAN retail initial=%d-%d repeat=%u-%u frames=%lu\r\n",
                first, last, g_retail_loop[i].repeat_first,
                g_retail_loop[i].repeat_last, program->repeat.total_frames);
            return 0;
        }
    }
    pc_log_debug("REPEAT_PLAN generic reason=unrecognized_full_range initial=%d-%d frames=%lu\r\n",
                 first, last, program->repeat.total_frames);
    return 0;
}

static int plans_equal(const PlaybackPlan *a, const PlaybackPlan *b)
{
    int i;
    if (a->count != b->count || a->total_frames != b->total_frames ||
        a->sample_rate != b->sample_rate) return 0;
    for (i = 0; i < a->count; ++i) {
        if (a->segment[i].track != b->segment[i].track ||
            a->segment[i].first_frame != b->segment[i].first_frame ||
            a->segment[i].end_frame != b->segment[i].end_frame) return 0;
    }
    return 1;
}

static void plan_position(const PlaybackPlan *plan, DWORD played,
                          DWORD *logical, DWORD *offset)
{
    DWORD length;
    int i;
    if (played > plan->total_frames) played = plan->total_frames;
    if (plan->count && played == plan->total_frames) {
        *logical = plan->endpoint_track;
        *offset = plan->endpoint_frame;
        return;
    }
    for (i = 0; i < plan->count; ++i) {
        length = plan->segment[i].end_frame - plan->segment[i].first_frame;
        if (played < length || i == plan->count - 1) {
            *logical = plan->segment[i].track;
            *offset = plan->segment[i].first_frame +
                      (played < length ? played : length);
            return;
        }
        played -= length;
    }
    *logical = g_current_track; *offset = 0;
}

void source_describe_position(int phase, DWORD relative_frame,
                              DWORD *logical, DWORD *offset)
{
    plan_position(phase_plan(phase), relative_frame, logical, offset);
}

static void playback_position(DWORD *logical, DWORD *offset)
{
    if (g_endpoint_latched) {
        *logical = g_transport_plan.endpoint_track;
        *offset = g_transport_plan.endpoint_frame;
        return;
    }
    plan_position(&g_transport_plan, backend_played_frames(), logical, offset);
}

static DWORD frames_to_msf(DWORD frames)
{
    DWORD cdframes = frames / (g_sample_rate / 75UL);
    return MCI_MAKE_MSF((BYTE)(cdframes / (60UL * 75UL)),
                        (BYTE)((cdframes / 75UL) % 60UL), (BYTE)(cdframes % 75UL));
}

static DWORD frames_to_tmsf(DWORD logical, DWORD frames)
{
    DWORD cdframes = frames / (g_sample_rate / 75UL);
    return MCI_MAKE_TMSF((BYTE)logical, (BYTE)(cdframes / (60UL * 75UL)),
                         (BYTE)((cdframes / 75UL) % 60UL), (BYTE)(cdframes % 75UL));
}

static void update_transport(void)
{
    LONG state = g_transport_state;
    if (state == TRANSPORT_PLAYING && backend_has_failed()) {
        InterlockedExchange(&g_transport_state, TRANSPORT_FAILED);
        pc_log_debug("LOGICAL backend_failed phase=%d frame=%lu\r\n",
                     backend_playback_phase(), backend_played_frames());
        pc_flush_log();
    } else if (state == TRANSPORT_PLAYING && !backend_is_playing()) {
        InterlockedExchange(&g_transport_state, TRANSPORT_STOPPED);
        pc_log_debug("LOGICAL backend_stopped_unexpected phase=%d frame=%lu\r\n",
                     backend_playback_phase(), backend_played_frames());
        pc_flush_log();
    } else if (state == TRANSPORT_PLAYING &&
               backend_completion_generation() != g_completion_base) {
        g_current_track = (int)g_transport_plan.endpoint_track;
        InterlockedExchange(&g_transport_state, TRANSPORT_COMPLETE_PROVISIONAL);
        g_endpoint_latched = 1;
        pc_log_debug("LOGICAL STOP endpoint_track=%u endpoint_frame=%lu generation=%lu provisional_active=1\r\n",
            g_transport_plan.endpoint_track, g_transport_plan.endpoint_frame,
            backend_completion_generation());
        pc_flush_log();
    } else if (state == TRANSPORT_COMPLETE_PROVISIONAL && backend_has_failed()) {
        InterlockedExchange(&g_transport_state, TRANSPORT_FAILED);
        pc_log_debug("LOGICAL provisional_backend_failed phase=%d frame=%lu\r\n",
                     backend_playback_phase(), backend_played_frames());
        pc_flush_log();
    }
}

static MCIERROR handle_status(DWORD flags, MCI_STATUS_PARMS *p)
{
    TrackFile *track;
    DWORD logical, offset;
    if (!p || !(flags & MCI_STATUS_ITEM)) return MCIERR_MISSING_PARAMETER;
    update_transport();
    switch (p->dwItem) {
    case MCI_STATUS_MODE:
        p->dwReturn = g_transport_state == TRANSPORT_PLAYING ? MCI_MODE_PLAY : MCI_MODE_STOP;
        break;
    case MCI_STATUS_NUMBER_OF_TRACKS: p->dwReturn = 32; break;
    case MCI_STATUS_CURRENT_TRACK:
        playback_position(&logical, &offset); p->dwReturn = logical; break;
    case MCI_STATUS_MEDIA_PRESENT:
    case MCI_STATUS_READY: p->dwReturn = g_sources_open ? TRUE : FALSE; break;
    case MCI_STATUS_TIME_FORMAT: p->dwReturn = g_time_format; break;
    case MCI_CDA_STATUS_TYPE_TRACK:
        p->dwReturn = track_file((int)p->dwTrack) ? MCI_CDA_TRACK_AUDIO : MCI_CDA_TRACK_OTHER; break;
    case MCI_STATUS_LENGTH:
        if (!(flags & MCI_TRACK) || !(track = track_file((int)p->dwTrack))) return MCIERR_OUTOFRANGE;
        p->dwReturn = frames_to_msf(track->frames); break;
    case MCI_STATUS_POSITION:
        if (flags & MCI_TRACK) {
            track = track_file((int)p->dwTrack);
            if (!track) return MCIERR_OUTOFRANGE;
            p->dwReturn = MCI_MAKE_TMSF((BYTE)p->dwTrack, 0, 0, 0);
        } else {
            playback_position(&logical, &offset);
            p->dwReturn = frames_to_tmsf(logical, offset);
        }
        break;
    default: return MCIERR_UNSUPPORTED_FUNCTION;
    }
    return 0;
}

static MCIERROR relay_mci(MCIDEVICEID id, UINT message, DWORD flags, DWORD_PTR param)
{
    return p_mciSendCommandA ? p_mciSendCommandA(id, message, flags, param) : MCIERR_CANNOT_LOAD_DRIVER;
}

MCIERROR WINAPI mciSendCommandA(MCIDEVICEID id, UINT message, DWORD flags, DWORD_PTR param)
{
    MCIERROR result = 0;
    ensure_core();
    if (message == MCI_OPEN && id == 0) {
        MCI_OPEN_PARMSA *p = (MCI_OPEN_PARMSA *)param;
        int is_cd = 0;
        if (p && (flags & MCI_OPEN_TYPE_ID))
            is_cd = LOWORD((DWORD_PTR)p->lpstrDeviceType) == MCI_DEVTYPE_CD_AUDIO;
        else if (p && (flags & MCI_OPEN_TYPE) && p->lpstrDeviceType)
            is_cd = lstrcmpiA(p->lpstrDeviceType, "cdaudio") == 0;
        if (!is_cd) return relay_mci(id, message, flags, param);
        if (!open_sources()) return MCIERR_DEVICE_NOT_READY;
        p->wDeviceID = PCMCD_MAGIC_DEVICE;
        g_mci_open = 1; g_time_format = MCI_FORMAT_MSF; g_current_track = 12;
        ZeroMemory(&g_program, sizeof(g_program));
        ZeroMemory(&g_transport_plan, sizeof(g_transport_plan));
        InterlockedExchange(&g_transport_state, TRANSPORT_STOPPED);
        g_endpoint_latched = 0;
        g_game_window = GetActiveWindow(); if (!g_game_window) g_game_window = GetDesktopWindow();
        log_machine();
        pc_log_debug("MCI OPEN flags=%08lX result=0 device=%08lX rate=%lu\r\n",
                     flags, (DWORD)PCMCD_MAGIC_DEVICE, g_sample_rate);
        return 0;
    }
    if (id != PCMCD_MAGIC_DEVICE) return relay_mci(id, message, flags, param);
    if (!g_mci_open && message != MCI_CLOSE) return MCIERR_INVALID_DEVICE_ID;
    switch (message) {
    case MCI_SET:
        if (!param || !(flags & MCI_SET_TIME_FORMAT)) result = MCIERR_MISSING_PARAMETER;
        else if (((MCI_SET_PARMS *)param)->dwTimeFormat != MCI_FORMAT_TMSF) result = MCIERR_UNSUPPORTED_FUNCTION;
        else g_time_format = MCI_FORMAT_TMSF;
        pc_log_debug("MCI SET flags=%08lX format=%lu result=%lu\r\n", flags,
                     param ? ((MCI_SET_PARMS *)param)->dwTimeFormat : 0, result);
        break;
    case MCI_PLAY:
        InterlockedIncrement(&g_play_calls);
        if (!param || !(flags & MCI_FROM)) result = MCIERR_MISSING_PARAMETER;
        else if (g_time_format != MCI_FORMAT_TMSF) result = MCIERR_UNSUPPORTED_FUNCTION;
        else {
            MCI_PLAY_PARMS *p = (MCI_PLAY_PARMS *)param;
            PlaybackProgram candidate;
            int adopt = 0;
            result = build_program(p->dwFrom, p->dwTo, flags, &candidate);
            if (!result) {
                if (g_transport_state == TRANSPORT_COMPLETE_PROVISIONAL &&
                    backend_is_playing() && !backend_has_failed() &&
                    plans_equal(&candidate.initial, &g_program.repeat) &&
                    plans_equal(&candidate.repeat, &g_program.repeat)) {
                    adopt = 1;
                    g_transport_plan = candidate.initial;
                    g_completion_base = backend_completion_generation();
                    g_endpoint_latched = 0;
                    g_current_track = MCI_TMSF_TRACK(p->dwFrom);
                    InterlockedExchange(&g_transport_state, TRANSPORT_PLAYING);
                    InterlockedIncrement(&g_adopted_plays);
                    pc_log_debug("MCI PLAY adopt_provisional from=%08lX to=%08lX phase=%d frame=%lu generation_base=%lu\r\n",
                        p->dwFrom, p->dwTo, backend_playback_phase(),
                        backend_played_frames(), g_completion_base);
                } else {
                    pc_log_debug("MCI PLAY restart from=%08lX to=%08lX prior_state=%ld backend_active=%d\r\n",
                        p->dwFrom, p->dwTo, g_transport_state,
                        backend_is_playing());
                    backend_stop();
                    InterlockedExchange(&g_transport_state, TRANSPORT_STOPPED);
                    g_program = candidate;
                    g_transport_plan = candidate.initial;
                    g_completion_base = 0;
                    g_endpoint_latched = 0;
                    g_current_track = MCI_TMSF_TRACK(p->dwFrom);
                    pc_log_debug("PROGRAM initial_segments=%d initial_frames=%lu repeat_segments=%d repeat_frames=%lu retail=%d\r\n",
                        g_program.initial.count, g_program.initial.total_frames,
                        g_program.repeat.count, g_program.repeat.total_frames,
                        g_program.retail_repeat);
                    if (!backend_play(g_game_window)) {
                        result = MCIERR_DEVICE_NOT_READY;
                        InterlockedExchange(&g_transport_state, TRANSPORT_FAILED);
                    } else {
                        InterlockedExchange(&g_transport_state, TRANSPORT_PLAYING);
                    }
                }
            }
            pc_log_debug("MCI PLAY from=%08lX to=%08lX flags=%08lX result=%lu adopted=%d\r\n",
                         p->dwFrom, p->dwTo, flags, result, adopt);
        }
        break;
    case MCI_STATUS:
        InterlockedIncrement(&g_status_calls);
        result = handle_status(flags, (MCI_STATUS_PARMS *)param);
        break;
    case MCI_STOP:
        InterlockedIncrement(&g_stop_calls);
        backend_stop(); InterlockedExchange(&g_transport_state, TRANSPORT_STOPPED);
        pc_log_debug("MCI STOP flags=%08lX result=0 phase=%d frame=%lu\r\n",
                     flags, backend_playback_phase(), backend_played_frames());
        pc_flush_log();
        break;
    case MCI_CLOSE:
        backend_close(); close_sources(); g_mci_open = 0;
        InterlockedExchange(&g_transport_state, TRANSPORT_STOPPED);
        g_endpoint_latched = 0;
        pc_log_debug("MCI CLOSE flags=%08lX result=0\r\n", flags); pc_flush_log();
        break;
    default: result = MCIERR_UNSUPPORTED_FUNCTION; break;
    }
    if (result) pc_log_debug("MCI ERROR message=%u flags=%08lX result=%lu\r\n", message, flags, result);
    return result;
}

UINT WINAPI auxGetNumDevs(void)
{
    ensure_core(); return g_real_aux_count + 1;
}

MMRESULT WINAPI auxGetDevCapsA(UINT_PTR id, LPAUXCAPSA caps, UINT cb)
{
    AUXCAPSA local;
    DWORD copy;
    ensure_core();
    if ((UINT)id != g_real_aux_count)
        return p_auxGetDevCapsA ? p_auxGetDevCapsA(id, caps, cb) : MMSYSERR_NODRIVER;
    if (!caps || !cb) return MMSYSERR_INVALPARAM;
    ZeroMemory(&local, sizeof(local));
    local.wMid = MM_MICROSOFT; local.wPid = 0xCDDA; local.vDriverVersion = 0x0100;
    lstrcpyA(local.szPname, "Jedi Knight PCM CD Audio");
    local.wTechnology = AUXCAPS_CDAUDIO;
    local.dwSupport = AUXCAPS_VOLUME | AUXCAPS_LRVOLUME;
    copy = cb < sizeof(local) ? cb : sizeof(local);
    CopyMemory(caps, &local, copy);
    return MMSYSERR_NOERROR;
}

MMRESULT WINAPI auxGetVolume(UINT id, LPDWORD volume)
{
    ensure_core();
    if (id != g_real_aux_count) return p_auxGetVolume ? p_auxGetVolume(id, volume) : MMSYSERR_NODRIVER;
    if (!volume) return MMSYSERR_INVALPARAM;
    *volume = g_aux_volume; return MMSYSERR_NOERROR;
}

MMRESULT WINAPI auxSetVolume(UINT id, DWORD volume)
{
    DWORD left, right;
    ensure_core();
    if (id != g_real_aux_count) return p_auxSetVolume ? p_auxSetVolume(id, volume) : MMSYSERR_NODRIVER;
    left = LOWORD(volume); right = HIWORD(volume);
    g_aux_volume = volume; backend_set_volume(volume);
    if (left != right) pc_log_debug("AUX unequal_volume left=%lu right=%lu average=%lu\r\n", left, right, (left + right) / 2);
    pc_log_debug("AUX SETVOLUME raw=%08lX left=%lu right=%lu\r\n", volume, left, right);
    return MMSYSERR_NOERROR;
}

UINT WINAPI joyGetNumDevs(void)
{
    ensure_core(); InterlockedIncrement(&g_joy_calls);
    return p_joyGetNumDevs ? p_joyGetNumDevs() : 0;
}

MMRESULT WINAPI joyGetDevCapsA(UINT_PTR id, LPJOYCAPSA caps, UINT cb)
{
    ensure_core(); InterlockedIncrement(&g_joy_calls);
    return p_joyGetDevCapsA ? p_joyGetDevCapsA(id, caps, cb) : MMSYSERR_NODRIVER;
}

MMRESULT WINAPI joyGetPos(UINT id, LPJOYINFO info)
{
    ensure_core(); InterlockedIncrement(&g_joy_calls);
    return p_joyGetPos ? p_joyGetPos(id, info) : MMSYSERR_NODRIVER;
}

MMRESULT WINAPI joyGetPosEx(UINT id, LPJOYINFOEX info)
{
    ensure_core(); InterlockedIncrement(&g_joy_calls);
    return p_joyGetPosEx ? p_joyGetPosEx(id, info) : MMSYSERR_NODRIVER;
}

DWORD WINAPI timeGetTime(void)
{
    DWORD now;
    ensure_core(); InterlockedIncrement(&g_time_calls);
    now = p_timeGetTime ? p_timeGetTime() : GetTickCount();
    maybe_summary(now); return now;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        g_instance = instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}
