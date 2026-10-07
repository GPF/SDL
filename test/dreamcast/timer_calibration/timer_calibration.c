/* Hardware check that SDL's clock matches real time on Dreamcast.
 *
 * SDL_GetTicks/SDL_GetTicksNS/SDL_GetPerformanceCounter are compared against
 * clock_gettime(CLOCK_MONOTONIC) (which tracks the host's wall clock) across
 * sleeping, vblank-waiting and busy phases. The old Dreamcast timer used the SH4
 * perf counter with a frequency constant that overflowed a 32-bit int, so it ran
 * 1.4x fast while busy and almost stopped while a thread slept; a tick-deadline
 * loop with SDL_Delay then took ~100x too long.
 *
 * Expected: every phase within 2%, frequency 1e9, deadline loop under 1.5 s.
 * Prints timer_calibration result=PASS/FAIL and returns 0/1.
 */
#include <SDL3/SDL.h>
#include <kos.h>
#include <math.h>
#include <time.h>

#define TOLERANCE_PERCENT 2.0

static Uint64 monotonic_ns(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (Uint64)t.tv_sec * 1000000000ull + (Uint64)t.tv_nsec;
}

static int failures;

static void check(const char *name, Uint64 ref0, Uint64 ticks_ns0)
{
    const Uint64 ref = monotonic_ns() - ref0;
    const Uint64 sdl = SDL_GetTicksNS() - ticks_ns0;
    const double err = ((double)sdl / (double)ref - 1.0) * 100.0;
    const bool ok = fabs(err) <= TOLERANCE_PERCENT;

    SDL_Log("timer_calibration %-22s clock_gettime=%6llu ms SDL_GetTicksNS=%6llu ms err=%+.2f%% %s", name,
            (unsigned long long)(ref / 1000000), (unsigned long long)(sdl / 1000000), err, ok ? "PASS" : "FAIL");
    failures += !ok;
}

int main(int argc, char **argv)
{
    Uint64 r, s;

    vid_set_mode(DM_640x480_VGA, PM_RGB565);

    {
        const Uint64 freq = SDL_GetPerformanceFrequency();
        const bool ok = (freq == 1000000000ull);
        SDL_Log("timer_calibration SDL_GetPerformanceFrequency=%llu %s", (unsigned long long)freq, ok ? "PASS" : "FAIL");
        failures += !ok;
    }

    r = monotonic_ns(); s = SDL_GetTicksNS();
    SDL_Delay(2000);
    check("SDL_Delay(2000)", r, s);

    r = monotonic_ns(); s = SDL_GetTicksNS();
    for (int i = 0; i < 200; ++i) {
        SDL_Delay(5);
    }
    check("200 x SDL_Delay(5)", r, s);

    for (int i = 0; i < 5; ++i) {
        vid_waitvbl();
    }
    r = monotonic_ns(); s = SDL_GetTicksNS();
    for (int i = 0; i < 60; ++i) {
        vid_waitvbl();
    }
    check("60 vblanks", r, s);

    r = monotonic_ns(); s = SDL_GetTicksNS();
    while (monotonic_ns() - r < 1000000000ull) {
    }
    check("1 s busy loop", r, s);

    /* The failure mode that matters to applications: a tick-based deadline loop. */
    r = monotonic_ns();
    {
        const Uint64 deadline = SDL_GetTicks() + 500;
        while (SDL_GetTicks() < deadline) {
            SDL_Delay(1);
        }
    }
    {
        const double ms = (double)(monotonic_ns() - r) / 1e6;
        const bool ok = ms < 1500.0;
        SDL_Log("timer_calibration deadline loop (500 ms of ticks) took %.0f ms of real time %s", ms, ok ? "PASS" : "FAIL");
        failures += !ok;
    }

    SDL_Log("timer_calibration result=%s", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
