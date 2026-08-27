#if defined(__WATCOMC__)
#define __declspec(x)
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>

typedef MCIERROR (WINAPI *PFN_MCI)(MCIDEVICEID, UINT, DWORD, DWORD_PTR);
typedef UINT (WINAPI *PFN_AUXNUM)(void);
typedef MMRESULT (WINAPI *PFN_AUXVOL)(UINT, DWORD);

static PFN_MCI send_mci;
static PFN_AUXNUM aux_num;
static PFN_AUXVOL aux_volume;
static MCIDEVICEID device;
static int failures;

static DWORD cd_frames_from_msf(DWORD value)
{
    return (DWORD)MCI_MSF_MINUTE(value) * 60UL * 75UL +
           (DWORD)MCI_MSF_SECOND(value) * 75UL + MCI_MSF_FRAME(value);
}

static DWORD make_tmsf(int track, DWORD cdframes)
{
    return MCI_MAKE_TMSF((BYTE)track, (BYTE)(cdframes / (60UL * 75UL)),
                         (BYTE)((cdframes / 75UL) % 60UL), (BYTE)(cdframes % 75UL));
}

static void check_result(const char *name, MCIERROR got, MCIERROR expected)
{
    if (got == expected) printf("PASS %s (result %lu)\n", name, (DWORD)got);
    else { printf("FAIL %s (got %lu, expected %lu)\n", name, (DWORD)got, (DWORD)expected); ++failures; }
}

static DWORD track_length(int track)
{
    MCI_STATUS_PARMS status;
    MCIERROR error;
    ZeroMemory(&status, sizeof(status));
    status.dwItem = MCI_STATUS_LENGTH;
    status.dwTrack = track;
    error = send_mci(device, MCI_STATUS, MCI_STATUS_ITEM | MCI_TRACK, (DWORD_PTR)&status);
    if (error) { printf("FAIL length for track %d: %lu\n", track, (DWORD)error); ++failures; return 0; }
    return cd_frames_from_msf(status.dwReturn);
}

static DWORD status_mode(void)
{
    MCI_STATUS_PARMS status;
    MCIERROR error;
    ZeroMemory(&status, sizeof(status));
    status.dwItem = MCI_STATUS_MODE;
    error = send_mci(device, MCI_STATUS, MCI_STATUS_ITEM, (DWORD_PTR)&status);
    check_result("STATUS mode", error, 0);
    return error ? 0 : status.dwReturn;
}

static void wait_until_stopped(DWORD timeout_ms)
{
    DWORD start = GetTickCount();
    MCI_STATUS_PARMS status;
    MCIERROR error;
    do {
        Sleep(100);
        ZeroMemory(&status, sizeof(status)); status.dwItem = MCI_STATUS_MODE;
        error = send_mci(device, MCI_STATUS, MCI_STATUS_ITEM, (DWORD_PTR)&status);
        if (error || status.dwReturn != MCI_MODE_PLAY) return;
    } while (GetTickCount() - start < timeout_ms);
    printf("FAIL playback timeout\n"); ++failures;
    send_mci(device, MCI_STOP, 0, 0);
}

static void play_range(const char *name, DWORD from, DWORD to)
{
    MCI_PLAY_PARMS play;
    MCIERROR error;
    ZeroMemory(&play, sizeof(play)); play.dwFrom = from; play.dwTo = to;
    printf("\n%s\n", name);
    error = send_mci(device, MCI_PLAY, MCI_FROM | MCI_TO, (DWORD_PTR)&play);
    if (error) { printf("FAIL play returned %lu\n", (DWORD)error); ++failures; return; }
    wait_until_stopped(15000);
    printf("Completed.\n");
    Sleep(500);
}

static void mapping_tests(void)
{
    DWORD length12 = track_length(12), length22 = track_length(22);
    DWORD five = 5UL * 75UL, ten = 10UL * 75UL;
    play_range("Disc 1: final five seconds of logical 12, then first five of logical 13",
               make_tmsf(12, length12 > five ? length12 - five : 0), make_tmsf(13, five));
    play_range("Disc 2: first ten seconds of logical 22",
               make_tmsf(22, 0), make_tmsf(22, ten));
    play_range("Disc 2: final five seconds of logical 22, then first five of logical 23",
               make_tmsf(22, length22 > five ? length22 - five : 0), make_tmsf(23, five));
    play_range("Disc 2: first ten seconds of logical 32",
               make_tmsf(32, 0), make_tmsf(32, ten));
}

static MCIERROR try_play(DWORD from, DWORD to, DWORD flags)
{
    MCI_PLAY_PARMS play;
    ZeroMemory(&play, sizeof(play)); play.dwFrom = from; play.dwTo = to;
    return send_mci(device, MCI_PLAY, flags, (DWORD_PTR)&play);
}

static void self_tests(void)
{
    MCI_STATUS_PARMS status;
    DWORD length12;
    MCIERROR error;
    ZeroMemory(&status, sizeof(status)); status.dwItem = MCI_STATUS_NUMBER_OF_TRACKS;
    check_result("STATUS track count", send_mci(device, MCI_STATUS, MCI_STATUS_ITEM, (DWORD_PTR)&status), 0);
    if (status.dwReturn != 32) { printf("FAIL track count is %lu\n", status.dwReturn); ++failures; }
    ZeroMemory(&status, sizeof(status)); status.dwItem = MCI_STATUS_READY;
    check_result("STATUS ready", send_mci(device, MCI_STATUS, MCI_STATUS_ITEM, (DWORD_PTR)&status), 0);
    if (!status.dwReturn) { printf("FAIL device is not ready\n"); ++failures; }
    ZeroMemory(&status, sizeof(status)); status.dwItem = MCI_STATUS_MEDIA_PRESENT;
    check_result("STATUS media present", send_mci(device, MCI_STATUS, MCI_STATUS_ITEM, (DWORD_PTR)&status), 0);
    if (!status.dwReturn) { printf("FAIL media is not present\n"); ++failures; }
    ZeroMemory(&status, sizeof(status)); status.dwItem = MCI_STATUS_TIME_FORMAT;
    check_result("STATUS time format", send_mci(device, MCI_STATUS, MCI_STATUS_ITEM, (DWORD_PTR)&status), 0);
    if (status.dwReturn != MCI_FORMAT_TMSF) { printf("FAIL time format is %lu\n", status.dwReturn); ++failures; }
    ZeroMemory(&status, sizeof(status)); status.dwItem = MCI_CDA_STATUS_TYPE_TRACK; status.dwTrack = 12;
    check_result("STATUS audio-track type", send_mci(device, MCI_STATUS, MCI_STATUS_ITEM | MCI_TRACK, (DWORD_PTR)&status), 0);
    if (status.dwReturn != MCI_CDA_TRACK_AUDIO) { printf("FAIL track 12 type is %lu\n", status.dwReturn); ++failures; }
    ZeroMemory(&status, sizeof(status)); status.dwItem = MCI_CDA_STATUS_TYPE_TRACK; status.dwTrack = 19;
    check_result("STATUS invalid-gap type", send_mci(device, MCI_STATUS, MCI_STATUS_ITEM | MCI_TRACK, (DWORD_PTR)&status), 0);
    if (status.dwReturn != MCI_CDA_TRACK_OTHER) { printf("FAIL gap type is %lu\n", status.dwReturn); ++failures; }
    check_result("raw physical track 2 rejected", try_play(make_tmsf(2, 0), make_tmsf(3, 0), MCI_FROM | MCI_TO), MCIERR_OUTOFRANGE);
    check_result("gap track 19 rejected", try_play(make_tmsf(19, 0), make_tmsf(22, 0), MCI_FROM | MCI_TO), MCIERR_OUTOFRANGE);
    check_result("cross-disc range rejected", try_play(make_tmsf(18, 0), make_tmsf(23, 0), MCI_FROM | MCI_TO), MCIERR_OUTOFRANGE);
    check_result("empty exclusive range rejected", try_play(make_tmsf(12, 0), make_tmsf(12, 0), MCI_FROM | MCI_TO), MCIERR_OUTOFRANGE);
    check_result("retail 13-to-12/13 plan", try_play(make_tmsf(13, 0), make_tmsf(14, 0), MCI_FROM | MCI_TO), 0);
    Sleep(100);
    check_result("retail-plan STOP", send_mci(device, MCI_STOP, 0, 0), 0);
    length12 = track_length(12);
    check_result("exclusive-TO PLAY", try_play(make_tmsf(12, length12 - 75), make_tmsf(13, 0), MCI_FROM | MCI_TO), 0);
    wait_until_stopped(3000);
    ZeroMemory(&status, sizeof(status)); status.dwItem = MCI_STATUS_POSITION;
    check_result("exclusive endpoint POSITION", send_mci(device, MCI_STATUS, MCI_STATUS_ITEM, (DWORD_PTR)&status), 0);
    if (MCI_TMSF_TRACK(status.dwReturn) != 13 || MCI_TMSF_MINUTE(status.dwReturn) ||
        MCI_TMSF_SECOND(status.dwReturn) || MCI_TMSF_FRAME(status.dwReturn)) {
        printf("FAIL exclusive endpoint is %08lX\n", status.dwReturn); ++failures;
    }
    ZeroMemory(&status, sizeof(status)); status.dwItem = MCI_STATUS_CURRENT_TRACK;
    check_result("exclusive endpoint CURRENT_TRACK", send_mci(device, MCI_STATUS, MCI_STATUS_ITEM, (DWORD_PTR)&status), 0);
    if (status.dwReturn != 13) { printf("FAIL endpoint current track is %lu\n", status.dwReturn); ++failures; }
    check_result("exclusive-TO STOP", send_mci(device, MCI_STOP, 0, 0), 0);
    check_result("partial-offset PLAY", try_play(make_tmsf(12, length12 - 75), make_tmsf(13, 75), MCI_FROM | MCI_TO), 0);
    Sleep(200);
    check_result("interrupted STOP", send_mci(device, MCI_STOP, 0, 0), 0);
    check_result("FROM-only PLAY", try_play(make_tmsf(32, 0), 0, MCI_FROM), 0);
    Sleep(200);
    check_result("repeated STOP", send_mci(device, MCI_STOP, 0, 0), 0);
    check_result("delayed-poll PLAY", try_play(make_tmsf(22, 0), make_tmsf(22, 75), MCI_FROM | MCI_TO), 0);
    Sleep(2600);
    if (status_mode() != MCI_MODE_STOP) {
        printf("FAIL delayed-poll range did not report STOP\n"); ++failures;
    } else printf("PASS delayed-poll range reports logical STOP\n");
    if (status_mode() != MCI_MODE_STOP) {
        printf("FAIL repeated completed STATUS did not remain STOP\n"); ++failures;
    } else printf("PASS repeated completed STATUS remains STOP\n");
    ZeroMemory(&status, sizeof(status)); status.dwItem = MCI_STATUS_POSITION;
    check_result("provisional endpoint POSITION", send_mci(device, MCI_STATUS, MCI_STATUS_ITEM, (DWORD_PTR)&status), 0);
    if (MCI_TMSF_TRACK(status.dwReturn) != 22 || MCI_TMSF_SECOND(status.dwReturn) != 1 ||
        MCI_TMSF_MINUTE(status.dwReturn) || MCI_TMSF_FRAME(status.dwReturn)) {
        printf("FAIL provisional endpoint is %08lX\n", status.dwReturn); ++failures;
    }
    ZeroMemory(&status, sizeof(status)); status.dwItem = MCI_STATUS_CURRENT_TRACK;
    check_result("provisional endpoint CURRENT_TRACK", send_mci(device, MCI_STATUS, MCI_STATUS_ITEM, (DWORD_PTR)&status), 0);
    if (status.dwReturn != 22) { printf("FAIL provisional current track is %lu\n", status.dwReturn); ++failures; }
    check_result("matching replacement PLAY", try_play(make_tmsf(22, 0), make_tmsf(22, 75), MCI_FROM | MCI_TO), 0);
    Sleep(1200);
    if (status_mode() != MCI_MODE_STOP) {
        printf("FAIL adopted range did not complete next cycle\n"); ++failures;
    } else printf("PASS adopted range completes at next cycle\n");
    check_result("differing replacement PLAY", try_play(make_tmsf(22, 75), make_tmsf(22, 150), MCI_FROM | MCI_TO), 0);
    if (aux_num && aux_volume) {
        UINT synthetic = aux_num() - 1;
        aux_volume(synthetic, 0x00000000UL);
        aux_volume(synthetic, 0x80008000UL);
        aux_volume(synthetic, 0xFFFFFFFFUL);
        printf("PASS active AUX volume ramp\n");
    }
    Sleep(200);
    check_result("differing PLAY/STOP", send_mci(device, MCI_STOP, 0, 0), 0);
    error = send_mci(device, MCI_STOP, 0, 0);
    check_result("final STOP", error, 0);
    check_result("close-during-provisional PLAY", try_play(make_tmsf(22, 0), make_tmsf(22, 75), MCI_FROM | MCI_TO), 0);
    Sleep(1200);
    if (status_mode() != MCI_MODE_STOP) {
        printf("FAIL close test did not reach provisional state\n"); ++failures;
    } else printf("PASS close test reached provisional state\n");
}

static void retail_loop_test(void)
{
    DWORD length13 = track_length(13);
    DWORD wait_ms;
    if (!length13) return;
    printf("\nRetail loop test: Track 13 once, then the retail continuation Tracks 12-13.\n");
    printf("Do not interrupt the test. There must be no silence at the transition.\n");
    check_result("retail Track 13 PLAY",
        try_play(make_tmsf(13, 0), make_tmsf(14, 0), MCI_FROM | MCI_TO), 0);
    if (failures) return;
    wait_ms = (length13 * 1000UL) / 75UL + 10000UL;
    printf("Listening without an MCI poll for %lu seconds...\n", wait_ms / 1000UL);
    Sleep(wait_ms);
    printf("Track 12 should now have been audible for about ten seconds.\n");
    if (status_mode() != MCI_MODE_STOP) {
        printf("FAIL completed retail range did not report STOP\n"); ++failures;
        return;
    }
    check_result("canonical Tracks 12-13 replacement PLAY",
        try_play(make_tmsf(12, 0), make_tmsf(14, 0), MCI_FROM | MCI_TO), 0);
    if (!failures) {
        printf("Listening ten more seconds; adoption must not click, pause, or restart.\n");
        Sleep(10000);
    }
    check_result("retail loop STOP", send_mci(device, MCI_STOP, 0, 0), 0);
}

int main(int argc, char **argv)
{
    const char *dll_name = argc > 1 ? argv[1] : "wincd_pcm_debug.dll";
    int selftest = argc > 2 && lstrcmpiA(argv[2], "/SELFTEST") == 0;
    int retailloop = argc > 2 && lstrcmpiA(argv[2], "/RETAILLOOP") == 0;
    HMODULE dll;
    MCI_OPEN_PARMSA open;
    MCI_SET_PARMS set;
    MCIERROR error;
    printf("Jedi Knight PCM/CDDA disc-map test\nLoading %s\n", dll_name);
    dll = LoadLibraryA(dll_name);
    if (!dll) { printf("ERROR LoadLibrary failed: %lu\n", GetLastError()); return 1; }
    send_mci = (PFN_MCI)GetProcAddress(dll, "mciSendCommandA");
    aux_num = (PFN_AUXNUM)GetProcAddress(dll, "auxGetNumDevs");
    aux_volume = (PFN_AUXVOL)GetProcAddress(dll, "auxSetVolume");
    if (!send_mci) { printf("ERROR mciSendCommandA export not found\n"); FreeLibrary(dll); return 1; }
    ZeroMemory(&open, sizeof(open)); open.lpstrDeviceType = "cdaudio";
    error = send_mci(0, MCI_OPEN, MCI_OPEN_TYPE, (DWORD_PTR)&open);
    if (error) { printf("ERROR MCI_OPEN returned %lu. Check MUSIC_PCM and pcmcd_error.log.\n", (DWORD)error); FreeLibrary(dll); return 1; }
    device = open.wDeviceID;
    ZeroMemory(&set, sizeof(set)); set.dwTimeFormat = MCI_FORMAT_TMSF;
    error = send_mci(device, MCI_SET, MCI_SET_TIME_FORMAT, (DWORD_PTR)&set);
    if (error) { printf("ERROR MCI_SET TMSF returned %lu\n", (DWORD)error); ++failures; }
    if (aux_num && aux_volume) aux_volume(aux_num() - 1, 0xFFFFFFFFUL);
    if (!failures) {
        if (selftest) self_tests();
        else if (retailloop) retail_loop_test();
        else mapping_tests();
    }
    send_mci(device, MCI_CLOSE, 0, 0);
    FreeLibrary(dll);
    printf("\n%s: %d failure(s).\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
