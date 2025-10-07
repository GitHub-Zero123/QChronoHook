#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <iostream>
#include <mutex>
#include <atomic>
#include <thread>
#include <MinHook.h>

#ifdef _DEBUG
#define WTESTLOG(msg) \
    do { std::wcout << msg << std::endl; } while (0)
#else
#define WTESTLOG(msg) \
    do { } while (0)
#endif

using QPC_t = BOOL(WINAPI*)(LARGE_INTEGER*);
using GTC_t = DWORD(WINAPI*)();
using GSTAFT_t = VOID(WINAPI*)(LPFILETIME);
using GSTPAFT_t = VOID(WINAPI*)(LPFILETIME);

// 一些需要hook的原始函数指针
static QPC_t fpQueryPerformanceCounter = nullptr;
static GTC_t fpGetTickCount = nullptr;
static GSTAFT_t fpGetSystemTimeAsFileTime = nullptr;
static GSTPAFT_t fpGetSystemTimePreciseAsFileTime = nullptr;

static std::mutex g_mutex;
#ifdef NDEBUG
static float g_factor = 1.0f;
#else
static float g_factor = 0.5f;
#endif
static std::atomic<bool> g_inited{ false };

static LONGLONG real_qpc_base = 0;
static LONGLONG virt_qpc_base = 0;
static DWORD64 real_tick_base = 0;
static DWORD64 virt_tick_base = 0;

// Hook GetSystemTimeAsFileTime 影响获取系统时间的函数
VOID WINAPI GetSystemTimeAsFileTime_Hook(LPFILETIME lpFileTime) {
    fpGetSystemTimeAsFileTime(lpFileTime);

    std::lock_guard<std::mutex> lk(g_mutex);
    if (!g_inited.load()) {
        return;
    }

    ULONGLONG real_now = (((ULONGLONG)lpFileTime->dwHighDateTime) << 32) | lpFileTime->dwLowDateTime;
    ULONGLONG delta = real_now - real_tick_base;
    long double scaled = (long double)delta * g_factor;
    ULONGLONG newVal = virt_tick_base + (ULONGLONG)scaled;

    lpFileTime->dwLowDateTime = (DWORD)newVal;
    lpFileTime->dwHighDateTime = (DWORD)(newVal >> 32);
}

// Hook GetSystemTimePreciseAsFileTime 影响获取系统时间的函数
VOID WINAPI GetSystemTimePreciseAsFileTime_Hook(LPFILETIME lpFileTime) {
    fpGetSystemTimePreciseAsFileTime(lpFileTime);

    std::lock_guard<std::mutex> lk(g_mutex);
    if (!g_inited.load()) {
        return;
    }

    ULONGLONG real_now = (((ULONGLONG)lpFileTime->dwHighDateTime) << 32) | lpFileTime->dwLowDateTime;
    ULONGLONG delta = real_now - real_tick_base;
    long double scaled = (long double)delta * g_factor;
    ULONGLONG newVal = virt_tick_base + (ULONGLONG)scaled;

    lpFileTime->dwLowDateTime = (DWORD)newVal;
    lpFileTime->dwHighDateTime = (DWORD)(newVal >> 32);
}

// Hook QueryPerformanceCounter 影响高精度计时函数
BOOL WINAPI QueryPerformanceCounter_Hook(LARGE_INTEGER* lpPerformanceCount) {
    BOOL ok = fpQueryPerformanceCounter(lpPerformanceCount);
    if (!ok) {
        return ok;
    }

    std::lock_guard<std::mutex> lk(g_mutex);
    LONGLONG real_now = lpPerformanceCount->QuadPart;
    if (!g_inited.load()) {
        real_qpc_base = real_now;
        virt_qpc_base = real_qpc_base;
        LARGE_INTEGER liFreq;
        QueryPerformanceFrequency(&liFreq);
        g_inited.store(true);
    }

    LONGLONG delta = real_now - real_qpc_base;
    long double scaled = (long double)delta * g_factor;
    lpPerformanceCount->QuadPart = virt_qpc_base + (LONGLONG)scaled;
    return TRUE;
}

// Hook GetTickCount 影响获取系统启动后经过的毫秒数
DWORD WINAPI GetTickCount_Hook() {
    FILETIME ft;
    fpGetSystemTimeAsFileTime(&ft);

    std::lock_guard<std::mutex> lk(g_mutex);
    if (!g_inited.load()) {
        ULONGLONG init_filetime = (((ULONGLONG)ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
        real_tick_base = init_filetime;
        virt_tick_base = init_filetime;
        g_inited.store(true);
    }

    ULONGLONG real_now_filetime = (((ULONGLONG)ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    ULONGLONG delta = real_now_filetime - real_tick_base;
    long double scaled = (long double)delta * g_factor;
    ULONGLONG newVal = virt_tick_base + (ULONGLONG)scaled;

    return static_cast<DWORD>((newVal / 10000ULL) & 0xFFFFFFFFu);
}

// 设置时间流逝速度因子
static void setSpeedFactor(float factor) {
    std::lock_guard<std::mutex> lk(g_mutex);
    if (factor <= 0.0f) {
        // 不允许非正数
        return;
    }

    if (g_inited.load()) {
        LARGE_INTEGER li;
        fpQueryPerformanceCounter(&li);
        LONGLONG delta_qpc = li.QuadPart - real_qpc_base;
        LONGLONG virt_now_qpc = virt_qpc_base + static_cast<LONGLONG>(delta_qpc * g_factor);

        FILETIME ft;
        fpGetSystemTimeAsFileTime(&ft);
        ULONGLONG delta_ft = (((ULONGLONG)ft.dwHighDateTime) << 32 | ft.dwLowDateTime) - real_tick_base;
        ULONGLONG virt_now_filetime = virt_tick_base + static_cast<ULONGLONG>(delta_ft * g_factor);

        real_qpc_base = li.QuadPart;
        virt_qpc_base = virt_now_qpc;
        real_tick_base = (((ULONGLONG)ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
        virt_tick_base = virt_now_filetime;
    }

    g_factor = factor;
    WTESTLOG(L"[speedhack] speed factor updated to " << factor);
}

static std::thread* _listener = nullptr;
static std::atomic<bool> g_stop { false };

// 共享内存消息循环通信
static void sharedMemoryListener() {
    float lastValue = 0.0f;
    HANDLE hMapFile = nullptr;
    float* pShared = nullptr;

    while (!g_stop.load()) {
        if (!hMapFile) {
            hMapFile = ::OpenFileMappingW(FILE_MAP_READ, FALSE, L"QuickChronoSpeed");
            if (hMapFile) {
                pShared = (float*)::MapViewOfFile(hMapFile, FILE_MAP_READ, 0, 0, sizeof(float));
                if (pShared) {
                    lastValue = *pShared;
                    if (lastValue > 0.001f) {
                        setSpeedFactor(lastValue); // 处理第一次数据
                    }
                }
            }
            if (!hMapFile) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                continue;
            }
        }
        // 读取共享内存数据同步写入
        float current = *pShared;
        if (current != lastValue) {
            lastValue = current;
            if (current > 0.001f) {
                setSpeedFactor(current);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    if (pShared) {
        ::UnmapViewOfFile(pShared);
    }
    if (hMapFile) {
        ::CloseHandle(hMapFile);
    }
}

extern "C" __declspec(dllexport) DWORD WINAPI _chronoHookInit(LPVOID) {
    if (g_inited.load()) {
        return 1;
    }

    if (MH_Initialize() != MH_OK) {
        return 1;
    }

    auto kernel32 = GetModuleHandleW(L"kernel32.dll");
    // 获取kernel32时间相关的函数地址
    fpQueryPerformanceCounter = reinterpret_cast<QPC_t>(GetProcAddress(kernel32, "QueryPerformanceCounter"));
    fpGetTickCount = reinterpret_cast<GTC_t>(GetProcAddress(kernel32, "GetTickCount"));
    fpGetSystemTimeAsFileTime = reinterpret_cast<GSTAFT_t>(GetProcAddress(kernel32, "GetSystemTimeAsFileTime"));
    fpGetSystemTimePreciseAsFileTime = reinterpret_cast<GSTPAFT_t>(GetProcAddress(kernel32, "GetSystemTimePreciseAsFileTime"));

    if (!fpQueryPerformanceCounter || !fpGetTickCount || !fpGetSystemTimeAsFileTime || !fpGetSystemTimePreciseAsFileTime) {
        MH_Uninitialize();
        return 1;
    }

    // 创建钩子
    MH_CreateHook(fpQueryPerformanceCounter, QueryPerformanceCounter_Hook, reinterpret_cast<LPVOID*>(&fpQueryPerformanceCounter));
    MH_CreateHook(fpGetTickCount, GetTickCount_Hook, reinterpret_cast<LPVOID*>(&fpGetTickCount));
    MH_CreateHook(fpGetSystemTimeAsFileTime, GetSystemTimeAsFileTime_Hook, reinterpret_cast<LPVOID*>(&fpGetSystemTimeAsFileTime));
    MH_CreateHook(fpGetSystemTimePreciseAsFileTime, GetSystemTimePreciseAsFileTime_Hook, reinterpret_cast<LPVOID*>(&fpGetSystemTimePreciseAsFileTime));

    MH_EnableHook(MH_ALL_HOOKS);

    LARGE_INTEGER li;
    fpQueryPerformanceCounter(&li);
    {
        // 初始化基准时间点
        std::lock_guard<std::mutex> lk(g_mutex);
        real_qpc_base = li.QuadPart;
        virt_qpc_base = real_qpc_base;

        FILETIME ftInit;
        fpGetSystemTimeAsFileTime(&ftInit);
        ULONGLONG init_filetime = (((ULONGLONG)ftInit.dwHighDateTime) << 32) | ftInit.dwLowDateTime;
        real_tick_base = init_filetime;
        virt_tick_base = init_filetime;
        g_inited.store(true);
    }
    if (!_listener) {
        // 启动监听线程
        _listener = new std::thread(sharedMemoryListener);
    }
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule); // 减少 DLL_THREAD_ATTACH/DETACH 调用
        break;
    case DLL_PROCESS_DETACH:
        g_stop.store(true);
        if (_listener) {
            // 安全的回收listener线程
            if (_listener->joinable()) {
                _listener->join();
            }
            delete _listener;
            _listener = nullptr;
        }
        if (g_inited.load()) {
            MH_DisableHook(MH_ALL_HOOKS);
            MH_RemoveHook(fpQueryPerformanceCounter);
            MH_RemoveHook(fpGetTickCount);
            MH_RemoveHook(fpGetSystemTimeAsFileTime);
            MH_RemoveHook(fpGetSystemTimePreciseAsFileTime);
            MH_Uninitialize();
        }
        break;
    }
    return TRUE;
}
