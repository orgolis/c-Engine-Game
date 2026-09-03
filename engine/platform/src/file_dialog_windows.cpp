#include "gws/platform/file_dialog.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shlobj.h>

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

} // namespace gws::platform
