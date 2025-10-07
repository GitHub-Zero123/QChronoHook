#include "quickInject.h"
// windows平台实现
#include <iostream>
#include <thread>
#include <chrono>
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>

namespace fs = std::filesystem;

bool QuickInject::isModuleLoaded(uint64_t pid, const std::wstring& targetPath) {
    fs::path target = fs::weakly_canonical(targetPath); // 目标路径规范化
    // 转小写，避免大小写差异
    std::wstring targetNorm = target.wstring();
    std::transform(targetNorm.begin(), targetNorm.end(), targetNorm.begin(), ::towlower);

    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, static_cast<DWORD>(pid));
    if (hSnapshot == INVALID_HANDLE_VALUE) {
        return false;
    }

    MODULEENTRY32W me;
    me.dwSize = sizeof(me);
    bool found = false;

    if (Module32FirstW(hSnapshot, &me)) {
        do {
            fs::path modPath = fs::weakly_canonical(me.szExePath);
            std::wstring modNorm = modPath.wstring();
            std::transform(modNorm.begin(), modNorm.end(), modNorm.begin(), ::towlower);

            if (modNorm == targetNorm) {
                found = true;
                break;
            }
        } while (Module32NextW(hSnapshot, &me));
    }

    CloseHandle(hSnapshot);
    return found;
}

uint64_t QuickInject::getPidByName(const std::wstring& procNameW) {
    if (procNameW.empty()) {
        return 0;
    }

    // 允许传入带路径的名字，取 filename 部分
    fs::path p {procNameW};
    std::wstring targetName = p.filename().wstring();
    if (targetName.empty()) {
        return 0;
    }

    // 规范到小写以便不区分大小写比较
    std::transform(targetName.begin(), targetName.end(), targetName.begin(), ::towlower);

    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) {
        return 0;
    }

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);

    DWORD foundPid = 0;
    if (Process32FirstW(hSnapshot, &pe)) {
        do {
            std::wstring exeName = pe.szExeFile;
            std::transform(exeName.begin(), exeName.end(), exeName.begin(), ::towlower);
            if (exeName == targetName) {
                foundPid = pe.th32ProcessID;
                break; // 返回第一个匹配的 PID
            }
        } while (Process32NextW(hSnapshot, &pe));
    }

    CloseHandle(hSnapshot);
    return static_cast<uint64_t>(foundPid);
}

bool QuickInject::injectDll(uint64_t pid, const std::wstring& dllPath, bool onceInject, const char* entryPoint) {
    // 打开目标进程
    // HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    HANDLE hProcess = OpenProcess(PROCESS_CREATE_THREAD |
        PROCESS_QUERY_INFORMATION |
        PROCESS_VM_OPERATION |
        PROCESS_VM_WRITE |
        PROCESS_VM_READ, FALSE, static_cast<DWORD>(pid)
    );
    if (!hProcess) {
        std::cerr << "OpenProcess failed: " << GetLastError() << "\n";
        // throw std::runtime_error("OpenProcess failed: " + std::to_string(GetLastError()));
        return false;
    }

    // 如果要求只注入一次，先检查是否已加载
    if (onceInject && isModuleLoaded(pid, dllPath)) {
        std::cout << "DLL already injected.\n";
        CloseHandle(hProcess);
        return false;
    }

    // 在目标进程中分配内存
    LPVOID pRemoteMem = VirtualAllocEx(hProcess, nullptr, (dllPath.size() + 1) * sizeof(wchar_t),
                    MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!pRemoteMem) {
        // 分配内存失败
        std::cerr << "VirtualAllocEx failed: " << GetLastError() << "\n";
        CloseHandle(hProcess);
        return false;
    }

    // 写入 DLL 路径
    if (!WriteProcessMemory(hProcess, pRemoteMem, dllPath.c_str(), (dllPath.size() + 1) * sizeof(wchar_t), nullptr)) {
        // 写入内存失败
        std::cerr << "WriteProcessMemory failed: " << GetLastError() << "\n";
        VirtualFreeEx(hProcess, pRemoteMem, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    // 创建远程线程调用 LoadLibraryW
    HANDLE hThread = CreateRemoteThread(hProcess, nullptr, 0,
        (LPTHREAD_START_ROUTINE) LoadLibraryW, pRemoteMem, 0, nullptr);
    if (!hThread) {
        // 创建远程线程失败
        std::cerr << "CreateRemoteThread failed: " << GetLastError() << "\n";
        VirtualFreeEx(hProcess, pRemoteMem, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    // 等待注入完成
    WaitForSingleObject(hThread, INFINITE);
    // 清理
    VirtualFreeEx(hProcess, pRemoteMem, 0, MEM_RELEASE);

    // 枚举模块来查找远程模块基址
    HMODULE remoteModuleBase = nullptr;
    {
        std::wstring targetName = std::filesystem::path(dllPath).filename().wstring();
        std::transform(targetName.begin(), targetName.end(), targetName.begin(), ::towlower);

        DWORD cbNeeded = 0;
        // 第一次调用以获得所需字节数
        if (!EnumProcessModulesEx(hProcess, nullptr, 0, &cbNeeded, LIST_MODULES_ALL) && cbNeeded == 0) {
            // 某些系统/权限下第一次可能失败，尝试一个合理的初始缓冲区
            cbNeeded = 4096;
        }

        std::vector<HMODULE> modules;
        modules.resize(cbNeeded / sizeof(HMODULE) + 16);

        if (!EnumProcessModulesEx(hProcess, modules.data(), static_cast<DWORD>(modules.size() * sizeof(HMODULE)), &cbNeeded, LIST_MODULES_ALL)) {
            // 可能缓冲区仍不足，按 cbNeeded 重新分配并重试
            size_t neededCount = (cbNeeded / sizeof(HMODULE)) + 8;
            modules.clear();
            modules.resize(neededCount);
            if (!EnumProcessModulesEx(hProcess, modules.data(), static_cast<DWORD>(modules.size() * sizeof(HMODULE)), &cbNeeded, LIST_MODULES_ALL)) {
                std::cerr << "EnumProcessModulesEx failed: " << GetLastError() << "\n";
            }
        }

        size_t count = cbNeeded / sizeof(HMODULE);
        if (count > modules.size()) {
            count = modules.size();
        }

        for (size_t i = 0; i < count; ++i) {
            wchar_t modPath[MAX_PATH];
            if (GetModuleFileNameExW(hProcess, modules[i], modPath, _countof(modPath))) {
                std::wstring mpath(modPath);
                std::transform(mpath.begin(), mpath.end(), mpath.begin(), ::towlower);
                std::wstring fname = std::filesystem::path(mpath).filename().wstring();
                if (fname == targetName) {
                    remoteModuleBase = modules[i];
                    break;
                }
            }
        }
    }

    // 关闭注入线程句柄（LoadLibraryW 线程）
    CloseHandle(hThread);

    if (entryPoint == nullptr) {
        CloseHandle(hProcess);
        return true;
    }

    if (!remoteModuleBase) {
        std::cerr << "Failed to locate injected module in remote process.\n";
        CloseHandle(hProcess);
        return false;
    }

    // 本地 LoadLibrary 以获取本地基址与导出地址（注意副作用）
    HMODULE hLocalModule = LoadLibraryW(dllPath.c_str());
    if (!hLocalModule) {
        std::cerr << "LoadLibraryW (local) failed: " << GetLastError() << "\n";
        CloseHandle(hProcess);
        return false;
    }

    FARPROC localProc = GetProcAddress(hLocalModule, entryPoint);
    if (!localProc) {
        DWORD err = GetLastError();
        std::cerr << "GetProcAddress (local) failed for: " << entryPoint
        << ". GetLastError() = " << err << "\n";
        FreeLibrary(hLocalModule);
        CloseHandle(hProcess);
        return false;
    }

    // 计算偏移并得到远程函数地址
    ptrdiff_t offset = reinterpret_cast<BYTE*>(localProc) - reinterpret_cast<BYTE*>(hLocalModule);
    LPTHREAD_START_ROUTINE remoteFunc = reinterpret_cast<LPTHREAD_START_ROUTINE>(reinterpret_cast<BYTE*>(remoteModuleBase) + offset);

    // 在目标进程创建线程调用导出函数（参数传 nullptr）
    HANDLE hThread2 = CreateRemoteThread(hProcess, nullptr, 0, remoteFunc, nullptr, 0, nullptr);
    if (!hThread2) {
        std::cerr << "CreateRemoteThread(entryPoint) failed: " << GetLastError() << "\n";
        FreeLibrary(hLocalModule);
        CloseHandle(hProcess);
        return false;
    }

    // 等待远程入口函数执行（不读取返回值）
    WaitForSingleObject(hThread2, INFINITE);

    // 清理
    CloseHandle(hThread2);
    FreeLibrary(hLocalModule);
    CloseHandle(hProcess);

    return true;
}

bool QuickInject::injectDll(uint64_t pid, const std::filesystem::path &dllPath, bool onceInject, const char* entryPoint) {
    return injectDll(pid, dllPath.wstring(), onceInject, entryPoint);
}
