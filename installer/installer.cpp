#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shldisp.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <chrono>

#include "resource.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "advapi32.lib")

namespace fs = std::filesystem;

const wchar_t* kUninstallSubKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Burnout 3 Takedown PC";

constexpr int IDC_BANNER           = 1001;
constexpr int IDC_DEST_EDIT        = 1002;
constexpr int IDC_BROWSE_DIR_BTN   = 1003;
constexpr int IDC_CHK_DESKTOP      = 1004;
constexpr int IDC_CHK_STARTMENU    = 1005;
constexpr int IDC_PROGRESS         = 1006;
constexpr int IDC_STATUS_TXT       = 1007;
constexpr int IDC_BTN_INSTALL      = 1008; // "INSTALAR AHORA" / "REPARAR"
constexpr int IDC_BTN_UNINSTALL    = 1009; // "DESINSTALAR"
constexpr int IDC_BTN_EXIT         = 1010; // "Salir" / "Cerrar"
constexpr int IDC_ISO_EDIT         = 1011;
constexpr int IDC_BROWSE_ISO_BTN   = 1012;

constexpr UINT WM_INSTALL_PROGRESS = WM_USER + 101;
constexpr UINT WM_INSTALL_DONE     = WM_USER + 102;
constexpr UINT WM_INSTALL_ERROR    = WM_USER + 103;
constexpr UINT WM_UNINSTALL_DONE   = WM_USER + 104;

struct AppState {
    HWND hwnd = nullptr;
    HWND hDestEdit = nullptr;
    HWND hBrowseDirBtn = nullptr;
    HWND hIsoEdit = nullptr;
    HWND hBrowseIsoBtn = nullptr;
    HWND hChkDesktop = nullptr;
    HWND hChkStartMenu = nullptr;
    HWND hProgress = nullptr;
    HWND hStatusTxt = nullptr;
    HWND hBtnInstall = nullptr;
    HWND hBtnUninstall = nullptr;
    HWND hBtnExit = nullptr;

    HWND hGrpDest = nullptr;
    HWND hLblDest = nullptr;
    HWND hLblIso = nullptr;
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
    std::wstring selected_iso_path;
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
        RegSetValueExW(hKey, name, 0, REG_SZ,
                       reinterpret_cast<const BYTE*>(val.c_str()),
                       static_cast<DWORD>((val.size() + 1) * sizeof(wchar_t)));
    };

    auto set_dword = [&](const wchar_t* name, DWORD val) {
        RegSetValueExW(hKey, name, 0, REG_DWORD,
                       reinterpret_cast<const BYTE*>(&val), sizeof(DWORD));
    };

    set_string(L"DisplayName", L"Burnout 3: Takedown (PC Port Nativo PAL)");
    set_string(L"DisplayVersion", L"1.0");
    set_string(L"Publisher", L"Project Takedown");
    std::wstring icon_path = dest_dir + L"\\burnout3.ico,0";
    std::wstring uninstall_cmd = L"\"" + dest_dir + L"\\Desinstalar.exe\" --uninstall";
    set_string(L"DisplayIcon", icon_path);
    set_string(L"UninstallString", uninstall_cmd);
    set_string(L"QuietUninstallString", uninstall_cmd);
    set_string(L"InstallLocation", dest_dir);
    set_dword(L"NoModify", 1);
    set_dword(L"NoRepair", 0);
    set_dword(L"EstimatedSize", 55000); // ~55 MB

    RegCloseKey(hKey);
    return true;
}

void UnregisterUninstallEntry() {
    RegDeleteKeyW(HKEY_CURRENT_USER, kUninstallSubKey);
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
    std::wstring pub_progs = GetSpecialFolderPath(CSIDL_COMMON_PROGRAMS);
    if (!pub_progs.empty()) {
        fs::remove_all(pub_progs + L"\\Burnout 3 Takedown PC", ec);
    }
}

void DeleteInstalledFiles(const std::wstring& dest_dir) {
    std::error_code ec;
    std::vector<std::wstring> files_to_delete = {
        L"burnout3_pal.exe",
        L"Burnout 3 Takedown.exe",
        L"Lanzador_Burnout3.exe",
        L"gpu_pipelines.txt",
        L"pc_settings.ini",
        L"burnout3.ico",
        L"launcher_banner.bmp",
        L"LEEME_PRIMERO.txt",
        L"Seleccionar_ISO.bat",
        L"Jugar_Burnout3_PAL.bat",
        L"Desinstalar.exe",
        L"Desinstalar.bat",
        L"vulkan-1.dll",
        L"vcruntime140.dll",
        L"vcruntime140_1.dll",
        L"msvcp140.dll",
        L"msvcp140_1.dll",
        L"msvcp140_2.dll"
    };

    for (const auto& file : files_to_delete) {
        fs::remove(dest_dir + L"\\" + file, ec);
    }
    fs::remove_all(dest_dir + L"\\texture pack", ec);

    // Keep memcard directory safe so player doesn't lose saves!
    bool memcard_has_files = false;
    std::wstring memcard_dir = dest_dir + L"\\memcard";
    if (fs::exists(memcard_dir, ec)) {
        for (const auto& entry : fs::recursive_directory_iterator(memcard_dir, ec)) {
            if (fs::is_regular_file(entry.path(), ec)) {
                memcard_has_files = true;
                break;
            }
        }
    }

    if (!memcard_has_files) {
        fs::remove_all(memcard_dir, ec);
    }

    // Try deleting destination folder if completely empty
    fs::remove(dest_dir, ec);
}

// Native Windows Shell Zip Extraction (100% reliable, zero external tool dependency)
bool UnzipWithShell(const std::wstring& zipPath, const std::wstring& destPath) {
    IShellDispatch* pISD = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_Shell, nullptr, CLSCTX_INPROC_SERVER, IID_IShellDispatch, (void**)&pISD);
    if (FAILED(hr) || !pISD) return false;

    VARIANT vZip, vDest;
    VariantInit(&vZip);
    vZip.vt = VT_BSTR;
    vZip.bstrVal = SysAllocString(zipPath.c_str());

    VariantInit(&vDest);
    vDest.vt = VT_BSTR;
    vDest.bstrVal = SysAllocString(destPath.c_str());

    bool ok = false;
    Folder *pZipFolder = nullptr, *pDestFolder = nullptr;
    if (SUCCEEDED(pISD->NameSpace(vZip, &pZipFolder)) && pZipFolder) {
        if (SUCCEEDED(pISD->NameSpace(vDest, &pDestFolder)) && pDestFolder) {
            FolderItems* pItems = nullptr;
            if (SUCCEEDED(pZipFolder->Items(&pItems)) && pItems) {
                VARIANT vItems, vOptions;
                VariantInit(&vItems);
                vItems.vt = VT_DISPATCH;
                vItems.pdispVal = pItems;

                VariantInit(&vOptions);
                vOptions.vt = VT_I4;
                vOptions.lVal = 4 | 16 | 512 | 1024; // FOF_SILENT | FOF_NOCONFIRMATION | FOF_NOCONFIRMMKDIR | FOF_NOERRORUI

                hr = pDestFolder->CopyHere(vItems, vOptions);
                ok = SUCCEEDED(hr);
                pItems->Release();
            }
            pDestFolder->Release();
        }
        pZipFolder->Release();
    }
    VariantClear(&vZip);
    VariantClear(&vDest);
    pISD->Release();
    return ok;
}

bool ExtractPayload(const std::wstring& zip_path, const std::wstring& dest_dir) {
    std::wstring pal_exe = dest_dir + L"\\burnout3_pal.exe";

    // 1. Native Windows Shell API
    UnzipWithShell(zip_path, dest_dir);
    if (fs::exists(pal_exe) && fs::file_size(pal_exe) > 1000000) return true;

    // 2. Fallback: Windows 10/11 system tar.exe
    std::wstring tar_cmd = L"tar.exe -xf \"" + zip_path + L"\" -C \"" + dest_dir + L"\"";
    STARTUPINFOW si = { sizeof(si) };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};
    if (CreateProcessW(nullptr, &tar_cmd[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 60000);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        if (fs::exists(pal_exe) && fs::file_size(pal_exe) > 1000000) return true;
    }

    // 3. Fallback: PowerShell Expand-Archive
    std::wstring ps_cmd = L"powershell.exe -NoProfile -ExecutionPolicy Bypass -Command \"Expand-Archive -LiteralPath '" + zip_path + L"' -DestinationPath '" + dest_dir + L"' -Force\"";
    if (CreateProcessW(nullptr, &ps_cmd[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 60000);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        if (fs::exists(pal_exe) && fs::file_size(pal_exe) > 1000000) return true;
    }

    return false;
}

DWORD WINAPI InstallThread(LPVOID param) {
    AppState* state = reinterpret_cast<AppState*>(param);
    HRESULT hr_co = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    auto finish_error = [&](const wchar_t* msg) {
        if (SUCCEEDED(hr_co)) CoUninitialize();
        PostMessageW(state->hwnd, WM_INSTALL_ERROR, 0, reinterpret_cast<LPARAM>(msg));
        return 1;
    };

    wchar_t dest_buf[MAX_PATH] = {};
    GetWindowTextW(state->hDestEdit, dest_buf, MAX_PATH);
    std::wstring dest_dir(dest_buf);

    while (!dest_dir.empty() && (dest_dir.back() == L'\\' || dest_dir.back() == L'/')) {
        dest_dir.pop_back();
    }

    if (dest_dir.empty()) {
        return finish_error(L"La ruta de instalaci\x00F3n no puede estar vac\x00EDa.");
    }

    for (size_t i = 2; i < dest_dir.size(); ++i) {
        if (dest_dir[i] == L':' || dest_dir[i] == L'*' || dest_dir[i] == L'?' ||
            dest_dir[i] == L'\"' || dest_dir[i] == L'<' || dest_dir[i] == L'>' || dest_dir[i] == L'|') {
            return finish_error(L"La ruta contiene caracteres no permitidos.");
        }
    }

    PostMessageW(state->hwnd, WM_INSTALL_PROGRESS, 10, (LPARAM)L"Preparando directorio de destino...");

    std::error_code ec;
    fs::create_directories(dest_dir, ec);
    if (ec) {
        return finish_error(L"No se pudo crear la carpeta de destino. Verifica los permisos de usuario.");
    }

    // 1. Locate and extract embedded ZIP resource
    PostMessageW(state->hwnd, WM_INSTALL_PROGRESS, 25, (LPARAM)L"Extrayendo paquete de instalaci\x00F3n...");
    HMODULE hMod = GetModuleHandleW(nullptr);
    HRSRC hRes = FindResourceW(hMod, MAKEINTRESOURCEW(IDR_PAYLOAD_ZIP), (LPCWSTR)RT_RCDATA);
    if (!hRes) {
        return finish_error(L"Error: No se encontr\x00F3 el paquete interno de instalaci\x00F3n.");
    }
    HGLOBAL hGlobal = LoadResource(hMod, hRes);
    DWORD res_size = SizeofResource(hMod, hRes);
    const void* res_data = LockResource(hGlobal);
    if (!res_data || res_size == 0) {
        return finish_error(L"Error al leer los datos de instalaci\x00F3n.");
    }

    wchar_t temp_path[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, temp_path);
    std::wstring zip_file = std::wstring(temp_path) + L"b3_setup_payload.zip";

    HANDLE hFile = CreateFileW(zip_file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        return finish_error(L"Error al escribir archivo temporal de instalaci\x00F3n.");
    }
    DWORD written = 0;
    WriteFile(hFile, res_data, res_size, &written, nullptr);
    CloseHandle(hFile);

    // 2. Extract payload using robust multi-tier unzipper
    PostMessageW(state->hwnd, WM_INSTALL_PROGRESS, 45, (LPARAM)L"Instalando motor nativo x64, Vulkan y librer\x00EDas...");

    bool extract_ok = ExtractPayload(zip_file, dest_dir);
    DeleteFileW(zip_file.c_str());

    std::wstring pal_exe = dest_dir + L"\\burnout3_pal.exe";
    std::wstring b3_exe  = dest_dir + L"\\Burnout 3 Takedown.exe";
    std::wstring ldr_exe = dest_dir + L"\\Lanzador_Burnout3.exe";

    if (!extract_ok || !fs::exists(pal_exe) || fs::file_size(pal_exe) < 1000000) {
        return finish_error(L"Error: No se pudieron extraer los archivos del juego. Verifica el espacio en disco y permisos.");
    }

    // 3. Ensure friendly executable copies exist in destination directory
    std::error_code ec_cp;
    fs::copy_file(pal_exe, b3_exe, fs::copy_options::overwrite_existing, ec_cp);
    fs::copy_file(pal_exe, ldr_exe, fs::copy_options::overwrite_existing, ec_cp);

    // 4. Save selected ISO path to pc_settings.ini if user picked one
    wchar_t iso_buf[MAX_PATH] = {};
    if (state->hIsoEdit) {
        GetWindowTextW(state->hIsoEdit, iso_buf, MAX_PATH);
    }
    std::wstring picked_iso(iso_buf);
    if (!picked_iso.empty() && fs::exists(picked_iso)) {
        std::wstring ini_path = dest_dir + L"\\pc_settings.ini";
        std::ifstream ini_in(ini_path);
        std::vector<std::string> lines;
        std::string line;
        bool found_disc = false;
        
        // Convert picked ISO to UTF-8
        int len = WideCharToMultiByte(CP_UTF8, 0, picked_iso.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string iso_utf8(len ? len - 1 : 0, '\0');
        if (len > 1) {
            WideCharToMultiByte(CP_UTF8, 0, picked_iso.c_str(), -1, &iso_utf8[0], len, nullptr, nullptr);
        }

        while (std::getline(ini_in, line)) {
            if (line.rfind("disc=", 0) == 0) {
                lines.push_back("disc=" + iso_utf8);
                found_disc = true;
            } else {
                lines.push_back(line);
            }
        }
        ini_in.close();

        if (!found_disc) {
            lines.push_back("disc=" + iso_utf8);
        }

        std::ofstream ini_out(ini_path);
        for (const auto& l : lines) {
            ini_out << l << "\n";
        }
    }

    // 5. Copy running installer to dest_dir\Desinstalar.exe for Windows Control Panel
    wchar_t self_exe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, self_exe, MAX_PATH);
    std::wstring unins_exe = dest_dir + L"\\Desinstalar.exe";
    std::error_code ec_self;
    fs::copy_file(self_exe, unins_exe, fs::copy_options::overwrite_existing, ec_self);

    // 6. Create Desinstalar.bat as fallback in destination folder
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

    // 7. Register in Windows Control Panel (Programs and Features / Settings)
    PostMessageW(state->hwnd, WM_INSTALL_PROGRESS, 75, (LPARAM)L"Registrando en el Panel de Control de Windows...");
    RegisterUninstallEntry(dest_dir);

    // 8. Create Shortcuts (Desktop & Start Menu)
    PostMessageW(state->hwnd, WM_INSTALL_PROGRESS, 88, (LPARAM)L"Creando accesos directos...");

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

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);

    state->installed_path = dest_dir;
    if (SUCCEEDED(hr_co)) CoUninitialize();
    PostMessageW(state->hwnd, WM_INSTALL_PROGRESS, 100, (LPARAM)L"Operaci\x00F3n completada con \x00E9xito.");
    PostMessageW(state->hwnd, WM_INSTALL_DONE, 0, 0);
    return 0;
}

DWORD WINAPI UninstallThread(LPVOID param) {
    AppState* state = reinterpret_cast<AppState*>(param);
    PostMessageW(state->hwnd, WM_INSTALL_PROGRESS, 20, (LPARAM)L"Eliminando accesos directos...");
    DeleteShortcuts();

    PostMessageW(state->hwnd, WM_INSTALL_PROGRESS, 50, (LPARAM)L"Eliminando del Panel de Control...");
    UnregisterUninstallEntry();

    PostMessageW(state->hwnd, WM_INSTALL_PROGRESS, 75, (LPARAM)L"Eliminando ejecutables y archivos del motor...");
    DeleteInstalledFiles(state->installed_path);

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    PostMessageW(state->hwnd, WM_INSTALL_PROGRESS, 100, (LPARAM)L"Desinstalaci\x00F3n completada.");
    PostMessageW(state->hwnd, WM_UNINSTALL_DONE, 0, 0);
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

            state->hFontUi = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                         DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                         CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
            state->hFontBold = CreateFontW(-13, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                           CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
            state->hFontPlay = CreateFontW(-14, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                           CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");

            state->hBrushBg = CreateSolidBrush(RGB(244, 245, 248));

            // Banner
            HWND hBanner = CreateWindowExW(0, L"STATIC", L"",
                                           WS_CHILD | WS_VISIBLE | SS_BITMAP | SS_CENTERIMAGE,
                                           0, 0, 584, 145, hwnd, (HMENU)(INT_PTR)(IDC_BANNER), nullptr, nullptr);
            state->hBannerBmp = reinterpret_cast<HBITMAP>(LoadImageW(GetModuleHandleW(nullptr),
                                                          MAKEINTRESOURCEW(IDB_BANNER),
                                                          IMAGE_BITMAP, 584, 145, LR_DEFAULTCOLOR));
            if (state->hBannerBmp) {
                SendMessageW(hBanner, STM_SETIMAGE, IMAGE_BITMAP, reinterpret_cast<LPARAM>(state->hBannerBmp));
            }

            // GroupBox 1: Destination & ISO Selection
            const wchar_t* grp1_title = state->is_already_installed ? L" Estado de la Instalaci\x00F3n " : L" Opciones de Instalaci\x00F3n ";
            state->hGrpDest = CreateWindowExW(0, L"BUTTON", grp1_title,
                                              WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                              12, 152, 560, 160, hwnd, nullptr, nullptr, nullptr);
            SendMessageW(state->hGrpDest, WM_SETFONT, reinterpret_cast<WPARAM>(state->hFontBold), TRUE);

            const wchar_t* lbl1_title = state->is_already_installed 
                ? L"El juego ya est\x00E1 instalado en esta carpeta:" 
                : L"Carpeta donde se instalar\x00E1 el juego:";
            state->hLblDest = CreateWindowExW(0, L"STATIC", lbl1_title,
                                              WS_CHILD | WS_VISIBLE,
                                              25, 173, 400, 18, hwnd, nullptr, nullptr, nullptr);
            SendMessageW(state->hLblDest, WM_SETFONT, reinterpret_cast<WPARAM>(state->hFontUi), TRUE);

            std::wstring initial_path = state->installed_path.empty() ? L"C:\\Juegos\\Burnout 3 Takedown PC" : state->installed_path;
            state->hDestEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", initial_path.c_str(),
                                               WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                               25, 193, 400, 24, hwnd, (HMENU)(INT_PTR)(IDC_DEST_EDIT), nullptr, nullptr);
            SendMessageW(state->hDestEdit, WM_SETFONT, reinterpret_cast<WPARAM>(state->hFontUi), TRUE);

            state->hBrowseDirBtn = CreateWindowExW(0, L"BUTTON", L"Examinar Carpeta...",
                                                   WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                                   435, 192, 125, 26, hwnd, (HMENU)(INT_PTR)(IDC_BROWSE_DIR_BTN), nullptr, nullptr);
            SendMessageW(state->hBrowseDirBtn, WM_SETFONT, reinterpret_cast<WPARAM>(state->hFontUi), TRUE);

            // Optional ISO row
            state->hLblIso = CreateWindowExW(0, L"STATIC", L"Imagen ISO de Burnout 3 PAL (Opcional - puedes elegirla ahora o al abrir el juego):",
                                             WS_CHILD | WS_VISIBLE,
                                             25, 225, 520, 18, hwnd, nullptr, nullptr, nullptr);
            SendMessageW(state->hLblIso, WM_SETFONT, reinterpret_cast<WPARAM>(state->hFontUi), TRUE);

            state->hIsoEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                              WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                              25, 245, 400, 24, hwnd, (HMENU)(INT_PTR)(IDC_ISO_EDIT), nullptr, nullptr);
            SendMessageW(state->hIsoEdit, WM_SETFONT, reinterpret_cast<WPARAM>(state->hFontUi), TRUE);

            state->hBrowseIsoBtn = CreateWindowExW(0, L"BUTTON", L"Examinar ISO...",
                                                   WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                                   435, 244, 125, 26, hwnd, (HMENU)(INT_PTR)(IDC_BROWSE_ISO_BTN), nullptr, nullptr);
            SendMessageW(state->hBrowseIsoBtn, WM_SETFONT, reinterpret_cast<WPARAM>(state->hFontUi), TRUE);

            // Checkboxes
            state->hChkDesktop = CreateWindowExW(0, L"BUTTON", L"Crear acceso directo en el Escritorio",
                                                 WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                                 25, 280, 260, 22, hwnd, (HMENU)(INT_PTR)(IDC_CHK_DESKTOP), nullptr, nullptr);
            SendMessageW(state->hChkDesktop, WM_SETFONT, reinterpret_cast<WPARAM>(state->hFontUi), TRUE);
            SendMessageW(state->hChkDesktop, BM_SETCHECK, BST_CHECKED, 0);

            state->hChkStartMenu = CreateWindowExW(0, L"BUTTON", L"Crear en el Men\x00FA Inicio",
                                                   WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                                   300, 280, 250, 22, hwnd, (HMENU)(INT_PTR)(IDC_CHK_STARTMENU), nullptr, nullptr);
            SendMessageW(state->hChkStartMenu, WM_SETFONT, reinterpret_cast<WPARAM>(state->hFontUi), TRUE);
            SendMessageW(state->hChkStartMenu, BM_SETCHECK, BST_CHECKED, 0);

            // GroupBox 2: Information & Features
            const wchar_t* grp2_title = state->is_already_installed ? L" Opciones de Mantenimiento " : L" Caracter\x00EDsticas y Requisitos ";
            state->hGrpInfo = CreateWindowExW(0, L"BUTTON", grp2_title,
                                              WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                              12, 320, 560, 125, hwnd, nullptr, nullptr, nullptr);
            SendMessageW(state->hGrpInfo, WM_SETFONT, reinterpret_cast<WPARAM>(state->hFontBold), TRUE);

            const wchar_t* fresh_info = 
                L"\x2022 Port nativo a 60 FPS estables con resoluci\x00F3n interna escalable hasta 4K Ultra HD.\n"
                L"\x2022 Se instalan autom\x00E1ticamente todas las librer\x00EDas necesarias (Vulkan 1.3 y VC++ Runtime).\n"
                L"\x2022 Soporte para mandos Xbox, DualShock/DualSense (con vibraci\x00F3n), teclado y rat\x00F3n.\n\n"
                L"\x26A0 AVISO: El instalador no incluye la imagen ISO de PS2 (suministrada por el usuario).";

            const wchar_t* maint_info = 
                L"\x2022 RESTAURAR / REPARAR: Reinstala los archivos del motor, restablece configuraci\x00F3n,\n"
                L"  actualiza librer\x00EDas, registra en Panel de Control y recrea los accesos directos.\n\n"
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
                                              25, 340, 535, 95, hwnd, nullptr, nullptr, nullptr);
            SendMessageW(state->hLblInfo, WM_SETFONT, reinterpret_cast<WPARAM>(state->hFontUi), TRUE);

            // Progress & Status
            const wchar_t* init_status = state->is_uninstall_only_mode 
                ? L"Listo para desinstalar el juego de tu equipo."
                : (state->is_already_installed ? L"El juego ya est\x00E1 instalado. Elige restaurar o desinstalar." : L"Listo para instalar el motor nativo x64 y librer\x00EDas necesarias.");
            state->hStatusTxt = CreateWindowExW(0, L"STATIC", init_status,
                                                WS_CHILD | WS_VISIBLE,
                                                25, 455, 535, 20, hwnd, (HMENU)(INT_PTR)(IDC_STATUS_TXT), nullptr, nullptr);
            SendMessageW(state->hStatusTxt, WM_SETFONT, reinterpret_cast<WPARAM>(state->hFontBold), TRUE);

            state->hProgress = CreateWindowExW(0, PROGRESS_CLASSW, L"",
                                               WS_CHILD | WS_VISIBLE | PBS_SMOOTH,
                                               20, 480, 544, 22, hwnd, (HMENU)(INT_PTR)(IDC_PROGRESS), nullptr, nullptr);
            SendMessageW(state->hProgress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
            SendMessageW(state->hProgress, PBM_SETPOS, 0, 0);

            // Action Buttons
            if (state->is_uninstall_only_mode) {
                state->hBtnInstall = nullptr;
                state->hBtnUninstall = CreateWindowExW(0, L"BUTTON", L"\x25BA  DESINSTALAR AHORA",
                                                     WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                                                     100, 515, 240, 45, hwnd, (HMENU)(INT_PTR)(IDC_BTN_UNINSTALL), nullptr, nullptr);
                SendMessageW(state->hBtnUninstall, WM_SETFONT, reinterpret_cast<WPARAM>(state->hFontPlay), TRUE);

                state->hBtnExit = CreateWindowExW(0, L"BUTTON", L"Cancelar",
                                                  WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                                  360, 515, 130, 45, hwnd, (HMENU)(INT_PTR)(IDC_BTN_EXIT), nullptr, nullptr);
                SendMessageW(state->hBtnExit, WM_SETFONT, reinterpret_cast<WPARAM>(state->hFontUi), TRUE);
            } else if (state->is_already_installed) {
                state->hBtnInstall = CreateWindowExW(0, L"BUTTON", L"\x25BA  RESTAURAR / REPARAR",
                                                     WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                                                     20, 515, 195, 45, hwnd, (HMENU)(INT_PTR)(IDC_BTN_INSTALL), nullptr, nullptr);
                SendMessageW(state->hBtnInstall, WM_SETFONT, reinterpret_cast<WPARAM>(state->hFontPlay), TRUE);

                state->hBtnUninstall = CreateWindowExW(0, L"BUTTON", L"DESINSTALAR",
                                                       WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                                       230, 515, 175, 45, hwnd, (HMENU)(INT_PTR)(IDC_BTN_UNINSTALL), nullptr, nullptr);
                SendMessageW(state->hBtnUninstall, WM_SETFONT, reinterpret_cast<WPARAM>(state->hFontPlay), TRUE);

                state->hBtnExit = CreateWindowExW(0, L"BUTTON", L"Salir",
                                                  WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                                  420, 515, 140, 45, hwnd, (HMENU)(INT_PTR)(IDC_BTN_EXIT), nullptr, nullptr);
                SendMessageW(state->hBtnExit, WM_SETFONT, reinterpret_cast<WPARAM>(state->hFontUi), TRUE);
            } else {
                state->hBtnInstall = CreateWindowExW(0, L"BUTTON", L"\x25BA  INSTALAR JUEGO AHORA",
                                                     WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                                                     90, 515, 260, 45, hwnd, (HMENU)(INT_PTR)(IDC_BTN_INSTALL), nullptr, nullptr);
                SendMessageW(state->hBtnInstall, WM_SETFONT, reinterpret_cast<WPARAM>(state->hFontPlay), TRUE);

                state->hBtnUninstall = nullptr;

                state->hBtnExit = CreateWindowExW(0, L"BUTTON", L"Salir",
                                                  WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                                  370, 515, 130, 45, hwnd, (HMENU)(INT_PTR)(IDC_BTN_EXIT), nullptr, nullptr);
                SendMessageW(state->hBtnExit, WM_SETFONT, reinterpret_cast<WPARAM>(state->hFontUi), TRUE);
            }

            SetFocus(state->hBtnInstall ? state->hBtnInstall : state->hBtnExit);
            return 0;
        }

        case WM_CTLCOLORSTATIC: {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(25, 25, 30));
            return reinterpret_cast<LRESULT>(state ? state->hBrushBg : GetStockObject(WHITE_BRUSH));
        }

        case WM_CTLCOLORBTN: {
            return reinterpret_cast<LRESULT>(state ? state->hBrushBg : GetStockObject(WHITE_BRUSH));
        }

        case WM_COMMAND: {
            const int id = LOWORD(wParam);
            const int code = HIWORD(wParam);

            if (id == IDC_BROWSE_DIR_BTN && code == BN_CLICKED) {
                BROWSEINFOW bi = {};
                bi.hwndOwner = hwnd;
                bi.lpszTitle = L"Selecciona la carpeta donde deseas instalar Burnout 3 Takedown PC:";
                bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_USENEWUI;
                PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
                if (pidl) {
                    wchar_t selected[MAX_PATH] = {};
                    if (SHGetPathFromIDListW(pidl, selected)) {
                        std::wstring p(selected);
                        if (p.rfind(L"Burnout 3 Takedown PC") == std::wstring::npos) {
                            if (!p.empty() && p.back() != L'\\') p += L'\\';
                            p += L"Burnout 3 Takedown PC";
                        }
                        SetWindowTextW(state->hDestEdit, p.c_str());
                    }
                    CoTaskMemFree(pidl);
                }
                return 0;
            }

            if (id == IDC_BROWSE_ISO_BTN && code == BN_CLICKED) {
                wchar_t szFile[MAX_PATH] = {};
                OPENFILENAMEW ofn = { sizeof(ofn) };
                ofn.hwndOwner = hwnd;
                ofn.lpstrFilter = L"Im\x00E1genes de Disco (*.iso;*.bin;*.cue)\0*.iso;*.bin;*.cue\0Todos los Archivos (*.*)\0*.*\0";
                ofn.lpstrFile = szFile;
                ofn.nMaxFile = MAX_PATH;
                ofn.lpstrTitle = L"Selecciona la imagen ISO de Burnout 3: Takedown (PAL / SLES-52584)";
                ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
                if (GetOpenFileNameW(&ofn)) {
                    SetWindowTextW(state->hIsoEdit, szFile);
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
                EnableWindow(state->hBrowseDirBtn, FALSE);
                if (state->hIsoEdit) EnableWindow(state->hIsoEdit, FALSE);
                if (state->hBrowseIsoBtn) EnableWindow(state->hBrowseIsoBtn, FALSE);
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
                EnableWindow(state->hBrowseDirBtn, FALSE);
                if (state->hIsoEdit) EnableWindow(state->hIsoEdit, FALSE);
                if (state->hBrowseIsoBtn) EnableWindow(state->hBrowseIsoBtn, FALSE);
                EnableWindow(state->hChkDesktop, FALSE);
                EnableWindow(state->hChkStartMenu, FALSE);
                EnableWindow(state->hBtnInstall, FALSE);
                EnableWindow(state->hBtnUninstall, FALSE);
                EnableWindow(state->hBtnExit, FALSE);

                SetWindowTextW(state->hBtnUninstall, L"Desinstalando...");

                CreateThread(nullptr, 0, UninstallThread, state, 0, nullptr);
                return 0;
            }

            if (id == IDC_BTN_EXIT && code == BN_CLICKED) {
                DestroyWindow(hwnd);
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
                SetWindowTextW(state->hStatusTxt, L"\x2713 \x00A1Juego reparado con \x00E9xito! Todos los archivos fueron restaurados.");
            } else {
                SetWindowTextW(state->hStatusTxt, L"\x2713 \x00A1Instalaci\x00F3n completada con \x00E9xito! Todos los archivos fueron instalados.");
            }
            
            if (state->hBtnUninstall) ShowWindow(state->hBtnUninstall, SW_HIDE);
            SetWindowPos(state->hBtnInstall, nullptr, 90, 515, 260, 45, SWP_NOZORDER | SWP_SHOWWINDOW);
            SetWindowPos(state->hBtnExit, nullptr, 370, 515, 130, 45, SWP_NOZORDER | SWP_SHOWWINDOW);

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

            SetWindowPos(state->hBtnExit, nullptr, 220, 515, 160, 45, SWP_NOZORDER | SWP_SHOWWINDOW);
            EnableWindow(state->hBtnExit, TRUE);
            SetWindowTextW(state->hBtnExit, L"Cerrar");
            SetFocus(state->hBtnExit);
            return 0;
        }

        case WM_INSTALL_ERROR: {
            state->is_busy = false;
            const wchar_t* err = reinterpret_cast<const wchar_t*>(lParam);
            MessageBoxW(hwnd, err ? err : L"Error durante la operaci\x00F3n.", L"Error", MB_OK | MB_ICONERROR);
            
            EnableWindow(state->hDestEdit, TRUE);
            EnableWindow(state->hBrowseDirBtn, TRUE);
            if (state->hIsoEdit) EnableWindow(state->hIsoEdit, TRUE);
            if (state->hBrowseIsoBtn) EnableWindow(state->hBrowseIsoBtn, TRUE);
            EnableWindow(state->hChkDesktop, TRUE);
            EnableWindow(state->hChkStartMenu, TRUE);
            if (state->hBtnInstall) {
                EnableWindow(state->hBtnInstall, TRUE);
                SetWindowTextW(state->hBtnInstall, state->is_already_installed ? L"\x25BA  RESTAURAR / REPARAR" : L"\x25BA  INSTALAR JUEGO AHORA");
            }
            if (state->hBtnUninstall) EnableWindow(state->hBtnUninstall, TRUE);
            EnableWindow(state->hBtnExit, TRUE);
            SetWindowTextW(state->hStatusTxt, L"La operaci\x00F3n se interrumpi\x00F3 por un error.");
            return 0;
        }

        case WM_DESTROY: {
            if (state) {
                if (state->hFontUi) DeleteObject(state->hFontUi);
                if (state->hFontBold) DeleteObject(state->hFontBold);
                if (state->hFontPlay) DeleteObject(state->hFontPlay);
                if (state->hBrushBg) DeleteObject(state->hBrushBg);
                if (state->hBannerBmp) DeleteObject(state->hBannerBmp);
            }
            PostQuitMessage(0);
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR pCmdLine, int nCmdShow) {
    INITCOMMONCONTROLSEX icex = { sizeof(icex), ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icex);

    AppState state;
    std::wstring cmd(pCmdLine ? pCmdLine : L"");
    if (cmd.find(L"--uninstall") != std::wstring::npos) {
        state.is_uninstall_only_mode = true;
    }

    state.is_already_installed = CheckIfGameInstalled(state.installed_path);

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_MAIN_ICON));
    wc.hIconSm = wc.hIcon;
    wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(244, 245, 248));
    wc.lpszClassName = L"Burnout3InstallerWndClass";

    RegisterClassExW(&wc);

    const int w = 600, h = 615;
    const int sx = (GetSystemMetrics(SM_CXSCREEN) - w) / 2;
    const int sy = (GetSystemMetrics(SM_CYSCREEN) - h) / 2;

    const wchar_t* wnd_title = state.is_uninstall_only_mode 
        ? L"Burnout 3 Takedown PC - Asistente de Desinstalaci\x00F3n"
        : (state.is_already_installed 
            ? L"Burnout 3 Takedown PC - Mantenimiento e Instalaci\x00F3n" 
            : L"Burnout 3 Takedown PC - Instalador Oficial v1.0");

    HWND hwnd = CreateWindowExW(
        WS_EX_APPWINDOW,
        wc.lpszClassName,
        wnd_title,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        sx, sy, w, h,
        nullptr, nullptr, hInstance, &state
    );

    if (!hwnd) return 1;

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return 0;
}
