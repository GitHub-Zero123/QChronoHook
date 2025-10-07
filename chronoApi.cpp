#include <iostream>
#include <thread>
#include <chrono>
#include <string>
#include <cstdint>
#include <filesystem>
#include <quickInject.h>
#include <pyipc.hpp>
// By Zero123

#ifdef WIN32
#define NOMINMAX
#define NOGDI
#include <Windows.h>
// DLL注入处理
static void dllInject(uint64_t pid) {
    // 获取当前exe所在目录自动拼接dll路径
    std::wstring exePath(MAX_PATH, L'\0');
    GetModuleFileNameW(NULL, exePath.data(), MAX_PATH);
    exePath.resize(wcslen(exePath.c_str())); // 去除多余的\0
    std::filesystem::path dllPath = std::filesystem::path(exePath).parent_path() / L"chronoHook.dll";
    QuickInject::injectDll(pid, dllPath, true, "_chronoHookInit");
}
#else
static void dllInject(uint64_t pid) = delete;
#endif

// 自动搜索Minecraft Pid
static void autoFindMinecraftPid(uint64_t& pid) {
    // Windows特定代码
    static std::wstring processName = L"Minecraft.Windows.exe"; // 目标进程名
    std::cout << "Waiting for process \"Minecraft\" to start...\n";
    while(pid == 0) {
        pid = QuickInject::getPidByName(processName);
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    std::wcout << L"Found process \"" << processName << L"\" with PID: " << pid << L"\n";
}

// 写入共享内存中速度值
static bool WriteSpeedToSharedMemory(float speed) {
    // static 用于在函数内保存全局状态
    static HANDLE hMapFile = nullptr;
    static float* pShared = nullptr;

    // 如果是第一次调用，初始化共享内存
    if (!hMapFile || !pShared) {
        hMapFile = ::CreateFileMappingW(
            INVALID_HANDLE_VALUE,    // 使用系统分页文件
            nullptr,                 // 默认安全属性
            PAGE_READWRITE,          // 可读可写
            0,                       // 高位文件大小
            sizeof(float),           // 内存大小
            L"QuickChronoSpeed"      // 名称
        );

        if (!hMapFile) {
            std::cerr << "[speedhack] CreateFileMapping failed: " << GetLastError() << "\n";
            return false;
        }

        pShared = (float*)::MapViewOfFile(
            hMapFile,
            FILE_MAP_READ | FILE_MAP_WRITE,
            0, 0,
            sizeof(float)
        );

        if (!pShared) {
            std::cerr << "[speedhack] MapViewOfFile failed: " << GetLastError() << "\n";
            ::CloseHandle(hMapFile);
            hMapFile = nullptr;
            return false;
        }

        // 注册进程退出时自动清理的回调
        static struct Cleaner {
            ~Cleaner() {
                if (pShared) {
                    ::UnmapViewOfFile(pShared);
                }
                if (hMapFile) {
                    ::CloseHandle(hMapFile);
                }
            }
        } cleaner;
    }

    // 写入数据
    if (pShared) {
        *pShared = speed;
        return true;
    }

    return false;
}

PY_IPC_REGISTER_HANDLER("set_game_speed", [](const PyIPC::json& data, PyIPC::json& result) {
    // 设置游戏速度API
    float speed = data.value("value", 1.0f);
    if(speed < 0.05f) {
        // 最小0.05倍速
        speed = 0.05f;
    }
    if(WriteSpeedToSharedMemory(speed)) {
        result["status"] = "success";
    } else {
        result["status"] = "error";
    }
});

PY_IPC_REGISTER_HANDLER("safe_close", [](const PyIPC::json& data, PyIPC::json& result) {
    WriteSpeedToSharedMemory(1.0f); // 关闭前恢复默认速度
    result["status"] = "success";
    std::exit(0); // 直接退出进程
});

int main(int argc, char** argv) {
    uint64_t pid = 0;
    if(argc >= 2) {
        pid = std::strtoull(argv[1], nullptr, 10);
    } else {
        // 用户未传递参数，自动搜索
        autoFindMinecraftPid(pid);
    }
    dllInject(pid);
    // 建立IPC消息循环，提供给Python端调用
    while(1) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        if(!PyIPC::checkParentProcessAliveFromArgs(argc, argv)) {
            return 0;
        }
        else if(PyIPC::update() == PyIPC::UPDATE_STATUS::ERROR) {
            return 1;
        }
    }
    // 测试循环更新速度
    // while(1) {
    //     std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    //     // 生成0.1-2.0的随机速度
    //     float speed = 0.1f + static_cast<float>(rand()) / (static_cast<float>(RAND_MAX/(2.0f - 0.1f)));
    //     WriteSpeedToSharedMemory(speed);
    //     std::cout << "Set speed to " << speed << "\n";
    // }
    return 0;
}