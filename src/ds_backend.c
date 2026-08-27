#define CINTERFACE
#define COBJMACROS
#include "pcmcd.h"
#include <directx/dsound.h>
#include "volume_lut.h"

typedef HRESULT (WINAPI *PFN_DIRECTSOUNDCREATE)(LPCGUID, LPDIRECTSOUND *, LPUNKNOWN);

static HMODULE g_dsound_dll;
static PFN_DIRECTSOUNDCREATE p_DirectSoundCreate;
static LPDIRECTSOUND g_ds;
static LPDIRECTSOUNDBUFFER g_buffer;
static DWORD g_buffer_bytes;
static DWORD g_half_bytes;
static DWORD g_backend_rate;
static HANDLE g_thread;
static HANDLE g_stop_event;
static HANDLE g_ready_event;
static volatile LONG g_active;
static volatile LONG g_start_ok;
static volatile LONG g_played_frames;
static volatile LONG g_playback_phase;
static volatile LONG g_completion_generation;
static volatile LONG g_repeat_cycles;
static volatile LONG g_failed;
static volatile LONG g_volume_dirty = 1;
static DWORD g_volume = 0xFFFFFFFFUL;
static HWND g_window;

static int load_dsound(void)
{
    char path[MAX_PATH];
    if (p_DirectSoundCreate) return 1;
    if (!GetSystemDirectoryA(path, MAX_PATH)) {
        pc_log_error("GetSystemDirectory", GetLastError(), "DSOUND.DLL"); return 0;
    }
    lstrcatA(path, "\\DSOUND.DLL");
    g_dsound_dll = LoadLibraryA(path);
    if (!g_dsound_dll) {
        pc_log_error("LoadLibrary", GetLastError(), path); return 0;
    }
    p_DirectSoundCreate = (PFN_DIRECTSOUNDCREATE)GetProcAddress(g_dsound_dll, "DirectSoundCreate");
    if (!p_DirectSoundCreate) {
        pc_log_error("GetProcAddress", GetLastError(), "DirectSoundCreate"); return 0;
    }
    return 1;
}

static int apply_volume(void)
{
    DWORD left, right, average, index;
    HRESULT hr;
    if (!g_buffer) return 1;
    left = LOWORD(g_volume); right = HIWORD(g_volume);
    average = (left + right) / 2UL;
    index = average >> 8;
    if (index > 255) index = 255;
    hr = IDirectSoundBuffer_SetVolume(g_buffer, pcmcd_volume_lut[index]);
    if (FAILED(hr)) {
        pc_log_error("IDirectSoundBuffer_SetVolume", (DWORD)hr, "volume"); return 0;
    }
    pc_log_debug("BACKEND volume left=%lu right=%lu index=%lu attenuation=%ld\r\n",
                 left, right, index, pcmcd_volume_lut[index]);
    InterlockedExchange(&g_volume_dirty, 0);
    return 1;
}

static int fill_region(DWORD offset, DWORD bytes)
{
    void *p1 = NULL, *p2 = NULL;
    DWORD n1 = 0, n2 = 0;
    int ok = 1;
    HRESULT hr;
    hr = IDirectSoundBuffer_Lock(g_buffer, offset, bytes, &p1, &n1, &p2, &n2, 0);
    if (hr == DSERR_BUFFERLOST) return -1;
    if (FAILED(hr)) {
        pc_log_error("IDirectSoundBuffer_Lock", (DWORD)hr, "refill"); return 0;
    }
    if (n1) {
        if (!source_read(p1, n1)) {
            ZeroMemory(p1, n1);
            ok = 0;
        }
    }
    if (n2) {
        if (ok && !source_read(p2, n2)) ok = 0;
        if (!ok) ZeroMemory(p2, n2);
    }
    hr = IDirectSoundBuffer_Unlock(g_buffer, p1, n1, p2, n2);
    if (FAILED(hr)) {
        pc_log_error("IDirectSoundBuffer_Unlock", (DWORD)hr, "refill"); return 0;
    }
    InterlockedIncrement(&g_buffer_fills);
    return ok;
}

static int create_objects(void)
{
    WAVEFORMATEX format;
    DSBUFFERDESC desc;
    DSCAPS caps;
    DWORD rate = source_sample_rate();
    HRESULT hr;
    if (g_buffer && g_backend_rate == rate) return 1;
    if (g_buffer) { IDirectSoundBuffer_Release(g_buffer); g_buffer = NULL; }
    if (g_ds) { IDirectSound_Release(g_ds); g_ds = NULL; }
    if (!load_dsound()) return 0;
    hr = p_DirectSoundCreate(NULL, &g_ds, NULL);
    if (FAILED(hr)) {
        pc_log_error("DirectSoundCreate", (DWORD)hr, "default device"); return 0;
    }
    hr = IDirectSound_SetCooperativeLevel(g_ds, g_window ? g_window : GetDesktopWindow(), DSSCL_NORMAL);
    if (FAILED(hr)) {
        pc_log_error("IDirectSound_SetCooperativeLevel", (DWORD)hr, "DSSCL_NORMAL"); return 0;
    }
    ZeroMemory(&caps, sizeof(caps)); caps.dwSize = sizeof(caps);
    hr = IDirectSound_GetCaps(g_ds, &caps);
    if (SUCCEEDED(hr))
        pc_log_debug("BACKEND caps flags=%08lX minrate=%lu maxrate=%lu primary=%lu freehw=%lu\r\n",
            caps.dwFlags, caps.dwMinSecondarySampleRate, caps.dwMaxSecondarySampleRate,
            caps.dwPrimaryBuffers, caps.dwFreeHwMixingAllBuffers);
    ZeroMemory(&format, sizeof(format));
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = PCMCD_CHANNELS;
    format.nSamplesPerSec = rate;
    format.wBitsPerSample = PCMCD_BITS;
    format.nBlockAlign = PCMCD_BLOCK_ALIGN;
    format.nAvgBytesPerSec = rate * PCMCD_BLOCK_ALIGN;
    g_half_bytes = (rate / 4UL) * PCMCD_BLOCK_ALIGN;
    g_buffer_bytes = g_half_bytes * 2UL;
    ZeroMemory(&desc, sizeof(desc)); desc.dwSize = sizeof(desc);
    desc.dwFlags = DSBCAPS_CTRLVOLUME | DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_LOCSOFTWARE;
    desc.dwBufferBytes = g_buffer_bytes;
    desc.lpwfxFormat = &format;
    hr = IDirectSound_CreateSoundBuffer(g_ds, &desc, &g_buffer, NULL);
    if (FAILED(hr)) {
        pc_log_error("IDirectSound_CreateSoundBuffer", (DWORD)hr, "software PCM buffer"); return 0;
    }
    g_backend_rate = rate;
    InterlockedExchange(&g_volume_dirty, 1);
    pc_log_debug("BACKEND create_buffer rate=%lu half_bytes=%lu total_bytes=%lu flags=%08lX\r\n",
                 rate, g_half_bytes, g_buffer_bytes, desc.dwFlags);
    return 1;
}

static int recover_buffer(int phase, DWORD played)
{
    HRESULT hr;
    int fill;
    InterlockedIncrement(&g_recoveries);
    hr = IDirectSoundBuffer_Restore(g_buffer);
    if (FAILED(hr)) {
        pc_log_error("IDirectSoundBuffer_Restore", (DWORD)hr, "lost buffer"); return 0;
    }
    if (!source_seek(phase, played)) {
        pc_log_error("source_seek", played, "lost buffer recovery"); return 0;
    }
    fill = fill_region(0, g_buffer_bytes);
    if (fill != 1) {
        pc_log_error("recover_fill", fill == -1 ? DSERR_BUFFERLOST : 0, "lost buffer"); return 0;
    }
    IDirectSoundBuffer_SetCurrentPosition(g_buffer, 0);
    hr = IDirectSoundBuffer_Play(g_buffer, 0, 0, DSBPLAY_LOOPING);
    if (FAILED(hr)) {
        pc_log_error("IDirectSoundBuffer_Play", (DWORD)hr, "recovery restart"); return 0;
    }
    pc_log_debug("BACKEND recovered phase=%d phase_frames=%lu\r\n", phase, played);
    return 1;
}

static int advance_playback(DWORD frames)
{
    DWORD position = (DWORD)g_played_frames;
    int phase = (int)g_playback_phase;
    DWORD total, remaining, logical, offset;
    while (frames) {
        total = source_phase_frames(phase);
        if (!total || position > total) {
            pc_log_error("advance_playback", position, "invalid phase position");
            return 0;
        }
        remaining = total - position;
        if (frames < remaining) {
            position += frames;
            frames = 0;
        } else {
            frames -= remaining;
            position = 0;
            if (!phase) {
                phase = 1;
                InterlockedExchange(&g_played_frames, 0);
                InterlockedExchange(&g_playback_phase, 1);
                InterlockedIncrement(&g_initial_completions);
                InterlockedIncrement(&g_completion_generation);
                source_describe_position(1, 0, &logical, &offset);
                pc_log_debug("BACKEND PRIMARY_COMPLETE generation=%lu initial_frames=%lu\r\n",
                    (DWORD)g_completion_generation, total);
                pc_log_debug("BACKEND PROVISIONAL_LOOP_START track=%lu offset_frames=%lu repeat_frames=%lu\r\n",
                    logical, offset, source_phase_frames(1));
            } else {
                InterlockedIncrement(&g_repeat_cycles);
                InterlockedIncrement(&g_provisional_cycles);
                InterlockedIncrement(&g_completion_generation);
                pc_log_debug("BACKEND PROVISIONAL_CYCLE cycle=%lu generation=%lu repeat_frames=%lu\r\n",
                    (DWORD)g_repeat_cycles, (DWORD)g_completion_generation, total);
            }
        }
    }
    InterlockedExchange(&g_playback_phase, phase);
    InterlockedExchange(&g_played_frames, (LONG)position);
    return 1;
}

static DWORD WINAPI playback_thread(void *unused)
{
    DWORD play = 0, write = 0, prior_cursor = 0, delta, delta_frames;
    DWORD now, prior_tick, cursor_log_tick, elapsed, logical, offset, wait_result;
    int prior_half = 0, current_half, fill;
    int failed = 0;
    HRESULT hr;
    (void)unused;
    if (!create_objects()) { failed = 1; goto done; }
    if (!source_seek(0, 0)) {
        pc_log_error("source_seek", 0, "play start"); failed = 1; goto done;
    }
    fill = fill_region(0, g_buffer_bytes);
    if (fill == -1) {
        hr = IDirectSoundBuffer_Restore(g_buffer);
        if (FAILED(hr) || !source_seek(0, 0) || fill_region(0, g_buffer_bytes) != 1) {
            pc_log_error("initial_buffer_restore", (DWORD)hr, "play start"); failed = 1; goto done;
        }
    } else if (!fill) { failed = 1; goto done; }
    if (!apply_volume()) { failed = 1; goto done; }
    hr = IDirectSoundBuffer_SetCurrentPosition(g_buffer, 0);
    if (FAILED(hr)) { pc_log_error("IDirectSoundBuffer_SetCurrentPosition", (DWORD)hr, "play start"); failed = 1; goto done; }
    hr = IDirectSoundBuffer_Play(g_buffer, 0, 0, DSBPLAY_LOOPING);
    if (FAILED(hr)) { pc_log_error("IDirectSoundBuffer_Play", (DWORD)hr, "play start"); failed = 1; goto done; }
    InterlockedExchange(&g_played_frames, 0);
    InterlockedExchange(&g_playback_phase, 0);
    InterlockedExchange(&g_active, 1);
    InterlockedExchange(&g_start_ok, 1);
    if (g_ready_event) SetEvent(g_ready_event);
    prior_tick = GetTickCount(); cursor_log_tick = prior_tick;
    while ((wait_result = WaitForSingleObject(g_stop_event, 20)) != WAIT_OBJECT_0) {
        if (wait_result == WAIT_FAILED) {
            pc_log_error("WaitForSingleObject", GetLastError(), "playback worker");
            failed = 1;
            break;
        }
        now = GetTickCount(); elapsed = now - prior_tick; prior_tick = now;
        if (elapsed > 100UL) InterlockedIncrement(&g_sched_gaps);
        if (elapsed * source_sample_rate() >= g_buffer_bytes * 250UL) {
            InterlockedIncrement(&g_underruns);
            pc_log_debug("BACKEND underrun elapsed_ms=%lu buffer_bytes=%lu\r\n", elapsed, g_buffer_bytes);
        }
        if (g_volume_dirty && !apply_volume()) { failed = 1; break; }
        hr = IDirectSoundBuffer_GetCurrentPosition(g_buffer, &play, &write);
        if (hr == DSERR_BUFFERLOST) {
            if (!recover_buffer((int)g_playback_phase, (DWORD)g_played_frames)) {
                failed = 1; break;
            }
            prior_cursor = 0; prior_half = 0; prior_tick = GetTickCount();
            continue;
        }
        if (FAILED(hr)) {
            pc_log_error("IDirectSoundBuffer_GetCurrentPosition", (DWORD)hr, "play cursor"); failed = 1; break;
        }
        delta = play >= prior_cursor ? play - prior_cursor : g_buffer_bytes - prior_cursor + play;
        delta -= delta % PCMCD_BLOCK_ALIGN;
        delta_frames = delta / PCMCD_BLOCK_ALIGN;
        if (!advance_playback(delta_frames)) { failed = 1; break; }
        if (now - cursor_log_tick >= 1000UL) {
            source_describe_position((int)g_playback_phase,
                                     (DWORD)g_played_frames, &logical, &offset);
            pc_log_debug("BACKEND cursor play=%lu write=%lu delta=%lu phase=%d phase_frames=%lu track=%lu offset_frames=%lu generation=%lu cycles=%lu\r\n",
                play, write, delta, (int)g_playback_phase,
                (DWORD)g_played_frames, logical, offset,
                (DWORD)g_completion_generation, (DWORD)g_repeat_cycles);
            cursor_log_tick = now;
        }
        prior_cursor = play;
        current_half = play < g_half_bytes ? 0 : 1;
        if (current_half != prior_half) {
            fill = fill_region(prior_half ? g_half_bytes : 0, g_half_bytes);
            if (fill == -1) {
                if (!recover_buffer((int)g_playback_phase,
                                    (DWORD)g_played_frames)) {
                    failed = 1; break;
                }
                prior_cursor = 0; prior_half = 0; prior_tick = GetTickCount();
                continue;
            }
            if (!fill) { failed = 1; break; }
            prior_half = current_half;
        }
    }
    hr = IDirectSoundBuffer_Stop(g_buffer);
    if (FAILED(hr) && !failed) {
        pc_log_error("IDirectSoundBuffer_Stop", (DWORD)hr, "worker stop");
        failed = 1;
    }
done:
    if (failed) InterlockedExchange(&g_failed, 1);
    InterlockedExchange(&g_active, 0);
    if (g_ready_event) SetEvent(g_ready_event);
    pc_log_debug("BACKEND thread_exit reason=%s phase=%d phase_frames=%lu generation=%lu cycles=%lu\r\n",
        failed ? "failed" : "stop", (int)g_playback_phase,
        (DWORD)g_played_frames, (DWORD)g_completion_generation,
        (DWORD)g_repeat_cycles);
    return 0;
}

void backend_log_caps(void)
{
    LPDIRECTSOUND probe = NULL;
    DSCAPS caps;
    HRESULT hr;
    if (!load_dsound()) return;
    hr = p_DirectSoundCreate(NULL, &probe, NULL);
    if (FAILED(hr)) {
        pc_log_error("DirectSoundCreate", (DWORD)hr, "capability probe"); return;
    }
    ZeroMemory(&caps, sizeof(caps)); caps.dwSize = sizeof(caps);
    hr = IDirectSound_GetCaps(probe, &caps);
    if (SUCCEEDED(hr))
        pc_log_debug("MACHINE directsound flags=%08lX minrate=%lu maxrate=%lu primary=%lu freehw=%lu\r\n",
            caps.dwFlags, caps.dwMinSecondarySampleRate, caps.dwMaxSecondarySampleRate,
            caps.dwPrimaryBuffers, caps.dwFreeHwMixingAllBuffers);
    else pc_log_error("IDirectSound_GetCaps", (DWORD)hr, "capability probe");
    IDirectSound_Release(probe);
}

int backend_play(HWND window)
{
    DWORD thread_id;
    backend_stop();
    g_window = window ? window : GetDesktopWindow();
    InterlockedExchange(&g_start_ok, 0);
    InterlockedExchange(&g_played_frames, 0);
    InterlockedExchange(&g_playback_phase, 0);
    InterlockedExchange(&g_completion_generation, 0);
    InterlockedExchange(&g_repeat_cycles, 0);
    InterlockedExchange(&g_failed, 0);
    g_stop_event = CreateEventA(NULL, TRUE, FALSE, NULL);
    g_ready_event = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (!g_stop_event || !g_ready_event) {
        pc_log_error("CreateEvent", GetLastError(), "playback worker"); backend_stop(); return 0;
    }
    g_thread = CreateThread(NULL, 0, playback_thread, NULL, 0, &thread_id);
    if (!g_thread) {
        pc_log_error("CreateThread", GetLastError(), "playback worker"); backend_stop(); return 0;
    }
    if (WaitForSingleObject(g_ready_event, 5000) != WAIT_OBJECT_0 || !g_start_ok) {
        pc_log_error("playback_start", WAIT_TIMEOUT, "worker did not initialize"); backend_stop(); return 0;
    }
    pc_log_debug("BACKEND play thread=%lu initial_frames=%lu repeat_frames=%lu rate=%lu\r\n",
        thread_id, source_phase_frames(0), source_phase_frames(1),
        source_sample_rate());
    return 1;
}

void backend_stop(void)
{
    if (g_stop_event) SetEvent(g_stop_event);
    if (g_thread) {
        WaitForSingleObject(g_thread, INFINITE);
        CloseHandle(g_thread);
    }
    g_thread = NULL;
    if (g_stop_event) CloseHandle(g_stop_event);
    if (g_ready_event) CloseHandle(g_ready_event);
    g_stop_event = NULL; g_ready_event = NULL;
    InterlockedExchange(&g_active, 0);
}

void backend_close(void)
{
    backend_stop();
    if (g_buffer) { IDirectSoundBuffer_Release(g_buffer); g_buffer = NULL; }
    if (g_ds) { IDirectSound_Release(g_ds); g_ds = NULL; }
    if (g_dsound_dll) { FreeLibrary(g_dsound_dll); g_dsound_dll = NULL; }
    p_DirectSoundCreate = NULL;
    g_backend_rate = 0; g_buffer_bytes = 0; g_half_bytes = 0;
}

int backend_is_playing(void) { return g_active != 0; }
int backend_has_failed(void) { return g_failed != 0; }
DWORD backend_played_frames(void) { return (DWORD)g_played_frames; }
int backend_playback_phase(void) { return (int)g_playback_phase; }
DWORD backend_completion_generation(void) { return (DWORD)g_completion_generation; }
DWORD backend_repeat_cycles(void) { return (DWORD)g_repeat_cycles; }

void backend_set_volume(DWORD volume)
{
    g_volume = volume;
    InterlockedExchange(&g_volume_dirty, 1);
}
