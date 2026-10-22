/*
 * Burnout 3 (PAL) in a wider picture (ASPECT RATIO, PC SETTINGS):
 * Full 16:9 widescreen menus, videos and screens (filling the entire screen),
 * with true 16:9 in-race HUD edge-anchoring and 3D camera projection.
 */

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#include "ps2host/host.h"
#include "ps2host/run.h"
#include "ps2host/settings.h"

// RenderWare's RwCameraSetViewWindow (see SetViewWindow).
void F_001E2100(lifted::Context& c);
// The race's world each frame: the cars, the traffic, the clock (see RaceWorld).
void F_0019A550(lifted::Context& c);
// The 2D batch: its builders (each appends GS packets to a scratchpad
// buffer), its flush (see Flush2D); the frontend's sprites (see Sprites2D).
void F_0021A4E0(lifted::Context& c);
void F_00364B00(lifted::Context& c);
void F_002178D0(lifted::Context& c);
void F_00217A10(lifted::Context& c);
void F_00217C30(lifted::Context& c);
void F_00217D30(lifted::Context& c);
void F_00217E60(lifted::Context& c);
void F_00217FB0(lifted::Context& c);
void F_002183F0(lifted::Context& c);
void F_002187A0(lifted::Context& c);
void F_00218CF0(lifted::Context& c);
void F_00219140(lifted::Context& c);
void F_00219520(lifted::Context& c);
void F_00219820(lifted::Context& c);
void F_00219A10(lifted::Context& c);
void F_00219C90(lifted::Context& c);
void F_00219D40(lifted::Context& c);
void F_00219F10(lifted::Context& c);

namespace {

// ------------------------------------------------------------ the camera

/* 0x001E2100(camera, &window): RenderWare's RwCameraSetViewWindow. */
void SetViewWindow(lifted::Context& c) {
    const float wider = ps2host::PictureAspect() / (4.0f / 3.0f);
    const u32 window = u32(c.r[5]);
    const u32 x_bits = generated::ReadU32(window), y_bits = generated::ReadU32(window + 4);
    float x, y;
    std::memcpy(&x, &x_bits, 4);
    std::memcpy(&y, &y_bits, 4);
    // A 4:3 window alone (a camera drawing for the screen), the game's own
    // copy put back after.
    if (std::fabs(wider - 1.0f) < 0.001f || !(x > 0.0f) || std::fabs(y / x - 0.75f) > 0.002f) {
        F_001E2100(c);
        return;
    }
    const float wide = x * wider;
    u32 wide_bits;
    std::memcpy(&wide_bits, &wide, 4);
    generated::Write32(window, wide_bits);
    F_001E2100(c);
    generated::Write32(window, x_bits);
}

// ------------------------------------------------------------ elements and anchors

enum class Anchor { kCentre, kLeft, kRight, kStretch };

struct ElementId {
    u32 context = 0, object = 0;
    char cell[3] = {'?', '?', 0};        // row T M B, column L C R
    bool operator<(const ElementId& o) const {
        if (cell[0] != o.cell[0]) return std::string("TMB").find(cell[0]) < std::string("TMB").find(o.cell[0]);
        if (cell[1] != o.cell[1]) return std::string("LCR").find(cell[1]) < std::string("LCR").find(o.cell[1]);
        if (object != o.object) return object < o.object;
        return context < o.context;
    }
    bool operator==(const ElementId& o) const {
        return context == o.context && object == o.object && cell[0] == o.cell[0] && cell[1] == o.cell[1];
    }
    bool known() const { return object != 0; }
    std::string Key() const {
        char key[48];
        std::snprintf(key, sizeof key, "hud.%06X.%06X.%s", unsigned(context), unsigned(object), cell);
        return key;
    }
};

/* Explicit in-race HUD anchors (settings take precedence). */
const std::pair<const char*, Anchor> kDefaultAnchors[] = {
    // The race (PAL SLES-525.84):
    {"hud.1A0780.1A5730.BL", Anchor::kLeft},
    {"hud.1A0780.1A6280.BC", Anchor::kLeft},
    {"hud.1A0780.1A6280.BL", Anchor::kLeft},
    {"hud.1A0780.1D5FA0.TL", Anchor::kLeft},
    {"hud.1A0780.1D5FA0.TR", Anchor::kRight},
    {"hud.1A0780.381380.BR", Anchor::kRight},  // speedometer
    {"hud.1A0780.381E00.TL", Anchor::kLeft},   // position
    {"hud.1A0780.381E00.TR", Anchor::kRight},  // lap
    {"hud.1A0780.3CDC80.BR", Anchor::kRight},  // panel
    {"hud.1A0780.3CDC80.TL", Anchor::kLeft},
    {"hud.1A0780.3CDC80.TR", Anchor::kRight},
    {"hud.1A0780.3D6F80.TR", Anchor::kRight},
    {"hud.1D75B0.1A7AF0.BC", Anchor::kLeft},
    {"hud.1D75B0.1A7AF0.BL", Anchor::kLeft},
    {"hud.1D75B0.1A8170.BL", Anchor::kLeft},   // boost bar
    // NTSC aliases:
    {"hud.1A0790.1A5740.BL", Anchor::kLeft},
    {"hud.1A0790.1A6290.BC", Anchor::kLeft},
    {"hud.1A0790.1A6290.BL", Anchor::kLeft},
    {"hud.1A0790.1D5FB0.TL", Anchor::kLeft},
    {"hud.1A0790.1D5FB0.TR", Anchor::kRight},
    {"hud.1A0790.381030.BR", Anchor::kRight},
    {"hud.1A0790.381AB0.TL", Anchor::kLeft},
    {"hud.1A0790.381AB0.TR", Anchor::kRight},
    {"hud.1A0790.3CD920.BR", Anchor::kRight},
    {"hud.1A0790.3CD920.TL", Anchor::kLeft},
    {"hud.1A0790.3CD920.TR", Anchor::kRight},
    {"hud.1A0790.3D6C30.TR", Anchor::kRight},
    {"hud.1D75C0.1A7B00.BC", Anchor::kLeft},
    {"hud.1D75C0.1A7B00.BL", Anchor::kLeft},
    {"hud.1D75C0.1A8180.BL", Anchor::kLeft},
};

const char* AnchorName(Anchor a) {
    switch (a) {
        case Anchor::kLeft: return "left";
        case Anchor::kRight: return "right";
        case Anchor::kStretch: return "stretch";
        default: return "centre";
    }
}

std::map<std::string, Anchor> g_anchors;

Anchor AnchorOf(const ElementId& id) {
    if (!id.known()) return Anchor::kStretch;
    const std::string key = id.Key();
    auto found = g_anchors.find(key);
    if (found != g_anchors.end()) return found->second;
    const std::string set = ps2host::Setting(key, "");
    if (set == "left") return g_anchors[key] = Anchor::kLeft;
    if (set == "right") return g_anchors[key] = Anchor::kRight;
    if (set == "centre") return g_anchors[key] = Anchor::kCentre;
    if (set == "stretch") return g_anchors[key] = Anchor::kStretch;

    for (const auto& [name, value] : kDefaultAnchors) {
        if (key == name) return g_anchors[key] = value;
    }

    // ONLY when racing (0x004EB760 != 0), anchor the active HUD elements to the screen corners:
    const bool racing = generated::ReadU8(0x004EB760u) != 0;
    if (racing && (id.context == 0x001A0780u || id.context == 0x001D75B0u ||
                   id.context == 0x001A0790u || id.context == 0x001D75C0u)) {
        // Boost bar and related panels -> Left
        if (id.object == 0x001A8170u || id.object == 0x001A8180u ||
            id.object == 0x001A7AF0u || id.object == 0x001A7B00u ||
            id.object == 0x001A5730u || id.object == 0x001A5740u ||
            id.object == 0x001A6280u || id.object == 0x001A6290u) {
            return g_anchors[key] = Anchor::kLeft;
        }
        // Speedometer, tachometer, lap times -> Right
        if (id.object == 0x00381380u || id.object == 0x00381030u ||
            id.object == 0x003D6F80u || id.object == 0x003D6C30u) {
            return g_anchors[key] = Anchor::kRight;
        }
        // Lap / position / corner panels by side:
        if (id.object == 0x00381E00u || id.object == 0x00381AB0u ||
            id.object == 0x003CDC80u || id.object == 0x003CD920u ||
            id.object == 0x001D5FA0u || id.object == 0x001D5FB0u) {
            if (id.cell[1] == 'L') return g_anchors[key] = Anchor::kLeft;
            if (id.cell[1] == 'R') return g_anchors[key] = Anchor::kRight;
        }
    }

    // For everything else (menus, options, pause screen, dialogs, title screen, videos):
    // Anchor::kStretch stretches across the entire 16:9 window to completely fill the screen
    // without black bars or borders.
    return g_anchors[key] = Anchor::kStretch;
}

/* 4:3 over the picture's shape: 1 for a 4:3 picture. */
float Squeeze() { return std::min(1.0f, (4.0f / 3.0f) / ps2host::PictureAspect()); }

/* x (about `centre`, `half` its distance to the picture's edges) brought in
   by k towards the middle, or the edge it is anchored to; stretched, as the
   game drew it - across the wider picture. */
float Place(float x, float centre, float half, float k, Anchor anchor) {
    if (anchor == Anchor::kStretch) return x;
    const float a = anchor == Anchor::kLeft ? centre - half : anchor == Anchor::kRight ? centre + half : centre;
    return a + (x - a) * k;
}

/* The cell of a point of the 4:3 picture, (0, 0) its top left, 640 x 448. */
void CellOf(float x, float y, char cell[3]) {
    cell[0] = y < 448.0f / 3 ? 'T' : y < 896.0f / 3 ? 'M' : 'B';
    cell[1] = x < 640.0f / 3 ? 'L' : x < 1280.0f / 3 ? 'C' : 'R';
    cell[2] = 0;
}

void Caller(ElementId* id) {
    const std::vector<u32>& calls = ps2host::CurrentCalls();
    const std::size_t n = calls.size();
    id->object = n >= 2 ? calls[n - 2] : 0;
    id->context = n >= 4 ? calls[n - 4] : 0;
}

// ------------------------------------------------------------ the HUD editor

struct Editor {
    bool on = false;
    ElementId chosen;
    std::map<ElementId, std::chrono::steady_clock::time_point> seen;
    bool keys[8] = {};
    std::size_t shown = ~std::size_t(0);
    bool paused = false;
};
Editor g_editor;

const char* ElementName(const ElementId& id) {
    switch (id.object) {
        case 0x003CDC80: case 0x003CD920: return "panel";
        case 0x00381E00: case 0x00381AB0: return id.cell[1] == 'L' ? "position" : id.cell[1] == 'R' ? "lap" : "counter";
        case 0x00381380: case 0x00381030: return "speed";
        case 0x001A8170: case 0x001A8180: return "boost bar";
        default: return "";
    }
}

const char* CellName(const char cell[3]) {
    static const char* const kRows[] = {"top", "middle", "bottom"};
    static const char* const kColumns[] = {"left", "centre", "right"};
    static char name[24];
    const std::size_t row = std::string("TMB").find(cell[0]), column = std::string("LCR").find(cell[1]);
    std::snprintf(name, sizeof name, "%s %s", row < 3 ? kRows[row] : "?", column < 3 ? kColumns[column] : "?");
    return name;
}

void Seen(const ElementId& id) {
    if (g_editor.on && id.known()) g_editor.seen[id] = std::chrono::steady_clock::now();
}

bool Hidden(const ElementId& id) {
    if (!g_editor.on || !(id == g_editor.chosen)) return false;
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now().time_since_epoch()).count();
    return (ms / 250) % 2 == 1;
}

void ShowStatus() {
    if (!g_editor.on) {
        ps2host::SetWindowOverlay({});
        ps2host::SetWindowStatus("");
        return;
    }
    std::vector<std::string> lines = {g_editor.paused ? "HUD EDITOR  (F10 closes)   - PAUSED (P resumes)"
                                                      : "HUD EDITOR  (F10 closes)   P pauses the race",
                                      "PgUp/PgDn choose   Home left   Insert centre   End right   Delete stretch", ""};
    std::size_t index = 0, count = 0;
    for (const auto& [id, when] : g_editor.seen) {
        ++count;
        const bool chosen = id == g_editor.chosen;
        if (chosen) index = count;
        char text[160];
        std::snprintf(text, sizeof text, "%s %-13s %-10s %06X/%06X   %s", chosen ? ">" : " ", CellName(id.cell),
                      ElementName(id), unsigned(id.object), unsigned(id.context), AnchorName(AnchorOf(id)));
        lines.push_back(text);
    }
    if (!count) lines.push_back("  (no HUD drawn)");
    g_editor.shown = count;
    char title[96];
    std::snprintf(title, sizeof title, "HUD EDIT %zu/%zu", index, count);
    ps2host::SetWindowStatus(title);
    ps2host::SetWindowOverlay(lines);
}

bool Press(int slot, int vk, int vk2 = 0) {
    const bool down = ps2host::WindowFocused() && (ps2host::HostKeyDown(vk) || (vk2 && ps2host::HostKeyDown(vk2)));
    const bool pressed = down && !g_editor.keys[slot];
    g_editor.keys[slot] = down;
    return pressed;
}

void EditorInput() {
    constexpr int kF10 = 0x79, kPageUp = 0x21, kPageDown = 0x22, kHome = 0x24, kEnd = 0x23, kInsert = 0x2D;
    constexpr int kPad4 = 0x64, kPad5 = 0x65, kPad6 = 0x66, kDelete = 0x2E, kPad8 = 0x68, kP = 'P';
    if (Press(0, kF10)) {
        g_editor.on = !g_editor.on && Squeeze() < 1.0f;
        g_editor.paused = false;
        g_editor.seen.clear();
        g_editor.chosen = ElementId{};
        g_editor.shown = ~std::size_t(0);
        ShowStatus();
    }
    if (!g_editor.on) return;
    const auto now = std::chrono::steady_clock::now();
    for (auto it = g_editor.seen.begin(); it != g_editor.seen.end();)
        it = now - it->second > std::chrono::milliseconds(500) ? g_editor.seen.erase(it) : std::next(it);
    if (g_editor.seen.size() != g_editor.shown) ShowStatus();
    std::vector<ElementId> list;
    for (const auto& [id, when] : g_editor.seen) list.push_back(id);
    const bool next = Press(1, kPageDown), previous = Press(2, kPageUp);
    if ((next || previous) && !list.empty()) {
        std::size_t at = list.size();
        for (std::size_t i = 0; i < list.size(); ++i)
            if (list[i] == g_editor.chosen) at = i;
        if (at == list.size()) at = next ? list.size() - 1 : 0;
        g_editor.chosen = list[(at + (next ? 1 : list.size() - 1)) % list.size()];
        ShowStatus();
    }
    Anchor set = Anchor::kCentre;
    bool setting = false;
    if (Press(3, kHome, kPad4)) setting = true, set = Anchor::kLeft;
    if (Press(4, kInsert, kPad5)) setting = true, set = Anchor::kCentre;
    if (Press(5, kEnd, kPad6)) setting = true, set = Anchor::kRight;
    if (Press(6, kDelete, kPad8)) setting = true, set = Anchor::kStretch;
    if (Press(7, kP)) {
        g_editor.paused = !g_editor.paused;
        ShowStatus();
    }
    if (setting && g_editor.chosen.known()) {
        const std::string key = g_editor.chosen.Key();
        ps2host::SetSetting(key, AnchorName(set));
        g_anchors[key] = set;
        ShowStatus();
    }
}

// ------------------------------------------------------------ the 2D batch

struct Batch2D {
    u32 fbw = 10;
    int centre = 0x8000;
    int middle = 0x7200 + 224 * 16;
    u32 prim = 6;
    u32 starts = 0;
};
Batch2D g_batch;

struct Piece {
    u32 begin, end;
    ElementId id;
};
std::vector<Piece> g_pieces;
std::vector<Piece> g_building;

template <void (*Build)(lifted::Context&)>
void Builder(lifted::Context& c) {
    const u32 gp = u32(c.r[28]);
    Piece piece{u32(generated::ReadU32(gp - 0x5D98u)), 0, {}};
    Caller(&piece.id);
    g_building.push_back(piece);
    Build(c);
    piece = g_building.back();
    g_building.pop_back();
    const u32 end = generated::ReadU32(gp - 0x5D98u);
    if (end > piece.begin) g_pieces.push_back({piece.begin, end, piece.id});
}

bool IsXyz(u32 reg) { return reg == 0x4 || reg == 0x5 || reg == 0xC || reg == 0xD; }
u64 Read64(u32 at) { return generated::ReadU32(at) | u64(generated::ReadU32(at + 4)) << 32; }

struct Vertex2D {
    u32 at, prim, start;
    bool picture;
    int centre, middle;
};

void SqueezeScissor(u32 at, float k) {
    const u64 scissor = Read64(at);
    const int x0 = int(scissor & 0x7FF), x1 = int((scissor >> 16) & 0x7FF);
    if (x0 <= 8 && x1 >= 631) return;
    const auto in = [k](int x) { return std::clamp(int(320.0f + float(x - 320) * k + 0.5f), 0, 2047); };
    const u64 squeezed = (scissor & ~0x07FF07FFull) | u64(in(x0)) | u64(in(x1)) << 16;
    generated::Write32(at, u32(squeezed));
}

u32 TagVertices(u32 at, u32 end, std::vector<Vertex2D>* vertices, float k) {
    const u64 tag = Read64(at), regs = Read64(at + 8);
    at += 16;
    const u32 nloop = u32(tag & 0x7FFF), flg = u32(tag >> 58) & 3;
    const auto set_prim = [](u32 value) {
        g_batch.prim = value & 7;
        ++g_batch.starts;
    };
    if ((tag >> 46) & 1) set_prim(u32(tag >> 47));
    u32 nreg = u32(tag >> 60) & 0xF;
    if (!nreg) nreg = 16;
    if (flg == 0) {
        for (u32 l = 0; l < nloop && at + 16 <= end; ++l)
            for (u32 r = 0; r < nreg && at + 16 <= end; ++r, at += 16) {
                const u32 reg = u32(regs >> (4 * r)) & 0xF;
                if (IsXyz(reg)) {
                    vertices->push_back({at, g_batch.prim, g_batch.starts, g_batch.fbw == 10, g_batch.centre, g_batch.middle});
                } else if (reg == 0x0) {
                    set_prim(generated::ReadU32(at));
                } else if (reg == 0xE) {
                    const u32 address = generated::ReadU8(at + 8);
                    if (IsXyz(address)) {
                        vertices->push_back({at, g_batch.prim, g_batch.starts, g_batch.fbw == 10, g_batch.centre, g_batch.middle});
                    } else if (address == 0x00) {
                        set_prim(generated::ReadU32(at));
                    } else if (address == 0x4C) {
                        g_batch.fbw = u32(Read64(at) >> 16) & 0x3F;
                    } else if (address == 0x18) {
                        g_batch.centre = int(Read64(at) & 0xFFFF) + 320 * 16;
                        g_batch.middle = int((Read64(at) >> 32) & 0xFFFF) + 224 * 16;
                    } else if (address == 0x40 && false) {
                        // GS Scissor register: do not squeeze scissors so 16:9 menus/videos/HUD aren't clipped.
                        SqueezeScissor(at, k);
                    }
                }
            }
        return at;
    }
    if (flg == 1) {
        const u32 words = nloop * nreg;
        for (u32 i = 0; i < words; ++i) {
            const u32 reg = u32(regs >> (4 * (i % nreg))) & 0xF;
            if (IsXyz(reg)) vertices->push_back({at + 8 * i, g_batch.prim, g_batch.starts, g_batch.fbw == 10, g_batch.centre, g_batch.middle});
            else if (reg == 0x0) set_prim(generated::ReadU32(at + 8 * i));
        }
        return at + (words + 1) / 2 * 16;
    }
    return at + nloop * 16;
}

u32 PrimitiveVertices(u32 prim) {
    switch (prim) {
        case 0: return 1;
        case 1: case 6: return 2;
        case 3: return 3;
        default: return 0;
    }
}

void PlaceVertices(const std::vector<Vertex2D>& vertices, float k) {
    const int half = 312 * 16;
    std::vector<int> owner(vertices.size(), -1);
    std::vector<float> lo_x(g_pieces.size(), 1e9f), hi_x(g_pieces.size(), -1e9f);
    std::vector<float> lo_y(g_pieces.size(), 1e9f), hi_y(g_pieces.size(), -1e9f);
    for (std::size_t v = 0; v < vertices.size(); ++v) {
        const u32 at = vertices[v].at;
        auto it = std::upper_bound(g_pieces.begin(), g_pieces.end(), at,
                                   [](u32 a, const Piece& p) { return a < p.begin; });
        if (it == g_pieces.begin()) continue;
        --it;
        if (at >= it->end) continue;
        const int p = int(it - g_pieces.begin());
        owner[v] = p;
        const u32 word = generated::ReadU32(at);
        const float x = float(int(word & 0xFFFF) - vertices[v].centre) / 16.0f + 320.0f;
        const float y = float(int(word >> 16) - vertices[v].middle) / 16.0f + 224.0f;
        lo_x[p] = std::min(lo_x[p], x);
        hi_x[p] = std::max(hi_x[p], x);
        lo_y[p] = std::min(lo_y[p], y);
        hi_y[p] = std::max(hi_y[p], y);
    }
    std::vector<Anchor> anchors(g_pieces.size(), Anchor::kStretch);
    std::vector<bool> hidden(g_pieces.size(), false);
    for (std::size_t p = 0; p < g_pieces.size(); ++p) {
        if (lo_x[p] > hi_x[p]) continue;
        CellOf((lo_x[p] + hi_x[p]) / 2, (lo_y[p] + hi_y[p]) / 2, g_pieces[p].id.cell);
        anchors[p] = AnchorOf(g_pieces[p].id);
        hidden[p] = Hidden(g_pieces[p].id);
        Seen(g_pieces[p].id);
    }
    for (std::size_t i = 0; i < vertices.size();) {
        const u32 size = PrimitiveVertices(vertices[i].prim);
        std::size_t j = i + 1;
        if (size == 0) {
            while (j < vertices.size() && vertices[j].start == vertices[i].start) ++j;
        } else {
            j = std::min(vertices.size(), i + size);
        }
        int x0 = 0x10000, x1 = -1;
        for (std::size_t v = i; v < j; ++v) {
            const int x = int(generated::ReadU32(vertices[v].at) & 0xFFFF);
            x0 = std::min(x0, x);
            x1 = std::max(x1, x);
        }
        const int centre = vertices[i].centre;
        const bool across = x0 <= centre - half && x1 >= centre + half;
        const int p = owner[i];
        const Anchor anchor = p >= 0 ? anchors[p] : Anchor::kStretch;
        if (anchor == Anchor::kStretch) {
            i = j;
            continue;
        }
        const bool hide = p >= 0 && hidden[p];
        for (std::size_t v = i; v < j; ++v) {
            const u32 word = generated::ReadU32(vertices[v].at);
            if (hide) {
                generated::Write32(vertices[v].at, generated::ReadU32(vertices[i].at));
                continue;
            }
            const int x = int(word & 0xFFFF);
            if (across && (x <= centre - half || x >= centre + half)) continue;
            const float placed = Place(float(x), float(centre), 320.0f * 16, k, anchor);
            const int at_x = int(std::lround(placed));
            generated::Write32(vertices[v].at, (word & ~0xFFFFu) | (u32(at_x) & 0xFFFFu));
        }
        i = j;
    }
}

/* 0x0021A4E0 in PAL SLES-525.84: the batch placed, then flushed. */
void Flush2D(lifted::Context& c) {
    EditorInput();
    const u32 gp = u32(c.r[28]);
    u32 at = generated::ReadU32(gp - 0x6D20u);
    const u32 end = generated::ReadU32(gp - 0x5D98u);
    const float k = Squeeze();
    for (const Piece& b : g_building)
        if (end > b.begin) g_pieces.push_back({b.begin, end, b.id});
    std::sort(g_pieces.begin(), g_pieces.end(), [](const Piece& x, const Piece& y) { return x.begin < y.begin; });
    std::vector<Vertex2D> all;
    while (at + 16 <= end && end - at < 0x10000) at = TagVertices(at, end, &all, k);
    if (k < 1.0f) {
        std::vector<Vertex2D> picture;
        for (const Vertex2D& v : all)
            if (v.picture) picture.push_back(v);
        PlaceVertices(picture, k);
    }
    g_pieces.clear();
    F_0021A4E0(c);
    for (Piece& b : g_building) b.begin = generated::ReadU32(gp - 0x5D98u);
}

// ------------------------------------------------------------ the frontend's sprites

/* 0x00364B00 in PAL SLES-525.84: draws frontend objects' sprites and 2D rivals' tags. */
void Sprites2D(lifted::Context& c) {
    const float k = Squeeze();
    const u32 vertices = u32(c.r[4]), count = u32(c.r[5]);
    if (k >= 1.0f || count == 0 || count > 4096) {
        F_00364B00(c);
        return;
    }
    ElementId id;
    Caller(&id);
    const Anchor anchor = AnchorOf(id);
    // Frontend sprites stretch across the screen by default:
    if (anchor == Anchor::kStretch) {
        F_00364B00(c);
        return;
    }
    std::vector<u32> saved(2 * count);
    float lo_x = 1e9f, hi_x = -1e9f, lo_y = 1e9f, hi_y = -1e9f;
    for (u32 i = 0; i < count; ++i) {
        saved[2 * i] = generated::ReadU32(vertices + 32 * i);
        saved[2 * i + 1] = generated::ReadU32(vertices + 32 * i + 4);
        float x, y;
        std::memcpy(&x, &saved[2 * i], 4);
        std::memcpy(&y, &saved[2 * i + 1], 4);
        lo_x = std::min(lo_x, x);
        hi_x = std::max(hi_x, x);
        lo_y = std::min(lo_y, y);
        hi_y = std::max(hi_y, y);
    }
    CellOf((lo_x + hi_x) / 2 + 320.0f, (lo_y + hi_y) / 2 + 224.0f, id.cell);
    const bool hide = Hidden(id);
    Seen(id);
    for (u32 i = 0; i < count; ++i) {
        float x;
        std::memcpy(&x, &saved[2 * i], 4);
        x = hide ? 0.0f : Place(x, 0.0f, 320.0f, k, anchor);
        u32 bits;
        std::memcpy(&bits, &x, 4);
        generated::Write32(vertices + 32 * i, bits);
        if (hide) generated::Write32(vertices + 32 * i + 4, saved[1]);
    }
    F_00364B00(c);
    for (u32 i = 0; i < count; ++i) {
        generated::Write32(vertices + 32 * i, saved[2 * i]);
        generated::Write32(vertices + 32 * i + 4, saved[2 * i + 1]);
    }
}

unsigned long long g_race_world_calls = 0;

void RaceWorld(lifted::Context& c) {
    ++g_race_world_calls;
    if (g_editor.on && g_editor.paused) return;
    F_0019A550(c);
}

}  // namespace

float FrontendPlace(u32 object, float x, float cx, float cy) {
    ElementId id;
    id.context = 0x0030D740u;
    id.object = object;
    CellOf(cx, cy, id.cell);
    return Place(x, 320.0f, 320.0f, Squeeze(), AnchorOf(id));
}

unsigned long long RaceWorldCalls() { return g_race_world_calls; }

void RegisterWidescreen() {
    ps2host::Override(0x0019A550u, RaceWorld, "the race's world each frame: held still by the HUD editor (P)");
    ps2host::Override(0x001E2100u, SetViewWindow, "RwCameraSetViewWindow: wider for ASPECT RATIO (PC SETTINGS)");
    ps2host::Override(0x0021A4E0u, Flush2D, "a 2D batch's flush: the HUD placed in a wider picture");
    ps2host::Override(0x00364B00u, Sprites2D, "the frontend's sprites: placed in a wider picture");
    ps2host::Override(0x002178D0u, Builder<F_002178D0>, "a 2D builder: its piece");
    ps2host::Override(0x00217A10u, Builder<F_00217A10>, "a 2D builder: its piece");
    ps2host::Override(0x00217C30u, Builder<F_00217C30>, "a 2D builder: its piece");
    ps2host::Override(0x00217D30u, Builder<F_00217D30>, "a 2D builder: its piece");
    ps2host::Override(0x00217E60u, Builder<F_00217E60>, "a 2D builder: its piece");
    ps2host::Override(0x00217FB0u, Builder<F_00217FB0>, "a 2D builder: its piece");
    ps2host::Override(0x002183F0u, Builder<F_002183F0>, "a 2D builder: its piece");
    ps2host::Override(0x002187A0u, Builder<F_002187A0>, "a 2D builder: its piece");
    ps2host::Override(0x00218CF0u, Builder<F_00218CF0>, "a 2D builder: its piece");
    ps2host::Override(0x00219140u, Builder<F_00219140>, "a 2D builder: its piece");
    ps2host::Override(0x00219520u, Builder<F_00219520>, "a 2D builder: its piece");
    ps2host::Override(0x00219820u, Builder<F_00219820>, "a 2D builder: its piece");
    ps2host::Override(0x00219A10u, Builder<F_00219A10>, "a 2D builder: its piece");
    ps2host::Override(0x00219C90u, Builder<F_00219C90>, "a 2D builder: its piece");
    ps2host::Override(0x00219D40u, Builder<F_00219D40>, "a 2D builder: its piece");
    ps2host::Override(0x00219F10u, Builder<F_00219F10>, "a 2D builder: its piece");
}
