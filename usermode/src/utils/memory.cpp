#include "pch.hpp"

// Attempts to initialize memory access to the target process.
// Tries to hijack a handle first, falls back to OpenProcess if needed.
bool c_memory::setup()
{
    // Try to get the process ID for cs2.exe
    const auto process_id = this->get_process_id("cs2.exe");
    if (!process_id.has_value()) {
        LOG_ERROR("Failed to get process id for 'cs2.exe'. Make sure the game is running.");
        return false;
    }
    this->m_id = process_id.value();

    // Try to hijack a handle for the process
    auto handle = this->hijack_handle();
    if (!handle.has_value()) {
        LOG_WARNING("Failed to hijack a handle for 'cs2.exe', using OpenProcess fallback.");
        this->m_handle = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, false, this->m_id);
    }
    else {
        this->m_handle = handle.value();
    }

    return this->m_handle != nullptr;
}

// Finds the process ID of a process by name.
std::optional<uint32_t> c_memory::get_process_id(const std::string_view& process_name)
{
    const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return {};

    PROCESSENTRY32 process_entry = { 0 };
    process_entry.dwSize = sizeof(process_entry);

    // Enumerate all processes
    if (Process32First(snapshot, &process_entry)) {
        do {
            if (std::string_view(process_entry.szExeFile) == process_name) {
                CloseHandle(snapshot);
                return process_entry.th32ProcessID;
            }
        } while (Process32Next(snapshot, &process_entry));
    }

    CloseHandle(snapshot);
    return {};
}

// Attempts to hijack a handle to the target process using system handle enumeration.
std::optional<void*> c_memory::hijack_handle()
{
    auto cleanup = [](std::vector<uint8_t>& handle_info, void*& process_handle)
        {
            handle_info.clear();
            if (process_handle != INVALID_HANDLE_VALUE) {
                CloseHandle(process_handle);
                process_handle = nullptr;
            }
        };

    // Load required functions from ntdll.dll
    const auto ntdll = GetModuleHandleA("ntdll.dll");
    if (!ntdll) return {};

    using fn_nt_query_system_information = long(__stdcall*)(unsigned long, void*, unsigned long, unsigned long*);
    using fn_nt_duplicate_object = long(__stdcall*)(void*, void*, void*, void**, unsigned long, unsigned long, unsigned long);
    using fn_nt_open_process = long(__stdcall*)(void**, unsigned long, OBJECT_ATTRIBUTES*, CLIENT_ID*);
    using fn_rtl_adjust_privilege = long(__stdcall*)(unsigned long, unsigned char, unsigned char, unsigned char*);

    const auto nt_query_system_information = reinterpret_cast<fn_nt_query_system_information>(GetProcAddress(ntdll, "NtQuerySystemInformation"));
    const auto nt_duplicate_object = reinterpret_cast<fn_nt_duplicate_object>(GetProcAddress(ntdll, "NtDuplicateObject"));
    const auto nt_open_process = reinterpret_cast<fn_nt_open_process>(GetProcAddress(ntdll, "NtOpenProcess"));
    const auto rtl_adjust_privilege = reinterpret_cast<fn_rtl_adjust_privilege>(GetProcAddress(ntdll, "RtlAdjustPrivilege"));

    if (!nt_query_system_information || !nt_duplicate_object || !nt_open_process || !rtl_adjust_privilege)
        return {};

    // Enable SeDebugPrivilege
    uint8_t old_privilege = 0;
    rtl_adjust_privilege(0x14, 1, 0, &old_privilege);

    OBJECT_ATTRIBUTES object_attributes{};
    InitializeObjectAttributes(&object_attributes, nullptr, 0, nullptr, nullptr);

    std::vector<uint8_t> handle_info(sizeof(system_handle_info_t));
    std::pair<void*, void*> handle{ nullptr, nullptr };
    CLIENT_ID client_id{};

    // Query system handles, resizing buffer as needed
    unsigned long status = 0;
    do {
        handle_info.resize(handle_info.size() * 2);
        status = nt_query_system_information(0x10, handle_info.data(), static_cast<unsigned long>(handle_info.size()), nullptr);
    } while (status == 0xc0000004); // STATUS_INFO_LENGTH_MISMATCH

    if (!NT_SUCCESS(status)) {
        cleanup(handle_info, handle.first);
        return {};
    }

    // Iterate over all system handles
    const auto system_handle_info = reinterpret_cast<system_handle_info_t*>(handle_info.data());
    for (uint32_t idx = 0; idx < system_handle_info->m_handle_count; ++idx) {
        const auto& system_handle = system_handle_info->m_handles[idx];
        if (reinterpret_cast<void*>(system_handle.m_handle) == INVALID_HANDLE_VALUE || system_handle.m_object_type_number != 0x07)
            continue;

        client_id.UniqueProcess = reinterpret_cast<void*>(system_handle.m_process_id);

        if (handle.first != nullptr && handle.first == INVALID_HANDLE_VALUE) {
            CloseHandle(handle.first);
            continue;
        }

        // Try to open the process
        const auto open_process = nt_open_process(&handle.first, PROCESS_DUP_HANDLE, &object_attributes, &client_id);
        if (!NT_SUCCESS(open_process))
            continue;

        // Try to duplicate the handle
        const auto duplicate_object = nt_duplicate_object(handle.first, reinterpret_cast<void*>(system_handle.m_handle), GetCurrentProcess(), &handle.second, PROCESS_ALL_ACCESS, 0, 0);
        if (!NT_SUCCESS(duplicate_object))
            continue;

        // Check if the duplicated handle is for the correct process
        if (GetProcessId(handle.second) == this->m_id) {
            cleanup(handle_info, handle.first);
            return handle.second;
        }

        CloseHandle(handle.second);
    }

    cleanup(handle_info, handle.first);
    return {};
}

// Finds a pattern in the memory of a loaded module.
std::optional<c_address> c_memory::find_pattern(const std::string_view& module_name, const std::string_view& pattern)
{
    // Converts a pattern string to a vector of bytes (supports wildcards '?')
    constexpr auto pattern_to_bytes = [](const std::string_view& pattern) {
        std::vector<int32_t> bytes;
        for (uint32_t idx = 0; idx < pattern.size(); ++idx) {
            switch (pattern[idx]) {
            case '?':
                bytes.push_back(-1);
                break;
            case ' ':
                break;
            default: {
                if (idx + 1 < pattern.size()) {
                    uint32_t value = 0;
                    if (const auto [ptr, ec] = std::from_chars(pattern.data() + idx, pattern.data() + idx + 2, value, 16); ec == std::errc()) {
                        bytes.push_back(value);
                        ++idx;
                    }
                }
                break;
            }
            }
        }
        return bytes;
        };

    // Get module base and size
    const auto [module_base, module_size] = this->get_module_info(module_name);
    if (!module_base.has_value() || !module_size.has_value())
        return {};

    // Read module memory
    const auto module_data = std::make_unique<uint8_t[]>(module_size.value());
    if (!this->read_t(module_base.value(), module_data.get(), module_size.value()))
        return {};

    // Search for the pattern
    const auto pattern_bytes = pattern_to_bytes(pattern);
    for (uint32_t idx = 0; idx < module_size.value() - pattern_bytes.size(); ++idx) {
        bool found = true;
        for (uint32_t b_idx = 0; b_idx < pattern_bytes.size(); ++b_idx) {
            if (module_data[idx + b_idx] != pattern_bytes[b_idx] && pattern_bytes[b_idx] != -1) {
                found = false;
                break;
            }
        }
        if (found)
            return c_address(module_base.value() + idx);
    }

    return {};
}

// Retrieves the base address and size of a module in the target process.
std::pair<std::optional<uintptr_t>, std::optional<uintptr_t>> c_memory::get_module_info(const std::string_view& module_name)
{
    const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, this->m_id);
    if (snapshot == INVALID_HANDLE_VALUE)
        return {};

    MODULEENTRY32 module_entry = { 0 };
    module_entry.dwSize = sizeof(module_entry);

    // Helper for case-insensitive string comparison
    auto equals_ignore_case = [](const std::string_view str_1, const std::string_view str_2) {
        return (str_1.size() == str_2.size()) &&
            std::equal(str_1.begin(), str_1.end(), str_2.begin(), [](const char a, const char b) {
            return tolower(a) == tolower(b);
                });
        };

    // Enumerate all modules
    if (Module32First(snapshot, &module_entry)) {
        do {
            if (equals_ignore_case(module_entry.szModule, module_name)) {
                CloseHandle(snapshot);
                return std::make_pair(reinterpret_cast<uintptr_t>(module_entry.modBaseAddr), static_cast<uintptr_t>(module_entry.modBaseSize));
            }
        } while (Module32Next(snapshot, &module_entry));
    }

    CloseHandle(snapshot);
    return {};
}
