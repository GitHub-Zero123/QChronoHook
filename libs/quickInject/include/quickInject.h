#pragma once
#include <string>
#include <filesystem>
#include <stdint.h>

namespace QuickInject {
    // 检查指定进程是否加载了特定模块（DLL）
    bool isModuleLoaded(uint64_t pid, const std::wstring& targetPath);

    // 根据进程名获取 PID，返回第一个匹配的 PID
    uint64_t getPidByName(const std::wstring& procNameW);

    // 注入动态链接库
    bool injectDll(uint64_t pid, const std::wstring& dllPath, bool onceInject=true, const char* entryPoint=nullptr);

    // 注入 DLL 接受 std::filesystem::path 作为路径参数
    bool injectDll(uint64_t pid, const std::filesystem::path& dllPath, bool onceInject=true, const char* entryPoint=nullptr);
}