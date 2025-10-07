#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <iostream>
#include <mutex>
#include <atomic>
#include <thread>
#include <MinHook.h>

// HOOK游戏进程时间函数，达到加速或减速的效果
// 目前支持 QueryPerformanceCounter 和 GetTickCount
#ifdef _DEBUG
#define WTESTLOG(msg) \
    do { \
        std::wcout << msg << std::endl; \
    } while (0)
#else
#define WTESTLOG(msg) \
    do { \
    } while (0)
#endif

using QPC_t = BOOL(WINAPI*)(LARGE_INTEGER*);
using GTC_t = DWORD(WINAPI*)();

// 原始函数指针
static QPC_t fpQueryPerformanceCounter = nullptr;
static GTC_t fpGetTickCount = nullptr;

// 同步与基址数据
static std::mutex g_mutex;
#ifdef NDEBUG
static float g_factor = 1.0f;
#else
static float g_factor = 0.5f; // 调试模式下默认减速
#endif
static std::atomic<bool> g_inited{false};

// bases
static LONGLONG real_qpc_base = 0;
static LONGLONG virt_qpc_base = 0;
static DWORD64 real_tick_base = 0;
static DWORD64 virt_tick_base = 0;

// Hook QueryPerformanceCounter
BOOL WINAPI QueryPerformanceCounter_Hook(LARGE_INTEGER* lpPerformanceCount) {
    // 调用原函数拿到真实时间
    BOOL ok = fpQueryPerformanceCounter(lpPerformanceCount);
    if (!ok) return ok;

    std::lock_guard<std::mutex> lk(g_mutex);

    LONGLONG real_now = lpPerformanceCount->QuadPart;
    if (!g_inited.load()) {
        // 第一次调用初始化基准
        real_qpc_base = real_now;
        virt_qpc_base = real_qpc_base;
        // 尝试初始化 tick 基准，确保一致性
        LARGE_INTEGER liFreq;
        QueryPerformanceFrequency(&liFreq);
        g_inited.store(true);
    }

    // 计算偏移并按 factor 缩放
    LONGLONG delta = real_now - real_qpc_base;
    long double scaled = (long double)delta * static_cast<long double>(g_factor);
    LONGLONG newVal = virt_qpc_base + (LONGLONG)scaled;
    lpPerformanceCount->QuadPart = newVal;
    return TRUE;
}

// Hook GetTickCount
DWORD WINAPI GetTickCount_Hook() {
    DWORD real_now = fpGetTickCount();
    std::lock_guard<std::mutex> lk(g_mutex);

    if (!g_inited.load()) {
        // QueryPerformanceCounter 初始化 tick 基准
        real_tick_base = real_now;
        virt_tick_base = real_tick_base;
        g_inited.store(true);
    }

    DWORD64 delta = (DWORD64)real_now - real_tick_base;
    long double scaled = (long double)delta * static_cast<long double>(g_factor);
    DWORD64 newVal64 = virt_tick_base + (DWORD64)scaled;
    // 截断回 32 位（GetTickCount 返回 DWORD）
    return static_cast<DWORD>(newVal64 & 0xFFFFFFFFu);
}

// 更新速度因子
static void setSpeedFactor(float factor) {
    std::lock_guard<std::mutex> lk(g_mutex);
    if (factor <= 0.0f) {
        return;
    }

    if (g_inited.load()) {
        LARGE_INTEGER li;
        fpQueryPerformanceCounter(&li);
        LONGLONG now_real_qpc = li.QuadPart;

        // 计算当前虚拟时间（推进到此刻）
        LONGLONG delta_qpc = now_real_qpc - real_qpc_base;
        LONGLONG virt_now_qpc = virt_qpc_base + static_cast<LONGLONG>((long double)delta_qpc * g_factor);

        // 同理处理 GetTickCount
        DWORD now_real_tick = fpGetTickCount();
        DWORD64 delta_tick = (DWORD64)now_real_tick - real_tick_base;
        DWORD64 virt_now_tick = virt_tick_base + static_cast<DWORD64>((long double)delta_tick * g_factor);

        // 更新基准
        real_qpc_base = now_real_qpc;
        virt_qpc_base = virt_now_qpc;
        real_tick_base = now_real_tick;
        virt_tick_base = virt_now_tick;
    }

    g_factor = factor;
    WTESTLOG(L"[speedhack] speed factor updated to " << factor);
}

static std::thread* _listener = nullptr;
static std::atomic<bool> g_stop{false};

// 共享内存数据监听循环逻辑
static void sharedMemoryListener() {
    float lastValue = 0.0f;
    HANDLE hMapFile = nullptr;
    float* pShared = nullptr;

    while (!g_stop.load()) {
        // 如果还没有映射，尝试连接
        if (!hMapFile) {
            hMapFile = ::OpenFileMappingW(FILE_MAP_READ, FALSE, L"QuickChronoSpeed");
            if (hMapFile) {
                pShared = (float*)::MapViewOfFile(hMapFile, FILE_MAP_READ, 0, 0, sizeof(float));
                if (pShared) {
                    lastValue = *pShared;
                    setSpeedFactor(lastValue);
                    // OutputDebugStringW(L"[speedhack] connected to shared memory.\n");
                    WTESTLOG(L"[speedhack] connected to shared memory, initial speed factor: " << lastValue);
                } else {
                    ::CloseHandle(hMapFile);
                    hMapFile = nullptr;
                }
            }

            // 如果还没连接上，共享内存可能还没创建，稍后重试
            if (!hMapFile) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                // std::cout << "[speedhack] waiting for shared memory code: " << GetLastError() << "\n";
                WTESTLOG(L"[speedhack] waiting for shared memory code: " << GetLastError());
                continue;
            }
        }

        // 检查共享内存数据
        float current = *pShared;
        if (current != lastValue) {
            lastValue = current;
            if(current > 0.001f) {
                setSpeedFactor(current);
                // std::cout << "[speedhack] set speed factor: " << current << "\n";
                WTESTLOG(L"[speedhack] set speed factor: " << current);
            }
        }

        // 稍作休眠（减少 CPU 占用）
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        // // 检查共享内存是否被关闭（主程序退出）
        // if (WaitForSingleObject(hMapFile, 0) == WAIT_ABANDONED) {
        //     OutputDebugStringW(L"[speedhack] shared memory closed, will retry.\n");
        //     ::UnmapViewOfFile(pShared);
        //     ::CloseHandle(hMapFile);
        //     hMapFile = nullptr;
        //     pShared = nullptr;
        //     std::this_thread::sleep_for(std::chrono::seconds(1));
        //     std::cout << "[speedhack] retrying to connect shared memory...\n";
        // }
        if (!pShared || !hMapFile) {
            // 重新连接
            continue;
        }
    }

    // 清理资源
    if (pShared) {
        ::UnmapViewOfFile(pShared);
    }
    if (hMapFile) {
        ::CloseHandle(hMapFile);
    }
}

// 初始化钩子（在 testDllInit 中调用）
extern "C" __declspec(dllexport) DWORD WINAPI _chronoHookInit(LPVOID) {
    if (g_inited.load()) {
        // OutputDebugStringW(L"[speedhack] already initialized\n");
        WTESTLOG(L"[speedhack] already initialized");
        return 1;
    }

    if (MH_Initialize() != MH_OK) {
        // OutputDebugStringW(L"[speedhack] MH_Initialize failed\n");
        WTESTLOG(L"[speedhack] MH_Initialize failed");
        return 1;
    }

    // 获取原函数地址
    fpQueryPerformanceCounter = reinterpret_cast<QPC_t>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "QueryPerformanceCounter"));
    fpGetTickCount = reinterpret_cast<GTC_t>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetTickCount"));

    if (!fpQueryPerformanceCounter || !fpGetTickCount) {
        // OutputDebugStringW(L"[speedhack] failed to get original function addresses\n");
        WTESTLOG(L"[speedhack] failed to get original function addresses");
        MH_Uninitialize();
        return 1;
    }

    // 创建钩子
    if (MH_CreateHook(reinterpret_cast<LPVOID>(fpQueryPerformanceCounter),
                      reinterpret_cast<LPVOID>(QueryPerformanceCounter_Hook),
                      reinterpret_cast<LPVOID*>(&fpQueryPerformanceCounter)) != MH_OK) {
        // OutputDebugStringW(L"[speedhack] MH_CreateHook QueryPerformanceCounter failed\n");
        WTESTLOG(L"[speedhack] MH_CreateHook QueryPerformanceCounter failed");
        MH_Uninitialize();
        return 1;
    }

    if (MH_CreateHook(reinterpret_cast<LPVOID>(fpGetTickCount),
                      reinterpret_cast<LPVOID>(GetTickCount_Hook),
                      reinterpret_cast<LPVOID*>(&fpGetTickCount)) != MH_OK) {
        // OutputDebugStringW(L"[speedhack] MH_CreateHook GetTickCount failed\n");
        WTESTLOG(L"[speedhack] MH_CreateHook GetTickCount failed");
        MH_RemoveHook(reinterpret_cast<LPVOID>(fpQueryPerformanceCounter));
        MH_Uninitialize();
        return 1;
    }

    // 启用 hooks
    if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK) {
        // OutputDebugStringW(L"[speedhack] MH_EnableHook failed\n");
        WTESTLOG(L"[speedhack] MH_EnableHook failed");
        MH_RemoveHook(reinterpret_cast<LPVOID>(fpQueryPerformanceCounter));
        MH_RemoveHook(reinterpret_cast<LPVOID>(fpGetTickCount));
        MH_Uninitialize();
        return 1;
    }

    // 初始化 触发一个真实时间采样
    LARGE_INTEGER li;
    fpQueryPerformanceCounter(&li);
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        real_qpc_base = li.QuadPart;
        virt_qpc_base = real_qpc_base;
        real_tick_base = fpGetTickCount();
        virt_tick_base = real_tick_base;
        g_inited.store(true);
    }

    if(_listener == nullptr) {
        _listener = new std::thread(sharedMemoryListener);
    }
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        break;
    case DLL_PROCESS_DETACH:
        // 移除钩子
        if (g_inited.load()) {
            MH_DisableHook(MH_ALL_HOOKS);
            MH_RemoveHook(reinterpret_cast<LPVOID>(QueryPerformanceCounter_Hook));
            MH_RemoveHook(reinterpret_cast<LPVOID>(GetTickCount_Hook));
            MH_Uninitialize();
            // OutputDebugStringW(L"[speedhack] hooks removed on detach\n");
            WTESTLOG(L"[speedhack] hooks removed on detach");
        }
        g_stop.store(true);
        if(_listener == nullptr) {
            // 安全退出监听线程
            if(_listener->joinable()) {
                _listener->join();
            }
            delete _listener;
            _listener = nullptr;
        }
        break;
    }
    return TRUE;
}
