#ifndef PS2HOST_LAUNCHER_INC
#define PS2HOST_LAUNCHER_INC

#pragma comment(lib, "shell32.lib")

// Forward declaration
std::string PickDisc(const std::string& initial_path);
bool IsGameDisc(const std::string& path);

enum ControlId {
    IDC_LAUNCHER_BANNER = 2001,
    IDC_LAUNCHER_ISO_EDIT,
    IDC_LAUNCHER_ISO_BROWSE,
    IDC_LAUNCHER_COMBO_RES,
    IDC_LAUNCHER_CHK_FULLSCREEN,
    IDC_LAUNCHER_COMBO_TEXDIST,
    IDC_LAUNCHER_CHK_VSYNC,
    IDC_LAUNCHER_COMBO_TEXFILTER,
    IDC_LAUNCHER_CHK_FPS,
    IDC_LAUNCHER_COMBO_ASPECT,
    IDC_LAUNCHER_CHK_ASK_DISC,
    IDC_LAUNCHER_COMBO_PROFILES,
    IDC_LAUNCHER_BTN_LOAD_PROFILE,
    IDC_LAUNCHER_BTN_BACKUP_PROFILE,
    IDC_LAUNCHER_BTN_OPEN_SAVES,
    IDC_LAUNCHER_BTN_OPEN_TEXTURES,
    IDC_LAUNCHER_BTN_PLAY,
    IDC_LAUNCHER_BTN_EXIT,
};

struct LauncherState {
    std::filesystem::path here;
    std::string disc;
    bool confirmed = false;
    HWND hwnd = nullptr;
    HWND hIsoEdit = nullptr;
    HWND hComboRes = nullptr;
    HWND hChkFullscreen = nullptr;
    HWND hComboTexDist = nullptr;
    HWND hChkVSync = nullptr;
    HWND hComboTexFilter = nullptr;
    HWND hChkFps = nullptr;
    HWND hComboAspect = nullptr;
    HWND hChkAskDisc = nullptr;
    HWND hLblActiveProfile = nullptr;
    HWND hComboProfiles = nullptr;
    HWND hBtnLoadProfile = nullptr;
    HWND hBtnBackupProfile = nullptr;
    HWND hBtnOpenSaves = nullptr;
    HWND hBtnOpenTextures = nullptr;
    HWND hBtnPlay = nullptr;
    HWND hBtnExit = nullptr;
    HBITMAP hBannerBmp = nullptr;
    HFONT hFontUi = nullptr;
    HFONT hFontBold = nullptr;
    HFONT hFontPlay = nullptr;
    HBRUSH hBrushBg = nullptr;
    std::vector<std::pair<std::wstring, std::filesystem::path>> available_profiles;
};

std::wstring Utf8ToWide(const std::string& str) {
    if (str.empty()) return L"";
    int count = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
    std::wstring out(count, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &out[0], count);
    if (!out.empty() && out.back() == 0) out.pop_back();
    return out;
}

std::wstring ReadSaveProfileName(const std::filesystem::path& save_file) {
    std::ifstream f(save_file, std::ios::binary);
    if (!f) return L"";
    f.seekg(0x0234, std::ios::beg);
    char buf[64] = {};
    f.read(buf, sizeof(buf));
    std::wstring name;
    for (size_t i = 0; i + 1 < sizeof(buf); i += 2) {
        wchar_t ch = static_cast<wchar_t>(static_cast<unsigned char>(buf[i]) | (static_cast<unsigned char>(buf[i + 1]) << 8));
        if (ch == 0) break;
        name += ch;
    }
    return name;
}

void RefreshProfilesList(LauncherState* state) {
    if (!state || !state->hComboProfiles || !state->hLblActiveProfile) return;
    SendMessageW(state->hComboProfiles, CB_RESETCONTENT, 0, 0);
    state->available_profiles.clear();

    const std::filesystem::path memcard_dir = state->here / "memcard";
    const std::filesystem::path active_save = memcard_dir / "BESLES-52584" / "BESLES-52584";
    std::wstring active_name = ReadSaveProfileName(active_save);
    if (active_name.empty()) {
        if (std::filesystem::exists(active_save)) {
            active_name = L"Sin Nombre";
        } else {
            active_name = L"No hay partida activa";
        }
    }
    std::wstring status_text = L"Partida Activa en Memoria:  " + active_name;
    if (std::filesystem::exists(active_save)) {
        std::error_code ec;
        auto sz = std::filesystem::file_size(active_save, ec);
        if (!ec) {
            status_text += L"  (" + std::to_wstring(sz / 1024) + L" KB)";
        }
    }
    SetWindowTextW(state->hLblActiveProfile, status_text.c_str());

    const std::filesystem::path perfiles_dir = memcard_dir / "perfiles";
    std::error_code ec;
    if (!std::filesystem::exists(perfiles_dir, ec)) {
        std::filesystem::create_directories(perfiles_dir, ec);
    }

    int selected_idx = -1;
    int idx = 0;
    for (const auto& entry : std::filesystem::directory_iterator(perfiles_dir, ec)) {
        if (entry.is_directory()) {
            std::filesystem::path p_save = entry.path() / "BESLES-52584";
            std::wstring p_name = ReadSaveProfileName(p_save);
            std::wstring folder_name = entry.path().filename().wstring();
            std::wstring display_name = folder_name;
            if (!p_name.empty() && p_name != folder_name) {
                display_name += L"  [" + p_name + L"]";
            }
            SendMessageW(state->hComboProfiles, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(display_name.c_str()));
            state->available_profiles.push_back({display_name, entry.path()});
            if (folder_name == active_name || p_name == active_name) {
                selected_idx = idx;
            }
            idx++;
        }
    }
    if (idx > 0) {
        SendMessageW(state->hComboProfiles, CB_SETCURSEL, selected_idx >= 0 ? selected_idx : 0, 0);
    } else {
        SendMessageW(state->hComboProfiles, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"(No hay perfiles en memcard/perfiles)"));
        SendMessageW(state->hComboProfiles, CB_SETCURSEL, 0, 0);
    }
}

LRESULT CALLBACK LauncherWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    LauncherState* state = reinterpret_cast<LauncherState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
        case WM_CREATE: {
            CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            state = reinterpret_cast<LauncherState*>(cs->lpCreateParams);
            state->hwnd = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));

            state->hFontUi = CreateFontW(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                         OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                         DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            state->hFontBold = CreateFontW(15, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                           DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            state->hFontPlay = CreateFontW(18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                           DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            state->hBrushBg = CreateSolidBrush(RGB(240, 240, 244));

            // Load banner bitmap
            const std::filesystem::path banner_path = state->here / "launcher_banner.bmp";
            state->hBannerBmp = reinterpret_cast<HBITMAP>(LoadImageA(
                nullptr, banner_path.string().c_str(), IMAGE_BITMAP, 560, 135, LR_LOADFROMFILE));

            // 1. Banner
            HWND hBanner = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_BITMAP,
                                           12, 10, 560, 135, hwnd,
                                           reinterpret_cast<HMENU>(IDC_LAUNCHER_BANNER), nullptr, nullptr);
            if (state->hBannerBmp) {
                SendMessageW(hBanner, STM_SETIMAGE, IMAGE_BITMAP, reinterpret_cast<LPARAM>(state->hBannerBmp));
            } else {
                SetWindowTextW(hBanner, L"BURNOUT 3: TAKEDOWN - PC EDITION");
            }

            // 2. GroupBox ISO
            HWND hGrpIso = CreateWindowExW(0, L"BUTTON", L" Imagen de Disco del Juego (ISO) ",
                                           WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                           12, 155, 560, 75, hwnd, nullptr, nullptr, nullptr);
            SendMessageW(hGrpIso, WM_SETFONT, reinterpret_cast<WPARAM>(state->hFontBold), TRUE);

            std::wstring wide_disc = Utf8ToWide(state->disc);
            state->hIsoEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", wide_disc.c_str(),
                                              WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_READONLY,
                                              25, 185, 405, 26, hwnd,
                                              reinterpret_cast<HMENU>(IDC_LAUNCHER_ISO_EDIT), nullptr, nullptr);
            SendMessageW(state->hIsoEdit, WM_SETFONT, reinterpret_cast<WPARAM>(state->hFontUi), TRUE);

            HWND hBrowse = CreateWindowExW(0, L"BUTTON", L"Examinar ISO...",
                                           WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                           440, 184, 115, 28, hwnd,
                                           reinterpret_cast<HMENU>(IDC_LAUNCHER_ISO_BROWSE), nullptr, nullptr);
            SendMessageW(hBrowse, WM_SETFONT, reinterpret_cast<WPARAM>(state->hFontUi), TRUE);

            // 3. GroupBox Settings
            HWND hGrpSettings = CreateWindowExW(0, L"BUTTON", L" Configuraci\x00F3n de Gr\x00E1\x0066icos y Renderizado ",
                                                WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                                12, 240, 560, 195, hwnd, nullptr, nullptr, nullptr);
            SendMessageW(hGrpSettings, WM_SETFONT, reinterpret_cast<WPARAM>(state->hFontBold), TRUE);

            // Row 1: Resolution & Fullscreen
            HWND hLblRes = CreateWindowExW(0, L"STATIC", L"Resoluci\x00F3n:", WS_CHILD | WS_VISIBLE,
                                           25, 268, 115, 20, hwnd, nullptr, nullptr, nullptr);
            SendMessageW(hLblRes, WM_SETFONT, reinterpret_cast<WPARAM>(state->hFontUi), TRUE);

            state->hComboRes = CreateWindowExW(0, L"COMBOBOX", L"",
                                               WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                                               145, 265, 175, 130, hwnd,
                                               reinterpret_cast<HMENU>(IDC_LAUNCHER_COMBO_RES), nullptr, nullptr);
            SendMessageW(state->hComboRes, WM_SETFONT, reinterpret_cast<WPARAM>(state->hFontUi), TRUE);
            SendMessageW(state->hComboRes, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Original (1X - 640x448)"));
            SendMessageW(state->hComboRes, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"HD (2X - 1280x896)"));
            SendMessageW(state->hComboRes, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Ultra HD (4X - 2560x1792)"));
            SendMessageW(state->hComboRes, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Pantalla (Nativa Monitor)"));
            const std::string cur_res = ps2host::Setting("resolution", "4x");
            int res_idx = (cur_res == "original") ? 0 : (cur_res == "2x") ? 1 : (cur_res == "display") ? 3 : 2;
            SendMessageW(state->hComboRes, CB_SETCURSEL, res_idx, 0);

            state->hChkFullscreen = CreateWindowExW(0, L"BUTTON", L"Pantalla Completa (Fullscreen)",
                                                    WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                                    345, 266, 215, 22, hwnd,
                                                    reinterpret_cast<HMENU>(IDC_LAUNCHER_CHK_FULLSCREEN), nullptr, nullptr);
            SendMessageW(state->hChkFullscreen, WM_SETFONT, reinterpret_cast<WPARAM>(state->hFontUi), TRUE);
            SendMessageW(state->hChkFullscreen, BM_SETCHECK, ps2host::SettingOn("fullscreen", true) ? BST_CHECKED : BST_UNCHECKED, 0);

            // Row 2: Texture Distance (LOD) & V-Sync
            HWND hLblTexDist = CreateWindowExW(0, L"STATIC", L"Distancia Texturas:", WS_CHILD | WS_VISIBLE,
                                               25, 303, 115, 20, hwnd, nullptr, nullptr, nullptr);
            SendMessageW(hLblTexDist, WM_SETFONT, reinterpret_cast<WPARAM>(state->hFontUi), TRUE);

            state->hComboTexDist = CreateWindowExW(0, L"COMBOBOX", L"",
                                                   WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                                                   145, 300, 175, 130, hwnd,
                                                   reinterpret_cast<HMENU>(IDC_LAUNCHER_COMBO_TEXDIST), nullptr, nullptr);
            SendMessageW(state->hComboTexDist, WM_SETFONT, reinterpret_cast<WPARAM>(state->hFontUi), TRUE);
            SendMessageW(state->hComboTexDist, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"PS2 Original (Normal)"));
            SendMessageW(state->hComboTexDist, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Mejorada (2X Distancia)"));
            SendMessageW(state->hComboTexDist, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"M\x00E1xima (Ultra N\x00EDtida)"));
            const std::string cur_dist = ps2host::Setting("texture_distance", "enhanced");
            int dist_idx = (cur_dist == "ps2" || cur_dist == "original") ? 0 : (cur_dist == "max" || cur_dist == "maxima") ? 2 : 1;
            SendMessageW(state->hComboTexDist, CB_SETCURSEL, dist_idx, 0);

            state->hChkVSync = CreateWindowExW(0, L"BUTTON", L"Sincronizaci\x00F3n Vertical (V-Sync)",
                                               WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                               345, 301, 215, 22, hwnd,
                                               reinterpret_cast<HMENU>(IDC_LAUNCHER_CHK_VSYNC), nullptr, nullptr);
            SendMessageW(state->hChkVSync, WM_SETFONT, reinterpret_cast<WPARAM>(state->hFontUi), TRUE);
            SendMessageW(state->hChkVSync, BM_SETCHECK, ps2host::SettingOn("vsync", false) ? BST_CHECKED : BST_UNCHECKED, 0);

            // Row 3: Texture Filtering & FPS Counter
            HWND hLblTexFilter = CreateWindowExW(0, L"STATIC", L"Filtro Texturas:", WS_CHILD | WS_VISIBLE,
                                                 25, 338, 115, 20, hwnd, nullptr, nullptr, nullptr);
            SendMessageW(hLblTexFilter, WM_SETFONT, reinterpret_cast<WPARAM>(state->hFontUi), TRUE);

            state->hComboTexFilter = CreateWindowExW(0, L"COMBOBOX", L"",
                                                     WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                                                     145, 335, 175, 130, hwnd,
                                                     reinterpret_cast<HMENU>(IDC_LAUNCHER_COMBO_TEXFILTER), nullptr, nullptr);
            SendMessageW(state->hComboTexFilter, WM_SETFONT, reinterpret_cast<WPARAM>(state->hFontUi), TRUE);
            SendMessageW(state->hComboTexFilter, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Original (PS2)"));
            SendMessageW(state->hComboTexFilter, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Vector HD (Sin P\x00EDxeles)"));
            const std::string cur_filter = ps2host::Setting("texture_filtering", "bilinear");
            int filter_idx = (cur_filter == "original" || cur_filter == "off") ? 0 : 1;
            SendMessageW(state->hComboTexFilter, CB_SETCURSEL, filter_idx, 0);

            state->hChkFps = CreateWindowExW(0, L"BUTTON", L"Contador de FPS (F1)",
                                             WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                             345, 336, 215, 22, hwnd,
                                             reinterpret_cast<HMENU>(IDC_LAUNCHER_CHK_FPS), nullptr, nullptr);
            SendMessageW(state->hChkFps, WM_SETFONT, reinterpret_cast<WPARAM>(state->hFontUi), TRUE);
            SendMessageW(state->hChkFps, BM_SETCHECK, ps2host::SettingOn("fps_counter", true) ? BST_CHECKED : BST_UNCHECKED, 0);

            // Row 4: Aspect Ratio
            HWND hLblAspect = CreateWindowExW(0, L"STATIC", L"Aspect Ratio:", WS_CHILD | WS_VISIBLE,
                                              25, 373, 115, 20, hwnd, nullptr, nullptr, nullptr);
            SendMessageW(hLblAspect, WM_SETFONT, reinterpret_cast<WPARAM>(state->hFontUi), TRUE);

            state->hComboAspect = CreateWindowExW(0, L"COMBOBOX", L"",
                                                  WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                                                  145, 370, 175, 130, hwnd,
                                                  reinterpret_cast<HMENU>(IDC_LAUNCHER_COMBO_ASPECT), nullptr, nullptr);
            SendMessageW(state->hComboAspect, WM_SETFONT, reinterpret_cast<WPARAM>(state->hFontUi), TRUE);
            SendMessageW(state->hComboAspect, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"16:9 (Panor\x00E1mico)"));
            SendMessageW(state->hComboAspect, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"4:3 (Est\x00E1ndar)"));
            SendMessageW(state->hComboAspect, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Auto"));
            const std::string cur_aspect = ps2host::Setting("aspect", "16:9");
            int aspect_idx = (cur_aspect == "4:3") ? 1 : (cur_aspect == "auto") ? 2 : 0;
            SendMessageW(state->hComboAspect, CB_SETCURSEL, aspect_idx, 0);

            // Row 5: Ask Disc Checkbox
            state->hChkAskDisc = CreateWindowExW(0, L"BUTTON", L"Mostrar este lanzador al iniciar el juego",
                                                 WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                                 25, 405, 350, 22, hwnd,
                                                 reinterpret_cast<HMENU>(IDC_LAUNCHER_CHK_ASK_DISC), nullptr, nullptr);
            SendMessageW(state->hChkAskDisc, WM_SETFONT, reinterpret_cast<WPARAM>(state->hFontUi), TRUE);
            SendMessageW(state->hChkAskDisc, BM_SETCHECK, BST_CHECKED, 0);

            // 4. GroupBox Savegames & Profiles
            HWND hGrpSave = CreateWindowExW(0, L"BUTTON", L" Partidas Guardadas (Perfiles & Respaldos) ",
                                            WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                            12, 442, 560, 130, hwnd, nullptr, nullptr, nullptr);
            SendMessageW(hGrpSave, WM_SETFONT, reinterpret_cast<WPARAM>(state->hFontBold), TRUE);

            // Save row 1: Active save info
            state->hLblActiveProfile = CreateWindowExW(0, L"STATIC", L"Partida Activa en Memoria:  Cargando...",
                                                       WS_CHILD | WS_VISIBLE,
                                                       25, 467, 530, 20, hwnd, nullptr, nullptr, nullptr);
            SendMessageW(state->hLblActiveProfile, WM_SETFONT, reinterpret_cast<WPARAM>(state->hFontBold), TRUE);

            // Save row 2: Profile combo & Load/Backup buttons
            HWND hLblChooseProf = CreateWindowExW(0, L"STATIC", L"Perfil / Guardado:", WS_CHILD | WS_VISIBLE,
                                                  25, 497, 115, 20, hwnd, nullptr, nullptr, nullptr);
            SendMessageW(hLblChooseProf, WM_SETFONT, reinterpret_cast<WPARAM>(state->hFontUi), TRUE);

            state->hComboProfiles = CreateWindowExW(0, L"COMBOBOX", L"",
                                                    WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                                                    145, 494, 235, 140, hwnd,
                                                    reinterpret_cast<HMENU>(IDC_LAUNCHER_COMBO_PROFILES), nullptr, nullptr);
            SendMessageW(state->hComboProfiles, WM_SETFONT, reinterpret_cast<WPARAM>(state->hFontUi), TRUE);

            state->hBtnLoadProfile = CreateWindowExW(0, L"BUTTON", L"Cargar",
                                                     WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                                     390, 493, 80, 28, hwnd,
                                                     reinterpret_cast<HMENU>(IDC_LAUNCHER_BTN_LOAD_PROFILE), nullptr, nullptr);
            SendMessageW(state->hBtnLoadProfile, WM_SETFONT, reinterpret_cast<WPARAM>(state->hFontBold), TRUE);

            state->hBtnBackupProfile = CreateWindowExW(0, L"BUTTON", L"Respaldar",
                                                       WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                                       476, 493, 84, 28, hwnd,
                                                       reinterpret_cast<HMENU>(IDC_LAUNCHER_BTN_BACKUP_PROFILE), nullptr, nullptr);
            SendMessageW(state->hBtnBackupProfile, WM_SETFONT, reinterpret_cast<WPARAM>(state->hFontUi), TRUE);

            // Save row 3: Open folders
            state->hBtnOpenSaves = CreateWindowExW(0, L"BUTTON", L"Abrir Carpeta Guardados",
                                                   WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                                   145, 530, 195, 28, hwnd,
                                                   reinterpret_cast<HMENU>(IDC_LAUNCHER_BTN_OPEN_SAVES), nullptr, nullptr);
            SendMessageW(state->hBtnOpenSaves, WM_SETFONT, reinterpret_cast<WPARAM>(state->hFontUi), TRUE);

            state->hBtnOpenTextures = CreateWindowExW(0, L"BUTTON", L"Abrir Carpeta Texturas HD",
                                                      WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                                      350, 530, 210, 28, hwnd,
                                                      reinterpret_cast<HMENU>(IDC_LAUNCHER_BTN_OPEN_TEXTURES), nullptr, nullptr);
            SendMessageW(state->hBtnOpenTextures, WM_SETFONT, reinterpret_cast<WPARAM>(state->hFontUi), TRUE);

            RefreshProfilesList(state);

            // 5. Action Buttons (Side by Side)
            state->hBtnPlay = CreateWindowExW(0, L"BUTTON", L"\x25B6  INICIAR JUEGO",
                                              WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                                              115, 586, 230, 46, hwnd,
                                              reinterpret_cast<HMENU>(IDC_LAUNCHER_BTN_PLAY), nullptr, nullptr);
            SendMessageW(state->hBtnPlay, WM_SETFONT, reinterpret_cast<WPARAM>(state->hFontPlay), TRUE);

            state->hBtnExit = CreateWindowExW(0, L"BUTTON", L"Salir",
                                              WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                              365, 586, 115, 46, hwnd,
                                              reinterpret_cast<HMENU>(IDC_LAUNCHER_BTN_EXIT), nullptr, nullptr);
            SendMessageW(state->hBtnExit, WM_SETFONT, reinterpret_cast<WPARAM>(state->hFontBold), TRUE);

            SetFocus(state->hBtnPlay);
            return 0;
        }

        case WM_CTLCOLORDLG: {
            return reinterpret_cast<LRESULT>(state ? state->hBrushBg : GetStockObject(WHITE_BRUSH));
        }

        case WM_CTLCOLORSTATIC: {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(25, 25, 30));
            return reinterpret_cast<LRESULT>(state ? state->hBrushBg : GetStockObject(WHITE_BRUSH));
        }

        case WM_COMMAND: {
            const int id = LOWORD(wParam);
            const int code = HIWORD(wParam);

            if (id == IDC_LAUNCHER_ISO_BROWSE && code == BN_CLICKED) {
                std::string chosen = PickDisc(state->disc);
                if (!chosen.empty() && IsGameDisc(chosen)) {
                    state->disc = chosen;
                    std::wstring wide = Utf8ToWide(state->disc);
                    SetWindowTextW(state->hIsoEdit, wide.c_str());
                    ps2host::SetSetting("disc", state->disc);
                }
            } else if (id == IDC_LAUNCHER_BTN_LOAD_PROFILE && code == BN_CLICKED) {
                int sel = (int)SendMessageW(state->hComboProfiles, CB_GETCURSEL, 0, 0);
                if (sel >= 0 && sel < (int)state->available_profiles.size()) {
                    const auto& prof = state->available_profiles[sel];
                    const std::filesystem::path dst_dir = state->here / "memcard" / "BESLES-52584";
                    std::error_code ec;
                    std::filesystem::create_directories(dst_dir, ec);
                    std::filesystem::copy(prof.second, dst_dir, std::filesystem::copy_options::overwrite_existing | std::filesystem::copy_options::recursive, ec);
                    RefreshProfilesList(state);
                    MessageBoxW(hwnd, L"\x00A1Perfil cargado con \x00E9xito!\nAl iniciar el juego se usar\x00E1 esta partida.",
                                L"Partida Cargada", MB_OK | MB_ICONINFORMATION);
                }
            } else if (id == IDC_LAUNCHER_BTN_BACKUP_PROFILE && code == BN_CLICKED) {
                const std::filesystem::path src_dir = state->here / "memcard" / "BESLES-52584";
                const std::filesystem::path active_save = src_dir / "BESLES-52584";
                if (std::filesystem::exists(active_save)) {
                    std::wstring active_name = ReadSaveProfileName(active_save);
                    if (active_name.empty()) active_name = L"perfil";
                    auto now = std::chrono::system_clock::now();
                    std::time_t tt = std::chrono::system_clock::to_time_t(now);
                    std::tm tm;
                    localtime_s(&tm, &tt);
                    wchar_t time_buf[64];
                    std::wcsftime(time_buf, sizeof(time_buf) / sizeof(wchar_t), L"%Y-%m-%d_%H-%M", &tm);
                    std::wstring backup_folder = L"Respaldo_" + std::wstring(time_buf) + L"_" + active_name;
                    std::filesystem::path backup_dir = state->here / "memcard" / "perfiles" / backup_folder;
                    std::error_code ec;
                    std::filesystem::create_directories(backup_dir, ec);
                    std::filesystem::copy(src_dir, backup_dir, std::filesystem::copy_options::overwrite_existing | std::filesystem::copy_options::recursive, ec);
                    RefreshProfilesList(state);
                    MessageBoxW(hwnd, (L"\x00A1Respaldo creado con \x00E9xito en:\nperfiles\\" + backup_folder).c_str(),
                                L"Respaldo Creado", MB_OK | MB_ICONINFORMATION);
                } else {
                    MessageBoxW(hwnd, L"No hay ninguna partida activa en memcard/BESLES-52584 para respaldar.",
                                L"Aviso", MB_OK | MB_ICONWARNING);
                }
            } else if (id == IDC_LAUNCHER_BTN_OPEN_SAVES && code == BN_CLICKED) {
                std::filesystem::path memcard_dir = state->here / "memcard";
                std::error_code ec;
                if (!std::filesystem::exists(memcard_dir, ec)) std::filesystem::create_directories(memcard_dir, ec);
                ShellExecuteW(nullptr, L"open", memcard_dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            } else if (id == IDC_LAUNCHER_BTN_OPEN_TEXTURES && code == BN_CLICKED) {
                std::filesystem::path tex_dir = state->here / "texture pack";
                std::error_code ec;
                if (!std::filesystem::exists(tex_dir, ec)) std::filesystem::create_directories(tex_dir, ec);
                ShellExecuteW(nullptr, L"open", tex_dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            } else if (id == IDC_LAUNCHER_BTN_PLAY && code == BN_CLICKED) {
                // Save settings
                int res_sel = (int)SendMessageW(state->hComboRes, CB_GETCURSEL, 0, 0);
                const char* res_vals[] = {"original", "2x", "4x", "display"};
                if (res_sel >= 0 && res_sel < 4) ps2host::SetSetting("resolution", res_vals[res_sel]);

                bool full = (SendMessageW(state->hChkFullscreen, BM_GETCHECK, 0, 0) == BST_CHECKED);
                ps2host::SetSettingOn("fullscreen", full);

                int dist_sel = (int)SendMessageW(state->hComboTexDist, CB_GETCURSEL, 0, 0);
                const char* dist_vals[] = {"original", "enhanced", "max"};
                if (dist_sel >= 0 && dist_sel < 3) ps2host::SetSetting("texture_distance", dist_vals[dist_sel]);

                bool vsync = (SendMessageW(state->hChkVSync, BM_GETCHECK, 0, 0) == BST_CHECKED);
                ps2host::SetSettingOn("vsync", vsync);

                int filter_sel = (int)SendMessageW(state->hComboTexFilter, CB_GETCURSEL, 0, 0);
                const char* filter_vals[] = {"original", "bilinear"};
                if (filter_sel >= 0 && filter_sel < 2) ps2host::SetSetting("texture_filtering", filter_vals[filter_sel]);

                bool fps = (SendMessageW(state->hChkFps, BM_GETCHECK, 0, 0) == BST_CHECKED);
                ps2host::SetSettingOn("fps_counter", fps);

                int aspect_sel = (int)SendMessageW(state->hComboAspect, CB_GETCURSEL, 0, 0);
                const char* aspect_vals[] = {"16:9", "4:3", "auto"};
                if (aspect_sel >= 0 && aspect_sel < 3) ps2host::SetSetting("aspect", aspect_vals[aspect_sel]);

                ps2host::SetSettingOn("ask_disc", true);

                if (state->disc.empty() || !IsGameDisc(state->disc)) {
                    int resp = MessageBoxW(hwnd,
                        L"Para iniciar el juego necesitas seleccionar tu copia de la imagen ISO de Burnout 3: Takedown (PAL / SLES-52584).\n\n\x00BFDeseas buscar y seleccionar tu archivo ISO ahora?",
                        L"Seleccionar ISO del Juego",
                        MB_YESNO | MB_ICONINFORMATION);
                    if (resp == IDYES) {
                        std::string chosen = PickDisc(state->disc);
                        if (!chosen.empty() && IsGameDisc(chosen)) {
                            state->disc = chosen;
                            std::wstring wide = Utf8ToWide(state->disc);
                            SetWindowTextW(state->hIsoEdit, wide.c_str());
                            ps2host::SetSetting("disc", state->disc);
                        } else if (!chosen.empty()) {
                            MessageBoxW(hwnd,
                                L"El archivo seleccionado no corresponde a la imagen de disco oficial de Burnout 3: Takedown (PAL / SLES-52584).\n\nPor favor selecciona una ISO v\x00E1lida de la versi\x00F3n europea de PS2.",
                                L"ISO no reconocida", MB_OK | MB_ICONWARNING);
                            return 0;
                        } else {
                            return 0;
                        }
                    } else {
                        return 0;
                    }
                }

                if (!state->disc.empty() && IsGameDisc(state->disc)) {
                    ps2host::SetSetting("disc", state->disc);
                }

                state->confirmed = true;
                DestroyWindow(hwnd);
            } else if (id == IDC_LAUNCHER_BTN_EXIT && code == BN_CLICKED) {
                state->confirmed = false;
                DestroyWindow(hwnd);
            }
            return 0;
        }

        case WM_CLOSE: {
            if (state) state->confirmed = false;
            DestroyWindow(hwnd);
            return 0;
        }

        case WM_DESTROY: {
            PostQuitMessage(0);
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool ShowLauncher(const std::filesystem::path& here, std::string& disc) {
    LauncherState state;
    state.here = here;
    state.disc = disc;

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = LauncherWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(240, 240, 244));
    wc.lpszClassName = L"Burnout3LauncherClass";
    RegisterClassExW(&wc);

    const int w = 600, h = 685;
    const int x = (GetSystemMetrics(SM_CXSCREEN) - w) / 2;
    const int y = (GetSystemMetrics(SM_CYSCREEN) - h) / 2;

    HWND hwnd = CreateWindowExW(
        0, L"Burnout3LauncherClass", L"Burnout 3: Takedown - Lanzador de PC",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        x, y, w, h,
        nullptr, nullptr, GetModuleHandleW(nullptr), &state);

    if (!hwnd) return false;

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (msg.message == WM_KEYDOWN) {
            if (msg.wParam == VK_RETURN) {
                SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDC_LAUNCHER_BTN_PLAY, BN_CLICKED), 0);
                continue;
            } else if (msg.wParam == VK_ESCAPE) {
                SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDC_LAUNCHER_BTN_EXIT, BN_CLICKED), 0);
                continue;
            }
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (state.hBannerBmp) DeleteObject(state.hBannerBmp);
    if (state.hFontUi) DeleteObject(state.hFontUi);
    if (state.hFontBold) DeleteObject(state.hFontBold);
    if (state.hFontPlay) DeleteObject(state.hFontPlay);
    if (state.hBrushBg) DeleteObject(state.hBrushBg);
    UnregisterClassW(L"Burnout3LauncherClass", GetModuleHandleW(nullptr));

    if (state.confirmed && IsGameDisc(state.disc)) {
        disc = state.disc;
        return true;
    }
    return false;
}

#endif  // PS2HOST_LAUNCHER_INC
