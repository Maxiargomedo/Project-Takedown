/*
 * Burnout 3: Takedown (PAL)'s host implementations of guest functions.
 *
 * An override replaces the translated function at its address: the way a port
 * redirects code that drives hardware the host provides differently, and the
 * way the PC features hook in (a settings menu in the game's own menus,
 * widescreen, the mouse's targets). Register them here with
 * ps2host::Override(address, function, "why"); the original stays callable as
 * F_<ADDRESS>(c). See the engine's docs/PORTING.md and the ps2-port skill.
 */

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "gpu_pipelines.h"
#include "ps2host/host.h"
#include "ps2host/mc2.h"
#include "ps2host/run.h"
#include "ps2host/settings.h"

// Forward declarations of lifted game functions
void F_001F23F8(lifted::Context& c);  // RenderWare packet wait
void F_0019A950(lifted::Context& c);  // Race loading step
void F_00132310(lifted::Context& c);  // Boot / game loading step
void F_002FAA00(lifted::Context& c);  // The frame's post effects: glow and speed blur

namespace {

void KernelStore(lifted::Context& c) {
    generated::Write32(u32(c.r[4]), u32(c.r[5]));
}

void TtyWrite(lifted::Context& c) {
    const u32 buffer = u32(c.r[4]), length = u32(c.r[5]);
    std::string text;
    for (u32 i = 0; i < length && i < 4096; ++i) text += char(generated::ReadU8(buffer + i));
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
    if (!text.empty()) std::fprintf(stderr, "[tty] %s\n", text.c_str());
    c.r[2] = length;
}

/*
 * Memory Card implementation for PC:
 * If a card directory is set (via --memcard, default in PC folder),
 * all save/load/format commands are handled cleanly on the host filesystem.
 */
template <void (*Host)(lifted::Context&)>
void CardOverride(lifted::Context& c) {
    if (!ps2host::mc2::CardDirectory().empty()) {
        Host(c);
    } else {
        c.r[2] = 0x81010002;  // no card
    }
}

/*
 * A function that may wait in an idle loop: in real time the wait sleeps
 * (ps2host::InterruptWait) so CPU doesn't spin at 100%, allowing the GPU
 * and audio threads to run smoothly without stutter.
 */
template <void (*Wait)(lifted::Context&)>
void WaitsForInterrupt(lifted::Context& c) {
    ps2host::InterruptWait waiting;
    Wait(c);
}

/*
 * Fast Loading: speeds up loading between races and at boot so it doesn't
 * take dozens of seconds waiting for simulated disc frames.
 */
template <void (*Load)(lifted::Context&)>
void FastLoading(lifted::Context& c) {
    static const bool console = [] {
        const char* speed = std::getenv("PS2HOST_LOAD_SPEED");
        return speed && std::atoi(speed) <= 1;
    }();
    constexpr u64 kStepsNs = 8'000'000;  // half a frame
    u64 args[32];
    std::memcpy(args, c.r, sizeof args);
    const u64 from = ps2host::ClockNs();
    Load(c);
    if (console) return;
    while (!u32(c.r[2]) && !ps2host::BlankDue() && ps2host::ClockNs() - from < kStepsNs &&
           ps2host::RunIopBetweenSteps()) {
        std::memcpy(c.r, args, sizeof args);
        Load(c);
    }
}

/*
 * The frame's post effects (0x002FAA00, from the race's and the frontend's
 * 3D drawing): the frame copied to a half-size buffer, its bright parts
 * (glow) and a radial speed blur gathered there over frames, the buffer laid
 * over the whole frame. At 320x224 that layer softens the picture and causes
 * massive GPU readbacks. BLUR OFF leaves the frame as drawn.
 */
void PostEffects(lifted::Context& c) {
    if (ps2host::Setting("blur", "original") == "off") return;
    F_002FAA00(c);
}

/*
 * RenderWare / game powf (0x0011E388):
 * Replaces hundreds of lifted MIPS instructions per wheel damping calculation
 * with native host CPU std::pow.
 */
void HostPow(lifted::Context& c) {
    float base = 0.0f, exp = 0.0f;
    std::memcpy(&base, &c.f[12], sizeof(float));
    std::memcpy(&exp, &c.f[13], sizeof(float));
    float res = (base > 0.0f) ? std::pow(base, exp) : 0.0f;
    std::memcpy(&c.f[0], &res, sizeof(float));
}

}  // namespace

void RegisterGameIopServers();
void RegisterSixtyFps();

void RegisterGameOverrides() {
    ps2host::SetVuFastDefault(true);
    ps2host::SetFramePacingDefault(false);
    ps2host::SetPresentAtBlankEnd(true);
    ps2host::SetGpuPipelineHints(kGpuPipelineHints, sizeof kGpuPipelineHints / sizeof kGpuPipelineHints[0]);

    // 60 FPS in races and crashes
    RegisterSixtyFps();

    // IOP modules & servers (Logitech, GTFSCDVD, RWA, B3ROUTE)
    RegisterGameIopServers();

    // Native PC Memory Card (saved in memcard/ folder)
    using namespace ps2host::mc2;
    ps2host::Override(0x00244208u, CardOverride<GetInfo>, "libmc2 GetInfo (host card)");
    ps2host::Override(0x002445C0u, CardOverride<Format>, "libmc2 Format (host card)");
    ps2host::Override(0x00246578u, CardOverride<ReadFile>, "libmc2 ReadFile (host card)");
    ps2host::Override(0x00246778u, CardOverride<WriteFile>, "libmc2 WriteFile (host card)");
    ps2host::Override(0x00245FB0u, CardOverride<Create>, "libmc2 Create/Mkdir (host card)");
    ps2host::Override(0x002470C8u, CardOverride<GetDir>, "libmc2 GetDir (host card)");
    ps2host::Override(0x00245388u, CardOverride<Lookup>, "libmc2 lookup (host card)");

    // Fix 3D lag & audio stutter: put spinloop to sleep while waiting for DMA packet flip
    ps2host::AddIdleLoop(0x001F24D0u);
    ps2host::Override(0x001F23F8u, WaitsForInterrupt<F_001F23F8>, "RenderWare's packet wait: asleep in real time");

    // The frame's post effects (glow and speed blur). With blur=off, skips 5 GB VRAM readbacks
    ps2host::Override(0x002FAA00u, PostEffects, "the frame's speed blur and glow: BLUR (PC SETTINGS)");

    // Fast native powf for vehicle wheel damping calculations
    ps2host::Override(0x0011E388u, HostPow, "RenderWare fast host powf");

    // Fast loading
    ps2host::Override(0x0019A950u, FastLoading<F_0019A950>, "a race's loading: fast-forwarded while it waits");
    ps2host::Override(0x00132310u, FastLoading<F_00132310>, "the game's loading: fast-forwarded while it waits");

    // Low-level EE kernel hooks
    ps2host::Override(0x001156C0u, KernelStore, "kernel-mode store");
    ps2host::Override(0x0010D3E8u, TtyWrite, "TTY write (printf)");
}
