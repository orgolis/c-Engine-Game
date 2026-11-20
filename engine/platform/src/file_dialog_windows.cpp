#include "gws/platform/file_dialog.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <commdlg.h>
#include <cstring>
#include <functional>

namespace gws::platform {

namespace {

// Shell dialogs need a single-threaded COM apartment, and the editor's main
// thread is not in one: miniaudio calls CoInitializeEx(COINIT_MULTITHREADED)
// on the thread that opens audio, which is the main thread. There the folder
// picker (SHBrowseForFolder) never returned -- not even its window appeared --
// and the editor froze (reproduced by calling it on an MTA thread). So every
// dialog runs on a thread of its own in an STA, owned by the caller's active
// window (in front of it, the editor disabled meanwhile), while the calling
// thread keeps dispatching its own window messages: the dialog sends its owner
// messages (enable, activate), and an owner that does not answer them would
// deadlock the two threads. The hang watchdog reads that wait
// (MsgWaitForMultipleObjects) as "a dialog is open", not as a hang.
struct DialogJob {
    std::function<void(HWND)> run;
    HWND owner = nullptr;
};

DWORD WINAPI dialog_thread(LPVOID p) {
    auto* job = static_cast<DialogJob*>(p);
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    job->run(job->owner);
    if (SUCCEEDED(init)) CoUninitialize();
    return 0;
}

void run_dialog(const std::function<void(HWND)>& run) {
    DialogJob job{run, GetActiveWindow()};
    HANDLE thread = CreateThread(nullptr, 0, dialog_thread, &job, 0, nullptr);
    if (!thread) {   // no thread: try here (works when this thread is an STA)
        run(job.owner);
        return;
    }
    for (;;) {
        const DWORD r = MsgWaitForMultipleObjects(1, &thread, FALSE, INFINITE, QS_ALLINPUT);
        if (r != WAIT_OBJECT_0 + 1) break;   // the thread ended (or the wait failed)
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    CloseHandle(thread);
}

// The rest of the engine passes paths as narrow strings in the ANSI code page.
std::string narrow(const wchar_t* w) {
    if (!w || !*w) return {};
    const int n = WideCharToMultiByte(CP_ACP, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n > 0 ? n - 1 : 0), '\0');
    if (n > 1) WideCharToMultiByte(CP_ACP, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring widen(const char* s) {
    if (!s || !*s) return {};
    const int n = MultiByteToWideChar(CP_ACP, 0, s, -1, nullptr, 0);
    std::wstring w(static_cast<size_t>(n > 0 ? n - 1 : 0), L'\0');
    if (n > 1) MultiByteToWideChar(CP_ACP, 0, s, -1, w.data(), n);
    return w;
}

}  // namespace

std::string browse_folder(const char* title) {
    std::string result;
    run_dialog([&](HWND owner) {
        IFileOpenDialog* dlg = nullptr;
        if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg))))
            return;
        DWORD opts = 0;
        dlg->GetOptions(&opts);
        dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        if (title) dlg->SetTitle(widen(title).c_str());
        if (SUCCEEDED(dlg->Show(owner))) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dlg->GetResult(&item))) {
                PWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                    result = narrow(path);
                    CoTaskMemFree(path);
                }
                item->Release();
            }
        }
        dlg->Release();
    });
    return result;
}

std::string browse_file(const char* title) {
    std::string result;
    run_dialog([&](HWND owner) {
        char path[MAX_PATH] = {0};
        OPENFILENAMEA dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = owner;
        dialog.lpstrFile = path;
        dialog.nMaxFile = sizeof(path);
        dialog.lpstrTitle = title;
        dialog.lpstrFilter = "All supported assets\0*.obj;*.gltf;*.glb;*.fbx;*.png;*.jpg;*.jpeg;*.tga;*.hdr;*.wav;*.mp3;*.flac;*.ogg\0All files\0*.*\0";
        dialog.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
        if (GetOpenFileNameA(&dialog)) result = path;
    });
    return result;
}

std::string save_file(const char* title, const char* default_filename) {
    std::string result;
    run_dialog([&](HWND owner) {
        char path[MAX_PATH] = {0};
        if (default_filename) std::strncpy(path, default_filename, sizeof(path) - 1);
        OPENFILENAMEA dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = owner;
        dialog.lpstrFile = path;
        dialog.nMaxFile = sizeof(path);
        dialog.lpstrTitle = title;
        dialog.lpstrFilter = "Scene files\0*.scene\0All files\0*.*\0";
        dialog.lpstrDefExt = "scene";
        dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (GetSaveFileNameA(&dialog)) result = path;
    });
    return result;
}

} // namespace gws::platform
