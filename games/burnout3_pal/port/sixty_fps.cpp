/*
 * Burnout 3 (PAL) at 60 pictures a second: crashes, movies, menus, vertical blank pacing,
 * and true 16:9 widescreen camera projection.
 */

#include <algorithm>
#include <cmath>
#include <cstring>

#include "ps2host/host.h"
#include "ps2host/run.h"
#include "ps2host/settings.h"

void F_001320F0(lifted::Context& c);
void F_001F1CD8(lifted::Context& c);
void F_00130DA0(lifted::Context& c);
void F_00131410(lifted::Context& c);
void F_001D3D20(lifted::Context& c);
void F_001E2100(lifted::Context& c);

void UpdateWidescreen() {
    const float aspect = ps2host::PictureAspect();
    const bool is_widescreen = (aspect > 1.4f);
    if (is_widescreen) {
        // PCSX2 PeterDelta / SLES-52584 widescreen native patches:
        // Sets widescreen active flag on screen manager (g_00666440_obj at 0x00666440, offset +0x0D):
        generated::Write8(0x0066644Du, 1);
        // Camera aspect multiplier (4.0f / 3.0f = 0x3FAAAAAB), expands horizontal FOV by 4/3:
        generated::Write32(0x004E0FB8u, 0x3FAAAAABu);
        // Splitscreen camera aspect multiplier (16.0f / 9.0f = 0x3FE38E39):
        generated::Write32(0x004E11FCu, 0x3FE38E39u);
        // Active camera cached aspect multipliers:
        generated::Write32(0x00666540u, 0x3FAAAAABu);
        generated::Write32(0x00666544u, 0x3FE38E39u);
        generated::Write32(0x006665E0u, 0x3FAAAAABu);
        generated::Write32(0x006665E4u, 0x3FE38E39u);
    } else {
        // Original 4:3 constants in PAL SLES_525.84:
        generated::Write8(0x0066644Du, 0);
        generated::Write32(0x004E0FB8u, 0x3F99999Au);  // 1.200000f
        generated::Write32(0x004E11FCu, 0x3FCF5C29u);  // 1.620000f
        generated::Write32(0x00666540u, 0x3F800000u);  // 1.000000f
        generated::Write32(0x00666544u, 0x3FAAAAABu);  // 1.333333f (4/3)
        generated::Write32(0x006665E0u, 0x3F800000u);  // 1.000000f
        generated::Write32(0x006665E4u, 0x3FAAAAABu);  // 1.333333f (4/3)
    }
}

namespace {

constexpr u32 kSetInterval = 0x001F4280u;  // a0: a picture every 1 or 2 blanks

/* 0x001F1CD8 (the vertical blank's handler). */
u32 g_picture_blanks = 1;

void BlankHandler(lifted::Context& c) {
    static u32 blanks = 0;
    const u32 data = u32(generated::ReadS32(u32(c.r[28]) - 0x5E24u));
    const bool start = u32(c.r[4]) == 2;
    const bool ready = generated::ReadU8(data - 0x5EA1u) != 0;
    if (start) ++blanks;
    F_001F1CD8(c);
    if (start && ready && generated::ReadU8(data - 0x5EA1u) == 0) {
        g_picture_blanks = std::max(1u, blanks);
        blanks = 0;
    }
    UpdateWidescreen();
}

/* 0x001320F0(game): crash slow motion / car views at 60 FPS.
   In PAL SLES_525.84:
   - racing flag is at 0x004EB760u
   - asked is game + 0x2DA65
   - followed is game + 0x2DA66
*/
void CrashRate(lifted::Context& c) {
    const u32 entry_ra = u32(c.r[31]);
    const u32 asked = u32(c.r[4]) + 0x30000u - 0x259Bu, followed = asked + 1;
    generated::Write8(followed, generated::ReadU8(asked));
    F_001320F0(c);
    const bool racing = generated::ReadU8(0x004EB760u) != 0;
    const bool behind = racing && generated::ReadU8(asked) != 0 && g_picture_blanks >= 2;
    generated::Write8(followed, behind ? 1u : 0u);
    if (racing) {
        u64 saved[6];
        std::memcpy(saved, &c.r[2], sizeof saved);
        c.r[4] = 1;
        lifted::Call(c, kSetInterval, entry_ra);
        std::memcpy(&c.r[2], saved, sizeof saved);
    }
    c.r[31] = entry_ra;
}

/* 0x00130DA0(screen): movie playback speed fix.
   The movie advances by dt (+0x20C). At 60 FPS, advancing by 1/30 (or 1/25) each picture
   causes videos to play at 2x accelerated speed. Halving the step keeps videos at normal 1.0x speed.
*/
void MovieRate(lifted::Context& c) {
    const u32 screen = u32(c.r[4]);
    const s32 before = generated::ReadS32(screen + 0x2F8u);
    F_00130DA0(c);
    if ((before == 2 || before == 0x18) && generated::ReadS32(screen + 0x2F8u) == 0x17) {
        const u32 bits = generated::ReadU32(screen + 0x20Cu);
        float step;
        std::memcpy(&step, &bits, 4);
        step /= 2.0f;
        u32 half;
        std::memcpy(&half, &step, 4);
        generated::Write32(screen + 0x20Cu, half);
    }
}

/* 0x00131410(screen): movie screen update.
   Allows skipping non-looping movies (EA logo, Criterion/RW logos, tutorial) with Start,
   or automatically if skip_intro is set in PC Settings.
*/
void SkipMovie(lifted::Context& c) {
    const u32 screen = u32(c.r[4]);
    static bool start_before = false;
    const bool start_held = (ps2host::PadButtonsHeld() & (1u << 3)) != 0;
    const bool start_pressed = start_held && !start_before;
    start_before = start_held;
    const bool auto_skip = ps2host::SettingOn("skip_intro", false);
    if ((auto_skip || start_pressed) && generated::ReadS32(screen + 0x2F8u) == 0x17) {
        const u32 movie = screen + 0x1C8u;
        if (!generated::ReadU8(movie + 0x4Bu)) {
            u64 saved[32];
            std::memcpy(saved, c.r, sizeof saved);
            c.r[4] = movie;
            lifted::Call(c, 0x00325050u, u32(saved[31]));
            std::memcpy(c.r, saved, sizeof saved);
        }
    }
    F_00131410(c);
}

/* 0x001D3D20(fps): menu frame rate / dt calculation.
   When the menu sets 25 or 30 FPS, each update frame advances by 33ms or 40ms.
   Because the PC presents at 60 FPS, this causes menus, timers, and the 3D garage
   to run at 2x accelerated speed. Changing 25/30 to 60 gives dt = 1/60s and ms = 16,
   so all menu animations, blinking cursors, and 3D car rotation move at normal 1.0x speed.
*/
void MenuFpsRate(lifted::Context& c) {
    if (c.r[4] == 25 || c.r[4] == 30) {
        c.r[4] = 60;
    }
    F_001D3D20(c);
}

}  // namespace

void RegisterSixtyFps() {
    ps2host::Override(0x001320F0u, CrashRate, "a crash's slow motion, splitscreen: at 60 FPS");
    ps2host::Override(0x001F1CD8u, BlankHandler, "the vertical blank's handler: the blanks a picture took");
    ps2host::Override(0x00130DA0u, MovieRate, "movie start: half step for authentic 1.0x playback at 60 FPS");
    ps2host::Override(0x00131410u, SkipMovie, "movie screen update: Start skips the movie");
    ps2host::Override(0x001D3D20u, MenuFpsRate, "frontend menu dt: normal 1.0x speed at 60 FPS");
    UpdateWidescreen();
}
