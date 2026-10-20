/*
 * Burnout 3 (PAL) at 60 pictures a second: crashes and vertical blank pacing.
 */

#include <algorithm>
#include <cstring>

#include "ps2host/host.h"
#include "ps2host/run.h"

void F_001320F0(lifted::Context& c);
void F_001F1CD8(lifted::Context& c);

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

}  // namespace

void RegisterSixtyFps() {
    ps2host::Override(0x001320F0u, CrashRate, "a crash's slow motion, splitscreen: at 60 FPS");
    ps2host::Override(0x001F1CD8u, BlankHandler, "the vertical blank's handler: the blanks a picture took");
}
