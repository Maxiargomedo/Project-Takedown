#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include "resource.h"

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "uuid.lib")

namespace fs = std::filesystem;

const wchar_t* kUninstallSubKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Burnout 3 Takedown PC";

constexpr int IDC_BANNER        = 1001;
constexpr int IDC_DEST_EDIT     = 1002;
constexpr int IDC_BROWSE_BTN    = 1003;
constexpr int IDC_CHK_DESKTOP   = 1004;
constexpr int IDC_CHK_STARTMENU = 1005;
constexpr int IDC_PROGRESS      = 1006;
constexpr int IDC_STATUS_TXT    = 1007;
constexpr int IDC_BTN_INSTALL   = 1008; // "INSTALAR AHORA" / "RESTAURAR"
constexpr int IDC_BTN_UNINSTALL = 1009; // "DESINSTALAR"
constexpr int IDC_BTN_EXIT      = 1010; // "Salir" / "Cerrar"

constexpr UINT WM_INSTALL_PROGRESS = WM_USER + 101;
constexpr UINT WM_INSTALL_DONE     = WM_USER + 102;
constexpr UINT WM_INSTALL_ERROR    = WM_USER + 103;
constexpr UINT WM_UNINSTALL_DONE   = WM_USER + 104;

struct AppState {
    HWND hwnd = nullptr;
    HWND hDestEdit = nullptr;
    HWND hBrowseBtn = nullptr;
    HWND hChkDesktop = nullptr;
    HWND hChkStartMenu = nullptr;
    HWND hProgress = nullptr;
    HWND hStatusTxt = nullptr;
    HWND hBtnInstall = nullptr;
    HWND hBtnUninstall = nullptr;
    HWND hBtnExit = nullptr;

    HWND hGrpDest = nullptr;
    HWND hLblDest = nullptr;
    HWND hGrpInfo = nullptr;
    HWND hLblInfo = nullptr;

    HFONT hFontUi = nullptr;
    HFONT hFontBold = nullptr;
    HFONT hFontPlay = nullptr;
    HBRUSH hBrushBg = nullptr;
    HBITMAP hBannerBmp = nullptr;

    bool is_already_installed = false;
    bool is_uninstall_only_mode = false;
    bool is_installed_or_restored = false;
    bool is_uninstalled_done = false;
    bool is_busy = false;
    bool make_desktop = true;
    bool make_startmenu = true;
    std::wstring installed_path;
};

HRESULT CreateShortcut(const std::wstring& target, const std::wstring& lnk_path, const std::wstring& icon, const std::wstring& desc, const std::wstring& dir) {
    IShellLinkW* psl = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (void**)&psl);
    if (SUCCEEDED(hr)) {
        psl->SetPath(target.c_str());
        psl->SetWorkingDirectory(dir.c_str());
        psl->SetDescription(desc.c_str());
        if (!icon.empty()) {
            psl->SetIconLocation(icon.c_str(), 0);
        }
        IPersistFile* ppf = nullptr;
        hr = psl->QueryInterface(IID_IPersistFile, (void**)&ppf);
        if (SUCCEEDED(hr)) {
            hr = ppf->Save(lnk_path.c_str(), TRUE);
            ppf->Release();
        }
        psl->Release();
    }
    return hr;
}

std::wstring GetSpecialFolderPath(int csidl) {
    wchar_t path[MAX_PATH] = {};
    SHGetFolderPathW(nullptr, csidl, nullptr, SHGFP_TYPE_CURRENT, path);
    return std::wstring(path);
}

bool CheckIfGameInstalled(std::wstring& out_path) {
    HKEY hKey = nullptr;
    LSTATUS status = RegOpenKeyExW(HKEY_CURRENT_USER, kUninstallSubKey, 0, KEY_READ, &hKey);
    if (status == ERROR_SUCCESS) {
        wchar_t buf[MAX_PATH] = {};
        DWORD cb = sizeof(buf);
        if (RegQueryValueExW(hKey, L"InstallLocation", nullptr, nullptr, reinterpret_cast<LPBYTE>(buf), &cb) == ERROR_SUCCESS) {
            out_path = buf;
            RegCloseKey(hKey);
            if (!out_path.empty() && (fs::exists(out_path + L"\\burnout3_pal.exe") || fs::exists(out_path + L"\\Burnout 3 Takedown.exe"))) {
                return true;
            }
        } else {
            RegCloseKey(hKey);
        }
    }
    // Fallback default folder check
    std::wstring def = L"C:\\Juegos\\Burnout 3 Takedown PC";
    if (fs::exists(def + L"\\burnout3_pal.exe") || fs::exists(def + L"\\Burnout 3 Takedown.exe")) {
        out_path = def;
        return true;
    }
    return false;
}

bool RegisterUninstallEntry(const std::wstring& dest_dir) {
    HKEY hKey = nullptr;
    DWORD disposition = 0;
    LSTATUS status = RegCreateKeyExW(
        HKEY_CURRENT_USER,
        kUninstallSubKey,
        0, nullptr,
        REG_OPTION_NON_VOLATILE,
        KEY_WRITE | KEY_QUERY_VALUE,
        nullptr,
        &hKey,
        &disposition
    );
    if (status != ERROR_SUCCESS) return false;

    auto set_string = [&](const wchar_t* name, const std::wstring& val) {
        RegSetValueExW(hKey, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(val.c_str()), static_cast<DWORD>((val.size() + 1) * sizeof(wchar_t)));
    };
    auto set_dword = [&](const wchar_t* name, DWORD val) {
        RegSetValueExW(hKey, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&val), sizeof(DWORD));
    };

    std::wstring display_name = L"Burnout 3: Takedown (Port Nativo PC)";
    std::wstring version = L"1.0.0";
    std::wstring publisher = L"Criterion Games / Port Nativo PC";
    std::wstring icon_path = dest_dir + L"\\burnout3.ico,0";
    std::wstring uninstall_cmd = L"\"" + dest_dir + L"\\Desinstalar.exe\" --uninstall";

    set_string(L"DisplayName", display_name);
    set_string(L"DisplayVersion", version);
    set_string(L"Publisher", publisher);
    set_string(L"DisplayIcon", icon_path);
    set_string(L"InstallLocation", dest_dir);
    set_string(L"UninstallString", uninstall_cmd);
    set_string(L"QuietUninstallString", uninstall_cmd);
    set_dword(L"EstimatedSize", 48000);
    set_dword(L"NoModify", 1);
    set_dword(L"NoRepair", 0);

    RegCloseKey(hKey);
    return true;
}

bool UnregisterUninstallEntry() {
    LSTATUS status = RegDeleteTreeW(HKEY_CURRENT_USER, kUninstallSubKey);
    return (status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND);
}

void DeleteShortcuts() {
    std::error_code ec;
    std::wstring desktop = GetSpecialFolderPath(CSIDL_DESKTOPDIRECTORY);
    if (!desktop.empty()) {
        fs::remove(desktop + L"\\Burnout 3 Takedown PC.lnk", ec);
        fs::remove(desktop + L"\\Lanzador Burnout 3.lnk", ec);
    }
    std::wstring pub_desktop = GetSpecialFolderPath(CSIDL_COMMON_DESKTOPDIRECTORY);
    if (!pub_desktop.empty()) {
        fs::remove(pub_desktop + L"\\Burnout 3 Takedown PC.lnk", ec);
        fs::remove(pub_desktop + L"\\Lanzador Burnout 3.lnk", ec);
    }
    std::wstring progs = GetSpecialFolderPath(CSIDL_PROGRAMS);
    if (!progs.empty()) {
        fs::remove_all(progs + L"\\Burnout 3 Takedown PC", ec);
    }
    std::wstring comm_progs = GetSpecialFolderPath(CSIDL_COMMON_PROGRAMS);
    if (!comm_progs.empty()) {
        fs::remove_all(comm_progs + L"\\Burnout 3 Takedown PC", ec);
    }
}

void DeleteInstalledFiles(const std::wstring& dest_dir) {
    std::error_code ec;
    const std::vector<std::wstring> files_to_delete = {
        L"burnout3_pal.exe",
        L"Burnout 3 Takedown.exe",
        L"Lanzador_Burnout3.exe",
        L"burnout3.ico",
        L"launcher_banner.bmp",
        L"pc_settings.ini",
        L"LEEME_PRIMERO.txt",
        L"Seleccionar_ISO.bat",
        L"Jugar_Burnout3_PAL.bat",
        L"Desinstalar.bat",
        L"Desinstalar.exe",
        L"b3_setup_payload.zip",
        L"gpu_pipelines.txt",
        L"gpu_pipeline_cache.bin"
    };
    for (const auto& file : files_to_delete) {
        fs::remove(dest_dir + L"\\" + file, ec);
    }
    fs::remove_all(dest_dir + L"\\texture pack", ec);

    // Keep savegames in memcard if any exists
    bool has_user_saves = false;
    std::wstring memcard_dir = dest_dir + L"\\memcard";
    if (fs::exists(memcard_dir)) {
        for (const auto& entry : fs::recursive_directory_iterator(memcard_dir, ec)) {
            if (entry.is_regular_file()) {
                std::wstring name = entry.path().filename().wstring();
                if (name != L"LEEME_PARTIDAS.txt") {
                    has_user_saves = true;
                    break;
                }
            }
        }
    }
    if (!has_user_saves) {
        fs::remove_all(memcard_dir, ec);
    }

    // Remove folder if now empty
    fs::remove(dest_dir, ec);
}

DWORD WINAPI UninstallThread(LPVOID param) {
    AppState* state = static_cast<AppState*>(param);
    HRESULT hr_co = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    PostMessageW(state->hwnd, WM_INSTALL_PROGRESS, 20, (LPARAM)L"Eliminando accesos directos...");
    DeleteShortcuts();

    PostMessageW(state->hwnd, WM_INSTALL_PROGRESS, 50, (LPARAM)L"Eliminando entrada del Panel de Control...");
    UnregisterUninstallEntry();

    PostMessageW(state->hwnd, WM_INSTALL_PROGRESS, 75, (LPARAM)L"Eliminando archivos del juego...");
    DeleteInstalledFiles(state->installed_path);

    PostMessageW(state->hwnd, WM_INSTALL_PROGRESS, 95, (LPARAM)L"Actualizando sistema...");
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);

    if (SUCCEEDED(hr_co)) CoUninitialize();

    PostMessageW(state->hwnd, WM_INSTALL_PROGRESS, 100, (LPARAM)L"Desinstalacion completada con exito.");
    PostMessageW(state->hwnd, WM_UNINSTALL_DONE, 0, 0);
    return 0;
}

DWORD WINAPI InstallThread(LPVOID param) {
    AppState* state = static_cast<AppState*>(param);
    HRESULT hr_co = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    auto finish_error = [&](const wchar_t* msg) -> DWORD {
        if (SUCCEEDED(hr_co)) CoUninitialize();
        PostMessageW(state->hwnd, WM_INSTALL_ERROR, 0, (LPARAM)msg);
        return 1;
    };

    wchar_t dest_buf[MAX_PATH] = {};
    GetWindowTextW(state->hDestEdit, dest_buf, MAX_PATH);
    std::wstring dest_dir(dest_buf);

    while (!dest_dir.empty() && (dest_dir.back() == L'\\' || dest_dir.back() == L'/')) {
        dest_dir.pop_back();
    }

    if (dest_dir.empty()) {
        return finish_error(L"La ruta de instalacion no puede estar vacia.");
    }

    // Check for illegal characters in folder name
    for (size_t i = 2; i < dest_dir.size(); ++i) {
        if (dest_dir[i] == L':' || dest_dir[i] == L'*' || dest_dir[i] == L'?' ||
            dest_dir[i] == L'\"' || dest_dir[i] == L'<' || dest_dir[i] == L'>' || dest_dir[i] == L'|') {
            return finish_error(L"La ruta contiene caracteres no permitidos como dos puntos (:).");
        }
    }

    PostMessageW(state->hwnd, WM_INSTALL_PROGRESS, 10, (LPARAM)L"Preparando directorio de destino...");

    std::error_code ec;
    fs::create_directories(dest_dir, ec);
    if (ec) {
        return finish_error(L"No se pudo crear la carpeta de destino. Verifica los permisos.");
    }

    // 1. Locate embedded ZIP resource
    PostMessageW(state->hwnd, WM_INSTALL_PROGRESS, 25, (LPARAM)L"Extrayendo paquete de instalacion...");
    HMODULE hMod = GetModuleHandleW(nullptr);
    HRSRC hRes = FindResourceW(hMod, MAKEINTRESOURCEW(IDR_PAYLOAD_ZIP), (LPCWSTR)RT_RCDATA);
    if (!hRes) {
        return finish_error(L"Error: no se encontro el recurso de instalacion.");
    }
    HGLOBAL hGlobal = LoadResource(hMod, hRes);
    DWORD res_size = SizeofResource(hMod, hRes);
    const void* res_data = LockResource(hGlobal);
    if (!res_data || res_size == 0) {
        return finish_error(L"Error al leer el recurso de instalacion.");
    }

    wchar_t temp_path[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, temp_path);
    std::wstring zip_file = std::wstring(temp_path) + L"b3_setup_payload.zip";

    HANDLE hFile = CreateFileW(zip_file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        return finish_error(L"Error al escribir archivo temporal de instalacion.");
    }
    DWORD written = 0;
    WriteFile(hFile, res_data, res_size, &written, nullptr);
    CloseHandle(hFile);

    // 2. Extract with system tar.exe
    PostMessageW(state->hwnd, WM_INSTALL_PROGRESS, 45, (LPARAM)L"Instalando motor nativo y archivos del juego...");

    std::wstring cmd = L"tar.exe -xf \"" + zip_file + L"\" -C \"" + dest_dir + L"\"";
    STARTUPINFOW si = { sizeof(si) };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};
    
    if (CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 60000);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    DeleteFileW(zip_file.c_str());

    // 3. Ensure friendly executable copies exist in destination directory
    std::wstring pal_exe = dest_dir + L"\\burnout3_pal.exe";
    std::wstring b3_exe  = dest_dir + L"\\Burnout 3 Takedown.exe";
    std::wstring ldr_exe = dest_dir + L"\\Lanzador_Burnout3.exe";
    if (fs::exists(pal_exe)) {
        std::error_code ec_cp;
        fs::copy_file(pal_exe, b3_exe, fs::copy_options::overwrite_existing, ec_cp);
        fs::copy_file(pal_exe, ldr_exe, fs::copy_options::overwrite_existing, ec_cp);
    }

    // 4. Copy running installer to dest_dir\Desinstalar.exe for Windows Control Panel
    wchar_t self_exe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, self_exe, MAX_PATH);
    std::wstring unins_exe = dest_dir + L"\\Desinstalar.exe";
    std::error_code ec_self;
    fs::copy_file(self_exe, unins_exe, fs::copy_options::overwrite_existing, ec_self);

    // 5. Create Desinstalar.bat as fallback in destination folder
    std::wstring uninstall_bat = dest_dir + L"\\Desinstalar.bat";
    std::ofstream unins(uninstall_bat);
    if (unins) {
        unins << "@echo off\r\n";
        unins << "title Desinstalar Burnout 3 Takedown PC\r\n";
        unins << "echo =====================================================\r\n";
        unins << "echo   Deseas desinstalar Burnout 3 Takedown PC?\r\n";
        unins << "echo =====================================================\r\n";
        unins << "echo.\r\n";
        unins << "pause\r\n";
        unins << "echo Eliminando accesos directos...\r\n";
        unins << "del \"%USERPROFILE%\\Desktop\\Burnout 3 Takedown PC.lnk\" 2>nul\r\n";
        unins << "del \"%USERPROFILE%\\Desktop\\Lanzador Burnout 3.lnk\" 2>nul\r\n";
        unins << "del \"%PUBLIC%\\Desktop\\Burnout 3 Takedown PC.lnk\" 2>nul\r\n";
        unins << "del \"%PUBLIC%\\Desktop\\Lanzador Burnout 3.lnk\" 2>nul\r\n";
        unins << "rd /s /q \"%APPDATA%\\Microsoft\\Windows\\Start Menu\\Programs\\Burnout 3 Takedown PC\" 2>nul\r\n";
        unins << "rd /s /q \"%PROGRAMDATA%\\Microsoft\\Windows\\Start Menu\\Programs\\Burnout 3 Takedown PC\" 2>nul\r\n";
        unins << "echo Eliminando registro de Windows...\r\n";
        unins << "reg delete \"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Burnout 3 Takedown PC\" /f 2>nul\r\n";
        unins << "echo.\r\n";
        unins << "echo Los archivos del juego estan en esta carpeta:\r\n";
        unins << "echo %~dp0\r\n";
        unins << "echo Puedes borrar esta carpeta manualmente para completar la desinstalacion.\r\n";
        unins << "echo.\r\n";
        unins << "pause\r\n";
    }

    // 6. Register in Windows Control Panel (Programs and Features / Settings)
    PostMessageW(state->hwnd, WM_INSTALL_PROGRESS, 70, (LPARAM)L"Registrando en el Panel de Control de Windows...");
    RegisterUninstallEntry(dest_dir);

    // 7. Create Shortcuts (Exactly 1 on Desktop, and Start Menu group)
    PostMessageW(state->hwnd, WM_INSTALL_PROGRESS, 85, (LPARAM)L"Creando accesos directos...");

    std::wstring main_exe = fs::exists(b3_exe) ? b3_exe : pal_exe;
    std::wstring ico_target = dest_dir + L"\\burnout3.ico";
    std::wstring iso_tool   = dest_dir + L"\\Seleccionar_ISO.bat";

    if (state->make_desktop) {
        std::wstring desktop = GetSpecialFolderPath(CSIDL_DESKTOPDIRECTORY);
        if (!desktop.empty()) {
            std::wstring lnk_game = desktop + L"\\Burnout 3 Takedown PC.lnk";
            CreateShortcut(main_exe, lnk_game, ico_target, L"Jugar a Burnout 3: Takedown (Port Nativo PC)", dest_dir);
        }
    }

    if (state->make_startmenu) {
        std::wstring progs = GetSpecialFolderPath(CSIDL_PROGRAMS);
        if (!progs.empty()) {
            std::wstring sm_folder = progs + L"\\Burnout 3 Takedown PC";
            std::error_code ec_sm;
            fs::create_directories(sm_folder, ec_sm);
            CreateShortcut(main_exe, sm_folder + L"\\Burnout 3 Takedown PC.lnk", ico_target, L"Jugar a Burnout 3: Takedown", dest_dir);
            CreateShortcut(iso_tool, sm_folder + L"\\Cambiar ISO del Juego.lnk", ico_target, L"Seleccionar imagen de disco ISO", dest_dir);
            CreateShortcut(unins_exe, sm_folder + L"\\Desinstalar Burnout 3 PC.lnk", ico_target, L"Desinstalar el juego", dest_dir);
        }
    }

    // 8. Notify Windows Shell
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);

    state->installed_path = dest_dir;
    if (SUCCEEDED(hr_co)) CoUninitialize();
    PostMessageW(state->hwnd, WM_INSTALL_PROGRESS, 100, (LPARAM)L"Operacion completada con exito!");
    PostMessageW(state->hwnd, WM_INSTALL_DONE, 0, 0);
    return 0;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    AppState* state = reinterpret_cast<AppState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
        case WM_CREATE: {
            CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            state = reinterpret_cast<AppState*>(cs->lpCreateParams);
            state->hwnd = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));

            // Fonts
            state->hFontUi = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                         DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                         CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            state->hFontBold = CreateFontW(16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                           CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            state->hFontPlay = CreateFontW(18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                           CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            state->hBrushBg = CreateSolidBrush(RGB(240, 240, 244));

            // Banner
            state->hBannerBmp = LoadBitmapW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDB_BANNER));
            HWND hBanner = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_BITMAP,
                                           12, 10, 560, 135, hwnd, (HMENU)(INT_PTR)(IDC_BANNER), nullptr, nullptr);
            if (state->hBannerBmp) {
                SendMessageW(hBanner, STM_SETIMAGE, IMAGE_BITMAP, reinterpret_cast<LPARAM>(state->hBannerBmp));
            }

            // GroupBox 1: Destination / State
            const wchar_t* grp1_title = state->is_already_installed ? L" Estado de la Instalaci\x00F3n " : L" Destino de la Instalaci\x00F3n ";
            state->hGrpDest = CreateWindowExW(0, L"BUTTON", grp1_title,
                                              WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                              12, 155, 560, 125, hwnd, nullptr, nullptr, nullptr);
            SendMessageW(state->hGrpDest, WM_SETFONT, reinterpret_cast<WPARAM>(state->hFontBold), TRUE);

            const wchar_t* lbl1_title = state->is_already_installed 
                ? L"El juego ya est\x00E1 instalado en tu equipo:" 
                : L"Carpeta donde se instalar\x00E1 el juego:";
            state->hLblDest = CreateWindowExW(0, L"STATIC", lbl1_title,
                                              WS_CHILD | WS_VISIBLE,
                                              25, 180, 400, 20, hwnd, nullptr, nullptr, nullptr);
            SendMessageW(state->hLblDest, WM_SETFONT, reinterpret_cast<WPARAM>(state->hFontUi), TRUE);

            std::wstring initial_path = state->installed_path.empty() ? L"C:\\Juegos\\Burnout 3 Takedown PC" : state->installed_path;
            state->hDestEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", initial_path.c_str(),
                                               WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                               25, 205, 410, 25, hwnd, (HMENU)(INT_PTR)(IDC_DEST_EDIT), nullptr, nullptr);
            SendMessageW(state->hDestEdit, WM_SETFONT, reinterpret_cast<WPARAM>(state->hFontUi), TRUE);

            state->hBrowseBtn = CreateWindowExW(0, L"BUTTON", L"Examinar...",
                                                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                                445, 204, 115, 27, hwnd, (HMENU)(INT_PTR)(IDC_BROWSE_BTN), nullptr, nullptr);
            SendMessageW(state->hBrowseBtn, WM_SETFONT, reinterpret_cast<WPARAM>(state->hFontUi), TRUE);

            state->hChkDesktop = CreateWindowExW(0, L"BUTTON", L"Crear acceso directo en el Escritorio",
                                                 WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                                 25, 245, 260, 22, hwnd, (HMENU)(INT_PTR)(IDC_CHK_DESKTOP), nullptr, nullptr);
            SendMessageW(state->hChkDesktop, WM_SETFONT, reinterpret_cast<WPARAM>(state->hFontUi), TRUE);
            SendMessageW(state->hChkDesktop, BM_SETCHECK, BST_CHECKED, 0);

            state->hChkStartMenu = CreateWindowExW(0, L"BUTTON", L"Crear en el Men\x00FA Inicio",
                                                   WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                                   300, 245, 250, 22, hwnd, (HMENU)(INT_PTR)(IDC_CHK_STARTMENU), nullptr, nullptr);
            SendMessageW(state->hChkStartMenu, WM_SETFONT, reinterpret_cast<WPARAM>(state->hFontUi), TRUE);
            SendMessageW(state->hChkStartMenu, BM_SETCHECK, BST_CHECKED, 0);

            // GroupBox 2: Information & Legal Notice or Maintenance options
            const wchar_t* grp2_title = state->is_already_installed ? L" Opciones de Mantenimiento " : L" Caracter\x00EDsticas y Aviso Legal ";
            state->hGrpInfo = CreateWindowExW(0, L"BUTTON", grp2_title,
                                              WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                              12, 290, 560, 140, hwnd, nullptr, nullptr, nullptr);
            SendMessageW(state->hGrpInfo, WM_SETFONT, reinterpret_cast<WPARAM>(state->hFontBold), TRUE);

            const wchar_t* fresh_info = 
                L"\x2022 Port nativo a 60 FPS estables con resoluci\x00F3n interna escalable hasta 4K Ultra HD.\n"
                L"\x2022 Filtrado de texturas suave anti-p\x00EDxel, gestor de partidas y lanzador integrado.\n"
                L"\x2022 Men\x00FA de pausa nativo con confirmaci\x00F3n de salida y ajustes en caliente (F9).\n\n"
                L"\x26A0 AVISO: Este paquete NO incluye la imagen ISO del juego de PS2.\n"
                L"  Para jugar se requiere tu propia ISO de Burnout 3: Takedown (PAL).";

            const wchar_t* maint_info = 
                L"\x2022 RESTAURAR: Reinstala los archivos del motor, restablece la configuraci\x00F3n inicial\n"
                L"  de f\x00E1brica, registra en el Panel de Control y recrea los accesos directos.\n\n"
                L"\x2022 DESINSTALAR: Elimina el juego por completo de tu equipo y lo borra del\n"
                L"  Panel de Control de Windows (Tus partidas en 'memcard' se conservar\x00E1n).";

            const wchar_t* unins_only_info =
                L"\x00BFDeseas desinstalar Burnout 3 Takedown PC de tu equipo?\n\n"
                L"\x2022 Se eliminar\x00E1n los ejecutables, texturas y configuraci\x00F3n.\n"
                L"\x2022 Se remover\x00E1n los accesos directos y el registro de Windows.\n"
                L"\x2022 Tus partidas guardadas en la carpeta 'memcard' no se perder\x00E1n.";

            const wchar_t* info_text = state->is_uninstall_only_mode ? unins_only_info : (state->is_already_installed ? maint_info : fresh_info);
            state->hLblInfo = CreateWindowExW(0, L"STATIC", info_text,
                                              WS_CHILD | WS_VISIBLE,
                                              25, 315, 535, 105, hwnd, nullptr, nullptr, nullptr);
            SendMessageW(state->hLblInfo, WM_SETFONT, reinterpret_cast<WPARAM>(state->hFontUi), TRUE);

            // Progress & Status
            const wchar_t* init_status = state->is_uninstall_only_mode 
                ? L"Listo para desinstalar el juego de tu equipo."
                : (state->is_already_installed ? L"El juego ya est\x00E1 instalado. Elige restaurar o desinstalar." : L"Listo para instalar en tu equipo.");
            state->hStatusTxt = CreateWindowExW(0, L"STATIC", init_status,
                                                WS_CHILD | WS_VISIBLE,
                                                25, 442, 535, 20, hwnd, (HMENU)(INT_PTR)(IDC_STATUS_TXT), nullptr, nullptr);
            SendMessageW(state->hStatusTxt, WM_SETFONT, reinterpret_cast<WPARAM>(state->hFontBold), TRUE);

            state->hProgress = CreateWindowExW(0, PROGRESS_CLASSW, L"",
                                               WS_CHILD | WS_VISIBLE | PBS_SMOOTH,
                                               20, 467, 544, 22, hwnd, (HMENU)(INT_PTR)(IDC_PROGRESS), nullptr, nullptr);
            SendMessageW(state->hProgress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
            SendMessageW(state->hProgress, PBM_SETPOS, 0, 0);

            // Action Buttons
            if (state->is_uninstall_only_mode) {
                state->hBtnInstall = nullptr;
                state->hBtnUninstall = CreateWindowExW(0, L"BUTTON", L"\x25BA  DESINSTALAR AHORA",
                                                     WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                                                     100, 510, 240, 45, hwnd, (HMENU)(INT_PTR)(IDC_BTN_UNINSTALL), nullptr, nullptr);
                SendMessageW(state->hBtnUninstall, WM_SETFONT, reinterpret_cast<WPARAM>(state->hFontPlay), TRUE);

                state->hBtnExit = CreateWindowExW(0, L"BUTTON", L"Cancelar",
                                                  WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                                  360, 510, 130, 45, hwnd, (HMENU)(INT_PTR)(IDC_BTN_EXIT), nullptr, nullptr);
                SendMessageW(state->hBtnExit, WM_SETFONT, reinterpret_cast<WPARAM>(state->hFontUi), TRUE);
            } else if (state->is_already_installed) {
                state->hBtnInstall = CreateWindowExW(0, L"BUTTON", L"\x25BA  RESTAURAR",
                                                     WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                                                     25, 510, 185, 45, hwnd, (HMENU)(INT_PTR)(IDC_BTN_INSTALL), nullptr, nullptr);
                SendMessageW(state->hBtnInstall, WM_SETFONT, reinterpret_cast<WPARAM>(state->hFontPlay), TRUE);

                state->hBtnUninstall = CreateWindowExW(0, L"BUTTON", L"DESINSTALAR",
                                                       WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                                       225, 510, 175, 45, hwnd, (HMENU)(INT_PTR)(IDC_BTN_UNINSTALL), nullptr, nullptr);
                SendMessageW(state->hBtnUninstall, WM_SETFONT, reinterpret_cast<WPARAM>(state->hFontPlay), TRUE);

                state->hBtnExit = CreateWindowExW(0, L"BUTTON", L"Salir",
                                                  WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                                  415, 510, 145, 45, hwnd, (HMENU)(INT_PTR)(IDC_BTN_EXIT), nullptr, nullptr);
                SendMessageW(state->hBtnExit, WM_SETFONT, reinterpret_cast<WPARAM>(state->hFontUi), TRUE);
            } else {
                state->hBtnInstall = CreateWindowExW(0, L"BUTTON", L"\x25BA  INSTALAR AHORA",
                                                     WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                                                     100, 510, 240, 45, hwnd, (HMENU)(INT_PTR)(IDC_BTN_INSTALL), nullptr, nullptr);
                SendMessageW(state->hBtnInstall, WM_SETFONT, reinterpret_cast<WPARAM>(state->hFontPlay), TRUE);

                state->hBtnUninstall = nullptr;

                state->hBtnExit = CreateWindowExW(0, L"BUTTON", L"Salir",
                                                  WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                                  360, 510, 130, 45, hwnd, (HMENU)(INT_PTR)(IDC_BTN_EXIT), nullptr, nullptr);
                SendMessageW(state->hBtnExit, WM_SETFONT, reinterpret_cast<WPARAM>(state->hFontUi), TRUE);
            }

            return 0;
        }

        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN: {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            SetBkMode(hdc, TRANSPARENT);
            return reinterpret_cast<INT_PTR>(state->hBrushBg);
        }

        case WM_COMMAND: {
            int id = LOWORD(wParam);
            int code = HIWORD(wParam);

            if (id == IDC_BROWSE_BTN && code == BN_CLICKED && !state->is_busy && !state->is_installed_or_restored) {
                BROWSEINFOW bi = {};
                bi.hwndOwner = hwnd;
                bi.lpszTitle = L"Selecciona la carpeta donde deseas instalar Burnout 3 Takedown PC:";
                bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
                LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
                if (pidl) {
                    wchar_t selected[MAX_PATH] = {};
                    if (SHGetPathFromIDListW(pidl, selected)) {
                        std::wstring folder(selected);
                        if (folder.find(L"Burnout") == std::wstring::npos) {
                            folder += L"\\Burnout 3 Takedown PC";
                        }
                        SetWindowTextW(state->hDestEdit, folder.c_str());
                    }
                    CoTaskMemFree(pidl);
                }
                return 0;
            }

            if (id == IDC_BTN_INSTALL && code == BN_CLICKED) {
                if (state->is_installed_or_restored) {
                    // Launch game and close installer
                    std::wstring exe = state->installed_path + L"\\Burnout 3 Takedown.exe";
                    if (!fs::exists(exe)) exe = state->installed_path + L"\\burnout3_pal.exe";
                    ShellExecuteW(nullptr, L"open", exe.c_str(), nullptr, state->installed_path.c_str(), SW_SHOWNORMAL);
                    DestroyWindow(hwnd);
                    return 0;
                }

                if (state->is_busy) return 0;

                state->make_desktop = (SendMessageW(state->hChkDesktop, BM_GETCHECK, 0, 0) == BST_CHECKED);
                state->make_startmenu = (SendMessageW(state->hChkStartMenu, BM_GETCHECK, 0, 0) == BST_CHECKED);

                state->is_busy = true;
                EnableWindow(state->hDestEdit, FALSE);
                EnableWindow(state->hBrowseBtn, FALSE);
                EnableWindow(state->hChkDesktop, FALSE);
                EnableWindow(state->hChkStartMenu, FALSE);
                EnableWindow(state->hBtnInstall, FALSE);
                if (state->hBtnUninstall) EnableWindow(state->hBtnUninstall, FALSE);
                EnableWindow(state->hBtnExit, FALSE);

                SetWindowTextW(state->hBtnInstall, state->is_already_installed ? L"Restaurando..." : L"Instalando...");

                CreateThread(nullptr, 0, InstallThread, state, 0, nullptr);
                return 0;
            }

            if (id == IDC_BTN_UNINSTALL && code == BN_CLICKED) {
                if (state->is_busy) return 0;

                int resp = MessageBoxW(
                    hwnd,
                    L"\x00BFEst\x00E1s seguro de que deseas desinstalar Burnout 3 Takedown PC de tu equipo?\n\nSe eliminar\x00E1n los ejecutables, texturas, accesos directos y el registro de Windows.\n(Tus partidas guardadas en la carpeta 'memcard' se conservar\x00E1n).",
                    L"Confirmar Desinstalaci\x00F3n",
                    MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2
                );
                if (resp != IDYES) return 0;

                state->is_busy = true;
                EnableWindow(state->hDestEdit, FALSE);
                EnableWindow(state->hBrowseBtn, FALSE);
                EnableWindow(state->hChkDesktop, FALSE);
                EnableWindow(state->hChkStartMenu, FALSE);
                if (state->hBtnInstall) EnableWindow(state->hBtnInstall, FALSE);
                EnableWindow(state->hBtnUninstall, FALSE);
                EnableWindow(state->hBtnExit, FALSE);

                SetWindowTextW(state->hBtnUninstall, L"Desinstalando...");

                CreateThread(nullptr, 0, UninstallThread, state, 0, nullptr);
                return 0;
            }

            if (id == IDC_BTN_EXIT && code == BN_CLICKED) {
                if (!state->is_busy) {
                    DestroyWindow(hwnd);
                }
                return 0;
            }
            break;
        }

        case WM_INSTALL_PROGRESS: {
            int pos = static_cast<int>(wParam);
            const wchar_t* txt = reinterpret_cast<const wchar_t*>(lParam);
            SendMessageW(state->hProgress, PBM_SETPOS, pos, 0);
            if (txt) SetWindowTextW(state->hStatusTxt, txt);
            return 0;
        }

        case WM_INSTALL_DONE: {
            state->is_busy = false;
            state->is_installed_or_restored = true;
            SendMessageW(state->hProgress, PBM_SETPOS, 100, 0);
            if (state->is_already_installed) {
                SetWindowTextW(state->hStatusTxt, L"\x2713 \x00A1Juego restaurado y reparado con \x00E9xito! Ya puedes jugar.");
            } else {
                SetWindowTextW(state->hStatusTxt, L"\x2713 \x00A1Instalaci\x00F3n completada con \x00E9xito! Ya puedes jugar.");
            }
            
            if (state->hBtnUninstall) ShowWindow(state->hBtnUninstall, SW_HIDE);
            SetWindowPos(state->hBtnInstall, nullptr, 100, 510, 240, 45, SWP_NOZORDER | SWP_SHOWWINDOW);
            SetWindowPos(state->hBtnExit, nullptr, 360, 510, 130, 45, SWP_NOZORDER | SWP_SHOWWINDOW);

            EnableWindow(state->hBtnInstall, TRUE);
            EnableWindow(state->hBtnExit, TRUE);
            SetWindowTextW(state->hBtnInstall, L"\x25BA  INICIAR JUEGO AHORA");
            SetWindowTextW(state->hBtnExit, L"Cerrar");
            SetFocus(state->hBtnInstall);
            return 0;
        }

        case WM_UNINSTALL_DONE: {
            state->is_busy = false;
            state->is_uninstalled_done = true;
            SendMessageW(state->hProgress, PBM_SETPOS, 100, 0);
            SetWindowTextW(state->hStatusTxt, L"\x2713 \x00A1Burnout 3 Takedown PC se ha desinstalado con \x00E9xito!");

            if (state->hBtnInstall) ShowWindow(state->hBtnInstall, SW_HIDE);
            if (state->hBtnUninstall) ShowWindow(state->hBtnUninstall, SW_HIDE);

            SetWindowPos(state->hBtnExit, nullptr, 220, 510, 160, 45, SWP_NOZORDER | SWP_SHOWWINDOW);
            EnableWindow(state->hBtnExit, TRUE);
            SetWindowTextW(state->hBtnExit, L"Cerrar");
            SetFocus(state->hBtnExit);
            return 0;
        }

        case WM_INSTALL_ERROR: {
            state->is_busy = false;
            const wchar_t* err = reinterpret_cast<const wchar_t*>(lParam);
            MessageBoxW(hwnd, err ? err : L"Error durante la operacion.", L"Error", MB_OK | MB_ICONERROR);
            
            EnableWindow(state->hDestEdit, TRUE);
            EnableWindow(state->hBrowseBtn, TRUE);
            EnableWindow(state->hChkDesktop, TRUE);
            EnableWindow(state->hChkStartMenu, TRUE);
            if (state->hBtnInstall) EnableWindow(state->hBtnInstall, TRUE);
            if (state->hBtnUninstall) EnableWindow(state->hBtnUninstall, TRUE);
            EnableWindow(state->hBtnExit, TRUE);

            if (state->hBtnInstall) {
                SetWindowTextW(state->hBtnInstall, state->is_already_installed ? L"\x25BA  RESTAURAR" : L"\x25BA  INSTALAR AHORA");
            }
            if (state->hBtnUninstall) {
                SetWindowTextW(state->hBtnUninstall, L"DESINSTALAR");
            }
            SetWindowTextW(state->hStatusTxt, L"Error en la operacion. Revisa los permisos o la ruta.");
            return 0;
        }

        case WM_CLOSE: {
            if (!state->is_busy) {
                DestroyWindow(hwnd);
            }
            return 0;
        }

        case WM_DESTROY: {
            if (state->hFontUi) DeleteObject(state->hFontUi);
            if (state->hFontBold) DeleteObject(state->hFontBold);
            if (state->hFontPlay) DeleteObject(state->hFontPlay);
            if (state->hBrushBg) DeleteObject(state->hBrushBg);
            if (state->hBannerBmp) DeleteObject(state->hBannerBmp);
            PostQuitMessage(0);
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR lpCmdLine, int) {
    INITCOMMONCONTROLSEX icex = { sizeof(icex), ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icex);
    CoInitialize(nullptr);

    AppState state;

    std::wstring cmdLine(lpCmdLine ? lpCmdLine : L"");
    bool is_uninstall_param = (cmdLine.find(L"--uninstall") != std::wstring::npos);
    bool is_temp_param = (cmdLine.find(L"--uninstall-from-temp") != std::wstring::npos);

    // If --uninstall called from install folder, relaunch from %TEMP% so executable can be deleted
    if (is_uninstall_param && !is_temp_param) {
        wchar_t self_path[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, self_path, MAX_PATH);
        std::wstring self_str(self_path);

        wchar_t temp_dir[MAX_PATH] = {};
        GetTempPathW(MAX_PATH, temp_dir);
        std::wstring temp_exe = std::wstring(temp_dir) + L"b3_uninstaller.exe";

        std::wstring parent_dir = fs::path(self_str).parent_path().wstring();

        std::error_code ec;
        fs::copy_file(self_str, temp_exe, fs::copy_options::overwrite_existing, ec);

        std::wstring relaunch_cmd = L"\"" + temp_exe + L"\" --uninstall-from-temp \"" + parent_dir + L"\"";
        STARTUPINFOW si = { sizeof(si) };
        PROCESS_INFORMATION pi = {};
        if (CreateProcessW(nullptr, &relaunch_cmd[0], nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            CoUninitialize();
            return 0;
        }
    }

    if (is_temp_param) {
        state.is_uninstall_only_mode = true;
        state.is_already_installed = true;
        // Parse directory after --uninstall-from-temp
        size_t pos = cmdLine.find(L"--uninstall-from-temp");
        if (pos != std::wstring::npos) {
            std::wstring sub = cmdLine.substr(pos + 21);
            while (!sub.empty() && (sub.front() == L' ' || sub.front() == L'\"')) sub.erase(sub.begin());
            while (!sub.empty() && (sub.back() == L' ' || sub.back() == L'\"')) sub.pop_back();
            state.installed_path = sub;
        }
    } else {
        std::wstring found_path;
        state.is_already_installed = CheckIfGameInstalled(found_path);
        state.installed_path = found_path;
        if (is_uninstall_param) {
            state.is_uninstall_only_mode = true;
        }
    }

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_MAIN_ICON));
    wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(240, 240, 244));
    wc.lpszClassName = L"Burnout3InstallerModern";
    RegisterClassExW(&wc);

    const int w = 600, h = 600;
    const int x = (GetSystemMetrics(SM_CXSCREEN) - w) / 2;
    const int y = (GetSystemMetrics(SM_CYSCREEN) - h) / 2;

    const wchar_t* win_title = state.is_uninstall_only_mode 
        ? L"Burnout 3 Takedown PC - Asistente de Desinstalaci\x00F3n"
        : (state.is_already_installed 
            ? L"Burnout 3 Takedown PC - Mantenimiento e Instalaci\x00F3n"
            : L"Burnout 3 Takedown PC - Asistente de Instalaci\x00F3n");

    HWND hwnd = CreateWindowExW(
        0, L"Burnout3InstallerModern", win_title,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        x, y, w, h,
        nullptr, nullptr, hInstance, &state);

    if (!hwnd) return 1;

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    UnregisterClassW(L"Burnout3InstallerModern", hInstance);
    CoUninitialize();
    return 0;
}
