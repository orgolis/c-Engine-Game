#include "gws/platform/file_dialog.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shlobj.h>
#include <commdlg.h>

namespace gws::platform {

std::string browse_folder(const char* title) {
    char path[MAX_PATH] = {0};

    BROWSEINFOA bi{};
    bi.lpszTitle = title;
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

    LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
    if (!pidl) {
        return {};
    }

    const BOOL ok = SHGetPathFromIDListA(pidl, path);
    CoTaskMemFree(pidl);

    return ok ? std::string(path) : std::string{};
}

std::string browse_file(const char* title) {
    char path[MAX_PATH] = {0};
    OPENFILENAMEA dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFile = path;
    dialog.nMaxFile = sizeof(path);
    dialog.lpstrTitle = title;
    dialog.lpstrFilter = "All supported assets\0*.obj;*.gltf;*.glb;*.fbx;*.png;*.jpg;*.jpeg;*.tga;*.hdr;*.wav;*.mp3;*.flac;*.ogg\0All files\0*.*\0";
    dialog.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    return GetOpenFileNameA(&dialog) ? std::string(path) : std::string{};
}

} // namespace gws::platform
