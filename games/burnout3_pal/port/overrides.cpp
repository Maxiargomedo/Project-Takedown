#include "controls.h"
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
#include <initializer_list>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <chrono>

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
void F_00271A70(lifted::Context& c);  // String lookup
void F_00318160(lifted::Context& c);  // Main menu frame update
void F_00318240(lifted::Context& c);  // Main menu input
void F_003215F0(lifted::Context& c);  // Menu list setup
void F_0031C3F0(lifted::Context& c);  // Menu ticker descriptions setup
void F_00319400(lifted::Context& c);  // Selector move up
void F_003193D0(lifted::Context& c);  // Selector move down
void UpdateWidescreen();

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

// ------------------------------------------------------------ IN-GAME PC SETTINGS & QUIT GAME
//
// The main menu gets two integrated entries with the native game font:
// - Row 3 ("ONLINE", whose servers are long gone) is replaced by PC SETTINGS (AJUSTES PC).
// - Row 5 (after DRIVER DETAILS) is QUIT GAME (SALIR DEL JUEGO), confirming in a native YES/NO box.
// The external F9 menu remains fully available over the entire game.

struct Donor { u32 id, chars; };
const Donor kDonors[] = {{108, 122}, {109, 135}, {110, 110}, {182, 99}, {1187, 143}, {1108, 75}, {3161, 82},
                         {2868, 68}, {2870, 68}, {1206, 68}, {112, 71}, {113, 67}, {116, 88}};

class GuestPool {
public:
    void Begin(lifted::Context& c) {
        if (ready_) return;
        u64 saved[32];
        std::memcpy(saved, c.r, sizeof saved);
        for (const Donor& d : kDonors) {
            c.r[4] = 0x0051BFC4u; // Holder in PAL
            c.r[5] = d.id;
            F_00271A70(c);
            if (u32(c.r[2])) regions_.push_back({u32(c.r[2]), 2 * d.chars});
        }
        std::memcpy(c.r, saved, sizeof saved);
        ready_ = true;
    }
    u32 Take(u32 bytes, u32 align = 2) {
        for (auto& [at, left] : regions_) {
            const u32 start = (at + align - 1) & ~(align - 1);
            if (start + bytes <= at + left) {
                left -= start + bytes - at;
                at = start + bytes;
                return start;
            }
        }
        return 0;
    }
private:
    bool ready_ = false;
    std::vector<std::pair<u32, u32>> regions_;
};
GuestPool g_pool;

struct GuestText {
    u32 at = 0, chars = 0;
    void Make(u32 room) { chars = room; at = g_pool.Take(2 * (room + 1)); }
    void Set(const std::string& text) {
        if (!at) return;
        u32 out = 0;
        for (std::size_t i = 0; i < text.size() && out < chars; ) {
            u8 b0 = u8(text[i++]);
            u16 ch = b0;
            if ((b0 & 0xE0) == 0xC0 && i < text.size()) {
                u8 b1 = u8(text[i++]);
                ch = ((b0 & 0x1F) << 6) | (b1 & 0x3F);
            }
            generated::Write16(at + 2 * out++, ch);
        }
        generated::Write16(at + 2 * out, 0);
    }
};

constexpr u32 kMainMenuOnline = 0xB8;       // String ID for "ONLINE"
constexpr u32 kOnlineHelp = 0xEC;           // String ID for ONLINE's help
constexpr u32 kQuestion = 0x7F02;           // Our string ID for YES/NO prompt
constexpr u32 kEntries = 6;                 // 5 game entries + QUIT GAME
constexpr u32 kPcSettingsEntry = 3;
constexpr u32 kQuitEntry = 5;

constexpr u32 kList = 0x01E89AC4u;         // Main menu list in PAL
constexpr u32 kLegend = 0x01E89D80u;       // Button legend in PAL
constexpr u32 kMessageBox = 0x01E89E68u;   // Message box in PAL
constexpr u32 kFlow = 0x01E907C0u;         // Frontend flow / fade in PAL
constexpr u32 kPoolLayer = 0x01E90960u;    // Frontend pool in PAL
constexpr u32 kYes = 5, kNo = 6;
constexpr u32 kRowChars = 38, kHelpChars = 70;

enum class Page { kMainMenu, kPcSettings, kVideo, kGraphics, kControls, kSavegames, kBindings };

struct PageInfo {
    Page page;
    const char* name;
    const char* help;
    const char* name_es;
    const char* help_es;
};

const PageInfo kPages[] = {
    {Page::kVideo, "VIDEO", "THE WINDOW, RESOLUTION AND V-SYNC",
                   "VIDEO", "PANTALLA, RESOLUCION Y SINCRONIZACION"},
    {Page::kGraphics, "GRAPHICS", "THE GAME'S OWN EFFECTS",
                      "GRAFICOS", "EFECTOS GRAFICOS DEL JUEGO"},
    {Page::kControls, "CONTROLS", "BUTTONS, KEYS, PROMPTS AND VIBRATION",
                      "CONTROLES", "ASIGNACION DE BOTONES, TECLAS Y VIBRACION"},
    {Page::kSavegames, "SAVEGAMES", "SAVED PROFILES AND BACKUPS",
                       "PARTIDAS", "GESTION DE PERFILES Y GUARDADO"},
};
constexpr u32 kPageCount = sizeof kPages / sizeof kPages[0];

struct BindingsPage {
    controls::Device device;
    controls::Context context;
    const char* name;
    const char* help;
    const char* name_es;
    const char* help_es;
};
const BindingsPage kBindingsPages[] = {
    {controls::Device::kKeyboard, controls::Context::kRacing, "MAP KEYBOARD (RACING)", "THE KEYS YOU RACE WITH",
                                                               "MAPEAR TECLADO (CARRERA)", "ASIGNAR TECLAS PARA CONDUCIR Y TURBO"},
    {controls::Device::kKeyboard, controls::Context::kMenus,  "MAP KEYBOARD (MENUS)",  "THE KEYS FOR THE MENUS AND PAUSE SCREEN",
                                                               "MAPEAR TECLADO (MENUS)",   "ASIGNAR TECLAS PARA MENUS Y PAUSA"},
};
constexpr int kBindingsPageCount = sizeof kBindingsPages / sizeof kBindingsPages[0];

int Fullscreen() { return ps2host::WindowFullscreen() ? 1 : 0; }
void SetFullscreen(int on) { ps2host::SetWindowFullscreen(on != 0); }
int Vsync() { return ps2host::WindowVsync() ? 1 : 0; }
void SetVsync(int on) { ps2host::SetWindowVsync(on != 0); }

template <std::size_t N>
int Among(const char* key, const char* const (&values)[N]) {
    const std::string now = ps2host::Setting(key, values[0]);
    for (std::size_t i = 0; i < N; ++i)
        if (now == values[i]) return int(i);
    return 0;
}

const char* const kResolutions[] = {"original", "2x", "4x", "display"};
int Resolution() { return Among("resolution", kResolutions); }
void SetResolution(int which) { ps2host::SetSetting("resolution", kResolutions[which]); }

const char* const kAspects[] = {"4:3", "16:9", "auto"};
int Aspect() { return Among("aspect", kAspects); }
void SetAspect(int which) { ps2host::SetWindowAspect(kAspects[which]); ::UpdateWidescreen(); }

const char* const kAntiAliasings[] = {"off", "fxaa", "smaa", "ssaa2"};
int AntiAliasing() { return Among("antialiasing", kAntiAliasings); }
void SetAntiAliasing(int which) { ps2host::SetWindowAntiAliasing(kAntiAliasings[which]); }

int Blur() { return ps2host::Setting("blur", "original") == "off" ? 1 : 0; }
void SetBlur(int off) { ps2host::SetSetting("blur", off ? "off" : "original"); }

const char* const kTexDistances[] = {"original", "enhanced", "max"};
int TexDistance() { return Among("texture_distance", kTexDistances); }
void SetTexDistance(int which) { ps2host::SetSetting("texture_distance", kTexDistances[which]); }

const char* const kTexFilterings[] = {"original", "bilinear"};
int TexFiltering() { return Among("texture_filtering", kTexFilterings); }
void SetTexFiltering(int which) { ps2host::SetSetting("texture_filtering", kTexFilterings[which]); }

std::string InputDeviceVal() {
    auto controllers = ps2host::WindowControllers();
    if (controllers.empty()) {
        return "TECLADO PC";
    }
    std::string name = controllers[0].name;
    for (char& c : name) c = (char)std::toupper((unsigned char)c);
    if (name.find("XBOX") != std::string::npos || name.find("X-BOX") != std::string::npos) return "MANDO XBOX";
    if (name.find("DUALSENSE") != std::string::npos || name.find("WIRELESS CONTROLLER") != std::string::npos) return "SONY DUALSENSE";
    if (name.find("DUALSHOCK") != std::string::npos || name.find("PS4") != std::string::npos) return "SONY DUALSHOCK";
    if (name.find("NINTENDO") != std::string::npos || name.find("SWITCH") != std::string::npos) return "NINTENDO PAD";
    if (name.size() > 16) name = name.substr(0, 16);
    return name;
}
int InputDeviceNow() { return 0; }
void SetInputDevice(int) {}

int ControlModeNow() {
    return ps2host::SettingOn("modern_controls", false) ? 1 : 0;
}
void SetControlMode(int which) {
    const bool modern = (which == 1);
    ps2host::SetSettingOn("modern_controls", modern);
    controls::SetRacingLayout(modern ? controls::Layout::kModern : controls::Layout::kClassic);
}

int VibrationNow() {
    return ps2host::SettingOn("vibration", true) ? 1 : 0;
}
void SetVibrationOpt(int which) {
    const bool on = (which != 0);
    ps2host::SetSettingOn("vibration", on);
    generated::Write8(0x004F5640u + 0x46u, on ? 0u : 1u);
}

const char* const kPromptStyles[] = {"auto", "playstation", "xbox"};
int ButtonPrompts() { return Among("button_prompts", kPromptStyles); }
void SetButtonPrompts(int which) { ps2host::SetSetting("button_prompts", kPromptStyles[which]); }

void SetVibration(lifted::Context& c) {
    const u32 obj = u32(c.r[4]);
    const u32 enabled = u32(c.r[5]);
    const bool allow = ps2host::SettingOn("vibration", true);
    // +0x46 is bVibrationDisabled (0 = allowed, 1 = disabled)
    generated::Write8(obj + 0x46u, (enabled != 0 && allow) ? 0u : 1u);
}

int SkipIntro() { return ps2host::SettingOn("skip_intro", false) ? 1 : 0; }
void SetSkipIntro(int on) { ps2host::SetSettingOn("skip_intro", on != 0); }

namespace fs = std::filesystem;

struct ProfileManager {
    std::string active_profile = "MAKISHI";
    std::vector<std::string> profiles;
    int selected_idx = 0;
    int load_status = 0;
    int backup_status = 0;

    std::string ReadSaveName(const fs::path& file) {
        std::ifstream f(file, std::ios::binary);
        if (!f) return "";
        f.seekg(0x0234, std::ios::beg);
        char buf[64] = {};
        f.read(buf, sizeof(buf));
        std::string name;
        for (size_t i = 0; i + 1 < sizeof(buf); i += 2) {
            char ch = buf[i];
            if (ch == 0) break;
            if (ch >= 32 && ch <= 126) name += (char)std::toupper((unsigned char)ch);
        }
        return name;
    }

    void Refresh() {
        profiles.clear();
        const std::string& card_dir = ps2host::mc2::CardDirectory();
        if (card_dir.empty()) return;
        fs::path base(card_dir);
        fs::path active_file = base / "BESLES-52584" / "BESLES-52584";
        std::string act = ReadSaveName(active_file);
        active_profile = act.empty() ? (fs::exists(active_file) ? "MAKISHI" : "VACIO") : act;

        fs::path perfiles_dir = base / "perfiles";
        std::error_code ec;
        if (!fs::exists(perfiles_dir, ec)) fs::create_directories(perfiles_dir, ec);
        for (const auto& it : fs::directory_iterator(perfiles_dir, ec)) {
            if (it.is_directory()) {
                std::string folder = it.path().filename().string();
                std::string upper_f = folder;
                for (auto& c : upper_f) c = (char)std::toupper((unsigned char)c);
                profiles.push_back(upper_f);
            }
        }
        if (profiles.empty()) profiles.push_back("MAKISHI");
        if (selected_idx >= int(profiles.size())) selected_idx = 0;
    }

    void LoadSelected() {
        const std::string& card_dir = ps2host::mc2::CardDirectory();
        if (card_dir.empty()) return;
        fs::path base(card_dir);
        fs::path perfiles_dir = base / "perfiles";
        std::error_code ec;
        for (const auto& it : fs::directory_iterator(perfiles_dir, ec)) {
            if (it.is_directory()) {
                std::string folder = it.path().filename().string();
                std::string upper_f = folder;
                for (auto& c : upper_f) c = (char)std::toupper((unsigned char)c);
                if (upper_f == profiles[selected_idx]) {
                    fs::path dst = base / "BESLES-52584";
                    fs::create_directories(dst, ec);
                    fs::copy(it.path(), dst, fs::copy_options::overwrite_existing | fs::copy_options::recursive, ec);
                    load_status = 1;
                    Refresh();
                    return;
                }
            }
        }
    }

    void CreateBackup() {
        const std::string& card_dir = ps2host::mc2::CardDirectory();
        if (card_dir.empty()) return;
        fs::path base(card_dir);
        fs::path src = base / "BESLES-52584";
        if (!fs::exists(src / "BESLES-52584")) return;
        auto now = std::chrono::system_clock::now();
        std::time_t tt = std::chrono::system_clock::to_time_t(now);
        std::tm tm;
        localtime_s(&tm, &tt);
        char buf[64];
        std::strftime(buf, sizeof(buf), "%m%d_%H%M", &tm);
        std::string backup_name = "BAK_" + std::string(buf) + "_" + active_profile;
        fs::path dst = base / "perfiles" / backup_name;
        std::error_code ec;
        fs::create_directories(dst, ec);
        fs::copy(src, dst, fs::copy_options::overwrite_existing | fs::copy_options::recursive, ec);
        backup_status = 1;
        Refresh();
    }
};

ProfileManager g_pm;

int SaveActiveNow() { return 0; }
void SetSaveActive(int) {}
std::string SaveActiveVal() { g_pm.Refresh(); return g_pm.active_profile; }

int SaveSelectNow() { return g_pm.selected_idx; }
void SetSaveSelect(int) {
    if (!g_pm.profiles.empty()) {
        g_pm.selected_idx = (g_pm.selected_idx + 1) % int(g_pm.profiles.size());
        g_pm.load_status = 0;
    }
}
std::string SaveSelectVal() {
    if (g_pm.profiles.empty()) return "NINGUNO";
    return g_pm.profiles[g_pm.selected_idx];
}

int SaveLoadNow() { return g_pm.load_status; }
void SetSaveLoad(int) { g_pm.LoadSelected(); }

int SaveBackupNow() { return g_pm.backup_status; }
void SetSaveBackup(int) { g_pm.CreateBackup(); }

struct Option {
    Page page;
    const char* name;
    const char* help;
    const char* name_es;
    const char* help_es;
    int (*now)();
    void (*set)(int);
    const char* values[4];
    const char* values_es[4];
    int count;
    std::string (*custom_val)() = nullptr;
};

const Option kOptions[] = {
    {Page::kVideo, "RESOLUTION", "DRAWN AS PS2, TWICE AS SHARP, OR SCREEN SIZE",
                   "RESOLUCION", "RESOLUCION INTERNA DEL RENDERIZADO",
     Resolution, SetResolution, {"ORIGINAL", "2X", "4X", "DISPLAY"}, {"ORIGINAL", "2X", "4X", "PANTALLA"}, 4},
    {Page::kVideo, "ASPECT RATIO", "THE PICTURE'S SHAPE",
                   "RELACION DE ASPECTO", "FORMA DE LA IMAGEN EN PANTALLA",
     Aspect, SetAspect, {"4:3", "16:9", "AUTO"}, {"4:3", "16:9", "AUTO"}, 3},
    {Page::kVideo, "FULLSCREEN", "THE WHOLE SCREEN OR A WINDOW (ALT+ENTER)",
                   "PANTALLA COMPLETA", "PANTALLA COMPLETA O VENTANA (ALT+ENTER)",
     Fullscreen, SetFullscreen, {"OFF", "ON"}, {"NO", "SI"}, 2},
    {Page::kVideo, "VERTICAL SYNC", "NO TEARING, OR THE LEAST DELAY",
                   "SINCRONIZACION VERTICAL", "ELIMINA EL PARPADEO DE LINEAS",
     Vsync, SetVsync, {"OFF", "ON"}, {"NO", "SI"}, 2},
    {Page::kVideo, "ANTIALIASING", "EDGES AS DRAWN, SMOOTHED (FXAA, SMAA), OR SSAA",
                   "ANTIALIASING", "SUAVIZADO DE BORDES",
     AntiAliasing, SetAntiAliasing, {"OFF", "FXAA", "SMAA", "SSAA 2X"}, {"NO", "FXAA", "SMAA", "SSAA 2X"}, 4},
    {Page::kVideo, "SKIP INTRO MOVIES", "SKIPS LOGO MOVIES ON LAUNCH STRAIGHT TO TITLE",
                   "SALTAR VIDEOS INTRO", "SALTA DIRECTO A LA PANTALLA DE TITULO AL INICIAR",
     SkipIntro, SetSkipIntro, {"OFF", "ON"}, {"NO", "SI"}, 2},
    {Page::kGraphics, "BLUR", "THE SPEED BLUR AND GLOW, OR A CLEAR PICTURE",
                      "DESENFOQUE DE VELOCIDAD", "DESENFOQUE Y RESPLANDOR AL CORRER",
     Blur, SetBlur, {"ORIGINAL", "OFF"}, {"ORIGINAL", "NO"}, 2},
    {Page::kGraphics, "TEXTURE DISTANCE", "PS2 LOD OR SHARP IN THE DISTANCE",
                      "DISTANCIA TEXTURAS", "DISTANCIA DE DETALLE DE TEXTURAS (LOD)",
     TexDistance, SetTexDistance, {"PS2 ORIGINAL", "MEJORADA", "MAXIMA"}, {"PS2 ORIGINAL", "MEJORADA", "MAXIMA"}, 3},
    {Page::kGraphics, "TEXTURE FILTERING", "PS2 PIXELS OR SMOOTH BILINEAR",
                      "FILTRADO TEXTURAS", "ELIMINA EL PIXELADO DE TEXTURAS",
     TexFiltering, SetTexFiltering, {"ORIGINAL", "SMOOTH"}, {"PS2 ORIGINAL", "SUAVE (BILINEAL)"}, 2},
    {Page::kControls, "INPUT DEVICE", "ACTIVE INPUT DETECTED BY THE GAME",
                      "DISPOSITIVO ACTIVO", "PERIFERICO O MANDO DETECTADO POR EL JUEGO",
     InputDeviceNow, SetInputDevice, {""}, {""}, 1, InputDeviceVal},
    {Page::kControls, "CONTROLLER MODE", "TRIGGERS ACCELERATE/BRAKE (MODERN) OR PS2 BUTTONS",
                      "MODO DE CONTROL", "BOTONES PS2 (CLASICO) O GATILLOS R2/L2 (MODERNO)",
     ControlModeNow, SetControlMode, {"CLASSIC", "MODERN"}, {"CLASICO (PS2)", "MODERNO (GATILLOS)"}, 2},
    {Page::kControls, "VIBRATION", "CONTROLLER MOTORS RUMBLE ON CRASHES AND DRIFTS",
                      "VIBRACION DEL MANDO", "MOTORES DE VIBRACION EN CHOQUES Y DERRAPES",
     VibrationNow, SetVibrationOpt, {"OFF", "ON"}, {"NO", "SI"}, 2},
    {Page::kControls, "BUTTON PROMPTS", "THE BUTTONS SHOWN: AUTO, PLAYSTATION OR XBOX",
                      "ICONOS DE BOTONES", "ICONOS EN PANTALLA: AUTO, PLAYSTATION O XBOX",
     ButtonPrompts, SetButtonPrompts, {"AUTO", "PLAYSTATION", "XBOX"}, {"AUTO", "PLAYSTATION", "XBOX"}, 3},
    {Page::kSavegames, "ACTIVE SAVE", "THE CURRENT PROFILE IN MEMORY CARD",
                       "PARTIDA ACTIVA", "PERFIL DE PILOTO ACTIVO EN MEMORIA",
     SaveActiveNow, SetSaveActive, {""}, {""}, 1, SaveActiveVal},
    {Page::kSavegames, "CHOOSE PROFILE", "PROFILES IN MEMCARD/PERFILES",
                       "ELEGIR PERFIL", "VER PERFILES GUARDADOS EN MEMCARD/PERFILES",
     SaveSelectNow, SetSaveSelect, {""}, {""}, 1, SaveSelectVal},
    {Page::kSavegames, "LOAD PROFILE", "COPY CHOSEN PROFILE TO ACTIVE MEMORY",
                       "ACTIVAR PERFIL", "RESTAURA EL PERFIL ELEGIDO A LA MEMORIA",
     SaveLoadNow, SetSaveLoad, {"PRESS CROSS", "LOADED OK!"}, {"PRESIONA X", "LISTO CARGADO!"}, 2},
    {Page::kSavegames, "CREATE BACKUP", "SAVE A SNAPSHOT BACKUP OF CURRENT SAVE",
                       "CREAR RESPALDO", "GUARDA UNA COPIA DE RESPALDO DE TU PARTIDA",
     SaveBackupNow, SetSaveBackup, {"PRESS CROSS", "SAVED OK!"}, {"PRESIONA X", "LISTO GUARDADO!"}, 2},
};
constexpr u32 kOptionCount = sizeof kOptions / sizeof kOptions[0];

struct Item {
    enum Kind { kPageLink, kOption, kBindingsLink, kAction, kReset } kind;
    int index;
};

enum class Asking { kNone, kQuit };

struct MainMenuState {
    u32 menu = 0;                           // Main menu object (F_003183E0's a0)
    u32 entries = 0, helps = 0;             // Arrays: kEntries entries (8 bytes), help pointers
    u32 game_entries[kEntries][2] = {};
    u32 game_helps[kEntries] = {};
    GuestText pc_settings, pc_settings_help, quit, quit_help, question, blank;
    GuestText row_names[kEntries], row_helps[kEntries];
    Page page = Page::kMainMenu;
    std::vector<Item> items;
    int scroll = 0;
    int bindings = 0;
    int capturing = -1;
    int seconds = 0;
    bool made = false;
    bool is_spanish = false;
    Asking asking = Asking::kNone;
};
MainMenuState g_main;

u32 GuestCall(lifted::Context& c, u32 target, std::initializer_list<u32> arguments) {
    u64 saved[32];
    std::memcpy(saved, c.r, sizeof saved);
    int r = 4;
    for (u32 a : arguments) c.r[r++] = s64(s32(a));
    lifted::Call(c, target, u32(saved[31]));
    const u32 result = u32(c.r[2]);
    std::memcpy(c.r, saved, sizeof saved);
    return result;
}

void DetectLanguage(lifted::Context& c) {
    u64 saved[32];
    std::memcpy(saved, c.r, sizeof saved);
    c.r[4] = 0x0051BFC4u;
    c.r[5] = 0xE5;  // World Tour string ID
    F_00271A70(c);
    const u32 at = u32(c.r[2]);
    std::memcpy(c.r, saved, sizeof saved);
    if (at && generated::ReadU16(at) == 'T') {
        g_main.is_spanish = true;  // "TOUR MUNDIAL"
    } else {
        g_main.is_spanish = false;
    }
}

void MakeTexts(lifted::Context& c) {
    if (g_main.made) return;
    g_pool.Begin(c);
    DetectLanguage(c);

    for (u32 i = 0; i < kEntries; ++i) g_main.row_helps[i].Make(kHelpChars);
    for (u32 i = 0; i < kEntries; ++i) g_main.row_names[i].Make(kRowChars);
    g_main.entries = g_pool.Take(8 * kEntries, 8);
    g_main.helps = g_pool.Take(4 * kEntries, 4);

    g_main.pc_settings.Make(20);
    g_main.pc_settings.Set(g_main.is_spanish ? "AJUSTES PC" : "PC SETTINGS");

    g_main.pc_settings_help.Make(40);
    g_main.pc_settings_help.Set(g_main.is_spanish ? "CONFIGURAR LA VERSION DE PC" : "SET UP THE PC VERSION");

    g_main.quit.Make(20);
    g_main.quit.Set(g_main.is_spanish ? "SALIR DEL JUEGO" : "QUIT GAME");

    g_main.quit_help.Make(40);
    g_main.quit_help.Set(g_main.is_spanish ? "CERRAR BURNOUT 3 Y VOLVER A WINDOWS" : "CLOSE BURNOUT 3");

    g_main.question.Make(36);
    g_main.blank.Make(2);
    g_main.blank.Set(" ");

    bool room = g_main.entries && g_main.helps && g_main.blank.at && g_main.question.at &&
                g_main.quit.at && g_main.quit_help.at && g_main.pc_settings.at && g_main.pc_settings_help.at;
    for (u32 i = 0; i < kEntries; ++i) room = room && g_main.row_names[i].at && g_main.row_helps[i].at;
    g_main.made = room;
}

std::string OptionText(u32 i) {
    const Option& o = kOptions[i];
    const char* name = g_main.is_spanish ? o.name_es : o.name;
    const std::string val = o.custom_val ? o.custom_val() : (g_main.is_spanish ? o.values_es[o.now()] : o.values[o.now()]);
    return std::string(name) + ": " + val;
}

std::string ActionSpanish(const std::string& en) {
    if (en == "ACCELERATE") return "ACELERAR";
    if (en == "BRAKE AND REVERSE") return "FRENAR Y REVERSA";
    if (en == "BOOST") return "TURBO";
    if (en == "STEER LEFT") return "GIRAR A LA IZQUIERDA";
    if (en == "STEER RIGHT") return "GIRAR A LA DERECHA";
    if (en == "CHANGE CAMERA") return "CAMBIAR CAMARA";
    if (en == "LOOK BACK") return "MIRAR ATRAS";
    if (en == "NEXT SONG") return "SIGUIENTE CANCION";
    if (en == "PAUSE") return "PAUSA";
    if (en == "HORN") return "CLAXON";
    if (en == "CRASH BREAKER") return "CRASH BREAKER";
    if (en == "ACCEPT") return "ACEPTAR / CONFIRMAR";
    if (en == "BACK") return "ATRAS / RETROCEDER";
    if (en == "UP") return "ARRIBA";
    if (en == "DOWN") return "ABAJO";
    if (en == "LEFT") return "IZQUIERDA";
    if (en == "RIGHT") return "DERECHA";
    if (en == "DETAILS") return "DETALLES";
    if (en == "START") return "INICIAR";
    return en;
}

std::string KeySpanish(const std::string& en) {
    if (en == "UP ARROW") return "FLECHA ARRIBA";
    if (en == "DOWN ARROW") return "FLECHA ABAJO";
    if (en == "LEFT ARROW") return "FLECHA IZQUIERDA";
    if (en == "RIGHT ARROW") return "FLECHA DERECHA";
    if (en == "SPACE") return "ESPACIO";
    if (en == "LEFT SHIFT") return "SHIFT IZQ";
    if (en == "RIGHT SHIFT") return "SHIFT DER";
    if (en == "LEFT CTRL") return "CTRL IZQ";
    if (en == "RIGHT CTRL") return "CTRL DER";
    if (en == "LEFT ALT") return "ALT IZQ";
    if (en == "RIGHT ALT") return "ALT DER";
    if (en == "BACKSPACE") return "RETROCESO";
    return en;
}

std::pair<std::string, std::string> ItemText(int item) {
    const Item& it = g_main.items[std::size_t(item)];
    const bool es = g_main.is_spanish;
    switch (it.kind) {
        case Item::kPageLink: {
            const PageInfo& p = kPages[it.index];
            return {es ? p.name_es : p.name, es ? p.help_es : p.help};
        }
        case Item::kOption: {
            const Option& o = kOptions[it.index];
            return {OptionText(u32(it.index)), es ? o.help_es : o.help};
        }
        case Item::kBindingsLink: {
            const BindingsPage& bp = kBindingsPages[it.index];
            return {es ? bp.name_es : bp.name, es ? bp.help_es : bp.help};
        }
        case Item::kReset:
            return {es ? "RESTAURAR DEFECTO" : "RESET TO DEFAULTS",
                    es ? "RESTAURA LOS CONTROLES ORIGINALES DE ESTA PAGINA" : "EVERY KEY OR BUTTON ON THIS PAGE AS IT FIRST WAS"};
        case Item::kAction: {
            const BindingsPage& page = kBindingsPages[g_main.bindings];
            const controls::Action& action = controls::ActionAt(page.device, page.context, it.index);
            const std::string raw_name = controls::ActionName(page.device, page.context, it.index);
            const std::string name = es ? ActionSpanish(raw_name) : raw_name;
            const bool keyboard = page.device == controls::Device::kKeyboard;
            if (item == g_main.capturing) {
                const std::string wait = keyboard ? (es ? "PULSA UNA TECLA" : "PRESS A KEY")
                                                   : (controls::ControllerPresent() ? (es ? "PULSA UN BOTON" : "PRESS A BUTTON")
                                                                                    : (es ? "SIN MANDO" : "NO CONTROLLER"));
                return {name + ": " + wait + " (" + std::to_string(std::max(1, g_main.seconds)) + ")",
                        es ? "PULSA LA NUEVA TECLA. ESC PARA CANCELAR" : "THE KEY TO USE. ESCAPE TO CANCEL"};
            }
            const std::string raw_bound = controls::BoundName(page.device, page.context, action.input);
            const std::string bound = es ? KeySpanish(raw_bound) : raw_bound;
            return {name + ": " + bound, action.help ? action.help : ""};
        }
    }
    return {"", ""};
}

void BuildItems() {
    std::vector<Item>& items = g_main.items;
    items.clear();
    switch (g_main.page) {
        case Page::kPcSettings:
            for (u32 i = 0; i < kPageCount; ++i) items.push_back({Item::kPageLink, int(i)});
            break;
        case Page::kBindings: {
            const BindingsPage& page = kBindingsPages[g_main.bindings];
            for (int i = 0; i < controls::ActionCount(page.device, page.context); ++i)
                items.push_back({Item::kAction, i});
            items.push_back({Item::kReset, 0});
            break;
        }
        case Page::kControls:
            for (u32 i = 0; i < kOptionCount; ++i)
                if (kOptions[i].page == Page::kControls) items.push_back({Item::kOption, int(i)});
            for (int i = 0; i < kBindingsPageCount; ++i)
                items.push_back({Item::kBindingsLink, i});
            break;
        default:
            for (u32 i = 0; i < kOptionCount; ++i)
                if (kOptions[i].page == g_main.page) items.push_back({Item::kOption, int(i)});
            break;
    }
}

void RefreshList(lifted::Context& c) {
    for (u32 row = 0; row < kEntries; ++row)
        GuestCall(c, 0x0031FFC0u, {kList, row, generated::ReadU8(g_main.entries + 8 * row + 4) ? 0u : 1u});
}

void Render(lifted::Context& c) {
    const int n = int(g_main.items.size());
    for (u32 row = 0; row < kEntries; ++row) {
        const int item = g_main.scroll + int(row);
        if (item < n) {
            const auto [name, help] = ItemText(item);
            g_main.row_names[row].Set(name);
            g_main.row_helps[row].Set(help);
            generated::Write32(g_main.entries + 8 * row, g_main.row_names[row].at);
            generated::Write32(g_main.helps + 4 * row, g_main.row_helps[row].at);
            generated::Write32(g_main.entries + 8 * row + 4, 0);
        } else {
            generated::Write32(g_main.entries + 8 * row, g_main.blank.at);
            generated::Write32(g_main.helps + 4 * row, g_main.blank.at);
            generated::Write32(g_main.entries + 8 * row + 4, 0);
        }
    }
    RefreshList(c);
    if (generated::ReadU32(kList + 0xA4u) == generated::ReadU16(g_main.menu + 0x14))
        generated::Write32(kList + 0xA4u, ~0u);
}

bool g_cross_drain = false;

void ShowPage(lifted::Context& c, Page page, int highlighted = 0) {
    g_cross_drain = true;
    generated::Write16(0x004F5640u + 0x44, 0);
    const u32 menu = g_main.menu;
    const bool was_main = g_main.page == Page::kMainMenu, on = page != Page::kMainMenu;
    if (on) {
        if (was_main) {
            for (u32 i = 0; i < kEntries; ++i) {
                g_main.game_entries[i][0] = generated::ReadU32(g_main.entries + 8 * i);
                g_main.game_entries[i][1] = generated::ReadU32(g_main.entries + 8 * i + 4);
                g_main.game_helps[i] = generated::ReadU32(g_main.helps + 4 * i);
            }
        }
        g_main.page = page;
        BuildItems();
        const int n = int(g_main.items.size());
        highlighted = std::clamp(highlighted, 0, n - 1);
        g_main.scroll = std::clamp(highlighted - int(kEntries) + 1, 0, std::max(0, n - int(kEntries)));
        generated::Write16(menu + 0x16, u16(std::min(n, int(kEntries))));
        generated::Write16(menu + 0x14, u16(highlighted - g_main.scroll));
        Render(c);
    } else {
        for (u32 i = 0; i < kEntries; ++i) {
            generated::Write32(g_main.entries + 8 * i, g_main.game_entries[i][0]);
            generated::Write32(g_main.entries + 8 * i + 4, g_main.game_entries[i][1]);
            generated::Write32(g_main.helps + 4 * i, g_main.game_helps[i]);
        }
        generated::Write16(menu + 0x16, kEntries);
        generated::Write16(menu + 0x14, u16(highlighted));
        g_main.page = page;
        RefreshList(c);
        if (generated::ReadU32(kList + 0xA4u) == generated::ReadU16(menu + 0x14))
            generated::Write32(kList + 0xA4u, ~0u);
    }
    if (on == !was_main) return;
    if (!on) {
        const u32 layer = u32(s32(generated::ReadS16(kLegend + 0x8u)));
        for (const u32 at : {kLegend + 0x1Cu, kLegend + 0x48u}) {
            if (const u32 object = generated::ReadU32(at)) {
                GuestCall(c, 0x001D7550u, {kPoolLayer, layer, object});
                generated::Write32(at, 0);
            }
        }
    }
    GuestCall(c, 0x0031EDF0u, {kLegend, on ? 0x105u : 0x104u, 3u});
}

int Highlighted() { return g_main.scroll + int(generated::ReadU16(g_main.menu + 0x14)); }

template <void (*Move)(lifted::Context&), int kStep>
void SelectorMove(lifted::Context& c) {
    const u32 selector = u32(c.r[4]);
    if (g_main.made && g_main.page != Page::kMainMenu && selector == g_main.menu + 0x14 && g_main.capturing < 0) {
        const int index = generated::ReadU16(selector), n = int(g_main.items.size());
        const bool down = kStep > 0 && index == int(kEntries) - 1 && g_main.scroll + int(kEntries) < n;
        const bool up = kStep < 0 && index == 0 && g_main.scroll > 0;
        if (down || up) {
            g_main.scroll += kStep;
            Render(c);
            c.r[2] = 1;
            return;
        }
    }
    if (g_main.capturing >= 0) {
        c.r[2] = 1;
        return;
    }
    Move(c);
}

void Ask(lifted::Context& c, Asking asking, const char* question) {
    g_main.question.Set(question);
    if (generated::ReadU8(kMessageBox + 0x0Au)) {
        GuestCall(c, generated::ReadU32(generated::ReadU32(kMessageBox) + 0x0Cu), {kMessageBox});
    }
    GuestCall(c, 0x0032D130u, {kMessageBox, 8u, g_main.question.at, 0x10u, 1u});
    generated::Write16(0x004F5640u + 0x44, 0);
    g_main.asking = asking;
}

bool TrianglePressed() {
    static bool before = false;
    const bool now = (ps2host::PadButtonsHeld() & (1u << 12)) != 0;
    const bool pressed = now && !before;
    before = now;
    return pressed;
}

bool CrossPressed(lifted::Context& c) {
    const bool held = (ps2host::PadButtonsHeld() & (1u << 14)) != 0;
    static bool before = false;
    if (g_cross_drain) {
        if (held) {
            before = true;
            return false;
        }
        g_cross_drain = false;
        before = false;
    }
    const bool pressed = held && !before;
    before = held;

    const bool game_pressed = GuestCall(c, 0x0022CFD0u, {0x004F5640u, 0}) != 0;
    if (pressed || game_pressed) {
        g_cross_drain = true;
        return true;
    }
    return false;
}

void GoBack(lifted::Context& c) {
    g_cross_drain = true;
    generated::Write16(0x004F5640u + 0x44, 0);
    switch (g_main.page) {
        case Page::kBindings:
            ShowPage(c, Page::kControls, 4);
            return;
        case Page::kPcSettings:
            ShowPage(c, Page::kMainMenu, kPcSettingsEntry);
            return;
        default: {
            int from = 0;
            for (u32 i = 0; i < kPageCount; ++i)
                if (kPages[i].page == g_main.page) from = int(i);
            ShowPage(c, Page::kPcSettings, from);
        }
    }
}

void Choose(lifted::Context& c, int item) {
    if (item < 0 || item >= int(g_main.items.size())) return;
    g_cross_drain = true;
    generated::Write16(0x004F5640u + 0x44, 0);
    const Item it = g_main.items[std::size_t(item)];
    if (it.kind == Item::kPageLink) {
        ShowPage(c, kPages[it.index].page);
        return;
    }
    if (it.kind == Item::kBindingsLink) {
        g_main.bindings = it.index;
        ShowPage(c, Page::kBindings);
        return;
    }
    if (it.kind == Item::kOption) {
        const Option& o = kOptions[it.index];
        o.set((o.now() + 1) % o.count);
        Render(c);
        return;
    }
    if (it.kind == Item::kReset) {
        const BindingsPage& page = kBindingsPages[g_main.bindings];
        controls::ResetDefaults(page.device, page.context);
        Render(c);
        return;
    }
    if (it.kind == Item::kAction) {
        const BindingsPage& page = kBindingsPages[g_main.bindings];
        const controls::Action& action = controls::ActionAt(page.device, page.context, it.index);
        controls::BeginCapture(page.device, page.context, action.input);
        g_main.capturing = item;
        controls::Capture(&g_main.seconds);
        Render(c);
        return;
    }
}

void GetString(lifted::Context& c) {
    const u32 id = u32(c.r[5]), caller = u32(c.r[31]);
    if ((id == kMainMenuOnline && caller == 0x00318470u) || id == kOnlineHelp || id == kQuestion) {
        MakeTexts(c);
        const GuestText& t = id == kMainMenuOnline ? g_main.pc_settings
                           : id == kOnlineHelp     ? g_main.pc_settings_help
                                                   : g_main.question;
        if (t.at) {
            c.r[2] = t.at;
            return;
        }
    }
    F_00271A70(c);
}

void MenuList(lifted::Context& c) {
    if (u32(c.r[31]) == 0x00318504u) {
        MakeTexts(c);
        if (g_main.made) {
            const u32 from = u32(c.r[6]);
            g_main.menu = u32(c.r[5]) - 0x14;
            for (u32 i = 0; i < 8 * (kEntries - 1); i += 4)
                generated::Write32(g_main.entries + i, generated::ReadU32(from + i));
            generated::Write32(g_main.entries + 8 * kQuitEntry, g_main.quit.at);
            generated::Write32(g_main.entries + 8 * kQuitEntry + 4, 0);
            generated::Write16(u32(c.r[5]) + 2, kEntries);
            c.r[6] = g_main.entries;
            g_main.page = Page::kMainMenu;
            g_main.asking = Asking::kNone;
        }
    }
    F_003215F0(c);
}

void MenuHelps(lifted::Context& c) {
    if (u32(c.r[31]) == 0x00318548u && g_main.made) {
        const u32 from = u32(c.r[6]);
        for (u32 i = 0; i < kEntries - 1; ++i)
            generated::Write32(g_main.helps + 4 * i, generated::ReadU32(from + 4 * i));
        generated::Write32(g_main.helps + 4 * kQuitEntry, g_main.quit_help.at);
        c.r[6] = g_main.helps;
    }
    F_0031C3F0(c);
}

void MainMenuInput(lifted::Context& c) {
    const u32 menu = u32(c.r[4]);
    if (g_main.made) g_main.menu = menu;

    const bool back = TrianglePressed();
    const bool settings = g_main.page != Page::kMainMenu;
    const u32 entry = generated::ReadU16(menu + 0x14);

    if (settings && g_main.capturing >= 0) {
        int seconds = 0;
        const controls::CaptureState state = controls::Capture(&seconds);
        if (state != controls::CaptureState::kWaiting || seconds != g_main.seconds) {
            g_main.seconds = seconds;
            if (state != controls::CaptureState::kWaiting) {
                g_main.capturing = -1;
            }
            Render(c);
        }
        c.r[2] = 1;
        return;
    }

    if (settings) {
        if (back) {
            GoBack(c);
            c.r[2] = 1;
            return;
        }
        if (CrossPressed(c)) {
            Choose(c, Highlighted());
            c.r[2] = 1;
            return;
        }
        if (GuestCall(c, 0x0022CED0u, {0x004F5640u, 0})) {
            GuestCall(c, 0x00319400u, {menu + 0x14});
        } else if (GuestCall(c, 0x0022CE50u, {0x004F5640u, 0})) {
            GuestCall(c, 0x003193D0u, {menu + 0x14});
        }
        c.r[2] = 1;
        return;
    }

    // In Main Menu:
    if (CrossPressed(c)) {
        if (entry == kPcSettingsEntry) {
            ShowPage(c, Page::kPcSettings);
            c.r[2] = 1;
            return;
        }
        if (entry == kQuitEntry) {
            Ask(c, Asking::kQuit, g_main.is_spanish ? "¿SALIR DE BURNOUT 3?" : "Quit Burnout 3?");
            c.r[2] = 1;
            return;
        }
    }

    // Never let entry 3 (former ONLINE) run original confirmation or online connection.
    // Allow Up/Down navigation.
    if (entry == kPcSettingsEntry) {
        if (GuestCall(c, 0x0022CED0u, {0x004F5640u, 0})) {
            GuestCall(c, 0x00319400u, {menu + 0x14});
        } else if (GuestCall(c, 0x0022CE50u, {0x004F5640u, 0})) {
            GuestCall(c, 0x003193D0u, {menu + 0x14});
        }
        c.r[2] = 1;
        return;
    }

    F_00318240(c);
}

void MainMenu(lifted::Context& c) {
    if (g_main.asking == Asking::kNone) {
        F_00318160(c);
        return;
    }
    const u32 pressed = GuestCall(c, 0x0032B490u, {kMessageBox, 0, 1});
    if (pressed == kYes && g_main.asking == Asking::kQuit) throw ps2host::GuestExit{0};
    if (pressed == kNo || TrianglePressed()) {
        GuestCall(c, generated::ReadU32(generated::ReadU32(kMessageBox) + 0x0C), {kMessageBox});
        GuestCall(c, 0x0031EDF0u, {kLegend, 0x104u, 3u});
        generated::Write32(kList + 0xA4u, ~0u);
        g_main.asking = Asking::kNone;
    }
}

}  // namespace

void RegisterGameIopServers();
void RegisterSixtyFps();
void RegisterWidescreen();

void RegisterGameOverrides() {
    ps2host::SetVuFastDefault(true);
    ps2host::SetFramePacingDefault(false);
    ps2host::SetPresentAtBlankEnd(true);
    ps2host::SetGpuPipelineHints(kGpuPipelineHints, sizeof kGpuPipelineHints / sizeof kGpuPipelineHints[0]);

    // 60 FPS in races, crashes, normal-speed videos & menus
    RegisterSixtyFps();

    // Widescreen HUD, menus anchoring and 3D camera
    RegisterWidescreen();

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

    // In-game native PC SETTINGS & QUIT GAME overrides
    ps2host::Override(0x00271A70u, GetString, "the string lookup: PC SETTINGS' words");
    ps2host::Override(0x00318240u, MainMenuInput, "the main menu's input: PC SETTINGS, QUIT GAME");
    ps2host::Override(0x00318160u, MainMenu, "the main menu: QUIT GAME's message box");
    ps2host::Override(0x003215F0u, MenuList, "a list: the main menu's with QUIT GAME");
    ps2host::Override(0x0031C3F0u, MenuHelps, "a ticker: the main menu's descriptions with QUIT GAME's");
    ps2host::Override(0x00319400u, SelectorMove<F_00319400, -1>, "a selector's Up: PC SETTINGS' pages scroll");
    ps2host::Override(0x003193D0u, SelectorMove<F_003193D0, 1>, "a selector's Down: PC SETTINGS' pages scroll");

    // Fix rumble motor logic: 0x00217050 writes enabled flag to 0x46 (0 = allowed, 1 = disabled)
    ps2host::Override(0x00217050u, SetVibration, "SetVibration: fixes motor flag so controllers rumble");
    generated::Write8(0x004F5640u + 0x46u, ps2host::SettingOn("vibration", true) ? 0u : 1u);

    // Controls: modern keyboard mapper, controller filter, and mouse hook
    controls::RegisterControls();

    // Low-level EE kernel hooks
    ps2host::Override(0x001156C0u, KernelStore, "kernel-mode store");
    ps2host::Override(0x0010D3E8u, TtyWrite, "TTY write (printf)");
}
