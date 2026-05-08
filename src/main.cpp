#include <chrono>
#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <windows.h>
#include <setupapi.h>

#if defined(GX12_HAS_GAMEINPUT)
#include <GameInput.h>
#endif
#include <hidapi.h>
#include <toml++/toml.hpp>

#ifndef GX12_APP_VERSION
#define GX12_APP_VERSION "0.1.0"
#endif

#ifndef GX12_TOMLPP_TAG
#define GX12_TOMLPP_TAG "v3.4.0"
#endif

#ifndef GX12_HIDAPI_TAG
#define GX12_HIDAPI_TAG "hidapi-0.15.0"
#endif

namespace {

constexpr const char* kGx12UsbHardwareId = "VID_1209&PID_4F54&MI_00";
constexpr const wchar_t* kLauncherStopEventName = L"Local\\GX12MouseLauncherStop";

constexpr int kTrainerProfileMaxSeconds = 24 * 60 * 60;
constexpr int kTrainerProfileIndefiniteSeconds = 0;

double ClampDouble(double value, double lo, double hi);
int ParseVirtualKeyName(const std::string& key_name);
std::string ToLowerAscii(std::string value);

void OptOutOfExecutionSpeedThrottling() {
    PROCESS_POWER_THROTTLING_STATE pt{};
    pt.Version     = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    pt.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
    pt.StateMask   = 0;

    BOOL ok = SetProcessInformation(GetCurrentProcess(),
                                    ProcessPowerThrottling,
                                    &pt,
                                    sizeof(pt));
    if (ok) {
        std::printf("  power throttle: opted out (EcoQoS execution speed)\n");
    } else {
        std::printf("  power throttle: opt-out FAILED (le=%lu)\n",
                    static_cast<unsigned long>(GetLastError()));
    }
}

void PrintBanner() {
    std::printf("gx12mouse v%s\n", GX12_APP_VERSION);

    std::printf("  toml++       : %d.%d.%d (tag %s)\n",
                TOML_LIB_MAJOR,
                TOML_LIB_MINOR,
                TOML_LIB_PATCH,
                GX12_TOMLPP_TAG);

    if (hid_init() == 0) {
        const struct hid_api_version* v = hid_version();
        if (v) {
            std::printf("  hidapi       : %d.%d.%d (tag %s)\n",
                        v->major,
                        v->minor,
                        v->patch,
                        GX12_HIDAPI_TAG);
        } else {
            std::printf("  hidapi       : (hid_version null; tag %s)\n",
                        GX12_HIDAPI_TAG);
        }
        hid_exit();
    } else {
        std::printf("  hidapi       : hid_init failed (tag %s)\n",
                    GX12_HIDAPI_TAG);
    }

    OptOutOfExecutionSpeedThrottling();
}

int ParsePositiveSeconds(const char* text, int fallback) {
    if (!text || text[0] == '\0') {
        return fallback;
    }

    char* end = nullptr;
    const long value = std::strtol(text, &end, 10);
    if (!end || *end != '\0' || value <= 0 || value > 30) {
        return -1;
    }

    return static_cast<int>(value);
}

void PrintUsage() {
    std::printf("\nusage:\n");
    std::printf("  gx12mouse                     Show dependency banner\n");
    std::printf("  gx12mouse --mouse-rate [sec] [sink|foreground|overlay|overlay-hold]\n");
    std::printf("                                  Measure Win32 Raw Input mouse delivery\n");
    std::printf("  gx12mouse --mouse-rate-gameinput [sec]\n");
    std::printf("                                  Measure Microsoft GameInput mouse delivery\n");
    std::printf("  gx12mouse --mouse-devices-gameinput [sec]\n");
    std::printf("                                  List GameInput mouse devices and show per-device deltas\n");
    std::printf("  gx12mouse --keyboard-rate-gameinput [sec]\n");
    std::printf("                                  Measure Microsoft GameInput keyboard delivery\n");
    std::printf("  gx12mouse --wooting-rate [sec] [keys...]\n");
    std::printf("                                  Measure Wooting Analog SDK key depth, e.g. W A S D Space\n");
    std::printf("  gx12mouse --hid-list          List HID devices with stable probe indexes\n");
    std::printf("  gx12mouse --hid-rate [sec] [index]\n");
    std::printf("                                  Read HID input reports from one device\n");
    std::printf("  gx12mouse --gx12-hid-capture [sec] [csv_path]\n");
    std::printf("                                  Decode GX12 joystick HID channel values and summarize granularity\n");
    std::printf("  gx12mouse --trainer COM3|auto [sec] [gain] [frame_rate_hz]\n");
    std::printf("                                  Send mouse-derived SBUS trainer frames over USB-VCP\n");
    std::printf("  gx12mouse --trainer-sweep COM3|auto [sec] [amplitude] [frame_rate_hz] [legacy|gx12_2x]\n");
    std::printf("                                  Send deterministic SBUS roll/pitch sweep over USB-VCP\n");
    std::printf("  gx12mouse --trainer-resolution-self-test\n");
    std::printf("                                  Compare legacy and GX12 2x trainer SBUS quantization\n");
    std::printf("  gx12mouse --trainer-profile FILE [sec|live]\n");
    std::printf("                                  Send mouse-derived SBUS using a TOML tuning profile\n");
    std::printf("                                  Add 'live' to reload mapper/keyboard edits while running\n");
    std::printf("  gx12mouse --mouse-aim-dry-run FILE [sec]\n");
    std::printf("                                  Run a Reticle Aim profile without opening serial\n");
    std::printf("  gx12mouse --mouse-left-dry-run FILE [sec]\n");
    std::printf("                                  Dry-run explicit right/left mouse role bindings without serial\n");
    std::printf("  gx12mouse --elastic-preview FILE\n");
    std::printf("                                  Print an ASCII preview of the profile's elastic return curve\n");
    std::printf("  gx12mouse --gimbal-preview FILE\n");
    std::printf("                                  Preview dynamic gimbal, adaptive gain, and radial gate behavior\n");
    std::printf("  gx12mouse --mouse-spike-test FILE\n");
    std::printf("                                  Test the profile's Hampel mouse despike filter on a synthetic spike\n");
    std::printf("  gx12mouse --input-filter-preview FILE\n");
    std::printf("                                  Preview off, legacy smoothing, and one-euro input filtering\n");
    std::printf("  gx12mouse --output-curve-preview FILE\n");
    std::printf("                                  Preview expo, node, and Actual Rates output curves\n");
    std::printf("  gx12mouse --tune FILE [sec]    Guided profile run with setup and tuning prompts\n");
    std::printf("  gx12mouse --show-profile FILE  Parse and print a TOML tuning profile without opening serial\n");
}

std::string WideToUtf8(const wchar_t* text) {
    if (!text || text[0] == L'\0') {
        return {};
    }

    const int bytes = WideCharToMultiByte(CP_UTF8,
                                          0,
                                          text,
                                          -1,
                                          nullptr,
                                          0,
                                          nullptr,
                                          nullptr);
    if (bytes <= 1) {
        return {};
    }

    std::string out(static_cast<size_t>(bytes - 1), '\0');
    WideCharToMultiByte(CP_UTF8,
                        0,
                        text,
                        -1,
                        out.data(),
                        bytes,
                        nullptr,
                        nullptr);
    return out;
}

std::string HidErrorUtf8(hid_device* device) {
    return WideToUtf8(hid_error(device));
}

std::string HidReadErrorUtf8(hid_device* device) {
    return WideToUtf8(hid_read_error(device));
}

struct HidDeviceEntry {
    int index = 0;
    std::string path;
    unsigned short vendor_id = 0;
    unsigned short product_id = 0;
    unsigned short release_number = 0;
    unsigned short usage_page = 0;
    unsigned short usage = 0;
    int interface_number = -1;
    hid_bus_type bus_type = HID_API_BUS_UNKNOWN;
    std::string manufacturer;
    std::string product;
    std::string serial;
};

bool IsMouseHid(const HidDeviceEntry& device) {
    return device.usage_page == 0x01 && device.usage == 0x02;
}

std::vector<HidDeviceEntry> EnumerateHidDevices() {
    std::vector<HidDeviceEntry> devices;
    hid_device_info* head = hid_enumerate(0, 0);
    int index = 0;
    for (hid_device_info* it = head; it; it = it->next) {
        HidDeviceEntry entry;
        entry.index = index++;
        entry.path = it->path ? it->path : "";
        entry.vendor_id = it->vendor_id;
        entry.product_id = it->product_id;
        entry.release_number = it->release_number;
        entry.usage_page = it->usage_page;
        entry.usage = it->usage;
        entry.interface_number = it->interface_number;
        entry.bus_type = it->bus_type;
        entry.manufacturer = WideToUtf8(it->manufacturer_string);
        entry.product = WideToUtf8(it->product_string);
        entry.serial = WideToUtf8(it->serial_number);
        devices.push_back(entry);
    }
    hid_free_enumeration(head);
    return devices;
}

const char* HidBusName(hid_bus_type bus) {
    switch (bus) {
    case HID_API_BUS_USB:
        return "USB";
    case HID_API_BUS_BLUETOOTH:
        return "Bluetooth";
    case HID_API_BUS_I2C:
        return "I2C";
    case HID_API_BUS_SPI:
        return "SPI";
    default:
        return "Unknown";
    }
}

int RunHidList() {
    if (hid_init() != 0) {
        std::fprintf(stderr, "hid_init failed: %s\n", HidErrorUtf8(nullptr).c_str());
        return 1;
    }

    const std::vector<HidDeviceEntry> devices = EnumerateHidDevices();
    std::printf("\n--hid-list: %zu HID device interface(s).\n", devices.size());
    std::printf("Mouse candidates have usage_page=0x0001 usage=0x0002.\n\n");

    for (const HidDeviceEntry& device : devices) {
        std::printf("[%02d]%s vid=0x%04x pid=0x%04x rel=0x%04x bus=%s usage=0x%04x:0x%04x iface=%d\n",
                    device.index,
                    IsMouseHid(device) ? " MOUSE" : "      ",
                    device.vendor_id,
                    device.product_id,
                    device.release_number,
                    HidBusName(device.bus_type),
                    device.usage_page,
                    device.usage,
                    device.interface_number);
        std::printf("      mfg='%s' product='%s' serial='%s'\n",
                    device.manufacturer.c_str(),
                    device.product.c_str(),
                    device.serial.c_str());
        std::printf("      path=%s\n", device.path.c_str());
    }

    hid_exit();
    return 0;
}

int ParseNonNegativeInt(const char* text, int fallback) {
    if (!text || text[0] == '\0') {
        return fallback;
    }

    char* end = nullptr;
    const long value = std::strtol(text, &end, 10);
    if (!end || *end != '\0' || value < 0 || value > 9999) {
        return -1;
    }

    return static_cast<int>(value);
}

int ParsePositiveIntLimit(const char* text, int fallback, int max_value) {
    if (!text || text[0] == '\0') {
        return fallback;
    }

    char* end = nullptr;
    const long value = std::strtol(text, &end, 10);
    if (!end || *end != '\0' || value <= 0 || value > max_value) {
        return -1;
    }

    return static_cast<int>(value);
}

int DefaultHidRateIndex(const std::vector<HidDeviceEntry>& devices) {
    for (const HidDeviceEntry& device : devices) {
        if (IsMouseHid(device) &&
            (device.product.find("OP1") != std::string::npos ||
             device.manufacturer.find("Endgame") != std::string::npos)) {
            return device.index;
        }
    }
    for (const HidDeviceEntry& device : devices) {
        if (IsMouseHid(device)) {
            return device.index;
        }
    }
    return devices.empty() ? -1 : devices.front().index;
}

const HidDeviceEntry* FindHidByIndex(const std::vector<HidDeviceEntry>& devices, int index) {
    for (const HidDeviceEntry& device : devices) {
        if (device.index == index) {
            return &device;
        }
    }
    return nullptr;
}

std::string HexPrefix(const unsigned char* data, int length) {
    constexpr char kHex[] = "0123456789abcdef";
    const int shown = length < 16 ? length : 16;
    std::string out;
    out.reserve(static_cast<size_t>(shown * 3));
    for (int i = 0; i < shown; ++i) {
        if (i != 0) {
            out.push_back(' ');
        }
        out.push_back(kHex[(data[i] >> 4) & 0x0f]);
        out.push_back(kHex[data[i] & 0x0f]);
    }
    return out;
}

struct HidRateStats {
    uint64_t reports = 0;
    uint64_t timeouts = 0;
    uint64_t errors = 0;
    uint64_t bytes = 0;
    int min_len = 0;
    int max_len = 0;
    int last_len = 0;
    std::array<unsigned char, 16> last_prefix{};
};

void RecordHidReport(HidRateStats* stats, const unsigned char* data, int length) {
    ++stats->reports;
    stats->bytes += static_cast<uint64_t>(length);
    stats->last_len = length;
    if (stats->min_len == 0 || length < stats->min_len) {
        stats->min_len = length;
    }
    if (length > stats->max_len) {
        stats->max_len = length;
    }

    stats->last_prefix = {};
    const int copy = length < static_cast<int>(stats->last_prefix.size())
                         ? length
                         : static_cast<int>(stats->last_prefix.size());
    for (int i = 0; i < copy; ++i) {
        stats->last_prefix[static_cast<size_t>(i)] = data[i];
    }
}

void PrintHidRateSample(const HidRateStats& previous,
                        const HidRateStats& current,
                        double elapsed_seconds) {
    const uint64_t reports = current.reports - previous.reports;
    const uint64_t timeouts = current.timeouts - previous.timeouts;
    const uint64_t errors = current.errors - previous.errors;
    const uint64_t bytes = current.bytes - previous.bytes;
    constexpr double kWindowSeconds = 0.250;
    const double hz = static_cast<double>(reports) / kWindowSeconds;
    const double avg_bytes = reports > 0 ? static_cast<double>(bytes) / reports : 0.0;
    const std::string last = HexPrefix(current.last_prefix.data(), current.last_len);

    std::printf("[%.3fs] reports=%5llu rate=%7.1f Hz bytes/report=%5.1f len=%d..%d timeout=%llu err=%llu last(%d)=%s\n",
                elapsed_seconds,
                static_cast<unsigned long long>(reports),
                hz,
                avg_bytes,
                current.min_len,
                current.max_len,
                static_cast<unsigned long long>(timeouts),
                static_cast<unsigned long long>(errors),
                current.last_len,
                last.c_str());
}

int RunHidRate(int seconds, int requested_index) {
    if (seconds <= 0 || seconds > 30) {
        std::fprintf(stderr, "--hid-rate duration must be 1..30 seconds.\n");
        return 2;
    }

    if (hid_init() != 0) {
        std::fprintf(stderr, "hid_init failed: %s\n", HidErrorUtf8(nullptr).c_str());
        return 1;
    }

    const std::vector<HidDeviceEntry> devices = EnumerateHidDevices();
    const int index = requested_index >= 0 ? requested_index : DefaultHidRateIndex(devices);
    const HidDeviceEntry* selected = FindHidByIndex(devices, index);
    if (!selected) {
        std::fprintf(stderr, "No HID device index %d. Run --hid-list first.\n", index);
        hid_exit();
        return 2;
    }

    std::printf("\n--hid-rate: opening index %d for %d second(s).\n", index, seconds);
    std::printf("  vid=0x%04x pid=0x%04x usage=0x%04x:0x%04x iface=%d product='%s'\n",
                selected->vendor_id,
                selected->product_id,
                selected->usage_page,
                selected->usage,
                selected->interface_number,
                selected->product.c_str());

    hid_device* device = hid_open_path(selected->path.c_str());
    if (!device) {
        std::fprintf(stderr,
                     "hid_open_path failed: %s\n",
                     HidErrorUtf8(nullptr).c_str());
        std::fprintf(stderr,
                     "If this is the Windows mouse collection, exclusive OS ownership may block user-mode reads.\n");
        hid_exit();
        return 1;
    }

    if (hid_set_nonblocking(device, 0) != 0) {
        std::fprintf(stderr, "hid_set_nonblocking failed: %s\n", HidErrorUtf8(device).c_str());
        hid_close(device);
        hid_exit();
        return 1;
    }

    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

    std::printf("Reading interrupt input reports. Move the target device continuously.\n\n");

    using clock = std::chrono::steady_clock;
    const auto start = clock::now();
    const auto end = start + std::chrono::seconds(seconds);
    auto next_print = start + std::chrono::milliseconds(250);
    HidRateStats stats;
    HidRateStats last_print;
    std::array<unsigned char, 1024> buffer{};
    std::string last_error;

    while (clock::now() < end) {
        const int read = hid_read_timeout(device, buffer.data(), buffer.size(), 1);
        if (read > 0) {
            RecordHidReport(&stats, buffer.data(), read);
        } else if (read == 0) {
            ++stats.timeouts;
        } else {
            ++stats.errors;
            last_error = HidReadErrorUtf8(device);
            break;
        }

        const auto now = clock::now();
        if (now >= next_print) {
            PrintHidRateSample(last_print,
                               stats,
                               std::chrono::duration<double>(now - start).count());
            std::fflush(stdout);
            last_print = stats;
            next_print += std::chrono::milliseconds(250);
        }
    }

    const double elapsed = std::chrono::duration<double>(clock::now() - start).count();
    std::printf("\nsummary: reports=%llu elapsed=%.3fs avg_rate=%.1f Hz timeouts=%llu err=%llu len=%d..%d bytes=%llu\n",
                static_cast<unsigned long long>(stats.reports),
                elapsed,
                elapsed > 0.0 ? static_cast<double>(stats.reports) / elapsed : 0.0,
                static_cast<unsigned long long>(stats.timeouts),
                static_cast<unsigned long long>(stats.errors),
                stats.min_len,
                stats.max_len,
                static_cast<unsigned long long>(stats.bytes));
    if (!last_error.empty()) {
        std::printf("last read error: %s\n", last_error.c_str());
    }

    hid_close(device);
    hid_exit();
    return stats.errors == 0 ? 0 : 1;
}

enum class MouseRateMode {
    Sink,
    Foreground,
    Overlay,
    OverlayHold,
};

const char* MouseRateModeName(MouseRateMode mode) {
    switch (mode) {
    case MouseRateMode::Sink:
        return "sink";
    case MouseRateMode::Foreground:
        return "foreground";
    case MouseRateMode::Overlay:
        return "overlay";
    case MouseRateMode::OverlayHold:
        return "overlay-hold";
    default:
        return "unknown";
    }
}

bool ParseMouseRateMode(const char* text, MouseRateMode* mode) {
    if (std::strcmp(text, "sink") == 0) {
        *mode = MouseRateMode::Sink;
        return true;
    }
    if (std::strcmp(text, "foreground") == 0) {
        *mode = MouseRateMode::Foreground;
        return true;
    }
    if (std::strcmp(text, "overlay") == 0) {
        *mode = MouseRateMode::Overlay;
        return true;
    }
    if (std::strcmp(text, "overlay-hold") == 0) {
        *mode = MouseRateMode::OverlayHold;
        return true;
    }
    return false;
}

bool IsOverlayMode(MouseRateMode mode) {
    return mode == MouseRateMode::Overlay || mode == MouseRateMode::OverlayHold;
}

bool IsFullScreenOverlayMode(MouseRateMode mode) {
    return mode == MouseRateMode::Overlay;
}

struct MouseRateStats {
    uint64_t wm_input_count = 0;
    uint64_t mouse_event_count = 0;
    uint64_t non_mouse_count = 0;
    uint64_t absolute_count = 0;
    uint64_t data_error_count = 0;
    DWORD last_error = 0;
    LONG last_x = 0;
    LONG last_y = 0;
    USHORT last_flags = 0;
    int64_t dx_sum = 0;
    int64_t dy_sum = 0;
};

MouseRateStats g_mouse_stats;
std::atomic_bool g_mouse_rate_stop{false};
std::atomic_bool g_cursor_lock_allowed{true};
std::atomic<int> g_stop_virtual_key{VK_ESCAPE};
std::atomic<int> g_freeze_virtual_key{VK_F2};

constexpr int kCursorLockHotkeyId = 1;
constexpr int kEmergencyStopHotkeyId = 2;
constexpr int kEmergencyUnlockHotkeyId = 3;
constexpr int kEmergencyQuitChordHotkeyId = 4;

struct CursorLockState {
    bool locked = false;
    POINT saved_pos{};
};

CursorLockState g_cursor_lock;

void ReleaseCursorClip(bool restore_position) {
    const bool was_locked = g_cursor_lock.locked;
    ClipCursor(nullptr);
    if (restore_position && was_locked) {
        SetCursorPos(g_cursor_lock.saved_pos.x, g_cursor_lock.saved_pos.y);
    }
    g_cursor_lock.locked = false;
    if (was_locked) {
        std::printf("cursor lock: off\n");
        std::fflush(stdout);
    }
}

void UnlockCursor() {
    ReleaseCursorClip(true);
}

BOOL WINAPI ConsoleCtrlHandler(DWORD ctrl_type) {
    switch (ctrl_type) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        g_mouse_rate_stop.store(true, std::memory_order_release);
        ReleaseCursorClip(false);
        return TRUE;
    default:
        return FALSE;
    }
}

bool LauncherStopRequested(HANDLE stop_event) {
    return stop_event != nullptr && WaitForSingleObject(stop_event, 0) == WAIT_OBJECT_0;
}

void ToggleCursorLock() {
    if (!g_cursor_lock_allowed.load(std::memory_order_acquire)) {
        ReleaseCursorClip(false);
        std::printf("cursor lock: disabled in this mode\n");
        std::fflush(stdout);
        return;
    }

    if (g_cursor_lock.locked) {
        UnlockCursor();
        return;
    }

    POINT pos{};
    if (!GetCursorPos(&pos)) {
        std::fprintf(stderr,
                     "cursor lock: GetCursorPos failed (le=%lu)\n",
                     static_cast<unsigned long>(GetLastError()));
        return;
    }

    RECT clip{};
    clip.left = pos.x;
    clip.top = pos.y;
    clip.right = pos.x + 1;
    clip.bottom = pos.y + 1;
    if (!ClipCursor(&clip)) {
        std::fprintf(stderr,
                     "cursor lock: ClipCursor failed (le=%lu)\n",
                     static_cast<unsigned long>(GetLastError()));
        return;
    }

    g_cursor_lock.saved_pos = pos;
    g_cursor_lock.locked = true;
    std::printf("cursor lock: on (freeze key toggles, stop key exits)\n");
    std::fflush(stdout);
}

bool StopKeyDown() {
    const int vk = g_stop_virtual_key.load(std::memory_order_acquire);
    return vk > 0 && (GetAsyncKeyState(vk) & 0x8000) != 0;
}

bool FreezeKeyDown() {
    const int vk = g_freeze_virtual_key.load(std::memory_order_acquire);
    return vk > 0 && (GetAsyncKeyState(vk) & 0x8000) != 0;
}

void HideAndDemoteCaptureWindow(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) {
        return;
    }

    ReleaseCapture();
    ShowWindow(hwnd, SW_HIDE);
    SetWindowPos(hwnd,
                 HWND_NOTOPMOST,
                 0,
                 0,
                 0,
                 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_HIDEWINDOW);
}

void EmergencyStop(HWND hwnd) {
    g_mouse_rate_stop.store(true, std::memory_order_release);
    ReleaseCursorClip(false);
    if (hwnd && IsWindow(hwnd)) {
        HideAndDemoteCaptureWindow(hwnd);
        DestroyWindow(hwnd);
    }
}

LRESULT CALLBACK MouseRateWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_INPUT) {
        ++g_mouse_stats.wm_input_count;

        RAWINPUT raw{};
        UINT size = sizeof(raw);
        const UINT read = GetRawInputData(reinterpret_cast<HRAWINPUT>(lparam),
                                          RID_INPUT,
                                          &raw,
                                          &size,
                                          sizeof(RAWINPUTHEADER));
        if (read == static_cast<UINT>(-1) || read == 0) {
            ++g_mouse_stats.data_error_count;
            g_mouse_stats.last_error = GetLastError();
        } else if (raw.header.dwType != RIM_TYPEMOUSE) {
            ++g_mouse_stats.non_mouse_count;
        } else {
            const RAWMOUSE& mouse = raw.data.mouse;
            g_mouse_stats.last_x = mouse.lLastX;
            g_mouse_stats.last_y = mouse.lLastY;
            g_mouse_stats.last_flags = mouse.usFlags;

            if ((mouse.usFlags & MOUSE_MOVE_ABSOLUTE) != 0) {
                ++g_mouse_stats.absolute_count;
            } else {
                ++g_mouse_stats.mouse_event_count;
                g_mouse_stats.dx_sum += mouse.lLastX;
                g_mouse_stats.dy_sum += mouse.lLastY;
            }
        }
    }

    if (msg == WM_HOTKEY) {
        if (wparam == kCursorLockHotkeyId) {
            ToggleCursorLock();
            return 0;
        }
        if (wparam == kEmergencyStopHotkeyId) {
            EmergencyStop(hwnd);
            return 0;
        }
        if (wparam == kEmergencyQuitChordHotkeyId) {
            EmergencyStop(hwnd);
            return 0;
        }
        if (wparam == kEmergencyUnlockHotkeyId) {
            ReleaseCursorClip(false);
            return 0;
        }
        return 0;
    }

    if (msg == WM_KEYDOWN &&
        static_cast<int>(wparam) == g_freeze_virtual_key.load(std::memory_order_acquire)) {
        const bool was_down = (lparam & (1L << 30)) != 0;
        if (!was_down) {
            ToggleCursorLock();
        }
        return 0;
    }

    if (msg == WM_KEYDOWN &&
        static_cast<int>(wparam) == g_stop_virtual_key.load(std::memory_order_acquire)) {
        EmergencyStop(hwnd);
        return 0;
    }

    if (msg == WM_CLOSE) {
        EmergencyStop(hwnd);
        return 0;
    }

    if (msg == WM_DESTROY) {
        UnlockCursor();
        return 0;
    }

    return DefWindowProc(hwnd, msg, wparam, lparam);
}

HWND CreateMouseRateWindow(HINSTANCE instance, MouseRateMode mode) {
    constexpr const wchar_t* kClassName = L"GX12MouseRateWindow";
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = MouseRateWndProc;
    wc.hInstance = instance;
    wc.lpszClassName = kClassName;
    wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));

    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        std::fprintf(stderr,
                     "RegisterClassExW failed (le=%lu).\n",
                     static_cast<unsigned long>(GetLastError()));
        return nullptr;
    }

    DWORD ex_style = WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
    DWORD style = WS_POPUP;
    int x = -32000;
    int y = -32000;
    int width = 1;
    int height = 1;

    if (mode == MouseRateMode::Foreground) {
        ex_style = WS_EX_APPWINDOW;
        style = WS_OVERLAPPEDWINDOW;
        x = CW_USEDEFAULT;
        y = CW_USEDEFAULT;
        width = 520;
        height = 180;
    } else if (IsFullScreenOverlayMode(mode)) {
        ex_style = WS_EX_APPWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED;
        style = WS_POPUP;
        x = GetSystemMetrics(SM_XVIRTUALSCREEN);
        y = GetSystemMetrics(SM_YVIRTUALSCREEN);
        width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    } else if (mode == MouseRateMode::OverlayHold) {
        ex_style = WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED;
        style = WS_POPUP;
        x = GetSystemMetrics(SM_XVIRTUALSCREEN);
        y = GetSystemMetrics(SM_YVIRTUALSCREEN);
        width = 2;
        height = 2;
    }

    HWND hwnd = CreateWindowExW(ex_style,
                                kClassName,
                                L"GX12 Mouse Rate Probe",
                                style,
                                x,
                                y,
                                width,
                                height,
                                nullptr,
                                nullptr,
                                instance,
                                nullptr);
    if (!hwnd) {
        std::fprintf(stderr,
                     "CreateWindowExW failed (le=%lu).\n",
                     static_cast<unsigned long>(GetLastError()));
    }

    if (hwnd && mode == MouseRateMode::Foreground) {
        ShowWindow(hwnd, SW_SHOWNORMAL);
        SetForegroundWindow(hwnd);
    } else if (hwnd && IsOverlayMode(mode)) {
        SetLayeredWindowAttributes(hwnd, 0, 1, LWA_ALPHA);
        ShowWindow(hwnd, SW_SHOW);
        SetWindowPos(hwnd,
                     HWND_TOPMOST,
                     x,
                     y,
                     width,
                     height,
                     SWP_SHOWWINDOW);
        SetForegroundWindow(hwnd);
    }

    return hwnd;
}

void DestroyMouseRateWindow(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) {
        return;
    }

    HideAndDemoteCaptureWindow(hwnd);
    DestroyWindow(hwnd);
}

bool RegisterMouseRawInput(HWND hwnd, MouseRateMode mode) {
    RAWINPUTDEVICE rid{};
    rid.usUsagePage = 0x01; // Generic Desktop
    rid.usUsage = 0x02;     // Mouse
    rid.dwFlags = mode == MouseRateMode::Sink ? RIDEV_INPUTSINK : 0;
    rid.hwndTarget = hwnd;

    if (!RegisterRawInputDevices(&rid, 1, sizeof(rid))) {
        std::fprintf(stderr,
                     "RegisterRawInputDevices failed (le=%lu).\n",
                     static_cast<unsigned long>(GetLastError()));
        return false;
    }

    return true;
}

void PrintMouseRateSample(const MouseRateStats& previous,
                          const MouseRateStats& current,
                          double elapsed_seconds) {
    const uint64_t events = current.mouse_event_count - previous.mouse_event_count;
    const uint64_t wm = current.wm_input_count - previous.wm_input_count;
    const uint64_t errors = current.data_error_count - previous.data_error_count;
    const uint64_t absolute = current.absolute_count - previous.absolute_count;
    const int64_t dx = current.dx_sum - previous.dx_sum;
    const int64_t dy = current.dy_sum - previous.dy_sum;
    constexpr double kWindowSeconds = 0.250;
    const double hz = static_cast<double>(events) / kWindowSeconds;

    std::printf("[%.3fs] events=%5llu rate=%7.1f Hz wm=%5llu dx=%+8lld dy=%+8lld abs=%llu err=%llu(le=%lu) last(dx=%+5ld dy=%+5ld fl=0x%04x)\n",
                elapsed_seconds,
                static_cast<unsigned long long>(events),
                hz,
                static_cast<unsigned long long>(wm),
                static_cast<long long>(dx),
                static_cast<long long>(dy),
                static_cast<unsigned long long>(absolute),
                static_cast<unsigned long long>(errors),
                static_cast<unsigned long>(current.last_error),
                static_cast<long>(current.last_x),
                static_cast<long>(current.last_y),
                static_cast<unsigned>(current.last_flags));
}

int RunMouseRate(int seconds, MouseRateMode mode) {
    if (seconds <= 0 || seconds > 30) {
        std::fprintf(stderr, "--mouse-rate duration must be 1..30 seconds.\n");
        return 2;
    }

    HINSTANCE instance = GetModuleHandleW(nullptr);
    g_mouse_stats = MouseRateStats{};
    g_mouse_rate_stop.store(false, std::memory_order_release);

    HWND hwnd = CreateMouseRateWindow(instance, mode);
    if (!hwnd) {
        std::fprintf(stderr,
                     "No HWND available for mouse-rate mode '%s'.\n",
                     MouseRateModeName(mode));
        return 1;
    }

    if (!RegisterMouseRawInput(hwnd, mode)) {
        DestroyWindow(hwnd);
        return 1;
    }

    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    std::printf("\n--mouse-rate: measuring Raw Input for %d second(s), mode=%s.\n",
                seconds,
                MouseRateModeName(mode));
    if (mode == MouseRateMode::Foreground) {
        std::printf("Focus the 'GX12 Mouse Rate Probe' window while moving the mouse. F2 toggles cursor lock.\n");
    } else if (mode == MouseRateMode::Overlay) {
        std::printf("A nearly transparent topmost capture window is foreground. Press Esc to close early.\n");
    } else if (mode == MouseRateMode::OverlayHold) {
        std::printf("A nearly transparent topmost capture window will try to reclaim foreground. Press Esc to close early.\n");
    }
    std::printf("Esc stops the run. F2 locks/unlocks the cursor at its current position.\n\n");

    using clock = std::chrono::steady_clock;
    const auto start = clock::now();
    const auto end = start + std::chrono::seconds(seconds);
    auto next_print = start + std::chrono::milliseconds(250);
    MouseRateStats last_print = g_mouse_stats;

    while (!g_mouse_rate_stop.load(std::memory_order_acquire) && clock::now() < end) {
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        if (mode == MouseRateMode::OverlayHold && GetForegroundWindow() != hwnd) {
            SetWindowPos(hwnd,
                         HWND_TOPMOST,
                         0,
                         0,
                         0,
                         0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
            SetForegroundWindow(hwnd);
        }

        const auto now = clock::now();
        if (now >= next_print) {
            const MouseRateStats current = g_mouse_stats;
            PrintMouseRateSample(last_print,
                                 current,
                                 std::chrono::duration<double>(now - start).count());
            std::fflush(stdout);
            last_print = current;
            next_print += std::chrono::milliseconds(250);
        }

        MsgWaitForMultipleObjectsEx(0,
                                    nullptr,
                                    10,
                                    QS_ALLINPUT,
                                    MWMO_INPUTAVAILABLE);
    }

    const MouseRateStats final_stats = g_mouse_stats;
    const double elapsed = std::chrono::duration<double>(clock::now() - start).count();
    std::printf("\nsummary: events=%llu elapsed=%.3fs avg_rate=%.1f Hz wm=%llu dx=%+lld dy=%+lld abs=%llu err=%llu last_error=%lu\n",
                static_cast<unsigned long long>(final_stats.mouse_event_count),
                elapsed,
                elapsed > 0.0 ? static_cast<double>(final_stats.mouse_event_count) / elapsed : 0.0,
                static_cast<unsigned long long>(final_stats.wm_input_count),
                static_cast<long long>(final_stats.dx_sum),
                static_cast<long long>(final_stats.dy_sum),
                static_cast<unsigned long long>(final_stats.absolute_count),
                static_cast<unsigned long long>(final_stats.data_error_count),
                static_cast<unsigned long>(final_stats.last_error));

    if (IsWindow(hwnd)) {
        DestroyWindow(hwnd);
    }
    return 0;
}

bool MouseDeviceTokenBindingIsAuto(const std::string& binding) {
    return binding.empty() || binding == "auto" || binding == "*";
}

#if defined(GX12_HAS_GAMEINPUT)
namespace gi = GameInput::v3;

std::string BytesToHexToken(const void* data, size_t size) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    std::string out;
    out.reserve(size * 2);
    constexpr char kHex[] = "0123456789abcdef";
    for (size_t i = 0; i < size; ++i) {
        out.push_back(kHex[(bytes[i] >> 4) & 0x0F]);
        out.push_back(kHex[bytes[i] & 0x0F]);
    }
    return out;
}

std::string GameInputDeviceToken(gi::IGameInputDevice* device) {
    if (!device) {
        return {};
    }
    const gi::GameInputDeviceInfo* info = nullptr;
    if (FAILED(device->GetDeviceInfo(&info)) || !info) {
        return {};
    }
    return BytesToHexToken(&info->deviceRootId, sizeof(info->deviceRootId));
}

std::string GameInputReadingDeviceToken(gi::IGameInputReading* reading) {
    if (!reading) {
        return {};
    }
    gi::IGameInputDevice* device = nullptr;
    reading->GetDevice(&device);
    std::string token = GameInputDeviceToken(device);
    if (device) {
        device->Release();
    }
    return token;
}

bool GameInputTokenMatches(const std::string& binding, const std::string& token) {
    if (binding.empty() || binding == "auto" || binding == "*") {
        return true;
    }
    return !token.empty() && ToLowerAscii(binding) == ToLowerAscii(token);
}

struct GameInputMouseRoleCounters {
    std::atomic<uint64_t> callbacks{0};
    std::atomic<uint64_t> mouse_states{0};
    std::atomic<uint64_t> errors{0};
    std::atomic<int64_t> dx_sum{0};
    std::atomic<int64_t> dy_sum{0};
    std::atomic<int64_t> wheel_x_sum{0};
    std::atomic<int64_t> wheel_y_sum{0};
    std::atomic<uint32_t> buttons{0};
};

struct GameInputMouseRoleSnapshot {
    uint64_t callbacks = 0;
    uint64_t mouse_states = 0;
    uint64_t errors = 0;
    int64_t dx_sum = 0;
    int64_t dy_sum = 0;
    int64_t wheel_x_sum = 0;
    int64_t wheel_y_sum = 0;
    uint32_t buttons = 0;
};

GameInputMouseRoleSnapshot SnapshotGameInputRole(const GameInputMouseRoleCounters& stats) {
    GameInputMouseRoleSnapshot out;
    out.callbacks = stats.callbacks.load(std::memory_order_acquire);
    out.mouse_states = stats.mouse_states.load(std::memory_order_acquire);
    out.errors = stats.errors.load(std::memory_order_acquire);
    out.dx_sum = stats.dx_sum.load(std::memory_order_acquire);
    out.dy_sum = stats.dy_sum.load(std::memory_order_acquire);
    out.wheel_x_sum = stats.wheel_x_sum.load(std::memory_order_acquire);
    out.wheel_y_sum = stats.wheel_y_sum.load(std::memory_order_acquire);
    out.buttons = stats.buttons.load(std::memory_order_acquire);
    return out;
}

double NormalizeGameInputWheelDelta(int64_t wheel_delta) {
    constexpr double kWheelDetent = 120.0;
    if (std::abs(wheel_delta) >= static_cast<int64_t>(kWheelDetent)) {
        return static_cast<double>(wheel_delta) / kWheelDetent;
    }
    return static_cast<double>(wheel_delta);
}

int GameInputMouse4Mouse5Axis(uint32_t buttons) {
    constexpr uint32_t kMouse4 = static_cast<uint32_t>(gi::GameInputMouseButton4);
    constexpr uint32_t kMouse5 = static_cast<uint32_t>(gi::GameInputMouseButton5);
    const int mouse4 = (buttons & kMouse4) != 0 ? 1 : 0;
    const int mouse5 = (buttons & kMouse5) != 0 ? 1 : 0;
    return mouse5 - mouse4;
}

void AddGameInputRoleDelta(GameInputMouseRoleCounters* role,
                           int64_t dx,
                           int64_t dy,
                           int64_t wheel_x,
                           int64_t wheel_y,
                           uint32_t buttons) {
    if (!role) {
        return;
    }
    role->callbacks.fetch_add(1, std::memory_order_relaxed);
    role->mouse_states.fetch_add(1, std::memory_order_relaxed);
    role->dx_sum.fetch_add(dx, std::memory_order_relaxed);
    role->dy_sum.fetch_add(dy, std::memory_order_relaxed);
    role->wheel_x_sum.fetch_add(wheel_x, std::memory_order_relaxed);
    role->wheel_y_sum.fetch_add(wheel_y, std::memory_order_relaxed);
    role->buttons.store(buttons, std::memory_order_release);
}

struct GameInputMouseRateStats {
    std::atomic<uint64_t> callbacks{0};
    std::atomic<uint64_t> mouse_states{0};
    std::atomic<uint64_t> errors{0};
    std::atomic<int64_t> dx_sum{0};
    std::atomic<int64_t> dy_sum{0};
    std::atomic<int64_t> last_x{0};
    std::atomic<int64_t> last_y{0};
    std::atomic<uint32_t> buttons{0};
    std::atomic<bool> have_previous{false};
    std::atomic<uint64_t> keyboard_callbacks{0};
    std::atomic<uint64_t> keyboard_states{0};
    std::array<std::atomic<uint8_t>, 256> key_down{};
    std::string right_device_token = "auto";
    std::string left_device_token;
    std::string auto_left_device_token;
    GameInputMouseRoleCounters right_mouse;
    GameInputMouseRoleCounters left_mouse;
    struct DevicePrevious {
        std::string token;
        int64_t x = 0;
        int64_t y = 0;
        int64_t wheel_x = 0;
        int64_t wheel_y = 0;
        bool have = false;
    };
    std::mutex previous_mutex;
    std::vector<DevicePrevious> previous_by_device;
};

bool GameInputMouseLeftRoleMatches(GameInputMouseRateStats* stats,
                                   const std::string& token,
                                   bool right_match) {
    if (!stats || stats->left_device_token.empty() || token.empty()) {
        return false;
    }

    const bool left_auto = MouseDeviceTokenBindingIsAuto(stats->left_device_token);
    if (!left_auto) {
        return GameInputTokenMatches(stats->left_device_token, token);
    }

    const bool right_auto = MouseDeviceTokenBindingIsAuto(stats->right_device_token);
    std::lock_guard<std::mutex> lock(stats->previous_mutex);
    if (stats->auto_left_device_token.empty()) {
        if (!right_auto && !right_match) {
            stats->auto_left_device_token = token;
        } else if (right_auto) {
            for (const auto& item : stats->previous_by_device) {
                if (item.token != token && !item.token.empty()) {
                    stats->auto_left_device_token = token;
                    break;
                }
            }
        }
    }
    return !stats->auto_left_device_token.empty() &&
           ToLowerAscii(stats->auto_left_device_token) == ToLowerAscii(token);
}

struct GameInputMouseRateSnapshot {
    uint64_t callbacks = 0;
    uint64_t mouse_states = 0;
    uint64_t errors = 0;
    int64_t dx_sum = 0;
    int64_t dy_sum = 0;
    uint32_t buttons = 0;
    uint64_t keyboard_callbacks = 0;
    uint64_t keyboard_states = 0;
};

GameInputMouseRateSnapshot SnapshotGameInputStats(const GameInputMouseRateStats& stats) {
    GameInputMouseRateSnapshot out;
    out.callbacks = stats.callbacks.load(std::memory_order_acquire);
    out.mouse_states = stats.mouse_states.load(std::memory_order_acquire);
    out.errors = stats.errors.load(std::memory_order_acquire);
    out.dx_sum = stats.dx_sum.load(std::memory_order_acquire);
    out.dy_sum = stats.dy_sum.load(std::memory_order_acquire);
    out.buttons = stats.buttons.load(std::memory_order_acquire);
    out.keyboard_callbacks = stats.keyboard_callbacks.load(std::memory_order_acquire);
    out.keyboard_states = stats.keyboard_states.load(std::memory_order_acquire);
    return out;
}

void CALLBACK GameInputMouseReadingCallback(gi::GameInputCallbackToken,
                                            void* context,
                                            gi::IGameInputReading* reading) {
    auto* stats = static_cast<GameInputMouseRateStats*>(context);
    if (!stats || !reading) {
        return;
    }

    stats->callbacks.fetch_add(1, std::memory_order_relaxed);

    gi::GameInputMouseState mouse{};
    if (!reading->GetMouseState(&mouse)) {
        stats->errors.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    stats->mouse_states.fetch_add(1, std::memory_order_relaxed);
    stats->buttons.store(static_cast<uint32_t>(mouse.buttons), std::memory_order_release);
    const std::string token = GameInputReadingDeviceToken(reading);
    int64_t dx = 0;
    int64_t dy = 0;
    int64_t wheel_x = 0;
    int64_t wheel_y = 0;
    bool have_delta = false;
    {
        std::lock_guard<std::mutex> lock(stats->previous_mutex);
        auto it = std::find_if(stats->previous_by_device.begin(),
                               stats->previous_by_device.end(),
                               [&token](const GameInputMouseRateStats::DevicePrevious& item) {
                                   return item.token == token;
                               });
        if (it == stats->previous_by_device.end()) {
            GameInputMouseRateStats::DevicePrevious item;
            item.token = token;
            item.x = mouse.positionX;
            item.y = mouse.positionY;
            item.wheel_x = mouse.wheelX;
            item.wheel_y = mouse.wheelY;
            item.have = true;
            stats->previous_by_device.push_back(item);
        } else {
            if (it->have) {
                dx = mouse.positionX - it->x;
                dy = mouse.positionY - it->y;
                wheel_x = mouse.wheelX - it->wheel_x;
                wheel_y = mouse.wheelY - it->wheel_y;
                have_delta = true;
            }
            it->x = mouse.positionX;
            it->y = mouse.positionY;
            it->wheel_x = mouse.wheelX;
            it->wheel_y = mouse.wheelY;
            it->have = true;
        }
    }
    stats->last_x.store(mouse.positionX, std::memory_order_release);
    stats->last_y.store(mouse.positionY, std::memory_order_release);
    const bool had_previous = stats->have_previous.exchange(true, std::memory_order_acq_rel);
    bool right_match = GameInputTokenMatches(stats->right_device_token, token);
    const bool left_match = GameInputMouseLeftRoleMatches(stats, token, right_match);
    if (right_match &&
        MouseDeviceTokenBindingIsAuto(stats->right_device_token) &&
        !stats->left_device_token.empty() &&
        left_match) {
        right_match = false;
    }
    if (had_previous && have_delta) {
        stats->dx_sum.fetch_add(dx, std::memory_order_relaxed);
        stats->dy_sum.fetch_add(dy, std::memory_order_relaxed);
    }
    if (right_match) {
        AddGameInputRoleDelta(&stats->right_mouse,
                              had_previous && have_delta ? dx : 0,
                              had_previous && have_delta ? dy : 0,
                              had_previous && have_delta ? wheel_x : 0,
                              had_previous && have_delta ? wheel_y : 0,
                              static_cast<uint32_t>(mouse.buttons));
    }
    if (left_match) {
        AddGameInputRoleDelta(&stats->left_mouse,
                              had_previous && have_delta ? dx : 0,
                              had_previous && have_delta ? dy : 0,
                              had_previous && have_delta ? wheel_x : 0,
                              had_previous && have_delta ? wheel_y : 0,
                              static_cast<uint32_t>(mouse.buttons));
    }
}

void CALLBACK GameInputKeyboardReadingCallback(gi::GameInputCallbackToken,
                                               void* context,
                                               gi::IGameInputReading* reading) {
    auto* stats = static_cast<GameInputMouseRateStats*>(context);
    if (!stats || !reading) {
        return;
    }

    stats->keyboard_callbacks.fetch_add(1, std::memory_order_relaxed);

    const uint32_t key_count = reading->GetKeyCount();
    std::array<gi::GameInputKeyState, 64> keys{};
    const uint32_t read_count = std::min<uint32_t>(key_count, static_cast<uint32_t>(keys.size()));
    if (read_count > 0) {
        reading->GetKeyState(read_count, keys.data());
    }

    for (auto& key : stats->key_down) {
        key.store(0, std::memory_order_relaxed);
    }
    for (uint32_t i = 0; i < read_count; ++i) {
        stats->key_down[keys[i].virtualKey].store(1, std::memory_order_release);
    }
    stats->keyboard_states.fetch_add(1, std::memory_order_release);
}

bool GameInputKeyDown(const GameInputMouseRateStats& stats, int virtual_key) {
    if (virtual_key <= 0 || virtual_key >= static_cast<int>(stats.key_down.size())) {
        return false;
    }
    return stats.key_down[static_cast<size_t>(virtual_key)].load(std::memory_order_acquire) != 0;
}

void PrintGameInputMouseRateSample(const GameInputMouseRateSnapshot& previous,
                                   const GameInputMouseRateSnapshot& current,
                                   double elapsed_seconds) {
    constexpr double kWindowSeconds = 0.250;
    const uint64_t callbacks = current.callbacks - previous.callbacks;
    const uint64_t mouse_states = current.mouse_states - previous.mouse_states;
    const uint64_t errors = current.errors - previous.errors;
    const int64_t dx = current.dx_sum - previous.dx_sum;
    const int64_t dy = current.dy_sum - previous.dy_sum;

    std::printf("[%.3fs] callbacks=%5llu rate=%7.1f Hz states=%5llu dx=%+8lld dy=%+8lld err=%llu\n",
                elapsed_seconds,
                static_cast<unsigned long long>(callbacks),
                static_cast<double>(callbacks) / kWindowSeconds,
                static_cast<unsigned long long>(mouse_states),
                static_cast<long long>(dx),
                static_cast<long long>(dy),
                static_cast<unsigned long long>(errors));
}

int RunMouseRateGameInput(int seconds) {
    if (seconds <= 0 || seconds > 30) {
        std::fprintf(stderr, "--mouse-rate-gameinput duration must be 1..30 seconds.\n");
        return 2;
    }

    gi::IGameInput* game_input = nullptr;
    HRESULT hr = gi::GameInputCreate(&game_input);
    if (FAILED(hr) || !game_input) {
        std::fprintf(stderr,
                     "GameInputCreate failed: hr=0x%08lx. The GameInput runtime may be missing.\n",
                     static_cast<unsigned long>(hr));
        return 1;
    }

    game_input->SetFocusPolicy(gi::GameInputEnableBackgroundInput);

    GameInputMouseRateStats stats;
    gi::GameInputCallbackToken token = 0;
    hr = game_input->RegisterReadingCallback(nullptr,
                                             gi::GameInputKindMouse,
                                             &stats,
                                             GameInputMouseReadingCallback,
                                             &token);
    if (FAILED(hr) || token == 0) {
        std::fprintf(stderr,
                     "RegisterReadingCallback(GameInputKindMouse) failed: hr=0x%08lx token=%llu.\n",
                     static_cast<unsigned long>(hr),
                     static_cast<unsigned long long>(token));
        game_input->Release();
        return 1;
    }

    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    std::printf("\n--mouse-rate-gameinput: measuring GameInput mouse callbacks for %d second(s).\n",
                seconds);
    std::printf("Keep VelociDrone foreground, wiggle the mouse continuously, press Esc to stop early.\n\n");

    using clock = std::chrono::steady_clock;
    const auto start = clock::now();
    const auto end = start + std::chrono::seconds(seconds);
    auto next_print = start + std::chrono::milliseconds(250);
    GameInputMouseRateSnapshot last_print = SnapshotGameInputStats(stats);

    while (clock::now() < end) {
        if ((GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0) {
            break;
        }

        const auto now = clock::now();
        if (now >= next_print) {
            const GameInputMouseRateSnapshot current = SnapshotGameInputStats(stats);
            PrintGameInputMouseRateSample(last_print,
                                          current,
                                          std::chrono::duration<double>(now - start).count());
            std::fflush(stdout);
            last_print = current;
            next_print += std::chrono::milliseconds(250);
        }

        Sleep(1);
    }

    game_input->UnregisterCallback(token);
    const GameInputMouseRateSnapshot final_stats = SnapshotGameInputStats(stats);
    const double elapsed = std::chrono::duration<double>(clock::now() - start).count();
    std::printf("\nsummary: callbacks=%llu elapsed=%.3fs avg_rate=%.1f Hz states=%llu dx=%+lld dy=%+lld err=%llu\n",
                static_cast<unsigned long long>(final_stats.callbacks),
                elapsed,
                elapsed > 0.0 ? static_cast<double>(final_stats.callbacks) / elapsed : 0.0,
                static_cast<unsigned long long>(final_stats.mouse_states),
                static_cast<long long>(final_stats.dx_sum),
                static_cast<long long>(final_stats.dy_sum),
                static_cast<unsigned long long>(final_stats.errors));

    game_input->Release();
    return 0;
}

struct GameInputMouseDeviceSample {
    std::string token;
    std::string device_token;
    std::string display_name;
    std::string pnp_path;
    uint16_t vendor_id = 0;
    uint16_t product_id = 0;
    uint64_t callbacks = 0;
    uint64_t states = 0;
    uint64_t errors = 0;
    int64_t last_x = 0;
    int64_t last_y = 0;
    bool have_previous = false;
    int64_t dx_sum = 0;
    int64_t dy_sum = 0;
    uint32_t buttons = 0;
};

struct GameInputMouseDeviceDiagnostics {
    std::mutex mutex;
    std::vector<GameInputMouseDeviceSample> devices;
};

void UpsertGameInputMouseDevice(GameInputMouseDeviceDiagnostics* diagnostics,
                                gi::IGameInputDevice* device) {
    if (!diagnostics || !device) {
        return;
    }
    const gi::GameInputDeviceInfo* info = nullptr;
    if (FAILED(device->GetDeviceInfo(&info)) || !info) {
        return;
    }
    const std::string root_token = BytesToHexToken(&info->deviceRootId, sizeof(info->deviceRootId));
    const std::string device_token = BytesToHexToken(&info->deviceId, sizeof(info->deviceId));
    std::lock_guard<std::mutex> lock(diagnostics->mutex);
    for (auto& item : diagnostics->devices) {
        if (item.token == root_token) {
            return;
        }
    }
    GameInputMouseDeviceSample item;
    item.token = root_token;
    item.device_token = device_token;
    item.display_name = info->displayName ? info->displayName : "";
    item.pnp_path = info->pnpPath ? info->pnpPath : "";
    item.vendor_id = info->vendorId;
    item.product_id = info->productId;
    diagnostics->devices.push_back(item);
}

void CALLBACK GameInputMouseDeviceCallback(gi::GameInputCallbackToken,
                                           void* context,
                                           gi::IGameInputDevice* device,
                                           uint64_t,
                                           gi::GameInputDeviceStatus current_status,
                                           gi::GameInputDeviceStatus) {
    if ((current_status & gi::GameInputDeviceConnected) != 0) {
        UpsertGameInputMouseDevice(static_cast<GameInputMouseDeviceDiagnostics*>(context), device);
    }
}

void CALLBACK GameInputMouseDeviceReadingCallback(gi::GameInputCallbackToken,
                                                  void* context,
                                                  gi::IGameInputReading* reading) {
    auto* diagnostics = static_cast<GameInputMouseDeviceDiagnostics*>(context);
    if (!diagnostics || !reading) {
        return;
    }
    gi::IGameInputDevice* device = nullptr;
    reading->GetDevice(&device);
    UpsertGameInputMouseDevice(diagnostics, device);
    const std::string token = GameInputDeviceToken(device);
    if (device) {
        device->Release();
    }

    gi::GameInputMouseState mouse{};
    const bool ok = reading->GetMouseState(&mouse);
    std::lock_guard<std::mutex> lock(diagnostics->mutex);
    for (auto& item : diagnostics->devices) {
        if (item.token != token) {
            continue;
        }
        ++item.callbacks;
        if (!ok) {
            ++item.errors;
            return;
        }
        ++item.states;
        item.buttons = static_cast<uint32_t>(mouse.buttons);
        if (item.have_previous) {
            item.dx_sum += mouse.positionX - item.last_x;
            item.dy_sum += mouse.positionY - item.last_y;
        }
        item.last_x = mouse.positionX;
        item.last_y = mouse.positionY;
        item.have_previous = true;
        return;
    }
}

int RunMouseDevicesGameInput(int seconds) {
    if (seconds <= 0 || seconds > 30) {
        std::fprintf(stderr, "--mouse-devices-gameinput duration must be 1..30 seconds.\n");
        return 2;
    }

    gi::IGameInput* game_input = nullptr;
    HRESULT hr = gi::GameInputCreate(&game_input);
    if (FAILED(hr) || !game_input) {
        std::fprintf(stderr,
                     "GameInputCreate failed: hr=0x%08lx. The GameInput runtime may be missing.\n",
                     static_cast<unsigned long>(hr));
        return 1;
    }

    game_input->SetFocusPolicy(gi::GameInputEnableBackgroundInput);
    GameInputMouseDeviceDiagnostics diagnostics;
    gi::GameInputCallbackToken device_token = 0;
    hr = game_input->RegisterDeviceCallback(nullptr,
                                            gi::GameInputKindMouse,
                                            gi::GameInputDeviceConnected,
                                            gi::GameInputBlockingEnumeration,
                                            &diagnostics,
                                            GameInputMouseDeviceCallback,
                                            &device_token);
    if (FAILED(hr)) {
        std::fprintf(stderr,
                     "RegisterDeviceCallback(GameInputKindMouse) failed: hr=0x%08lx.\n",
                     static_cast<unsigned long>(hr));
    }

    gi::GameInputCallbackToken reading_token = 0;
    hr = game_input->RegisterReadingCallback(nullptr,
                                             gi::GameInputKindMouse,
                                             &diagnostics,
                                             GameInputMouseDeviceReadingCallback,
                                             &reading_token);
    if (FAILED(hr) || reading_token == 0) {
        std::fprintf(stderr,
                     "RegisterReadingCallback(GameInputKindMouse) failed: hr=0x%08lx token=%llu.\n",
                     static_cast<unsigned long>(hr),
                     static_cast<unsigned long long>(reading_token));
        if (device_token != 0) game_input->UnregisterCallback(device_token);
        game_input->Release();
        return 1;
    }

    std::printf("\n--mouse-devices-gameinput: move one mouse at a time for %d second(s).\n",
                seconds);
    std::printf("Use the root token as mouse_devices.right / mouse_devices.left in a profile. Esc stops early.\n\n");

    using clock = std::chrono::steady_clock;
    const auto start = clock::now();
    const auto end = start + std::chrono::seconds(seconds);
    auto next_print = start + std::chrono::milliseconds(250);
    std::vector<GameInputMouseDeviceSample> last_print;

    while (clock::now() < end) {
        if ((GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0) {
            break;
        }
        const auto now = clock::now();
        if (now >= next_print) {
            std::vector<GameInputMouseDeviceSample> current;
            {
                std::lock_guard<std::mutex> lock(diagnostics.mutex);
                current = diagnostics.devices;
            }
            std::printf("[%.3fs] devices=%zu\n",
                        std::chrono::duration<double>(now - start).count(),
                        current.size());
            for (size_t i = 0; i < current.size(); ++i) {
                const auto& item = current[i];
                uint64_t prev_callbacks = 0;
                int64_t prev_dx = 0;
                int64_t prev_dy = 0;
                for (const auto& prev : last_print) {
                    if (prev.token == item.token) {
                        prev_callbacks = prev.callbacks;
                        prev_dx = prev.dx_sum;
                        prev_dy = prev.dy_sum;
                        break;
                    }
                }
                std::printf("  [%zu] root=%s vid=0x%04x pid=0x%04x cb=%5llu dx=%+7lld dy=%+7lld buttons=0x%08x name='%s'\n",
                            i,
                            item.token.c_str(),
                            static_cast<unsigned>(item.vendor_id),
                            static_cast<unsigned>(item.product_id),
                            static_cast<unsigned long long>(item.callbacks - prev_callbacks),
                            static_cast<long long>(item.dx_sum - prev_dx),
                            static_cast<long long>(item.dy_sum - prev_dy),
                            static_cast<unsigned>(item.buttons),
                            item.display_name.c_str());
            }
            std::fflush(stdout);
            last_print = current;
            next_print += std::chrono::milliseconds(250);
        }
        Sleep(1);
    }

    if (reading_token != 0) game_input->UnregisterCallback(reading_token);
    if (device_token != 0) game_input->UnregisterCallback(device_token);
    {
        std::lock_guard<std::mutex> lock(diagnostics.mutex);
        std::printf("\nsummary devices=%zu\n", diagnostics.devices.size());
        for (size_t i = 0; i < diagnostics.devices.size(); ++i) {
            const auto& item = diagnostics.devices[i];
            std::printf("  [%zu] root=%s device=%s vid=0x%04x pid=0x%04x total_dx=%+lld total_dy=%+lld name='%s'\n",
                        i,
                        item.token.c_str(),
                        item.device_token.c_str(),
                        static_cast<unsigned>(item.vendor_id),
                        static_cast<unsigned>(item.product_id),
                        static_cast<long long>(item.dx_sum),
                        static_cast<long long>(item.dy_sum),
                        item.display_name.c_str());
            if (!item.pnp_path.empty()) {
                std::printf("      pnp=%s\n", item.pnp_path.c_str());
            }
        }
    }
    game_input->Release();
    return 0;
}

struct GameInputMouseCapture {
    gi::IGameInput* game_input = nullptr;
    gi::GameInputCallbackToken token = 0;
    gi::GameInputCallbackToken keyboard_token = 0;
    GameInputMouseRateStats stats;
};

bool StartGameInputMouseCapture(GameInputMouseCapture* capture,
                                bool enable_keyboard = false,
                                const std::string& right_device_token = "auto",
                                const std::string& left_device_token = "") {
    if (!capture) {
        return false;
    }

    HRESULT hr = gi::GameInputCreate(&capture->game_input);
    if (FAILED(hr) || !capture->game_input) {
        std::fprintf(stderr,
                     "GameInputCreate failed: hr=0x%08lx. The GameInput runtime may be missing.\n",
                     static_cast<unsigned long>(hr));
        capture->game_input = nullptr;
        return false;
    }

    capture->game_input->SetFocusPolicy(gi::GameInputEnableBackgroundInput);
    capture->stats.right_device_token = right_device_token.empty() ? "auto" : right_device_token;
    capture->stats.left_device_token = left_device_token;
    hr = capture->game_input->RegisterReadingCallback(nullptr,
                                                      gi::GameInputKindMouse,
                                                      &capture->stats,
                                                      GameInputMouseReadingCallback,
                                                      &capture->token);
    if (FAILED(hr) || capture->token == 0) {
        std::fprintf(stderr,
                     "RegisterReadingCallback(GameInputKindMouse) failed: hr=0x%08lx token=%llu.\n",
                     static_cast<unsigned long>(hr),
                     static_cast<unsigned long long>(capture->token));
        capture->game_input->Release();
        capture->game_input = nullptr;
        capture->token = 0;
        return false;
    }

    if (enable_keyboard) {
        hr = capture->game_input->RegisterReadingCallback(nullptr,
                                                          gi::GameInputKindKeyboard,
                                                          &capture->stats,
                                                          GameInputKeyboardReadingCallback,
                                                          &capture->keyboard_token);
        if (FAILED(hr) || capture->keyboard_token == 0) {
            std::fprintf(stderr,
                         "RegisterReadingCallback(GameInputKindKeyboard) failed: hr=0x%08lx token=%llu.\n",
                         static_cast<unsigned long>(hr),
                         static_cast<unsigned long long>(capture->keyboard_token));
            if (capture->token != 0) {
                capture->game_input->UnregisterCallback(capture->token);
                capture->token = 0;
            }
            capture->game_input->Release();
            capture->game_input = nullptr;
            capture->keyboard_token = 0;
            return false;
        }
    }

    return true;
}

void StopGameInputMouseCapture(GameInputMouseCapture* capture) {
    if (!capture || !capture->game_input) {
        return;
    }
    if (capture->token != 0) {
        capture->game_input->UnregisterCallback(capture->token);
        capture->token = 0;
    }
    if (capture->keyboard_token != 0) {
        capture->game_input->UnregisterCallback(capture->keyboard_token);
        capture->keyboard_token = 0;
    }
    capture->game_input->Release();
    capture->game_input = nullptr;
}

std::string FormatPressedGameInputKeys(const GameInputMouseRateStats& stats) {
    std::string out;
    for (int vk = 1; vk < 256; ++vk) {
        if (!GameInputKeyDown(stats, vk)) {
            continue;
        }
        char item[16];
        if (vk >= 'A' && vk <= 'Z') {
            std::snprintf(item, sizeof(item), "%c", static_cast<char>(vk));
        } else if (vk >= '0' && vk <= '9') {
            std::snprintf(item, sizeof(item), "%c", static_cast<char>(vk));
        } else {
            std::snprintf(item, sizeof(item), "VK%u", static_cast<unsigned>(vk));
        }
        if (!out.empty()) {
            out += '+';
        }
        out += item;
    }
    return out.empty() ? std::string("-") : out;
}

int RunKeyboardRateGameInput(int seconds) {
    if (seconds <= 0 || seconds > 30) {
        std::fprintf(stderr, "--keyboard-rate-gameinput duration must be 1..30 seconds.\n");
        return 2;
    }

    gi::IGameInput* game_input = nullptr;
    HRESULT hr = gi::GameInputCreate(&game_input);
    if (FAILED(hr) || !game_input) {
        std::fprintf(stderr,
                     "GameInputCreate failed: hr=0x%08lx. The GameInput runtime may be missing.\n",
                     static_cast<unsigned long>(hr));
        return 1;
    }

    game_input->SetFocusPolicy(gi::GameInputEnableBackgroundInput);

    GameInputMouseRateStats stats;
    gi::GameInputCallbackToken token = 0;
    hr = game_input->RegisterReadingCallback(nullptr,
                                             gi::GameInputKindKeyboard,
                                             &stats,
                                             GameInputKeyboardReadingCallback,
                                             &token);
    if (FAILED(hr) || token == 0) {
        std::fprintf(stderr,
                     "RegisterReadingCallback(GameInputKindKeyboard) failed: hr=0x%08lx token=%llu.\n",
                     static_cast<unsigned long>(hr),
                     static_cast<unsigned long long>(token));
        game_input->Release();
        return 1;
    }

    std::printf("\n--keyboard-rate-gameinput: measuring GameInput keyboard callbacks for %d second(s).\n",
                seconds);
    std::printf("Keep VelociDrone foreground and press W/S/A/D/Space; Esc stops early.\n\n");

    using clock = std::chrono::steady_clock;
    const auto start = clock::now();
    const auto end = start + std::chrono::seconds(seconds);
    auto next_print = start + std::chrono::milliseconds(250);
    GameInputMouseRateSnapshot last_print = SnapshotGameInputStats(stats);

    while (clock::now() < end) {
        if ((GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0) {
            break;
        }

        const auto now = clock::now();
        if (now >= next_print) {
            const GameInputMouseRateSnapshot current = SnapshotGameInputStats(stats);
            const uint64_t callbacks = current.keyboard_callbacks - last_print.keyboard_callbacks;
            const uint64_t states = current.keyboard_states - last_print.keyboard_states;
            std::printf("[%.3fs] keyboard_callbacks=%5llu rate=%7.1f Hz states=%5llu down=%s\n",
                        std::chrono::duration<double>(now - start).count(),
                        static_cast<unsigned long long>(callbacks),
                        static_cast<double>(callbacks) / 0.250,
                        static_cast<unsigned long long>(states),
                        FormatPressedGameInputKeys(stats).c_str());
            std::fflush(stdout);
            last_print = current;
            next_print += std::chrono::milliseconds(250);
        }

        Sleep(1);
    }

    game_input->UnregisterCallback(token);
    const GameInputMouseRateSnapshot final_stats = SnapshotGameInputStats(stats);
    const double elapsed = std::chrono::duration<double>(clock::now() - start).count();
    std::printf("\nsummary: keyboard_callbacks=%llu elapsed=%.3fs avg_rate=%.1f Hz states=%llu down=%s\n",
                static_cast<unsigned long long>(final_stats.keyboard_callbacks),
                elapsed,
                elapsed > 0.0 ? static_cast<double>(final_stats.keyboard_callbacks) / elapsed : 0.0,
                static_cast<unsigned long long>(final_stats.keyboard_states),
                FormatPressedGameInputKeys(stats).c_str());

    game_input->Release();
    return 0;
}
#else
int RunMouseRateGameInput(int) {
    std::fprintf(stderr,
                 "--mouse-rate-gameinput is not available in this build; Microsoft.GameInput was not found under tool-cache.\n");
    return 2;
}
int RunKeyboardRateGameInput(int) {
    std::fprintf(stderr,
                 "--keyboard-rate-gameinput is not available in this build; Microsoft.GameInput was not found under tool-cache.\n");
    return 2;
}
#endif

std::string ToLowerAscii(std::string value) {
    for (char& ch : value) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    return value;
}

bool ContainsCaseInsensitive(const std::string& haystack, const char* needle) {
    return ToLowerAscii(haystack).find(ToLowerAscii(needle ? needle : "")) != std::string::npos;
}

std::string ExtractComPortName(const std::string& text) {
    size_t start = text.find("(COM");
    if (start != std::string::npos) {
        ++start;
    } else {
        start = text.find("COM");
    }
    if (start == std::string::npos) {
        return {};
    }

    size_t end = start + 3;
    while (end < text.size() && text[end] >= '0' && text[end] <= '9') {
        ++end;
    }
    if (end == start + 3) {
        return {};
    }
    return text.substr(start, end - start);
}

std::string FindGx12ComPortFromUsbRegistry() {
    HKEY root = nullptr;
    constexpr const char* kEnumPath =
        "SYSTEM\\CurrentControlSet\\Enum\\USB\\VID_1209&PID_4F54&MI_00";
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, kEnumPath, 0, KEY_READ, &root) != ERROR_SUCCESS) {
        return {};
    }

    std::string found;
    for (DWORD index = 0; found.empty(); ++index) {
        char instance[256]{};
        DWORD instance_len = sizeof(instance);
        LONG status = RegEnumKeyExA(root,
                                    index,
                                    instance,
                                    &instance_len,
                                    nullptr,
                                    nullptr,
                                    nullptr,
                                    nullptr);
        if (status == ERROR_NO_MORE_ITEMS) {
            break;
        }
        if (status != ERROR_SUCCESS) {
            continue;
        }

        std::string params_path = std::string(instance) + "\\Device Parameters";
        HKEY params = nullptr;
        if (RegOpenKeyExA(root, params_path.c_str(), 0, KEY_READ, &params) != ERROR_SUCCESS) {
            continue;
        }

        char port_name[64]{};
        DWORD type = 0;
        DWORD bytes = sizeof(port_name);
        if (RegQueryValueExA(params,
                             "PortName",
                             nullptr,
                             &type,
                             reinterpret_cast<LPBYTE>(port_name),
                             &bytes) == ERROR_SUCCESS &&
            type == REG_SZ &&
            port_name[0] != '\0') {
            found = port_name;
        }
        RegCloseKey(params);
    }

    RegCloseKey(root);
    return found;
}

bool IsLiveComPortName(const std::string& port) {
    if (port.size() < 4 ||
        (port[0] != 'C' && port[0] != 'c') ||
        (port[1] != 'O' && port[1] != 'o') ||
        (port[2] != 'M' && port[2] != 'm')) {
        return false;
    }

    for (size_t i = 3; i < port.size(); ++i) {
        if (port[i] < '0' || port[i] > '9') {
            return false;
        }
    }

    char target[512]{};
    return QueryDosDeviceA(port.c_str(), target, static_cast<DWORD>(sizeof(target))) != 0;
}

std::string FindGx12ComPort() {
    std::string port = FindGx12ComPortFromUsbRegistry();
    if (!port.empty() && IsLiveComPortName(port)) {
        return port;
    } else if (!port.empty()) {
        std::fprintf(stderr,
                     "auto COM scan: ignoring stale registry port %s for GX12 CDC (le=%lu).\n",
                     port.c_str(),
                     static_cast<unsigned long>(GetLastError()));
    }

    GUID ports_guid{};
    DWORD guid_count = 0;
    if (!SetupDiClassGuidsFromNameA("Ports", &ports_guid, 1, &guid_count) || guid_count == 0) {
        std::fprintf(stderr,
                     "auto COM scan: SetupDiClassGuidsFromNameA('Ports') failed (le=%lu).\n",
                     static_cast<unsigned long>(GetLastError()));
        return {};
    }

    HDEVINFO devices = SetupDiGetClassDevsA(&ports_guid,
                                            nullptr,
                                            nullptr,
                                            DIGCF_PRESENT);
    if (devices == INVALID_HANDLE_VALUE) {
        std::fprintf(stderr,
                     "auto COM scan: SetupDiGetClassDevsA failed (le=%lu).\n",
                     static_cast<unsigned long>(GetLastError()));
        return {};
    }

    std::string found;
    for (DWORD index = 0; found.empty(); ++index) {
        SP_DEVINFO_DATA info{};
        info.cbSize = sizeof(info);
        if (!SetupDiEnumDeviceInfo(devices, index, &info)) {
            if (GetLastError() != ERROR_NO_MORE_ITEMS) {
                std::fprintf(stderr,
                             "auto COM scan: SetupDiEnumDeviceInfo failed at index %lu (le=%lu).\n",
                             static_cast<unsigned long>(index),
                             static_cast<unsigned long>(GetLastError()));
            }
            break;
        }

        char hardware_id[1024]{};
        if (!SetupDiGetDeviceRegistryPropertyA(devices,
                                               &info,
                                               SPDRP_HARDWAREID,
                                               nullptr,
                                               reinterpret_cast<PBYTE>(hardware_id),
                                               sizeof(hardware_id),
                                               nullptr) ||
            !ContainsCaseInsensitive(hardware_id, kGx12UsbHardwareId)) {
            continue;
        }

        std::string candidate;
        char friendly_name[512]{};
        if (SetupDiGetDeviceRegistryPropertyA(devices,
                                              &info,
                                              SPDRP_FRIENDLYNAME,
                                              nullptr,
                                              reinterpret_cast<PBYTE>(friendly_name),
                                              sizeof(friendly_name),
                                              nullptr)) {
            candidate = ExtractComPortName(friendly_name);
        }

        if (candidate.empty()) {
            HKEY key = SetupDiOpenDevRegKey(devices,
                                            &info,
                                            DICS_FLAG_GLOBAL,
                                            0,
                                            DIREG_DEV,
                                            KEY_QUERY_VALUE);
            if (key != INVALID_HANDLE_VALUE) {
                char port_name[64]{};
                DWORD type = 0;
                DWORD bytes = sizeof(port_name);
                if (RegQueryValueExA(key,
                                     "PortName",
                                     nullptr,
                                     &type,
                                     reinterpret_cast<LPBYTE>(port_name),
                                     &bytes) == ERROR_SUCCESS &&
                    type == REG_SZ) {
                    candidate = port_name;
                }
                RegCloseKey(key);
            }
        }

        if (candidate.empty()) {
            continue;
        }
        if (!IsLiveComPortName(candidate)) {
            std::fprintf(stderr,
                         "auto COM scan: skipping non-live present-device port %s for GX12 CDC (le=%lu).\n",
                         candidate.c_str(),
                         static_cast<unsigned long>(GetLastError()));
            continue;
        }
        found = candidate;
    }

    SetupDiDestroyDeviceInfoList(devices);
    return found;
}

std::string ResolveTrainerPortName(const char* requested_port) {
    if (!requested_port || requested_port[0] == '\0') {
        return {};
    }
    if (_stricmp(requested_port, "auto") == 0) {
        std::string port = FindGx12ComPort();
        if (port.empty()) {
            std::fprintf(stderr,
                         "auto COM scan: no GX12 CDC interface found for USB\\%s.\n",
                         kGx12UsbHardwareId);
        } else {
            std::printf("auto COM scan: selected %s for GX12 CDC (%s).\n",
                        port.c_str(),
                        kGx12UsbHardwareId);
        }
        return port;
    }
    return requested_port;
}

std::string WindowsComPath(const char* port) {
    if (!port || port[0] == '\0') {
        return {};
    }
    if (std::strncmp(port, "\\\\.\\", 4) == 0) {
        return port;
    }
    return std::string("\\\\.\\") + port;
}

int ClampTrainerPulse(int64_t value) {
    if (value > 512) {
        return 512;
    }
    if (value < -512) {
        return -512;
    }
    return static_cast<int>(value);
}

int ClampTrainerResx(int64_t value) {
    if (value > 1024) {
        return 1024;
    }
    if (value < -1024) {
        return -1024;
    }
    return static_cast<int>(value);
}

enum class TrainerResolutionMode {
    Legacy,
    Gx12_2x,
};

#if defined(GX12_ENABLE_RESOLUTION_2X)
constexpr bool kGx12Resolution2xBuild = true;
#else
constexpr bool kGx12Resolution2xBuild = false;
#endif

int TrainerDomainLimit(TrainerResolutionMode mode) {
    return mode == TrainerResolutionMode::Gx12_2x ? 1024 : 512;
}

int TrainerLowValue(TrainerResolutionMode mode) {
    return -TrainerDomainLimit(mode);
}

int ClampTrainerOutput(int64_t value, TrainerResolutionMode mode) {
    return mode == TrainerResolutionMode::Gx12_2x ? ClampTrainerResx(value)
                                                  : ClampTrainerPulse(value);
}

constexpr int kTrainerProfileDomainLimit = 512;

int TrainerProfileLowValue() {
    return -kTrainerProfileDomainLimit;
}

int QuantizeTrainerProfileOutput(double value, TrainerResolutionMode mode) {
    if (mode == TrainerResolutionMode::Gx12_2x) {
        return ClampTrainerResx(static_cast<int64_t>(std::lround(value * 2.0)));
    }
    return ClampTrainerPulse(static_cast<int64_t>(std::lround(value)));
}

int ParseTrainerFrameRate(const char* text, int fallback) {
    const int value = ParsePositiveIntLimit(text, fallback, 8000);
    if (value < 0) {
        return -1;
    }
    return value;
}

uint16_t TrainerPulseToSbusRaw(int pulse) {
    constexpr int kSbusCenter = 0x3E0;
    const int magnitude = std::abs(pulse);
    const int scaled = (magnitude * 8 + 4) / 5;
    int raw = kSbusCenter + (pulse < 0 ? -scaled : scaled);
    if (raw < 0) {
        raw = 0;
    }
    if (raw > 0x07FF) {
        raw = 0x07FF;
    }
    return static_cast<uint16_t>(raw);
}

uint16_t TrainerResxToSbusRaw(int resx) {
    constexpr int kSbusCenter = 0x3E0;
    const int magnitude = std::abs(resx);
    const int scaled = (magnitude * 4 + 2) / 5;
    int raw = kSbusCenter + (resx < 0 ? -scaled : scaled);
    if (raw < 0) {
        raw = 0;
    }
    if (raw > 0x07FF) {
        raw = 0x07FF;
    }
    return static_cast<uint16_t>(raw);
}

uint16_t TrainerOutputToSbusRaw(int value, TrainerResolutionMode mode) {
    if (mode == TrainerResolutionMode::Gx12_2x) {
        return TrainerResxToSbusRaw(ClampTrainerResx(value));
    }
    return TrainerPulseToSbusRaw(ClampTrainerPulse(value));
}

constexpr size_t kSbusFrameSize = 25;
constexpr int kSbusChannels = 16;
constexpr int kSafeThrottleLow = -512;
constexpr uint8_t kSbusTrainerMaskMarker = 1U << 4;
constexpr uint8_t kSbusTrainerMaskRightActive = 1U << 5;
constexpr uint8_t kSbusTrainerMaskLeftActive = 1U << 6;
constexpr uint8_t kSbusTrainerResolution2x = 1U << 7;

void BuildSbusFrame(const std::array<int, kSbusChannels>& trainer_pulses,
                    uint8_t trainer_active_flags,
                    TrainerResolutionMode resolution_mode,
                    uint8_t* frame) {
    constexpr uint16_t kSbusCenter = 0x03E0;

    std::memset(frame, 0, kSbusFrameSize);
    frame[0] = 0x0F;
    frame[23] = trainer_active_flags;
    frame[24] = 0x00;

    uint16_t channels[kSbusChannels]{};
    for (uint16_t& channel : channels) {
        channel = kSbusCenter;
    }
    for (int i = 0; i < kSbusChannels; ++i) {
        channels[i] = TrainerOutputToSbusRaw(trainer_pulses[static_cast<size_t>(i)], resolution_mode);
    }

    uint32_t bit_index = 0;
    for (uint16_t channel : channels) {
        const uint16_t value = channel & 0x07FF;
        for (uint32_t bit = 0; bit < 11; bit++) {
            if (value & (1U << bit)) {
                const uint32_t byte_index = 1 + ((bit_index + bit) / 8);
                const uint32_t bit_in_byte = (bit_index + bit) % 8;
                frame[byte_index] |= static_cast<uint8_t>(1U << bit_in_byte);
            }
        }
        bit_index += 11;
    }
}

void BuildSbusFrame(const std::array<int, kSbusChannels>& trainer_pulses,
                    uint8_t trainer_active_flags,
                    uint8_t* frame) {
    BuildSbusFrame(trainer_pulses, trainer_active_flags, TrainerResolutionMode::Legacy, frame);
}

void BuildSbusFrame(const std::array<int, kSbusChannels>& trainer_pulses, uint8_t* frame) {
    BuildSbusFrame(trainer_pulses, 0, frame);
}

void BuildSbusFrame(int roll_pulse, int pitch_pulse, uint8_t* frame) {
    std::array<int, kSbusChannels> pulses{};
    pulses[0] = roll_pulse;
    pulses[1] = pitch_pulse;
    pulses[2] = kSafeThrottleLow;
    BuildSbusFrame(pulses, frame);
}

int RoundScaleSigned(int value, int numerator, int denominator) {
    const int magnitude = std::abs(value);
    const int scaled = (magnitude * numerator + (denominator / 2)) / denominator;
    return value < 0 ? -scaled : scaled;
}

int DecodeLegacySbusRawForSelfTest(uint16_t raw) {
    constexpr int kSbusCenter = 0x3E0;
    return (static_cast<int>(raw) - kSbusCenter) * 5 / 8;
}

int DecodeResolution2xSbusRawForSelfTest(uint16_t raw) {
    constexpr int kSbusCenter = 0x3E0;
    return RoundScaleSigned(static_cast<int>(raw) - kSbusCenter, 5, 4);
}

int RunTrainerResolutionSelfTest() {
    std::array<bool, 2049> legacy_seen{};
    std::array<bool, 2049> resolution2x_seen{};
    int legacy_max_error = 0;
    int resolution2x_max_error = 0;

    for (int pulse = -512; pulse <= 512; ++pulse) {
        const uint16_t raw = TrainerPulseToSbusRaw(pulse);
        const int decoded_resx = ClampTrainerResx(DecodeLegacySbusRawForSelfTest(raw) * 2);
        legacy_seen[static_cast<size_t>(decoded_resx + 1024)] = true;
        legacy_max_error = std::max(legacy_max_error, std::abs(decoded_resx - (pulse * 2)));
    }

    for (int resx = -1024; resx <= 1024; ++resx) {
        const uint16_t raw = TrainerResxToSbusRaw(resx);
        const int decoded_resx = ClampTrainerResx(DecodeResolution2xSbusRawForSelfTest(raw));
        resolution2x_seen[static_cast<size_t>(decoded_resx + 1024)] = true;
        resolution2x_max_error = std::max(resolution2x_max_error, std::abs(decoded_resx - resx));
    }

    const int legacy_count = static_cast<int>(
        std::count(legacy_seen.begin(), legacy_seen.end(), true));
    const int resolution2x_count = static_cast<int>(
        std::count(resolution2x_seen.begin(), resolution2x_seen.end(), true));

    std::printf("\n--trainer-resolution-self-test\n");
    std::printf("  build: gx12_2x_enabled=%s\n", kGx12Resolution2xBuild ? "true" : "false");
    std::printf("  legacy: input=-512..+512 distinct_resx_outputs=%d max_roundtrip_error_resx=%d\n",
                legacy_count,
                legacy_max_error);
    std::printf("  gx12_2x: input=-1024..+1024 distinct_resx_outputs=%d max_roundtrip_error_resx=%d\n",
                resolution2x_count,
                resolution2x_max_error);
    std::printf("  improvement=%.3fx\n",
                legacy_count > 0 ? static_cast<double>(resolution2x_count) /
                                       static_cast<double>(legacy_count)
                                  : 0.0);
    const int legacy_profile_full = QuantizeTrainerProfileOutput(
        static_cast<double>(kTrainerProfileDomainLimit),
        TrainerResolutionMode::Legacy);
    const int resolution2x_profile_full = QuantizeTrainerProfileOutput(
        static_cast<double>(kTrainerProfileDomainLimit),
        TrainerResolutionMode::Gx12_2x);
    std::printf("  profile_units: +512 full-stick -> legacy=%+d gx12_2x=%+d RESX\n",
                legacy_profile_full,
                resolution2x_profile_full);
    std::printf("  samples:\n");
    for (int value = -4; value <= 4; ++value) {
        const uint16_t legacy_raw = TrainerPulseToSbusRaw(value);
        const uint16_t resolution2x_raw = TrainerResxToSbusRaw(value);
        std::printf("    %+4d: legacy_raw=%4u legacy_resx=%+4d  gx12_2x_raw=%4u gx12_2x_resx=%+4d\n",
                    value,
                    legacy_raw,
                    DecodeLegacySbusRawForSelfTest(legacy_raw) * 2,
                    resolution2x_raw,
                    DecodeResolution2xSbusRawForSelfTest(resolution2x_raw));
    }

    return resolution2x_count > legacy_count &&
                   legacy_profile_full == 512 &&
                   resolution2x_profile_full == 1024
               ? 0
               : 1;
}

double ClampDouble(double value, double lo, double hi);

enum class ElasticReturnMode {
    Linear,
    Progressive,
    Smoothstep,
    Expo,
};

enum class PositionModel {
    Integrator,
    DynamicGimbal,
};

enum class InputGainMode {
    Flat,
    Adaptive,
};

enum class InputFilterMode {
    Off,
    Smoothing,
    OneEuro,
};

enum class OutputCurveMode {
    Expo,
    Nodes,
    Actual,
};

enum class GateShape {
    Axis,
    Circle,
    Octagon,
    Square,
};

struct StickShapeNode {
    double x = 0.5;
    double y = 0.5;
    double width = 0.25;
};

struct StickShapeCurve {
    bool enabled = false;
    std::vector<StickShapeNode> nodes;
};

constexpr double kStickShapeMinWidth = 0.02;
constexpr double kStickShapeMinEffectiveWidth = 0.05;
constexpr double kStickShapeMaxWidth = 1.0;

struct KeyboardLeftStickProfile {
    bool enabled = false;
    bool block_selected_keys = false;
    enum class InputSource {
        GameInput,
        WootingAnalog,
        Auto,
    };
    InputSource input_source = InputSource::GameInput;
    bool require_analog = false;
    std::string throttle_up_key = "W";
    std::string throttle_down_key = "S";
    std::string yaw_left_key = "A";
    std::string yaw_right_key = "D";
    std::string throttle_cut_key = "Space";
    std::string analog_keycode_mode = "virtual_key_translate";
    int throttle_up_vk = 'W';
    int throttle_down_vk = 'S';
    int yaw_left_vk = 'A';
    int yaw_right_vk = 'D';
    int throttle_cut_vk = VK_SPACE;
    double throttle_rate = 8.0;
    bool throttle_return_enabled = false;
    double throttle_return_rate = 0.0;
    int yaw_pulse = 512;
    double yaw_slew_rate = 4096.0;
    double analog_deadzone = 0.04;
    double analog_curve = 1.0;
    double analog_min = 0.0;
    double analog_max = 1.0;
    bool invert_yaw = false;
};

struct MouseLeftStickProfile {
    bool enabled = false;
    bool require_device = true;
    bool invert_throttle = false;
    bool invert_yaw = false;
    bool swap_axes = false;
    double throttle_rate = 4096.0;
    bool throttle_return_enabled = false;
    double throttle_return_rate = 0.0;
    int yaw_pulse = 512;
    double yaw_gain = 10.0;
    int yaw_deadband = 0;
    double yaw_smoothing = 0.0;
    double yaw_slew_rate = 4096.0;
    bool yaw_shaping_enabled = false;
    InputFilterMode yaw_input_filter = InputFilterMode::Off;
    double yaw_one_euro_min_cutoff_hz = 1.0;
    double yaw_one_euro_beta = 0.05;
    double yaw_one_euro_dcutoff_hz = 1.0;
    bool yaw_despike_enabled = false;
    bool yaw_despike_count_enabled = false;
    int yaw_despike_window = 5;
    double yaw_despike_threshold_sigma = 3.0;
    OutputCurveMode yaw_output_curve = OutputCurveMode::Expo;
    double yaw_expo = 0.0;
    double yaw_actual_center = 0.45;
    double yaw_actual_max = 1.0;
    double yaw_actual_expo = 0.30;
    PositionModel yaw_position_model = PositionModel::Integrator;
    double yaw_gimbal_frequency_hz = 5.0;
    double yaw_gimbal_damping_ratio = 1.15;
    double yaw_gimbal_input_impulse = 1.0;
    double yaw_gimbal_static_friction = 0.0;
    double yaw_gimbal_dynamic_friction = 0.0;
    double yaw_gimbal_edge_bumper = 0.0;
    bool yaw_gimbal_antiwindup_enabled = true;
    double yaw_gimbal_antiwindup_start = 0.92;
    double yaw_gimbal_antiwindup_min_gain = 0.10;
    InputGainMode yaw_input_gain_mode = InputGainMode::Flat;
    double yaw_adaptive_slow_gain = 0.65;
    double yaw_adaptive_fast_gain = 1.60;
    double yaw_adaptive_speed_low = 120.0;
    double yaw_adaptive_speed_high = 1800.0;
    double yaw_adaptive_curve = 1.0;
    double yaw_adaptive_tracker_ms = 35.0;
    GateShape yaw_gate_shape = GateShape::Axis;
    double yaw_diagonal_scale = 1.0;
    bool yaw_return_enabled = false;
    double yaw_return_rate = 0.0;
    double yaw_return_idle_ms = 0.0;
    bool yaw_constant_return_enabled = false;
    double yaw_constant_return_rate = 0.0;
    bool yaw_elastic_return_enabled = false;
    ElasticReturnMode yaw_elastic_return_mode = ElasticReturnMode::Progressive;
    double yaw_elastic_return_coefficient = 0.0;
    double yaw_elastic_return_curve = 0.0;
    StickShapeCurve yaw_output_shape;
    StickShapeCurve yaw_return_shape;
};

struct RightMouseLeftStickProfile {
    bool enabled = false;
    bool invert_throttle = false;
    bool invert_yaw = false;
    bool swap_axes = false;
    double throttle_step = 64.0;
    double throttle_button_rate = 4096.0;
    bool throttle_return_enabled = false;
    double throttle_return_rate = 0.0;
    int yaw_pulse = 512;
    double yaw_scroll_step = 64.0;
    double yaw_slew_rate = 4096.0;
};

const char* KeyboardInputSourceName(KeyboardLeftStickProfile::InputSource source) {
    switch (source) {
    case KeyboardLeftStickProfile::InputSource::GameInput:
        return "gameinput";
    case KeyboardLeftStickProfile::InputSource::WootingAnalog:
        return "wooting_analog";
    case KeyboardLeftStickProfile::InputSource::Auto:
        return "auto";
    }
    return "gameinput";
}

bool ParseKeyboardInputSourceName(const std::string& text, KeyboardLeftStickProfile::InputSource* source) {
    const std::string value = ToLowerAscii(text);
    if (value.empty() || value == "gameinput" || value == "digital") {
        *source = KeyboardLeftStickProfile::InputSource::GameInput;
        return true;
    }
    if (value == "wooting" || value == "wooting_analog" || value == "analog") {
        *source = KeyboardLeftStickProfile::InputSource::WootingAnalog;
        return true;
    }
    if (value == "auto") {
        *source = KeyboardLeftStickProfile::InputSource::Auto;
        return true;
    }
    return false;
}

enum class WootingAnalogKeycodeMode : int {
    Hid = 0,
    ScanCode1 = 1,
    VirtualKey = 2,
    VirtualKeyTranslate = 3,
};

bool ParseWootingAnalogKeycodeMode(const std::string& text, WootingAnalogKeycodeMode* mode) {
    const std::string value = ToLowerAscii(text);
    if (value.empty() || value == "virtual_key_translate" || value == "vk_translate" ||
        value == "translate") {
        *mode = WootingAnalogKeycodeMode::VirtualKeyTranslate;
        return true;
    }
    if (value == "virtual_key" || value == "vk") {
        *mode = WootingAnalogKeycodeMode::VirtualKey;
        return true;
    }
    if (value == "hid") {
        *mode = WootingAnalogKeycodeMode::Hid;
        return true;
    }
    if (value == "scancode1" || value == "scan_code1" || value == "scan_code_1") {
        *mode = WootingAnalogKeycodeMode::ScanCode1;
        return true;
    }
    return false;
}

const char* WootingResultName(int result) {
    switch (result) {
    case 1: return "Ok";
    case -2000: return "UnInitialized";
    case -1999: return "NoDevices";
    case -1998: return "DeviceDisconnected";
    case -1997: return "Failure";
    case -1996: return "InvalidArgument";
    case -1995: return "NoPlugins";
    case -1994: return "FunctionNotFound";
    case -1993: return "NoMapping";
    case -1992: return "NotAvailable";
    case -1991: return "IncompatibleVersion";
    case -1990: return "DLLNotFound";
    default: return "Unknown";
    }
}

struct WootingAnalogDeviceInfo {
    uint16_t vendor_id;
    uint16_t product_id;
    char* manufacturer_name;
    char* device_name;
    uint64_t device_id;
    int device_type;
};

class WootingAnalogSdk {
public:
    using InitialiseFn = int(__cdecl*)();
    using UninitialiseFn = int(__cdecl*)();
    using SetKeycodeModeFn = int(__cdecl*)(int);
    using ReadAnalogFn = float(__cdecl*)(unsigned short);
    using GetConnectedDevicesInfoFn = int(__cdecl*)(WootingAnalogDeviceInfo**, unsigned int);
    using VersionFn = int(__cdecl*)();
    using VersionSemverFn = const char*(__cdecl*)();

    ~WootingAnalogSdk() {
        Shutdown();
    }

    bool Load(std::string* error) {
        if (module_) {
            return true;
        }

        const wchar_t* dll_names[] = {
            L"wooting_analog_sdk_dist.dll",
            L"wooting-analog-sdk_dist.dll",
            L"wooting_analog_sdk.dll",
            L"wooting-analog-sdk.dll",
            L"wooting_analog_wrapper.dll",
            L"wooting-analog-wrapper.dll",
        };

        for (const wchar_t* name : dll_names) {
            module_ = LoadLibraryW(name);
            if (module_) {
                dll_name_ = WideToUtf8(name);
                wchar_t full_path[MAX_PATH]{};
                if (GetModuleFileNameW(module_, full_path, MAX_PATH) > 0) {
                    dll_path_ = WideToUtf8(full_path);
                }
                break;
            }
        }

        if (!module_) {
            if (error) {
                *error = "could not load Wooting Analog SDK DLL from PATH/application directory";
            }
            return false;
        }

        initialise_ = reinterpret_cast<InitialiseFn>(GetProcAddress(module_, "wooting_analog_initialise"));
        uninitialise_ = reinterpret_cast<UninitialiseFn>(GetProcAddress(module_, "wooting_analog_uninitialise"));
        set_keycode_mode_ = reinterpret_cast<SetKeycodeModeFn>(GetProcAddress(module_, "wooting_analog_set_keycode_mode"));
        read_analog_ = reinterpret_cast<ReadAnalogFn>(GetProcAddress(module_, "wooting_analog_read_analog"));
        get_connected_devices_info_ = reinterpret_cast<GetConnectedDevicesInfoFn>(GetProcAddress(module_, "wooting_analog_get_connected_devices_info"));
        version_ = reinterpret_cast<VersionFn>(GetProcAddress(module_, "wooting_analog_version"));
        version_semver_ = reinterpret_cast<VersionSemverFn>(GetProcAddress(module_, "wooting_analog_version_semver"));

        if (!initialise_ || !uninitialise_ || !set_keycode_mode_ || !read_analog_) {
            if (error) {
                *error = "Wooting Analog SDK DLL is missing required C API exports";
            }
            Shutdown();
            return false;
        }

        return true;
    }

    bool Initialise(WootingAnalogKeycodeMode mode, std::string* error) {
        if (!Load(error)) {
            return false;
        }
        if (!initialised_) {
            const int result = initialise_();
            if (result < 0) {
                if (error) {
                    char text[160];
                    std::snprintf(text,
                                  sizeof(text),
                                  "wooting_analog_initialise failed: %s (%d)",
                                  WootingResultName(result),
                                  result);
                    *error = text;
                }
                return false;
            }
            device_count_ = result;
            initialised_ = true;
        }

        const int mode_result = set_keycode_mode_(static_cast<int>(mode));
        if (mode_result < 0) {
            if (error) {
                char text[160];
                std::snprintf(text,
                              sizeof(text),
                              "wooting_analog_set_keycode_mode failed: %s (%d)",
                              WootingResultName(mode_result),
                              mode_result);
                *error = text;
            }
            return false;
        }
        keycode_mode_ = mode;
        return true;
    }

    void Shutdown() {
        if (initialised_ && uninitialise_) {
            (void)uninitialise_();
        }
        initialised_ = false;
        if (module_) {
            FreeLibrary(module_);
            module_ = nullptr;
        }
        initialise_ = nullptr;
        uninitialise_ = nullptr;
        set_keycode_mode_ = nullptr;
        read_analog_ = nullptr;
        get_connected_devices_info_ = nullptr;
        version_ = nullptr;
        version_semver_ = nullptr;
        dll_name_.clear();
        dll_path_.clear();
        device_count_ = 0;
    }

    float ReadAnalog(unsigned short vk) const {
        if (!initialised_ || !read_analog_) {
            return -2000.0f;
        }
        return read_analog_(vk);
    }

    int DeviceCount() const {
        return device_count_;
    }

    const std::string& DllName() const {
        return dll_name_;
    }

    const std::string& DllPath() const {
        return dll_path_;
    }

    std::string VersionText() const {
        if (version_semver_) {
            const char* semver = version_semver_();
            if (semver && semver[0] != '\0') {
                return semver;
            }
        }
        if (version_) {
            const int major = version_();
            if (major > 0) {
                return std::to_string(major);
            }
        }
        return {};
    }

    std::vector<std::string> DeviceDescriptions() const {
        std::vector<std::string> descriptions;
        if (!initialised_ || !get_connected_devices_info_) {
            return descriptions;
        }

        std::array<WootingAnalogDeviceInfo*, 8> devices{};
        const int count = get_connected_devices_info_(devices.data(),
                                                      static_cast<unsigned int>(devices.size()));
        if (count <= 0) {
            return descriptions;
        }
        for (int i = 0; i < count && i < static_cast<int>(devices.size()); ++i) {
            const WootingAnalogDeviceInfo* device = devices[static_cast<size_t>(i)];
            if (!device) {
                continue;
            }
            char text[256];
            std::snprintf(text,
                          sizeof(text),
                          "vid=0x%04x pid=0x%04x name='%s' manufacturer='%s'",
                          device->vendor_id,
                          device->product_id,
                          device->device_name ? device->device_name : "",
                          device->manufacturer_name ? device->manufacturer_name : "");
            descriptions.emplace_back(text);
        }
        return descriptions;
    }

private:
    HMODULE module_ = nullptr;
    InitialiseFn initialise_ = nullptr;
    UninitialiseFn uninitialise_ = nullptr;
    SetKeycodeModeFn set_keycode_mode_ = nullptr;
    ReadAnalogFn read_analog_ = nullptr;
    GetConnectedDevicesInfoFn get_connected_devices_info_ = nullptr;
    VersionFn version_ = nullptr;
    VersionSemverFn version_semver_ = nullptr;
    std::string dll_name_;
    std::string dll_path_;
    bool initialised_ = false;
    int device_count_ = 0;
    WootingAnalogKeycodeMode keycode_mode_ = WootingAnalogKeycodeMode::VirtualKeyTranslate;
};

struct AnalogKeyDepths {
    double throttle_up = 0.0;
    double throttle_down = 0.0;
    double throttle_cut = 0.0;
    double yaw_left = 0.0;
    double yaw_right = 0.0;
    uint32_t errors = 0;
};

double ApplyAnalogDepthProfile(double raw, const KeyboardLeftStickProfile& keyboard) {
    if (raw < 0.0) {
        return 0.0;
    }
    double value = (raw - keyboard.analog_min) /
                   std::max(0.001, keyboard.analog_max - keyboard.analog_min);
    value = ClampDouble(value, 0.0, 1.0);
    if (value <= keyboard.analog_deadzone) {
        return 0.0;
    }
    value = (value - keyboard.analog_deadzone) /
            std::max(0.001, 1.0 - keyboard.analog_deadzone);
    value = ClampDouble(value, 0.0, 1.0);
    if (std::abs(keyboard.analog_curve - 1.0) > 0.001) {
        value = std::pow(value, keyboard.analog_curve);
    }
    return ClampDouble(value, 0.0, 1.0);
}

double ReadWootingDepth(WootingAnalogSdk* sdk,
                        int virtual_key,
                        const KeyboardLeftStickProfile& keyboard,
                        uint32_t* errors) {
    if (!sdk || virtual_key <= 0) {
        return 0.0;
    }
    const float raw = sdk->ReadAnalog(static_cast<unsigned short>(virtual_key));
    if (raw < 0.0f) {
        if (errors) {
            ++*errors;
        }
        return 0.0;
    }
    return ApplyAnalogDepthProfile(static_cast<double>(raw), keyboard);
}

AnalogKeyDepths ReadWootingKeyDepths(WootingAnalogSdk* sdk,
                                     const KeyboardLeftStickProfile& keyboard) {
    AnalogKeyDepths depths;
    depths.throttle_up = ReadWootingDepth(sdk, keyboard.throttle_up_vk, keyboard, &depths.errors);
    depths.throttle_down = ReadWootingDepth(sdk, keyboard.throttle_down_vk, keyboard, &depths.errors);
    depths.throttle_cut = ReadWootingDepth(sdk, keyboard.throttle_cut_vk, keyboard, &depths.errors);
    depths.yaw_left = ReadWootingDepth(sdk, keyboard.yaw_left_vk, keyboard, &depths.errors);
    depths.yaw_right = ReadWootingDepth(sdk, keyboard.yaw_right_vk, keyboard, &depths.errors);
    return depths;
}

int RunWootingRate(int seconds, const std::vector<std::string>& key_names) {
    if (seconds <= 0 || seconds > 30) {
        std::fprintf(stderr, "--wooting-rate duration must be 1..30 seconds.\n");
        return 2;
    }

    std::vector<std::pair<std::string, int>> keys;
    if (key_names.empty()) {
        keys = {{"W", 'W'}, {"A", 'A'}, {"S", 'S'}, {"D", 'D'}, {"Space", VK_SPACE}};
    } else {
        for (const std::string& name : key_names) {
            const int vk = ParseVirtualKeyName(name);
            if (vk <= 0) {
                std::fprintf(stderr, "invalid --wooting-rate key name: %s\n", name.c_str());
                return 2;
            }
            keys.emplace_back(name, vk);
        }
    }

    WootingAnalogSdk sdk;
    std::string error;
    if (!sdk.Initialise(WootingAnalogKeycodeMode::VirtualKeyTranslate, &error)) {
        std::fprintf(stderr, "--wooting-rate: %s.\n", error.c_str());
        std::fprintf(stderr,
                     "Install/update Wootility or put the Wooting Analog SDK wrapper DLL beside gx12mouse.exe/in PATH.\n");
        return 1;
    }

    std::printf("\n--wooting-rate: measuring Wooting Analog SDK for %d second(s).\n", seconds);
    const std::string sdk_version = sdk.VersionText();
    std::printf("dll=%s%s%s devices=%d keycode_mode=virtual_key_translate\n",
                sdk.DllName().c_str(),
                sdk_version.empty() ? "" : " sdk_version=",
                sdk_version.empty() ? "" : sdk_version.c_str(),
                sdk.DeviceCount());
    if (!sdk.DllPath().empty()) {
        std::printf("dll_path=%s\n", sdk.DllPath().c_str());
    }
    if (sdk.DeviceCount() <= 0) {
        std::printf("warning: Windows may see the keyboard, but the Wooting Analog SDK reported no analog devices.\n");
        std::printf("         Check Wootility/firmware SDK support; Gamepad Mode does not affect SDK device enumeration.\n");
    }
    const std::vector<std::string> devices = sdk.DeviceDescriptions();
    for (const std::string& device : devices) {
        std::printf("  device: %s\n", device.c_str());
    }
    std::printf("Keep VelociDrone foreground if desired; press the listed keys; Esc stops early.\n\n");

    using clock = std::chrono::steady_clock;
    const auto start = clock::now();
    const auto end = start + std::chrono::seconds(seconds);
    auto next_print = start + std::chrono::milliseconds(250);
    uint64_t samples = 0;
    uint64_t read_errors = 0;
    std::vector<double> latest(keys.size(), 0.0);
    std::vector<int> latest_errors(keys.size(), 0);

    while (clock::now() < end) {
        if ((GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0) {
            break;
        }

        for (size_t i = 0; i < keys.size(); ++i) {
            const float raw = sdk.ReadAnalog(static_cast<unsigned short>(keys[i].second));
            if (raw < 0.0f) {
                ++read_errors;
                latest[i] = 0.0;
                latest_errors[i] = static_cast<int>(raw);
            } else {
                latest[i] = ClampDouble(static_cast<double>(raw), 0.0, 1.0);
                latest_errors[i] = 0;
            }
        }
        ++samples;

        const auto now = clock::now();
        if (now >= next_print) {
            std::printf("[%.3fs] samples=%llu errors=%llu",
                        std::chrono::duration<double>(now - start).count(),
                        static_cast<unsigned long long>(samples),
                        static_cast<unsigned long long>(read_errors));
            for (size_t i = 0; i < keys.size(); ++i) {
                std::printf(" %s=%.3f", keys[i].first.c_str(), latest[i]);
            }
            for (size_t i = 0; i < keys.size(); ++i) {
                if (latest_errors[i] < 0) {
                    std::printf(" %s_error=%s(%d)",
                                keys[i].first.c_str(),
                                WootingResultName(latest_errors[i]),
                                latest_errors[i]);
                }
            }
            std::printf("\n");
            std::fflush(stdout);
            next_print += std::chrono::milliseconds(250);
        }

        Sleep(1);
    }

    const double elapsed = std::chrono::duration<double>(clock::now() - start).count();
    std::printf("\nsummary: samples=%llu elapsed=%.3fs poll_rate=%.1f Hz errors=%llu",
                static_cast<unsigned long long>(samples),
                elapsed,
                elapsed > 0.0 ? static_cast<double>(samples) / elapsed : 0.0,
                static_cast<unsigned long long>(read_errors));
    for (size_t i = 0; i < keys.size(); ++i) {
        std::printf(" %s=%.3f", keys[i].first.c_str(), latest[i]);
    }
    for (size_t i = 0; i < keys.size(); ++i) {
        if (latest_errors[i] < 0) {
            std::printf(" %s_error=%s(%d)",
                        keys[i].first.c_str(),
                        WootingResultName(latest_errors[i]),
                        latest_errors[i]);
        }
    }
    std::printf("\n");
    return (samples > 0 && read_errors == samples * keys.size()) ? 1 : 0;
}

enum class ControlMode {
    DirectMouse,
    DroneMouseAim,
};

struct ElasticAxisState {
    double velocity = 0.0;
};

struct HampelAxisState {
    std::array<double, 15> samples{};
    int count = 0;
    int next = 0;
};

struct OneEuroAxisState {
    bool initialized = false;
    double value = 0.0;
    double derivative = 0.0;
};

struct RightStickSharedState {
    double adaptive_speed = 0.0;
    double adaptive_gain = 1.0;
    double gate_scale = 1.0;
    HampelAxisState despike_roll;
    HampelAxisState despike_pitch;
    std::array<uint32_t, 10> despike_recent_buckets{};
    int despike_recent_bucket = 0;
    double despike_recent_elapsed_s = 0.0;
    uint64_t despike_recent_count = 0;
    uint64_t despike_total_count = 0;
    OneEuroAxisState one_euro_roll;
    OneEuroAxisState one_euro_pitch;
    double raw_roll_source = 0.0;
    double raw_pitch_source = 0.0;
    double filtered_roll_source = 0.0;
    double filtered_pitch_source = 0.0;
    bool gimbal_antiwindup_active = false;
};

struct MouseAimProfile {
    double sensitivity_x = 1.0;
    double sensitivity_y = 1.0;
    double reticle_limit = 512.0;
    double reticle_deadband = 8.0;
    double reticle_return_rate = 0.0;
    double output_smoothing = 0.10;
    double roll_gain = 0.65;
    double yaw_gain = 0.55;
    double pitch_gain = 0.85;
    int roll_max = 420;
    int yaw_max = 360;
    int pitch_max = 420;
    double slew_rate = 9000.0;
    bool invert_x = false;
    bool invert_y = false;
};

struct MouseAimState {
    double reticle_x = 0.0;
    double reticle_y = 0.0;
    double roll = 0.0;
    double pitch = 0.0;
    double yaw = 0.0;
};

std::array<std::atomic<uint8_t>, 256> g_blocked_keyboard_vks{};
std::array<std::atomic<uint8_t>, 256> g_blocked_keyboard_down{};
std::atomic<bool> g_keyboard_blocker_active{false};

LRESULT CALLBACK BlockSelectedKeyboardHookProc(int code, WPARAM wparam, LPARAM lparam) {
    if (code == HC_ACTION && g_keyboard_blocker_active.load(std::memory_order_acquire)) {
        const auto* key = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lparam);
        if (key && key->vkCode < g_blocked_keyboard_vks.size() &&
            g_blocked_keyboard_vks[key->vkCode].load(std::memory_order_acquire) != 0) {
            switch (wparam) {
            case WM_KEYDOWN:
            case WM_SYSKEYDOWN:
                g_blocked_keyboard_down[key->vkCode].store(1, std::memory_order_release);
                return 1;
            case WM_KEYUP:
            case WM_SYSKEYUP:
                g_blocked_keyboard_down[key->vkCode].store(0, std::memory_order_release);
                return 1;
            default:
                break;
            }
        }
    }
    return CallNextHookEx(nullptr, code, wparam, lparam);
}

bool BlockedKeyboardKeyDown(int virtual_key) {
    if (virtual_key <= 0 || virtual_key >= static_cast<int>(g_blocked_keyboard_down.size())) {
        return false;
    }
    return g_blocked_keyboard_down[static_cast<size_t>(virtual_key)].load(std::memory_order_acquire) != 0;
}

std::vector<int> ExpandModifierVirtualKey(int virtual_key) {
    switch (virtual_key) {
    case VK_SHIFT:
        return {VK_SHIFT, VK_LSHIFT, VK_RSHIFT};
    case VK_CONTROL:
        return {VK_CONTROL, VK_LCONTROL, VK_RCONTROL};
    case VK_MENU:
        return {VK_MENU, VK_LMENU, VK_RMENU};
    default:
        return {virtual_key};
    }
}

template <typename IsDown>
bool AnyExpandedKeyDown(int virtual_key, IsDown&& is_down) {
    for (int expanded_vk : ExpandModifierVirtualKey(virtual_key)) {
        if (is_down(expanded_vk)) {
            return true;
        }
    }
    return false;
}

class SelectedKeyboardBlocker {
public:
    SelectedKeyboardBlocker() = default;
    SelectedKeyboardBlocker(const SelectedKeyboardBlocker&) = delete;
    SelectedKeyboardBlocker& operator=(const SelectedKeyboardBlocker&) = delete;

    ~SelectedKeyboardBlocker() {
        Stop();
    }

    bool Start(const KeyboardLeftStickProfile& keyboard) {
        if (!keyboard.enabled || !keyboard.block_selected_keys) {
            return true;
        }

        Stop();
        for (auto& blocked : g_blocked_keyboard_vks) {
            blocked.store(0, std::memory_order_release);
        }
        for (auto& down : g_blocked_keyboard_down) {
            down.store(0, std::memory_order_release);
        }
        const int keys[] = {
            keyboard.throttle_up_vk,
            keyboard.throttle_down_vk,
            keyboard.throttle_cut_vk,
            keyboard.yaw_left_vk,
            keyboard.yaw_right_vk,
        };
        for (int vk : keys) {
            for (int expanded_vk : ExpandModifierVirtualKey(vk)) {
                if (expanded_vk > 0 && expanded_vk < static_cast<int>(g_blocked_keyboard_vks.size())) {
                    g_blocked_keyboard_vks[static_cast<size_t>(expanded_vk)].store(1, std::memory_order_release);
                }
            }
        }
        blocked_vks_.clear();
        for (int vk = 1; vk < static_cast<int>(g_blocked_keyboard_vks.size()); ++vk) {
            if (g_blocked_keyboard_vks[static_cast<size_t>(vk)].load(std::memory_order_acquire) != 0) {
                blocked_vks_.push_back(vk);
            }
        }

        ready_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!ready_event_) {
            std::fprintf(stderr,
                         "warning: keyboard block setup failed: CreateEventW le=%lu. Continuing without key blocking.\n",
                         static_cast<unsigned long>(GetLastError()));
            return false;
        }

        thread_ = std::thread([this]() { HookThreadMain(); });
        const DWORD wait = WaitForSingleObject(ready_event_, 3000);
        CloseHandle(ready_event_);
        ready_event_ = nullptr;
        if (wait != WAIT_OBJECT_0 || !started_.load(std::memory_order_acquire)) {
            Stop();
            std::fprintf(stderr,
                         "warning: keyboard block hook did not start. Continuing without key blocking.\n");
            return false;
        }
        std::printf("keyboard_block=active keys=");
        for (size_t i = 0; i < blocked_vks_.size(); ++i) {
            std::printf("%sVK%d", i == 0 ? "" : ",", blocked_vks_[i]);
        }
        std::printf(" hotkeys=%d hook=on\n", registered_hotkeys_.load(std::memory_order_acquire));
        return true;
    }

    void Stop() {
        const DWORD thread_id = thread_id_.exchange(0, std::memory_order_acq_rel);
        if (thread_id != 0) {
            PostThreadMessageW(thread_id, WM_QUIT, 0, 0);
        }
        if (thread_.joinable()) {
            thread_.join();
        }
        g_keyboard_blocker_active.store(false, std::memory_order_release);
        started_.store(false, std::memory_order_release);
        registered_hotkeys_.store(0, std::memory_order_release);
        blocked_vks_.clear();
        for (auto& blocked : g_blocked_keyboard_vks) {
            blocked.store(0, std::memory_order_release);
        }
        for (auto& down : g_blocked_keyboard_down) {
            down.store(0, std::memory_order_release);
        }
    }

private:
    void HookThreadMain() {
        thread_id_.store(GetCurrentThreadId(), std::memory_order_release);
        MSG msg{};
        PeekMessageW(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);

        HHOOK hook = SetWindowsHookExW(WH_KEYBOARD_LL,
                                       BlockSelectedKeyboardHookProc,
                                       GetModuleHandleW(nullptr),
                                       0);
        if (!hook) {
            thread_id_.store(0, std::memory_order_release);
            started_.store(false, std::memory_order_release);
            if (ready_event_) {
                SetEvent(ready_event_);
            }
            return;
        }

        g_keyboard_blocker_active.store(true, std::memory_order_release);
        int hotkeys = 0;
        for (size_t i = 0; i < blocked_vks_.size(); ++i) {
            const int id = kHotkeyBaseId + static_cast<int>(i);
            if (RegisterHotKey(nullptr, id, MOD_NOREPEAT, static_cast<UINT>(blocked_vks_[i]))) {
                registered_hotkey_ids_.push_back(id);
                ++hotkeys;
            }
        }
        registered_hotkeys_.store(hotkeys, std::memory_order_release);
        started_.store(true, std::memory_order_release);
        if (ready_event_) {
            SetEvent(ready_event_);
        }

        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        g_keyboard_blocker_active.store(false, std::memory_order_release);
        for (int id : registered_hotkey_ids_) {
            UnregisterHotKey(nullptr, id);
        }
        registered_hotkey_ids_.clear();
        registered_hotkeys_.store(0, std::memory_order_release);
        UnhookWindowsHookEx(hook);
        thread_id_.store(0, std::memory_order_release);
    }

    static constexpr int kHotkeyBaseId = 0x4712;
    std::thread thread_;
    std::atomic<DWORD> thread_id_{0};
    std::atomic<bool> started_{false};
    std::atomic<int> registered_hotkeys_{0};
    std::vector<int> blocked_vks_;
    std::vector<int> registered_hotkey_ids_;
    HANDLE ready_event_ = nullptr;
};

struct TrainerProfile {
    std::string source_file;
    std::string name = "unnamed";
    std::string port = "COM3";
    std::string stop_key = "Esc";
    int stop_key_vk = VK_ESCAPE;
    std::string freeze_key = "F2";
    int freeze_key_vk = VK_F2;
    int seconds = kTrainerProfileIndefiniteSeconds;
    int frame_rate_hz = 1000;
    TrainerResolutionMode resolution_mode = TrainerResolutionMode::Legacy;
    double roll_gain = 50.0;
    double pitch_gain = 50.0;
    int max_output = 512;
    int deadband = 0;
    double expo = 0.0;
    double smoothing = 0.0;
    InputFilterMode input_filter = InputFilterMode::Off;
    double one_euro_min_cutoff_hz = 1.0;
    double one_euro_beta = 0.05;
    double one_euro_dcutoff_hz = 1.0;
    bool despike_enabled = false;
    bool despike_count_enabled = false;
    int despike_window = 5;
    double despike_threshold_sigma = 3.0;
    OutputCurveMode output_curve = OutputCurveMode::Expo;
    double actual_center = 0.45;
    double actual_max = 1.0;
    double actual_expo = 0.30;
    PositionModel position_model = PositionModel::Integrator;
    double gimbal_frequency_hz = 5.0;
    double gimbal_damping_ratio = 1.15;
    double gimbal_input_impulse = 1.0;
    double gimbal_static_friction = 0.0;
    double gimbal_dynamic_friction = 0.0;
    double gimbal_edge_bumper = 0.0;
    bool gimbal_antiwindup_enabled = true;
    double gimbal_antiwindup_start = 0.92;
    double gimbal_antiwindup_min_gain = 0.10;
    InputGainMode input_gain_mode = InputGainMode::Flat;
    double adaptive_slow_gain = 0.65;
    double adaptive_fast_gain = 1.60;
    double adaptive_speed_low = 120.0;
    double adaptive_speed_high = 1800.0;
    double adaptive_curve = 1.0;
    double adaptive_tracker_ms = 35.0;
    GateShape gate_shape = GateShape::Axis;
    double diagonal_scale = 1.0;
    bool return_enabled = false;
    bool return_enabled_explicit = false;
    double return_rate = 0.0;
    double return_idle_ms = 0.0;
    bool constant_return_enabled = false;
    double constant_return_rate = 0.0;
    bool elastic_return_enabled = false;
    ElasticReturnMode elastic_return_mode = ElasticReturnMode::Progressive;
    double elastic_return_coefficient = 0.0;
    double elastic_return_curve = 0.0;
    StickShapeCurve output_shape;
    StickShapeCurve return_shape;
    bool invert_roll = false;
    bool invert_pitch = false;
    bool swap_axes = false;
    ControlMode control_mode = ControlMode::DirectMouse;
    MouseAimProfile mouse_aim;
    bool mouse_right_stick_enabled = true;
    std::string mouse_right_device_token = "auto";
    std::string mouse_left_device_token;
    MouseLeftStickProfile mouse_left;
    RightMouseLeftStickProfile right_mouse_left;
    KeyboardLeftStickProfile keyboard_left;
    bool log_csv = false;
    std::string log_path = "logs\\trainer-profile.csv";
    int log_every_n_frames = 1;
};

constexpr double kTrainerGainReferenceHz = 1000.0;
constexpr int kTrainerMapperReferenceHz = 1000;

double ClampDouble(double value, double lo, double hi) {
    return std::max(lo, std::min(value, hi));
}

double LerpDouble(double a, double b, double t) {
    return a + ((b - a) * t);
}

double ApplyStickShapeCurve(double norm, const StickShapeCurve& shape) {
    norm = ClampDouble(norm, 0.0, 1.0);
    if (shape.nodes.empty()) {
        return norm;
    }
    constexpr double kPi = 3.14159265358979323846;
    double sum_k = 0.0;
    double sum_ky = 0.0;
    double max_k = 0.0;
    for (const auto& node : shape.nodes) {
        const double width = std::max(kStickShapeMinEffectiveWidth, std::min(kStickShapeMaxWidth, node.width));
        const double dx = norm - node.x;
        if (std::abs(dx) >= width) {
            continue;
        }
        const double k = 0.5 * (1.0 + std::cos((kPi * dx) / width));
        const double y = ClampDouble(node.y, 0.0, 1.0);
        sum_k += k;
        sum_ky += k * y;
        if (k > max_k) {
            max_k = k;
        }
    }
    if (sum_k <= 0.0) {
        return norm;
    }
    const double weighted_y = sum_ky / sum_k;
    const double blend = ClampDouble(max_k, 0.0, 1.0);
    return ClampDouble(blend * weighted_y + (1.0 - blend) * norm, 0.0, 1.0);
}

bool StickShapeCurveIsValid(const StickShapeCurve& shape) {
    for (const auto& node : shape.nodes) {
        if (node.x < 0.0 || node.x > 1.0) {
            return false;
        }
        if (node.y < 0.0 || node.y > 1.0) {
            return false;
        }
        if (node.width < kStickShapeMinWidth || node.width > kStickShapeMaxWidth) {
            return false;
        }
    }
    return true;
}

std::string DescribeStickShapeNodes(const StickShapeCurve& shape) {
    if (shape.nodes.empty()) {
        return "linear";
    }
    std::string out;
    out.reserve(shape.nodes.size() * 24);
    for (size_t i = 0; i < shape.nodes.size(); ++i) {
        char buf[64];
        std::snprintf(buf,
                      sizeof(buf),
                      "%s(%.3f,%.3f,w=%.3f)",
                      i == 0 ? "" : " ",
                      shape.nodes[i].x,
                      shape.nodes[i].y,
                      shape.nodes[i].width);
        out += buf;
    }
    return out;
}

double MoveTowardDouble(double current, double target, double step) {
    if (step <= 0.0 || current == target) {
        return target;
    }
    if (current < target) {
        return std::min(target, current + step);
    }
    return std::max(target, current - step);
}

double TrainerRateGainScale(int frame_rate_hz) {
    return static_cast<double>(frame_rate_hz) / kTrainerGainReferenceHz;
}

const char* ControlModeName(ControlMode mode) {
    switch (mode) {
    case ControlMode::DirectMouse:
        return "direct_mouse";
    case ControlMode::DroneMouseAim:
        return "drone_mouse_aim";
    }
    return "direct_mouse";
}

bool ParseControlModeName(const std::string& text, ControlMode* mode) {
    const std::string value = ToLowerAscii(text);
    if (value.empty() || value == "direct" || value == "direct_mouse") {
        *mode = ControlMode::DirectMouse;
        return true;
    }
    if (value == "drone_mouse_aim" || value == "mouse_aim" || value == "war_thunder") {
        *mode = ControlMode::DroneMouseAim;
        return true;
    }
    return false;
}

const char* TrainerResolutionModeName(TrainerResolutionMode mode) {
    switch (mode) {
    case TrainerResolutionMode::Legacy:
        return "legacy";
    case TrainerResolutionMode::Gx12_2x:
        return "gx12_2x";
    }
    return "legacy";
}

bool ParseTrainerResolutionModeName(const std::string& text, TrainerResolutionMode* mode) {
    const std::string value = ToLowerAscii(text);
    if (value.empty() || value == "legacy") {
        *mode = TrainerResolutionMode::Legacy;
        return true;
    }
    if (value == "gx12_2x" || value == "gx12-2x" || value == "2x") {
        *mode = TrainerResolutionMode::Gx12_2x;
        return true;
    }
    return false;
}

const char* PositionModelName(PositionModel model) {
    switch (model) {
    case PositionModel::Integrator:
        return "integrator";
    case PositionModel::DynamicGimbal:
        return "dynamic_gimbal";
    }
    return "integrator";
}

bool ParsePositionModelName(const std::string& text, PositionModel* model) {
    const std::string value = ToLowerAscii(text);
    if (value.empty() || value == "integrator" || value == "classic" || value == "legacy") {
        *model = PositionModel::Integrator;
        return true;
    }
    if (value == "dynamic_gimbal" || value == "gimbal" || value == "spring_damper") {
        *model = PositionModel::DynamicGimbal;
        return true;
    }
    return false;
}

const char* InputGainModeName(InputGainMode mode) {
    switch (mode) {
    case InputGainMode::Flat:
        return "flat";
    case InputGainMode::Adaptive:
        return "adaptive";
    }
    return "flat";
}

bool ParseInputGainModeName(const std::string& text, InputGainMode* mode) {
    const std::string value = ToLowerAscii(text);
    if (value.empty() || value == "flat" || value == "off" || value == "linear") {
        *mode = InputGainMode::Flat;
        return true;
    }
    if (value == "adaptive" || value == "speed_adaptive" || value == "accel") {
        *mode = InputGainMode::Adaptive;
        return true;
    }
    return false;
}

const char* InputFilterModeName(InputFilterMode mode) {
    switch (mode) {
    case InputFilterMode::Off:
        return "off";
    case InputFilterMode::Smoothing:
        return "smoothing";
    case InputFilterMode::OneEuro:
        return "one_euro";
    }
    return "off";
}

bool ParseInputFilterModeName(const std::string& text, InputFilterMode* mode) {
    const std::string value = ToLowerAscii(text);
    if (value.empty() || value == "off" || value == "none") {
        *mode = InputFilterMode::Off;
        return true;
    }
    if (value == "smoothing" || value == "legacy" || value == "lowpass") {
        *mode = InputFilterMode::Smoothing;
        return true;
    }
    if (value == "one_euro" || value == "one-euro" || value == "1euro" || value == "1_euro") {
        *mode = InputFilterMode::OneEuro;
        return true;
    }
    return false;
}

const char* OutputCurveModeName(OutputCurveMode mode) {
    switch (mode) {
    case OutputCurveMode::Expo:
        return "expo";
    case OutputCurveMode::Nodes:
        return "nodes";
    case OutputCurveMode::Actual:
        return "actual";
    }
    return "expo";
}

bool ParseOutputCurveModeName(const std::string& text, OutputCurveMode* mode) {
    const std::string value = ToLowerAscii(text);
    if (value.empty() || value == "expo" || value == "legacy") {
        *mode = OutputCurveMode::Expo;
        return true;
    }
    if (value == "nodes" || value == "node" || value == "shape" || value == "custom") {
        *mode = OutputCurveMode::Nodes;
        return true;
    }
    if (value == "actual" || value == "actual_rates" || value == "actual-rates") {
        *mode = OutputCurveMode::Actual;
        return true;
    }
    return false;
}

const char* GateShapeName(GateShape shape) {
    switch (shape) {
    case GateShape::Axis:
        return "axis";
    case GateShape::Circle:
        return "circle";
    case GateShape::Octagon:
        return "octagon";
    case GateShape::Square:
        return "square";
    }
    return "axis";
}

bool ParseGateShapeName(const std::string& text, GateShape* shape) {
    const std::string value = ToLowerAscii(text);
    if (value.empty() || value == "axis" || value == "independent") {
        *shape = GateShape::Axis;
        return true;
    }
    if (value == "circle" || value == "circular") {
        *shape = GateShape::Circle;
        return true;
    }
    if (value == "octagon" || value == "octagonal") {
        *shape = GateShape::Octagon;
        return true;
    }
    if (value == "square") {
        *shape = GateShape::Square;
        return true;
    }
    return false;
}

const char* ElasticReturnModeName(ElasticReturnMode mode) {
    switch (mode) {
    case ElasticReturnMode::Linear:
        return "linear";
    case ElasticReturnMode::Progressive:
        return "progressive";
    case ElasticReturnMode::Smoothstep:
        return "smoothstep";
    case ElasticReturnMode::Expo:
        return "expo";
    }
    return "progressive";
}

bool ParseElasticReturnModeName(const std::string& text, ElasticReturnMode* mode) {
    const std::string value = ToLowerAscii(text);
    if (value.empty() || value == "progressive" || value == "power" || value == "legacy") {
        *mode = ElasticReturnMode::Progressive;
        return true;
    }
    if (value == "linear" || value == "hooke" || value == "spring") {
        *mode = ElasticReturnMode::Linear;
        return true;
    }
    if (value == "smoothstep" || value == "smooth" || value == "s_curve" || value == "s-curve") {
        *mode = ElasticReturnMode::Smoothstep;
        return true;
    }
    if (value == "expo" || value == "exponential" || value == "edge") {
        *mode = ElasticReturnMode::Expo;
        return true;
    }
    return false;
}

double ElasticReturnCurveScale(double norm, ElasticReturnMode mode, double curve) {
    norm = ClampDouble(norm, 0.0, 1.0);
    curve = std::max(0.0, curve);

    switch (mode) {
    case ElasticReturnMode::Linear:
        return 1.0;

    case ElasticReturnMode::Progressive:
        if (curve <= 0.0) {
            return 1.0;
        }
        return std::pow(norm, curve);

    case ElasticReturnMode::Smoothstep: {
        const double smooth = norm * norm * (3.0 - (2.0 * norm));
        if (curve <= 0.0) {
            return smooth;
        }
        return std::pow(smooth, 1.0 + curve);
    }

    case ElasticReturnMode::Expo: {
        const double k = 1.0 + curve;
        const double denom = std::exp(k) - 1.0;
        if (denom <= 0.0) {
            return norm;
        }
        return (std::exp(k * norm) - 1.0) / denom;
    }
    }

    return 1.0;
}

double ElasticReturnRatePerSecond(double abs_position,
                                  int max_output,
                                  double coefficient,
                                  ElasticReturnMode mode,
                                  double curve) {
    const double max_position = std::max(1.0, static_cast<double>(max_output));
    abs_position = ClampDouble(std::abs(abs_position), 0.0, max_position);
    coefficient = std::max(0.0, coefficient);
    if (abs_position <= 0.0 || coefficient <= 0.0) {
        return 0.0;
    }
    const double norm = abs_position / max_position;
    return abs_position * coefficient * ElasticReturnCurveScale(norm, mode, curve);
}

double ShapedElasticReturnRatePerSecond(double abs_position,
                                        int max_output,
                                        double coefficient,
                                        ElasticReturnMode mode,
                                        double curve,
                                        const StickShapeCurve& return_shape) {
    const double max_position = std::max(1.0, static_cast<double>(max_output));
    abs_position = ClampDouble(std::abs(abs_position), 0.0, max_position);
    coefficient = std::max(0.0, coefficient);
    if (abs_position <= 0.0 || coefficient <= 0.0) {
        return 0.0;
    }
    if (return_shape.enabled) {
        const double norm = abs_position / max_position;
        return max_position * coefficient * ApplyStickShapeCurve(norm, return_shape);
    }
    return ElasticReturnRatePerSecond(abs_position, max_output, coefficient, mode, curve);
}

double MedianOfVector(std::vector<double> values) {
    if (values.empty()) {
        return 0.0;
    }
    const size_t mid = values.size() / 2;
    std::nth_element(values.begin(), values.begin() + mid, values.end());
    double median = values[mid];
    if ((values.size() % 2U) == 0U) {
        std::nth_element(values.begin(), values.begin() + mid - 1U, values.end());
        median = (median + values[mid - 1U]) * 0.5;
    }
    return median;
}

double ApplyHampelAxis(double sample,
                       HampelAxisState* state,
                       int window,
                       double threshold_sigma,
                       bool* replaced = nullptr) {
    if (replaced) {
        *replaced = false;
    }
    if (!state) {
        return sample;
    }
    window = std::max(3, std::min(15, window));
    if ((window % 2) == 0) {
        --window;
    }
    threshold_sigma = std::max(0.0, threshold_sigma);

    state->samples[static_cast<size_t>(state->next)] = sample;
    state->next = (state->next + 1) % window;
    state->count = std::min(window, state->count + 1);
    if (state->count < window || threshold_sigma <= 0.0) {
        return sample;
    }

    std::vector<double> values;
    values.reserve(static_cast<size_t>(state->count));
    for (int i = 0; i < state->count; ++i) {
        values.push_back(state->samples[static_cast<size_t>(i)]);
    }
    const double median = MedianOfVector(values);
    for (double& value : values) {
        value = std::abs(value - median);
    }
    const double mad = MedianOfVector(values);
    const double sigma = 1.4826 * mad;
    if (sigma <= 0.000001) {
        return sample;
    }
    const bool is_spike = std::abs(sample - median) > threshold_sigma * sigma;
    if (replaced) {
        *replaced = is_spike;
    }
    return is_spike ? median : sample;
}

void UpdateDespikeCounters(RightStickSharedState* state, double dt, bool discarded_sample) {
    if (!state) {
        return;
    }
    dt = std::max(0.0, dt);
    state->despike_recent_elapsed_s += dt;
    while (state->despike_recent_elapsed_s >= 1.0) {
        state->despike_recent_elapsed_s -= 1.0;
        state->despike_recent_bucket =
            (state->despike_recent_bucket + 1) %
            static_cast<int>(state->despike_recent_buckets.size());
        state->despike_recent_count -= state->despike_recent_buckets[static_cast<size_t>(state->despike_recent_bucket)];
        state->despike_recent_buckets[static_cast<size_t>(state->despike_recent_bucket)] = 0;
    }
    if (discarded_sample) {
        ++state->despike_total_count;
        ++state->despike_recent_count;
        ++state->despike_recent_buckets[static_cast<size_t>(state->despike_recent_bucket)];
    }
}

double LowPassAlpha(double cutoff_hz, double dt) {
    cutoff_hz = std::max(0.000001, cutoff_hz);
    dt = std::max(0.000001, dt);
    const double tau = 1.0 / (2.0 * 3.14159265358979323846 * cutoff_hz);
    return 1.0 / (1.0 + (tau / dt));
}

double ApplyOneEuroAxis(double sample,
                        double dt,
                        OneEuroAxisState* state,
                        const TrainerProfile& profile) {
    if (!state) {
        return sample;
    }
    dt = std::max(0.000001, dt);
    if (!state->initialized) {
        state->initialized = true;
        state->value = sample;
        state->derivative = 0.0;
        return sample;
    }

    const double raw_derivative = (sample - state->value) / dt;
    const double d_alpha = LowPassAlpha(profile.one_euro_dcutoff_hz, dt);
    state->derivative += (raw_derivative - state->derivative) * d_alpha;
    const double cutoff = std::max(0.05, profile.one_euro_min_cutoff_hz) +
                          (std::max(0.0, profile.one_euro_beta) *
                           std::abs(state->derivative));
    const double alpha = LowPassAlpha(cutoff, dt);
    state->value += (sample - state->value) * alpha;
    return state->value;
}

void ApplyRightStickInputPreprocessors(double* roll_source,
                                       double* pitch_source,
                                       double dt,
                                       const TrainerProfile& profile,
                                       RightStickSharedState* state) {
    if (!roll_source || !pitch_source) {
        return;
    }
    if (!state) {
        return;
    }

    state->raw_roll_source = *roll_source;
    state->raw_pitch_source = *pitch_source;

    if (profile.despike_enabled) {
        bool roll_discarded = false;
        bool pitch_discarded = false;
        *roll_source = ApplyHampelAxis(*roll_source,
                                       &state->despike_roll,
                                       profile.despike_window,
                                       profile.despike_threshold_sigma,
                                       &roll_discarded);
        *pitch_source = ApplyHampelAxis(*pitch_source,
                                        &state->despike_pitch,
                                        profile.despike_window,
                                        profile.despike_threshold_sigma,
                                        &pitch_discarded);
        if (profile.despike_count_enabled) {
            UpdateDespikeCounters(state, dt, roll_discarded || pitch_discarded);
        }
    }
    if (profile.input_filter == InputFilterMode::OneEuro) {
        *roll_source = ApplyOneEuroAxis(*roll_source, dt, &state->one_euro_roll, profile);
        *pitch_source = ApplyOneEuroAxis(*pitch_source, dt, &state->one_euro_pitch, profile);
    }

    state->filtered_roll_source = *roll_source;
    state->filtered_pitch_source = *pitch_source;
}

double ApplyActualRatesCurve(double norm, const TrainerProfile& profile) {
    norm = ClampDouble(norm, 0.0, 1.0);
    const double center = ClampDouble(profile.actual_center, 0.0, 1.0);
    const double max_rate = ClampDouble(profile.actual_max, 0.0, 1.0);
    const double expo = ClampDouble(profile.actual_expo, 0.0, 0.95);
    const double expo_norm = norm * (1.0 - expo) + (norm * norm * norm * expo);
    return ClampDouble((center * norm) + ((max_rate - center) * expo_norm * norm),
                       0.0,
                       1.0);
}

double ShapeTrainerAxis(double value, const TrainerProfile& profile) {
    const double max_output = static_cast<double>(profile.max_output);
    const double sign = value < 0.0 ? -1.0 : 1.0;
    double magnitude = std::abs(value);
    if (magnitude <= static_cast<double>(profile.deadband)) {
        return 0.0;
    }

    magnitude = std::min(magnitude, max_output);
    double norm = 0.0;
    if (profile.deadband > 0 && profile.deadband < profile.max_output) {
        norm = (magnitude - static_cast<double>(profile.deadband)) /
               static_cast<double>(profile.max_output - profile.deadband);
    } else {
        norm = magnitude / max_output;
    }
    norm = ClampDouble(norm, 0.0, 1.0);

    double curved = 0.0;
    switch (profile.output_curve) {
    case OutputCurveMode::Nodes:
        curved = ApplyStickShapeCurve(norm, profile.output_shape);
        break;
    case OutputCurveMode::Actual:
        curved = ApplyActualRatesCurve(norm, profile);
        break;
    case OutputCurveMode::Expo:
    default:
        curved = ((1.0 - profile.expo) * norm) + (profile.expo * norm * norm * norm);
        break;
    }
    return sign * curved * max_output;
}

int ShapeTrainerPulse(double source_delta,
                      double gain,
                      bool invert,
                      double* filtered,
                      const TrainerProfile& profile) {
    double value = source_delta * gain;
    if (invert) {
        value = -value;
    }

    const double shaped = ShapeTrainerAxis(value, profile);
    if (profile.input_filter == InputFilterMode::Smoothing) {
        *filtered = (profile.smoothing * *filtered) +
                    ((1.0 - profile.smoothing) * shaped);
    } else {
        *filtered = shaped;
    }
    return QuantizeTrainerProfileOutput(*filtered, profile.resolution_mode);
}

int ShapeTrainerSpringPulse(double source_delta,
                            double gain,
                            bool invert,
                            double return_step,
                            double elastic_return_coefficient,
                            ElasticReturnMode elastic_return_mode,
                            double elastic_return_curve,
                            double dt,
                            bool apply_return,
                            double* position,
                            ElasticAxisState* elastic_state,
                            const TrainerProfile& profile) {
    (void)elastic_state;
    double input = source_delta * gain;
    if (invert) {
        input = -input;
    }

    if (apply_return) {
        const double abs_position = std::abs(*position);
        return_step += ShapedElasticReturnRatePerSecond(abs_position,
                                                        profile.max_output,
                                                        elastic_return_coefficient,
                                                        elastic_return_mode,
                                                        elastic_return_curve,
                                                        profile.return_shape) *
                       dt;
        if (*position > 0.0) {
            *position = std::max(0.0, *position - return_step);
        } else if (*position < 0.0) {
            *position = std::min(0.0, *position + return_step);
        }
    }

    *position = ClampDouble(*position + input,
                            -static_cast<double>(profile.max_output),
                            static_cast<double>(profile.max_output));

    const double shaped = ShapeTrainerAxis(*position, profile);
    return QuantizeTrainerProfileOutput(shaped, profile.resolution_mode);
}

bool RightStickNeedsPositionMapper(const TrainerProfile& profile) {
    return profile.position_model == PositionModel::DynamicGimbal ||
           profile.gate_shape != GateShape::Axis ||
           profile.return_enabled_explicit ||
           profile.return_rate > 0.0 ||
           profile.constant_return_enabled ||
           profile.constant_return_rate > 0.0 ||
           profile.elastic_return_enabled ||
           profile.elastic_return_coefficient > 0.0;
}

double AdaptiveInputGainFromTrackedSpeed(double tracked_speed,
                                         const TrainerProfile& profile) {
    const double low = std::max(0.0, profile.adaptive_speed_low);
    const double high = std::max(low + 1.0, profile.adaptive_speed_high);
    double t = ClampDouble((tracked_speed - low) / (high - low), 0.0, 1.0);
    const double curve = std::max(0.01, profile.adaptive_curve);
    if (std::abs(curve - 1.0) > 0.0001) {
        t = std::pow(t, curve);
    }
    return std::max(0.0,
                    LerpDouble(profile.adaptive_slow_gain,
                               profile.adaptive_fast_gain,
                               t));
}

double UpdateAdaptiveInputGain(double roll_source,
                               double pitch_source,
                               double dt,
                               const TrainerProfile& profile,
                               RightStickSharedState* state) {
    if (!state) {
        return 1.0;
    }
    if (profile.input_gain_mode != InputGainMode::Adaptive) {
        state->adaptive_speed = 0.0;
        state->adaptive_gain = 1.0;
        return 1.0;
    }

    dt = std::max(0.000001, dt);
    const double raw_speed = std::sqrt((roll_source * roll_source) +
                                       (pitch_source * pitch_source)) / dt;
    const double tracker_seconds = std::max(0.0, profile.adaptive_tracker_ms) / 1000.0;
    const double alpha = tracker_seconds <= 0.0
                             ? 1.0
                             : (1.0 - std::exp(-dt / tracker_seconds));
    state->adaptive_speed += (raw_speed - state->adaptive_speed) * ClampDouble(alpha, 0.0, 1.0);

    state->adaptive_gain = AdaptiveInputGainFromTrackedSpeed(state->adaptive_speed, profile);
    return state->adaptive_gain;
}

void ApplyRightStickAxisReturn(double return_step,
                               double elastic_return_coefficient,
                               ElasticReturnMode elastic_return_mode,
                               double elastic_return_curve,
                               double dt,
                               bool apply_return,
                               double* position,
                               const TrainerProfile& profile) {
    if (!apply_return || !position) {
        return;
    }

    const double abs_position = std::abs(*position);
    return_step += ShapedElasticReturnRatePerSecond(abs_position,
                                                    profile.max_output,
                                                    elastic_return_coefficient,
                                                    elastic_return_mode,
                                                    elastic_return_curve,
                                                    profile.return_shape) *
                   dt;
    if (*position > 0.0) {
        *position = std::max(0.0, *position - return_step);
    } else if (*position < 0.0) {
        *position = std::min(0.0, *position + return_step);
    }
}

void StepDynamicGimbalAxis(double input,
                           double dt,
                           double* position,
                           ElasticAxisState* state,
                           const TrainerProfile& profile,
                           bool* antiwindup_active) {
    if (!position || !state) {
        return;
    }

    dt = std::max(0.000001, dt);
    const double max_position = std::max(1.0, static_cast<double>(profile.max_output));
    const double omega = 2.0 * 3.14159265358979323846 *
                         ClampDouble(profile.gimbal_frequency_hz, 0.1, 80.0);
    const double damping = std::max(0.0, profile.gimbal_damping_ratio);

    if (profile.gimbal_antiwindup_enabled) {
        const double start = ClampDouble(profile.gimbal_antiwindup_start, 0.0, 0.999);
        const double min_gain = ClampDouble(profile.gimbal_antiwindup_min_gain, 0.0, 1.0);
        const double mag = std::abs(*position) / max_position;
        const double t = (mag - start) / std::max(0.000001, 1.0 - start);
        if (t > 0.0 && input * *position > 0.0) {
            const double clamped = ClampDouble(t, 0.0, 1.0);
            const double taper = ClampDouble(1.0 - clamped, min_gain, 1.0);
            input *= taper;
            if (antiwindup_active) {
                *antiwindup_active = true;
            }
        }
    }

    state->velocity += input * std::max(0.0, profile.gimbal_input_impulse) / dt;

    double acceleration = (-omega * omega * *position) -
                          (2.0 * damping * omega * state->velocity);

    const double edge_bumper = std::max(0.0, profile.gimbal_edge_bumper);
    if (edge_bumper > 0.0) {
        const double edge_start = max_position * 0.86;
        const double over = std::abs(*position) - edge_start;
        if (over > 0.0) {
            const double sign = *position < 0.0 ? -1.0 : 1.0;
            acceleration -= sign * edge_bumper * omega * omega * over;
        }
    }

    state->velocity += acceleration * dt;

    const double dynamic_friction = std::max(0.0, profile.gimbal_dynamic_friction);
    if (dynamic_friction > 0.0 && std::abs(state->velocity) > 0.0) {
        const double dv = dynamic_friction * max_position * dt;
        if (std::abs(state->velocity) <= dv && std::abs(input) < 0.000001) {
            state->velocity = 0.0;
        } else {
            state->velocity -= (state->velocity < 0.0 ? -dv : dv);
        }
    }

    *position += state->velocity * dt;

    const double static_friction = std::max(0.0, profile.gimbal_static_friction);
    if (static_friction > 0.0 &&
        std::abs(input) < 0.000001 &&
        std::abs(*position) <= static_friction &&
        std::abs(state->velocity) <= static_friction * omega) {
        *position = 0.0;
        state->velocity = 0.0;
    }

    if (*position > max_position) {
        *position = max_position;
        if (state->velocity > 0.0) {
            state->velocity = 0.0;
        }
    } else if (*position < -max_position) {
        *position = -max_position;
        if (state->velocity < 0.0) {
            state->velocity = 0.0;
        }
    }
}

void ApplyRadialGate(double* roll_position,
                     double* pitch_position,
                     const TrainerProfile& profile,
                     double* gate_scale) {
    if (!roll_position || !pitch_position) {
        return;
    }

    const double max_position = std::max(1.0, static_cast<double>(profile.max_output));
    *roll_position = ClampDouble(*roll_position, -max_position, max_position);
    *pitch_position = ClampDouble(*pitch_position, -max_position, max_position);
    if (gate_scale) {
        *gate_scale = 1.0;
    }
    if (profile.gate_shape == GateShape::Axis) {
        return;
    }

    const double x = *roll_position;
    const double y = *pitch_position;
    const double radius = std::sqrt((x * x) + (y * y));
    if (radius <= 0.0) {
        return;
    }

    const double unit_x = std::abs(x) / radius;
    const double unit_y = std::abs(y) / radius;
    const double square_limit = max_position / std::max(unit_x, unit_y);
    const double diagonal_scale = ClampDouble(profile.diagonal_scale, 0.0, 1.5);
    double radial_limit = max_position;

    switch (profile.gate_shape) {
    case GateShape::Axis:
    case GateShape::Circle:
        radial_limit = max_position;
        break;
    case GateShape::Octagon:
        radial_limit = max_position + ((square_limit - max_position) * 0.5 * diagonal_scale);
        break;
    case GateShape::Square:
        radial_limit = max_position + ((square_limit - max_position) * diagonal_scale);
        break;
    }

    const double scale = radius > radial_limit ? radial_limit / radius : 1.0;
    *roll_position = ClampDouble(x * scale, -max_position, max_position);
    *pitch_position = ClampDouble(y * scale, -max_position, max_position);
    if (gate_scale) {
        *gate_scale = scale;
    }
}

void ShapeRightStickPositionPulses(double roll_source,
                                   double pitch_source,
                                   double gain_scale,
                                   double return_step,
                                   double elastic_return_coefficient,
                                   double dt,
                                   bool apply_return,
                                   double* roll_position,
                                   double* pitch_position,
                                   ElasticAxisState* roll_state,
                                   ElasticAxisState* pitch_state,
                                   RightStickSharedState* shared_state,
                                   const TrainerProfile& profile,
                                   int* roll_pulse,
                                   int* pitch_pulse) {
    ApplyRightStickInputPreprocessors(&roll_source,
                                      &pitch_source,
                                      dt,
                                      profile,
                                      shared_state);
    const double input_gain = UpdateAdaptiveInputGain(roll_source,
                                                      pitch_source,
                                                      dt,
                                                      profile,
                                                      shared_state);
    double roll_input = roll_source * input_gain * profile.roll_gain * gain_scale;
    double pitch_input = pitch_source * input_gain * profile.pitch_gain * gain_scale;
    if (profile.invert_roll) {
        roll_input = -roll_input;
    }
    if (profile.invert_pitch) {
        pitch_input = -pitch_input;
    }

    if (apply_return) {
        ApplyRightStickAxisReturn(return_step,
                                  elastic_return_coefficient,
                                  profile.elastic_return_mode,
                                  profile.elastic_return_curve,
                                  dt,
                                  true,
                                  roll_position,
                                  profile);
        ApplyRightStickAxisReturn(return_step,
                                  elastic_return_coefficient,
                                  profile.elastic_return_mode,
                                  profile.elastic_return_curve,
                                  dt,
                                  true,
                                  pitch_position,
                                  profile);
    }

    const double max_position = std::max(1.0, static_cast<double>(profile.max_output));
    if (profile.position_model == PositionModel::DynamicGimbal) {
        bool antiwindup_active = false;
        StepDynamicGimbalAxis(roll_input, dt, roll_position, roll_state, profile, &antiwindup_active);
        StepDynamicGimbalAxis(pitch_input, dt, pitch_position, pitch_state, profile, &antiwindup_active);
        if (shared_state) {
            shared_state->gimbal_antiwindup_active = antiwindup_active;
        }
    } else {
        if (roll_state) {
            roll_state->velocity = 0.0;
        }
        if (pitch_state) {
            pitch_state->velocity = 0.0;
        }
        if (shared_state) {
            shared_state->gimbal_antiwindup_active = false;
        }
        *roll_position = ClampDouble(*roll_position + roll_input, -max_position, max_position);
        *pitch_position = ClampDouble(*pitch_position + pitch_input, -max_position, max_position);
    }

    const double before_roll = *roll_position;
    const double before_pitch = *pitch_position;
    double gate_scale = 1.0;
    ApplyRadialGate(roll_position, pitch_position, profile, &gate_scale);
    if (shared_state) {
        shared_state->gate_scale = gate_scale;
    }
    if (gate_scale < 0.999) {
        if (roll_state && std::abs(before_roll) > std::abs(*roll_position)) {
            roll_state->velocity *= gate_scale;
        }
        if (pitch_state && std::abs(before_pitch) > std::abs(*pitch_position)) {
            pitch_state->velocity *= gate_scale;
        }
    }

    if (roll_pulse) {
        *roll_pulse = QuantizeTrainerProfileOutput(
            ShapeTrainerAxis(*roll_position, profile),
            profile.resolution_mode);
    }
    if (pitch_pulse) {
        *pitch_pulse = QuantizeTrainerProfileOutput(
            ShapeTrainerAxis(*pitch_position, profile),
            profile.resolution_mode);
    }
}

TrainerProfile MakeMouseLeftYawMapperProfile(const TrainerProfile& profile) {
    TrainerProfile yaw_profile = profile;
    yaw_profile.roll_gain = 0.0;
    yaw_profile.pitch_gain = profile.mouse_left.yaw_gain;
    yaw_profile.max_output = std::max(1, profile.mouse_left.yaw_pulse);
    yaw_profile.deadband = std::max(0, profile.mouse_left.yaw_deadband);
    yaw_profile.smoothing = profile.mouse_left.yaw_smoothing;
    yaw_profile.input_filter = profile.mouse_left.yaw_input_filter;
    yaw_profile.one_euro_min_cutoff_hz = profile.mouse_left.yaw_one_euro_min_cutoff_hz;
    yaw_profile.one_euro_beta = profile.mouse_left.yaw_one_euro_beta;
    yaw_profile.one_euro_dcutoff_hz = profile.mouse_left.yaw_one_euro_dcutoff_hz;
    yaw_profile.despike_enabled = profile.mouse_left.yaw_despike_enabled;
    yaw_profile.despike_count_enabled = profile.mouse_left.yaw_despike_count_enabled;
    yaw_profile.despike_window = profile.mouse_left.yaw_despike_window;
    yaw_profile.despike_threshold_sigma = profile.mouse_left.yaw_despike_threshold_sigma;
    yaw_profile.output_curve = profile.mouse_left.yaw_output_curve;
    yaw_profile.expo = profile.mouse_left.yaw_expo;
    yaw_profile.actual_center = profile.mouse_left.yaw_actual_center;
    yaw_profile.actual_max = profile.mouse_left.yaw_actual_max;
    yaw_profile.actual_expo = profile.mouse_left.yaw_actual_expo;
    yaw_profile.position_model = profile.mouse_left.yaw_position_model;
    yaw_profile.gimbal_frequency_hz = profile.mouse_left.yaw_gimbal_frequency_hz;
    yaw_profile.gimbal_damping_ratio = profile.mouse_left.yaw_gimbal_damping_ratio;
    yaw_profile.gimbal_input_impulse = profile.mouse_left.yaw_gimbal_input_impulse;
    yaw_profile.gimbal_static_friction = profile.mouse_left.yaw_gimbal_static_friction;
    yaw_profile.gimbal_dynamic_friction = profile.mouse_left.yaw_gimbal_dynamic_friction;
    yaw_profile.gimbal_edge_bumper = profile.mouse_left.yaw_gimbal_edge_bumper;
    yaw_profile.gimbal_antiwindup_enabled = profile.mouse_left.yaw_gimbal_antiwindup_enabled;
    yaw_profile.gimbal_antiwindup_start = profile.mouse_left.yaw_gimbal_antiwindup_start;
    yaw_profile.gimbal_antiwindup_min_gain = profile.mouse_left.yaw_gimbal_antiwindup_min_gain;
    yaw_profile.input_gain_mode = profile.mouse_left.yaw_input_gain_mode;
    yaw_profile.adaptive_slow_gain = profile.mouse_left.yaw_adaptive_slow_gain;
    yaw_profile.adaptive_fast_gain = profile.mouse_left.yaw_adaptive_fast_gain;
    yaw_profile.adaptive_speed_low = profile.mouse_left.yaw_adaptive_speed_low;
    yaw_profile.adaptive_speed_high = profile.mouse_left.yaw_adaptive_speed_high;
    yaw_profile.adaptive_curve = profile.mouse_left.yaw_adaptive_curve;
    yaw_profile.adaptive_tracker_ms = profile.mouse_left.yaw_adaptive_tracker_ms;
    yaw_profile.gate_shape = profile.mouse_left.yaw_gate_shape;
    yaw_profile.diagonal_scale = profile.mouse_left.yaw_diagonal_scale;
    yaw_profile.return_enabled_explicit = true;
    yaw_profile.return_enabled = profile.mouse_left.yaw_return_enabled;
    yaw_profile.return_rate = profile.mouse_left.yaw_return_rate;
    yaw_profile.return_idle_ms = profile.mouse_left.yaw_return_idle_ms;
    yaw_profile.constant_return_enabled = profile.mouse_left.yaw_constant_return_enabled;
    yaw_profile.constant_return_rate = profile.mouse_left.yaw_constant_return_rate;
    yaw_profile.elastic_return_enabled = profile.mouse_left.yaw_elastic_return_enabled;
    yaw_profile.elastic_return_mode = profile.mouse_left.yaw_elastic_return_mode;
    yaw_profile.elastic_return_coefficient = profile.mouse_left.yaw_elastic_return_coefficient;
    yaw_profile.elastic_return_curve = profile.mouse_left.yaw_elastic_return_curve;
    yaw_profile.output_shape = profile.mouse_left.yaw_output_shape;
    yaw_profile.return_shape = profile.mouse_left.yaw_return_shape;
    yaw_profile.invert_roll = false;
    yaw_profile.invert_pitch = profile.mouse_left.invert_yaw;
    yaw_profile.swap_axes = false;
    return yaw_profile;
}

int ShapeMouseLeftYawWithMapper(double yaw_source,
                                double gain_scale,
                                double return_step,
                                double elastic_return_coefficient,
                                double dt,
                                bool apply_return,
                                double* yaw_position,
                                double* dummy_position,
                                ElasticAxisState* yaw_state,
                                ElasticAxisState* dummy_state,
                                RightStickSharedState* shared_state,
                                const TrainerProfile& profile,
                                double* filtered_yaw) {
    TrainerProfile yaw_profile = MakeMouseLeftYawMapperProfile(profile);
    int dummy_pulse = 0;
    int yaw_pulse = 0;
    if (RightStickNeedsPositionMapper(yaw_profile)) {
        ShapeRightStickPositionPulses(0.0,
                                      yaw_source,
                                      gain_scale,
                                      return_step,
                                      elastic_return_coefficient,
                                      dt,
                                      apply_return,
                                      dummy_position,
                                      yaw_position,
                                      dummy_state,
                                      yaw_state,
                                      shared_state,
                                      yaw_profile,
                                      &dummy_pulse,
                                      &yaw_pulse);
        if (filtered_yaw) {
            *filtered_yaw = *yaw_position;
        }
        return yaw_pulse;
    }

    double filtered_dummy_source = 0.0;
    double filtered_yaw_source = yaw_source;
    ApplyRightStickInputPreprocessors(&filtered_dummy_source,
                                      &filtered_yaw_source,
                                      dt,
                                      yaw_profile,
                                      shared_state);
    const double input_gain = UpdateAdaptiveInputGain(filtered_dummy_source,
                                                      filtered_yaw_source,
                                                      dt,
                                                      yaw_profile,
                                                      shared_state);
    return ShapeTrainerPulse(filtered_yaw_source,
                             yaw_profile.pitch_gain * gain_scale * input_gain,
                             yaw_profile.invert_pitch,
                             filtered_yaw,
                             yaw_profile);
}

double ShapeMouseAimError(double reticle, const MouseAimProfile& aim) {
    const double sign = reticle < 0.0 ? -1.0 : 1.0;
    const double magnitude = std::abs(reticle);
    if (magnitude <= aim.reticle_deadband) {
        return 0.0;
    }
    const double usable = std::max(1.0, aim.reticle_limit - aim.reticle_deadband);
    return sign * ClampDouble((magnitude - aim.reticle_deadband) / usable, 0.0, 1.0);
}

int SlewMouseAimAxis(double target, double step, double* current) {
    *current = MoveTowardDouble(*current, target, step);
    return ClampTrainerPulse(static_cast<int64_t>(std::lround(*current)));
}

void UpdateDroneMouseAim(int64_t dx,
                         int64_t dy,
                         double dt,
                         const MouseAimProfile& aim,
                         MouseAimState* state,
                         int* roll,
                         int* pitch,
                         int* yaw) {
    const double x_dir = aim.invert_x ? -1.0 : 1.0;
    const double y_dir = aim.invert_y ? -1.0 : 1.0;
    state->reticle_x = ClampDouble(state->reticle_x + (static_cast<double>(dx) * aim.sensitivity_x * x_dir),
                                   -aim.reticle_limit,
                                   aim.reticle_limit);
    state->reticle_y = ClampDouble(state->reticle_y + (static_cast<double>(-dy) * aim.sensitivity_y * y_dir),
                                   -aim.reticle_limit,
                                   aim.reticle_limit);

    if (aim.reticle_return_rate > 0.0) {
        const double return_step = aim.reticle_return_rate * dt;
        state->reticle_x = MoveTowardDouble(state->reticle_x, 0.0, return_step);
        state->reticle_y = MoveTowardDouble(state->reticle_y, 0.0, return_step);
    }

    const double x_error = ShapeMouseAimError(state->reticle_x, aim);
    const double y_error = ShapeMouseAimError(state->reticle_y, aim);
    const double roll_target = ClampDouble(x_error * aim.roll_gain * static_cast<double>(aim.roll_max),
                                           -static_cast<double>(aim.roll_max),
                                           static_cast<double>(aim.roll_max));
    const double yaw_target = ClampDouble(x_error * aim.yaw_gain * static_cast<double>(aim.yaw_max),
                                          -static_cast<double>(aim.yaw_max),
                                          static_cast<double>(aim.yaw_max));
    const double pitch_target = ClampDouble(y_error * aim.pitch_gain * static_cast<double>(aim.pitch_max),
                                            -static_cast<double>(aim.pitch_max),
                                            static_cast<double>(aim.pitch_max));

    const double smoothing = ClampDouble(aim.output_smoothing, 0.0, 0.95);
    const double step = std::max(0.0, aim.slew_rate * dt);
    const double smoothed_roll = (smoothing * state->roll) + ((1.0 - smoothing) * roll_target);
    const double smoothed_yaw = (smoothing * state->yaw) + ((1.0 - smoothing) * yaw_target);
    const double smoothed_pitch = (smoothing * state->pitch) + ((1.0 - smoothing) * pitch_target);
    *roll = SlewMouseAimAxis(smoothed_roll, step, &state->roll);
    *pitch = SlewMouseAimAxis(smoothed_pitch, step, &state->pitch);
    *yaw = SlewMouseAimAxis(smoothed_yaw, step, &state->yaw);
}

int ParseVirtualKeyName(const std::string& key_name) {
    const std::string key = ToLowerAscii(key_name);
    if (key.empty() || key == "none" || key == "off") {
        return 0;
    }
    if (key.size() == 1) {
        const unsigned char ch = static_cast<unsigned char>(key[0]);
        if (std::isalpha(ch)) {
            return std::toupper(ch);
        }
        if (std::isdigit(ch)) {
            return ch;
        }
    }
    if (key == "space" || key == "spacebar") return VK_SPACE;
    if (key == "esc" || key == "escape") return VK_ESCAPE;
    if (key == "shift") return VK_SHIFT;
    if (key == "ctrl" || key == "control") return VK_CONTROL;
    if (key == "alt") return VK_MENU;
    if (key == "tab") return VK_TAB;
    if (key == "enter" || key == "return") return VK_RETURN;
    if (key == "backspace") return VK_BACK;
    if (key == "up") return VK_UP;
    if (key == "down") return VK_DOWN;
    if (key == "left") return VK_LEFT;
    if (key == "right") return VK_RIGHT;
    if (key.size() >= 2 && key[0] == 'f') {
        char* end = nullptr;
        const long fkey = std::strtol(key.c_str() + 1, &end, 10);
        if (end && *end == '\0' && fkey >= 1 && fkey <= 24) {
            return VK_F1 + static_cast<int>(fkey) - 1;
        }
    }
    if (key.rfind("vk", 0) == 0) {
        char* end = nullptr;
        const long vk = std::strtol(key.c_str() + 2, &end, 10);
        if (end && *end == '\0' && vk >= 1 && vk <= 255) {
            return static_cast<int>(vk);
        }
    }
    return -1;
}

bool ResolveKeyboardLeftStickKeys(KeyboardLeftStickProfile* keyboard) {
    keyboard->throttle_up_vk = ParseVirtualKeyName(keyboard->throttle_up_key);
    keyboard->throttle_down_vk = ParseVirtualKeyName(keyboard->throttle_down_key);
    keyboard->yaw_left_vk = ParseVirtualKeyName(keyboard->yaw_left_key);
    keyboard->yaw_right_vk = ParseVirtualKeyName(keyboard->yaw_right_key);
    keyboard->throttle_cut_vk = ParseVirtualKeyName(keyboard->throttle_cut_key);
    return keyboard->throttle_up_vk >= 0 &&
           keyboard->throttle_down_vk >= 0 &&
           keyboard->yaw_left_vk >= 0 &&
           keyboard->yaw_right_vk >= 0 &&
           keyboard->throttle_cut_vk >= 0;
}

bool ResolveTrainerProfileKeys(TrainerProfile* profile) {
    profile->stop_key_vk = ParseVirtualKeyName(profile->stop_key);
    profile->freeze_key_vk = ParseVirtualKeyName(profile->freeze_key);
    return profile->stop_key_vk > 0 && profile->freeze_key_vk > 0;
}

bool ValidateTrainerProfile(const TrainerProfile& profile) {
    if (profile.port.empty()) {
        std::fprintf(stderr, "profile error: trainer.port must not be empty.\n");
        return false;
    }
    if (profile.seconds < 0 || profile.seconds > kTrainerProfileMaxSeconds) {
        std::fprintf(stderr,
                     "profile error: trainer.seconds must be 0..%d, where 0 means run until stopped.\n",
                     kTrainerProfileMaxSeconds);
        return false;
    }
    if (profile.frame_rate_hz <= 0 || profile.frame_rate_hz > 8000) {
        std::fprintf(stderr, "profile error: trainer.frame_rate_hz must be 1..8000.\n");
        return false;
    }
    if (profile.resolution_mode == TrainerResolutionMode::Gx12_2x) {
        const bool has_supported_mouse_trainer_axis =
            profile.mouse_right_stick_enabled || profile.mouse_left.enabled;
        if (profile.control_mode != ControlMode::DirectMouse ||
            !has_supported_mouse_trainer_axis ||
            profile.keyboard_left.enabled) {
            std::fprintf(stderr,
                         "profile error: gx12_2x resolution currently requires direct_mouse with right-stick mouse and/or second-mouse left-stick enabled; keyboard-left and reticle-aim modes are still legacy.\n");
            return false;
        }
    }
    if (profile.stop_key_vk <= 0) {
        std::fprintf(stderr,
                     "profile error: safety.stop_key must be a valid key name, for example Esc, F12, or Space.\n");
        return false;
    }
    if (profile.freeze_key_vk <= 0) {
        std::fprintf(stderr,
                     "profile error: safety.freeze_key must be a valid key name, for example F2, F12, or Space.\n");
        return false;
    }
    if (profile.roll_gain < 0.0 || profile.roll_gain > 5000.0 ||
        profile.pitch_gain < 0.0 || profile.pitch_gain > 5000.0) {
        std::fprintf(stderr, "profile error: mapper gains must be 0..5000.\n");
        return false;
    }
    const int max_trainer_output = kTrainerProfileDomainLimit;
    if (profile.max_output <= 0 || profile.max_output > max_trainer_output) {
        std::fprintf(stderr,
                     "profile error: mapper.max_output must be 1..%d.\n",
                     max_trainer_output);
        return false;
    }
    if (profile.deadband < 0 || profile.deadband >= profile.max_output) {
        std::fprintf(stderr, "profile error: mapper.deadband must be 0..max_output-1.\n");
        return false;
    }
    if (profile.expo < 0.0 || profile.expo > 1.0) {
        std::fprintf(stderr, "profile error: mapper.expo must be 0.0..1.0.\n");
        return false;
    }
    if (profile.smoothing < 0.0 || profile.smoothing >= 1.0) {
        std::fprintf(stderr, "profile error: mapper.smoothing must be >=0.0 and <1.0.\n");
        return false;
    }
    if (profile.one_euro_min_cutoff_hz < 0.05 || profile.one_euro_min_cutoff_hz > 120.0) {
        std::fprintf(stderr, "profile error: mapper.one_euro_min_cutoff_hz must be 0.05..120.\n");
        return false;
    }
    if (profile.one_euro_beta < 0.0 || profile.one_euro_beta > 2.0) {
        std::fprintf(stderr, "profile error: mapper.one_euro_beta must be 0..2.\n");
        return false;
    }
    if (profile.one_euro_dcutoff_hz < 0.1 || profile.one_euro_dcutoff_hz > 120.0) {
        std::fprintf(stderr, "profile error: mapper.one_euro_dcutoff_hz must be 0.1..120.\n");
        return false;
    }
    if (profile.despike_window < 3 || profile.despike_window > 15 || (profile.despike_window % 2) == 0) {
        std::fprintf(stderr, "profile error: mapper.despike_window must be an odd value from 3..15.\n");
        return false;
    }
    if (profile.despike_threshold_sigma <= 0.0 || profile.despike_threshold_sigma > 20.0) {
        std::fprintf(stderr, "profile error: mapper.despike_threshold_sigma must be >0 and <=20.\n");
        return false;
    }
    if (profile.actual_center < 0.0 || profile.actual_center > 1.0 ||
        profile.actual_max < 0.0 || profile.actual_max > 1.0 ||
        profile.actual_expo < 0.0 || profile.actual_expo > 0.95) {
        std::fprintf(stderr, "profile error: mapper actual rates fields must satisfy center/max 0..1 and expo 0..0.95.\n");
        return false;
    }
    if (profile.gimbal_frequency_hz < 0.1 || profile.gimbal_frequency_hz > 80.0) {
        std::fprintf(stderr, "profile error: mapper.gimbal_frequency_hz must be 0.1..80.\n");
        return false;
    }
    if (profile.gimbal_damping_ratio < 0.0 || profile.gimbal_damping_ratio > 5.0) {
        std::fprintf(stderr, "profile error: mapper.gimbal_damping_ratio must be 0..5.\n");
        return false;
    }
    if (profile.gimbal_input_impulse < 0.0 || profile.gimbal_input_impulse > 5.0) {
        std::fprintf(stderr, "profile error: mapper.gimbal_input_impulse must be 0..5.\n");
        return false;
    }
    if (profile.gimbal_static_friction < 0.0 || profile.gimbal_static_friction > 128.0) {
        std::fprintf(stderr, "profile error: mapper.gimbal_static_friction must be 0..128 trainer units.\n");
        return false;
    }
    if (profile.gimbal_dynamic_friction < 0.0 || profile.gimbal_dynamic_friction > 100.0) {
        std::fprintf(stderr, "profile error: mapper.gimbal_dynamic_friction must be 0..100.\n");
        return false;
    }
    if (profile.gimbal_edge_bumper < 0.0 || profile.gimbal_edge_bumper > 20.0) {
        std::fprintf(stderr, "profile error: mapper.gimbal_edge_bumper must be 0..20.\n");
        return false;
    }
    if (profile.gimbal_antiwindup_start < 0.0 || profile.gimbal_antiwindup_start >= 1.0) {
        std::fprintf(stderr, "profile error: mapper.gimbal_antiwindup_start must be >=0 and <1.\n");
        return false;
    }
    if (profile.gimbal_antiwindup_min_gain < 0.0 || profile.gimbal_antiwindup_min_gain > 1.0) {
        std::fprintf(stderr, "profile error: mapper.gimbal_antiwindup_min_gain must be 0..1.\n");
        return false;
    }
    if (profile.adaptive_slow_gain < 0.0 || profile.adaptive_slow_gain > 5.0 ||
        profile.adaptive_fast_gain < 0.0 || profile.adaptive_fast_gain > 5.0) {
        std::fprintf(stderr, "profile error: mapper adaptive gains must be 0..5.\n");
        return false;
    }
    if (profile.adaptive_speed_low < 0.0 || profile.adaptive_speed_low > 100000.0 ||
        profile.adaptive_speed_high <= profile.adaptive_speed_low ||
        profile.adaptive_speed_high > 100000.0) {
        std::fprintf(stderr, "profile error: mapper adaptive speeds must satisfy 0 <= low < high <= 100000 counts/s.\n");
        return false;
    }
    if (profile.adaptive_curve <= 0.0 || profile.adaptive_curve > 5.0) {
        std::fprintf(stderr, "profile error: mapper.adaptive_curve must be >0 and <=5.\n");
        return false;
    }
    if (profile.adaptive_tracker_ms < 0.0 || profile.adaptive_tracker_ms > 1000.0) {
        std::fprintf(stderr, "profile error: mapper.adaptive_tracker_ms must be 0..1000.\n");
        return false;
    }
    if (profile.diagonal_scale < 0.0 || profile.diagonal_scale > 1.5) {
        std::fprintf(stderr, "profile error: mapper.diagonal_scale must be 0..1.5.\n");
        return false;
    }
    if (profile.return_rate < 0.0 || profile.return_rate > 20000.0) {
        std::fprintf(stderr, "profile error: mapper.return_rate must be 0..20000 trainer units per second.\n");
        return false;
    }
    if (profile.constant_return_rate < 0.0 || profile.constant_return_rate > 20000.0) {
        std::fprintf(stderr, "profile error: mapper.constant_return_rate must be 0..20000 trainer units per second.\n");
        return false;
    }
    if (profile.elastic_return_coefficient < 0.0 || profile.elastic_return_coefficient > 100.0) {
        std::fprintf(stderr, "profile error: mapper.elastic_return_coefficient must be 0..100 per second.\n");
        return false;
    }
    if (profile.elastic_return_curve < 0.0 || profile.elastic_return_curve > 5.0) {
        std::fprintf(stderr, "profile error: mapper.elastic_return_curve must be 0..5.\n");
        return false;
    }
    if (!StickShapeCurveIsValid(profile.output_shape)) {
        std::fprintf(stderr,
                     "profile error: mapper.output_shape_nodes entries must be [x,y,width] with 0<=x<=1, 0<=y<=1, %.2f<=width<=%.2f.\n",
                     kStickShapeMinWidth,
                     kStickShapeMaxWidth);
        return false;
    }
    if (!StickShapeCurveIsValid(profile.return_shape)) {
        std::fprintf(stderr,
                     "profile error: mapper.return_shape_nodes entries must be [x,y,width] with 0<=x<=1, 0<=y<=1, %.2f<=width<=%.2f.\n",
                     kStickShapeMinWidth,
                     kStickShapeMaxWidth);
        return false;
    }
    if (profile.return_idle_ms < 0.0 || profile.return_idle_ms > 1000.0) {
        std::fprintf(stderr, "profile error: mapper.return_idle_ms must be 0..1000 milliseconds.\n");
        return false;
    }
    if (profile.mouse_aim.sensitivity_x < 0.0 || profile.mouse_aim.sensitivity_x > 50.0 ||
        profile.mouse_aim.sensitivity_y < 0.0 || profile.mouse_aim.sensitivity_y > 50.0) {
        std::fprintf(stderr, "profile error: mouse_aim sensitivity values must be 0..50.\n");
        return false;
    }
    if (profile.mouse_aim.reticle_limit <= 0.0 || profile.mouse_aim.reticle_limit > 4096.0) {
        std::fprintf(stderr, "profile error: mouse_aim.reticle_limit must be >0 and <=4096.\n");
        return false;
    }
    if (profile.mouse_aim.reticle_deadband < 0.0 ||
        profile.mouse_aim.reticle_deadband >= profile.mouse_aim.reticle_limit) {
        std::fprintf(stderr, "profile error: mouse_aim.reticle_deadband must be 0..reticle_limit-1.\n");
        return false;
    }
    if (profile.mouse_aim.reticle_return_rate < 0.0 ||
        profile.mouse_aim.reticle_return_rate > 20000.0) {
        std::fprintf(stderr, "profile error: mouse_aim.reticle_return_rate must be 0..20000.\n");
        return false;
    }
    if (profile.mouse_aim.output_smoothing < 0.0 || profile.mouse_aim.output_smoothing >= 1.0) {
        std::fprintf(stderr, "profile error: mouse_aim.output_smoothing must be >=0.0 and <1.0.\n");
        return false;
    }
    if (profile.mouse_aim.roll_gain < 0.0 || profile.mouse_aim.roll_gain > 5.0 ||
        profile.mouse_aim.yaw_gain < 0.0 || profile.mouse_aim.yaw_gain > 5.0 ||
        profile.mouse_aim.pitch_gain < 0.0 || profile.mouse_aim.pitch_gain > 5.0) {
        std::fprintf(stderr, "profile error: mouse_aim output gains must be 0..5.\n");
        return false;
    }
    if (profile.mouse_aim.roll_max < 0 || profile.mouse_aim.roll_max > 512 ||
        profile.mouse_aim.yaw_max < 0 || profile.mouse_aim.yaw_max > 512 ||
        profile.mouse_aim.pitch_max < 0 || profile.mouse_aim.pitch_max > 512) {
        std::fprintf(stderr, "profile error: mouse_aim output max values must be 0..512.\n");
        return false;
    }
    if (profile.mouse_aim.slew_rate < 0.0 || profile.mouse_aim.slew_rate > 20000.0) {
        std::fprintf(stderr, "profile error: mouse_aim.slew_rate must be 0..20000.\n");
        return false;
    }
    if (profile.mouse_left.enabled) {
        if (profile.keyboard_left.enabled) {
            std::fprintf(stderr,
                         "profile error: mouse_left_stick.enabled is mutually exclusive with keyboard_left_stick.enabled for now.\n");
            return false;
        }
        if (profile.control_mode != ControlMode::DirectMouse) {
            std::fprintf(stderr,
                         "profile error: mouse_left_stick currently requires control.mode=\"direct_mouse\".\n");
            return false;
        }
        if (profile.mouse_left.require_device && profile.mouse_left_device_token.empty()) {
            std::fprintf(stderr,
                         "profile error: mouse_left_stick.require_device=true requires mouse_devices.left to be set to a GameInput root token.\n");
            return false;
        }
        if (!profile.mouse_left_device_token.empty() &&
            !MouseDeviceTokenBindingIsAuto(profile.mouse_right_device_token) &&
            !MouseDeviceTokenBindingIsAuto(profile.mouse_left_device_token) &&
            ToLowerAscii(profile.mouse_right_device_token) == ToLowerAscii(profile.mouse_left_device_token)) {
            std::fprintf(stderr,
                         "profile error: mouse_devices.right and mouse_devices.left must name different devices when mouse_left_stick is enabled.\n");
            return false;
        }
        if (profile.mouse_left.throttle_rate < 0.0 ||
            profile.mouse_left.throttle_rate > 10000.0) {
            std::fprintf(stderr,
                         "profile error: mouse_left_stick.throttle_rate must be 0..10000 trainer units per second.\n");
            return false;
        }
        if (profile.mouse_left.throttle_return_rate < 0.0 ||
            profile.mouse_left.throttle_return_rate > 20000.0) {
            std::fprintf(stderr,
                         "profile error: mouse_left_stick.throttle_return_rate must be 0..20000 trainer units per second.\n");
            return false;
        }
        if (profile.mouse_left.yaw_pulse < 0 || profile.mouse_left.yaw_pulse > 512) {
            std::fprintf(stderr,
                         "profile error: mouse_left_stick.yaw_pulse must be 0..512.\n");
            return false;
        }
        if (profile.mouse_left.yaw_gain < 0.0 || profile.mouse_left.yaw_gain > 5000.0) {
            std::fprintf(stderr,
                         "profile error: mouse_left_stick.yaw_gain must be 0..5000.\n");
            return false;
        }
        if (profile.mouse_left.yaw_deadband < 0 || profile.mouse_left.yaw_deadband >= 512) {
            std::fprintf(stderr,
                         "profile error: mouse_left_stick.yaw_deadband must be 0..511.\n");
            return false;
        }
        if (profile.mouse_left.yaw_smoothing < 0.0 || profile.mouse_left.yaw_smoothing >= 1.0) {
            std::fprintf(stderr,
                         "profile error: mouse_left_stick.yaw_smoothing must be >=0.0 and <1.0.\n");
            return false;
        }
        if (profile.mouse_left.yaw_slew_rate < 0.0 ||
            profile.mouse_left.yaw_slew_rate > 20000.0) {
            std::fprintf(stderr,
                         "profile error: mouse_left_stick.yaw_slew_rate must be 0..20000 trainer units per second.\n");
            return false;
        }
        if (profile.mouse_left.yaw_return_rate < 0.0 ||
            profile.mouse_left.yaw_return_rate > 20000.0 ||
            profile.mouse_left.yaw_constant_return_rate < 0.0 ||
            profile.mouse_left.yaw_constant_return_rate > 20000.0) {
            std::fprintf(stderr,
                         "profile error: mouse_left_stick yaw return rates must be 0..20000 trainer units per second.\n");
            return false;
        }
        if (profile.mouse_left.yaw_return_idle_ms < 0.0 ||
            profile.mouse_left.yaw_return_idle_ms > 60000.0) {
            std::fprintf(stderr,
                         "profile error: mouse_left_stick.yaw_return_idle_ms must be 0..60000.\n");
            return false;
        }
        if (profile.mouse_left.yaw_elastic_return_coefficient < 0.0 ||
            profile.mouse_left.yaw_elastic_return_coefficient > 100.0) {
            std::fprintf(stderr,
                         "profile error: mouse_left_stick.yaw_elastic_return_coefficient must be 0..100 per second.\n");
            return false;
        }
        if (profile.mouse_left.yaw_elastic_return_curve < 0.0 ||
            profile.mouse_left.yaw_elastic_return_curve > 5.0) {
            std::fprintf(stderr,
                         "profile error: mouse_left_stick.yaw_elastic_return_curve must be 0..5.\n");
            return false;
        }
        if (profile.mouse_left.yaw_one_euro_min_cutoff_hz < 0.05 ||
            profile.mouse_left.yaw_one_euro_min_cutoff_hz > 120.0) {
            std::fprintf(stderr,
                         "profile error: mouse_left_stick.yaw_one_euro_min_cutoff_hz must be 0.05..120.\n");
            return false;
        }
        if (profile.mouse_left.yaw_one_euro_beta < 0.0 ||
            profile.mouse_left.yaw_one_euro_beta > 2.0) {
            std::fprintf(stderr,
                         "profile error: mouse_left_stick.yaw_one_euro_beta must be 0..2.\n");
            return false;
        }
        if (profile.mouse_left.yaw_one_euro_dcutoff_hz < 0.1 ||
            profile.mouse_left.yaw_one_euro_dcutoff_hz > 120.0) {
            std::fprintf(stderr,
                         "profile error: mouse_left_stick.yaw_one_euro_dcutoff_hz must be 0.1..120.\n");
            return false;
        }
        if (profile.mouse_left.yaw_despike_window < 3 ||
            profile.mouse_left.yaw_despike_window > 15 ||
            (profile.mouse_left.yaw_despike_window % 2) == 0) {
            std::fprintf(stderr,
                         "profile error: mouse_left_stick.yaw_despike_window must be an odd value from 3..15.\n");
            return false;
        }
        if (profile.mouse_left.yaw_despike_threshold_sigma <= 0.0 ||
            profile.mouse_left.yaw_despike_threshold_sigma > 20.0) {
            std::fprintf(stderr,
                         "profile error: mouse_left_stick.yaw_despike_threshold_sigma must be >0 and <=20.\n");
            return false;
        }
        if (profile.mouse_left.yaw_expo < 0.0 ||
            profile.mouse_left.yaw_expo > 1.0) {
            std::fprintf(stderr,
                         "profile error: mouse_left_stick.yaw_expo must be 0.0..1.0.\n");
            return false;
        }
        if (profile.mouse_left.yaw_actual_center < 0.0 ||
            profile.mouse_left.yaw_actual_center > 1.0 ||
            profile.mouse_left.yaw_actual_max < 0.0 ||
            profile.mouse_left.yaw_actual_max > 1.0 ||
            profile.mouse_left.yaw_actual_expo < 0.0 ||
            profile.mouse_left.yaw_actual_expo > 0.95) {
            std::fprintf(stderr,
                         "profile error: mouse_left_stick yaw Actual Rates fields must satisfy center/max 0..1 and expo 0..0.95.\n");
            return false;
        }
        if (profile.mouse_left.yaw_gimbal_frequency_hz < 0.1 ||
            profile.mouse_left.yaw_gimbal_frequency_hz > 80.0) {
            std::fprintf(stderr,
                         "profile error: mouse_left_stick.yaw_gimbal_frequency_hz must be 0.1..80.\n");
            return false;
        }
        if (profile.mouse_left.yaw_gimbal_damping_ratio < 0.0 ||
            profile.mouse_left.yaw_gimbal_damping_ratio > 5.0) {
            std::fprintf(stderr,
                         "profile error: mouse_left_stick.yaw_gimbal_damping_ratio must be 0..5.\n");
            return false;
        }
        if (profile.mouse_left.yaw_gimbal_input_impulse < 0.0 ||
            profile.mouse_left.yaw_gimbal_input_impulse > 5.0) {
            std::fprintf(stderr,
                         "profile error: mouse_left_stick.yaw_gimbal_input_impulse must be 0..5.\n");
            return false;
        }
        if (profile.mouse_left.yaw_gimbal_static_friction < 0.0 ||
            profile.mouse_left.yaw_gimbal_static_friction > 128.0) {
            std::fprintf(stderr,
                         "profile error: mouse_left_stick.yaw_gimbal_static_friction must be 0..128 trainer units.\n");
            return false;
        }
        if (profile.mouse_left.yaw_gimbal_dynamic_friction < 0.0 ||
            profile.mouse_left.yaw_gimbal_dynamic_friction > 100.0) {
            std::fprintf(stderr,
                         "profile error: mouse_left_stick.yaw_gimbal_dynamic_friction must be 0..100.\n");
            return false;
        }
        if (profile.mouse_left.yaw_gimbal_edge_bumper < 0.0 ||
            profile.mouse_left.yaw_gimbal_edge_bumper > 20.0) {
            std::fprintf(stderr,
                         "profile error: mouse_left_stick.yaw_gimbal_edge_bumper must be 0..20.\n");
            return false;
        }
        if (profile.mouse_left.yaw_gimbal_antiwindup_start < 0.0 ||
            profile.mouse_left.yaw_gimbal_antiwindup_start >= 1.0) {
            std::fprintf(stderr,
                         "profile error: mouse_left_stick.yaw_gimbal_antiwindup_start must be >=0 and <1.\n");
            return false;
        }
        if (profile.mouse_left.yaw_gimbal_antiwindup_min_gain < 0.0 ||
            profile.mouse_left.yaw_gimbal_antiwindup_min_gain > 1.0) {
            std::fprintf(stderr,
                         "profile error: mouse_left_stick.yaw_gimbal_antiwindup_min_gain must be 0..1.\n");
            return false;
        }
        if (profile.mouse_left.yaw_adaptive_slow_gain < 0.0 ||
            profile.mouse_left.yaw_adaptive_slow_gain > 5.0 ||
            profile.mouse_left.yaw_adaptive_fast_gain < 0.0 ||
            profile.mouse_left.yaw_adaptive_fast_gain > 5.0) {
            std::fprintf(stderr,
                         "profile error: mouse_left_stick yaw adaptive gains must be 0..5.\n");
            return false;
        }
        if (profile.mouse_left.yaw_adaptive_speed_low < 0.0 ||
            profile.mouse_left.yaw_adaptive_speed_low > 100000.0 ||
            profile.mouse_left.yaw_adaptive_speed_high <= profile.mouse_left.yaw_adaptive_speed_low ||
            profile.mouse_left.yaw_adaptive_speed_high > 100000.0) {
            std::fprintf(stderr,
                         "profile error: mouse_left_stick yaw adaptive speeds must satisfy 0 <= low < high <= 100000 counts/s.\n");
            return false;
        }
        if (profile.mouse_left.yaw_adaptive_curve <= 0.0 ||
            profile.mouse_left.yaw_adaptive_curve > 5.0) {
            std::fprintf(stderr,
                         "profile error: mouse_left_stick.yaw_adaptive_curve must be >0 and <=5.\n");
            return false;
        }
        if (profile.mouse_left.yaw_adaptive_tracker_ms < 0.0 ||
            profile.mouse_left.yaw_adaptive_tracker_ms > 1000.0) {
            std::fprintf(stderr,
                         "profile error: mouse_left_stick.yaw_adaptive_tracker_ms must be 0..1000.\n");
            return false;
        }
        if (profile.mouse_left.yaw_diagonal_scale < 0.0 ||
            profile.mouse_left.yaw_diagonal_scale > 1.5) {
            std::fprintf(stderr,
                         "profile error: mouse_left_stick.yaw_diagonal_scale must be 0..1.5.\n");
            return false;
        }
        if (!StickShapeCurveIsValid(profile.mouse_left.yaw_output_shape)) {
            std::fprintf(stderr,
                         "profile error: mouse_left_stick.yaw_output_shape_nodes entries must be [x,y,width] with 0<=x<=1, 0<=y<=1, %.2f<=width<=%.2f.\n",
                         kStickShapeMinWidth,
                         kStickShapeMaxWidth);
            return false;
        }
        if (!StickShapeCurveIsValid(profile.mouse_left.yaw_return_shape)) {
            std::fprintf(stderr,
                         "profile error: mouse_left_stick.yaw_return_shape_nodes entries must be [x,y,width] with 0<=x<=1, 0<=y<=1, %.2f<=width<=%.2f.\n",
                         kStickShapeMinWidth,
                         kStickShapeMaxWidth);
            return false;
        }
    }
    if (profile.right_mouse_left.enabled) {
        if (profile.keyboard_left.enabled || profile.mouse_left.enabled) {
            std::fprintf(stderr,
                         "profile error: right_mouse_left_stick.enabled is mutually exclusive with keyboard_left_stick.enabled and mouse_left_stick.enabled.\n");
            return false;
        }
        if (profile.control_mode != ControlMode::DirectMouse) {
            std::fprintf(stderr,
                         "profile error: right_mouse_left_stick currently requires control.mode=\"direct_mouse\".\n");
            return false;
        }
        if (!profile.mouse_right_stick_enabled) {
            std::fprintf(stderr,
                         "profile error: right_mouse_left_stick requires mouse_right_stick.enabled=true.\n");
            return false;
        }
        if (profile.right_mouse_left.throttle_step < 0.0 ||
            profile.right_mouse_left.throttle_step > 1024.0) {
            std::fprintf(stderr,
                         "profile error: right_mouse_left_stick.throttle_step must be 0..1024 trainer units per wheel notch.\n");
            return false;
        }
        if (profile.right_mouse_left.throttle_button_rate < 0.0 ||
            profile.right_mouse_left.throttle_button_rate > 10000.0) {
            std::fprintf(stderr,
                         "profile error: right_mouse_left_stick.throttle_button_rate must be 0..10000 trainer units per second.\n");
            return false;
        }
        if (profile.right_mouse_left.throttle_return_rate < 0.0 ||
            profile.right_mouse_left.throttle_return_rate > 20000.0) {
            std::fprintf(stderr,
                         "profile error: right_mouse_left_stick.throttle_return_rate must be 0..20000 trainer units per second.\n");
            return false;
        }
        if (profile.right_mouse_left.yaw_pulse < 0 ||
            profile.right_mouse_left.yaw_pulse > 512) {
            std::fprintf(stderr,
                         "profile error: right_mouse_left_stick.yaw_pulse must be 0..512.\n");
            return false;
        }
        if (profile.right_mouse_left.yaw_scroll_step < 0.0 ||
            profile.right_mouse_left.yaw_scroll_step > 1024.0) {
            std::fprintf(stderr,
                         "profile error: right_mouse_left_stick.yaw_scroll_step must be 0..1024 trainer units per wheel notch.\n");
            return false;
        }
        if (profile.right_mouse_left.yaw_slew_rate < 0.0 ||
            profile.right_mouse_left.yaw_slew_rate > 20000.0) {
            std::fprintf(stderr,
                         "profile error: right_mouse_left_stick.yaw_slew_rate must be 0..20000 trainer units per second.\n");
            return false;
        }
    }
    if (profile.keyboard_left.enabled) {
        WootingAnalogKeycodeMode analog_mode;
        if (!ParseWootingAnalogKeycodeMode(profile.keyboard_left.analog_keycode_mode, &analog_mode)) {
            std::fprintf(stderr,
                         "profile error: keyboard_left_stick.analog_keycode_mode must be virtual_key_translate, virtual_key, hid, or scancode1.\n");
            return false;
        }
        if (profile.keyboard_left.throttle_up_vk <= 0 ||
            profile.keyboard_left.throttle_down_vk <= 0 ||
            profile.keyboard_left.yaw_left_vk <= 0 ||
            profile.keyboard_left.yaw_right_vk <= 0) {
            std::fprintf(stderr,
                         "profile error: keyboard_left_stick movement keys must be valid key names.\n");
            return false;
        }
        if (profile.keyboard_left.throttle_rate < 0.0 ||
            profile.keyboard_left.throttle_rate > 10000.0) {
            std::fprintf(stderr,
                         "profile error: keyboard_left_stick.throttle_rate must be 0..10000 trainer units per second.\n");
            return false;
        }
        if (profile.keyboard_left.throttle_return_rate < 0.0 ||
            profile.keyboard_left.throttle_return_rate > 20000.0) {
            std::fprintf(stderr,
                         "profile error: keyboard_left_stick.throttle_return_rate must be 0..20000 trainer units per second.\n");
            return false;
        }
        if (profile.keyboard_left.yaw_pulse < 0 || profile.keyboard_left.yaw_pulse > 512) {
            std::fprintf(stderr,
                         "profile error: keyboard_left_stick.yaw_pulse must be 0..512.\n");
            return false;
        }
        if (profile.keyboard_left.yaw_slew_rate < 0.0 ||
            profile.keyboard_left.yaw_slew_rate > 20000.0) {
            std::fprintf(stderr,
                         "profile error: keyboard_left_stick.yaw_slew_rate must be 0..20000 trainer units per second.\n");
            return false;
        }
        if (profile.keyboard_left.analog_deadzone < 0.0 ||
            profile.keyboard_left.analog_deadzone >= 0.95) {
            std::fprintf(stderr,
                         "profile error: keyboard_left_stick.analog_deadzone must be >=0.0 and <0.95.\n");
            return false;
        }
        if (profile.keyboard_left.analog_curve <= 0.0 ||
            profile.keyboard_left.analog_curve > 5.0) {
            std::fprintf(stderr,
                         "profile error: keyboard_left_stick.analog_curve must be >0.0 and <=5.0.\n");
            return false;
        }
        if (profile.keyboard_left.analog_min < 0.0 ||
            profile.keyboard_left.analog_min >= 1.0 ||
            profile.keyboard_left.analog_max <= profile.keyboard_left.analog_min ||
            profile.keyboard_left.analog_max > 1.0) {
            std::fprintf(stderr,
                         "profile error: keyboard_left_stick analog_min/max must satisfy 0 <= min < max <= 1.\n");
            return false;
        }
    }
    if (profile.log_every_n_frames <= 0) {
        std::fprintf(stderr, "profile error: logging.every_n_frames must be >=1.\n");
        return false;
    }
    return true;
}

uint8_t TrainerActiveFlags(const TrainerProfile& profile) {
    uint8_t flags = kSbusTrainerMaskMarker;
    if (profile.mouse_right_stick_enabled) {
        flags |= kSbusTrainerMaskRightActive;
    }
    if (profile.keyboard_left.enabled || profile.mouse_left.enabled ||
        profile.right_mouse_left.enabled ||
        profile.control_mode == ControlMode::DroneMouseAim) {
        flags |= kSbusTrainerMaskLeftActive;
    }
    if (profile.resolution_mode == TrainerResolutionMode::Gx12_2x) {
        flags |= kSbusTrainerResolution2x;
    }
    return flags;
}

bool LoadTrainerProfile(const char* path, TrainerProfile* profile) {
    if (!path || path[0] == '\0') {
        std::fprintf(stderr, "profile path must not be empty.\n");
        return false;
    }

    profile->source_file = path;

    toml::table config;
    try {
        config = toml::parse_file(path);
    } catch (const toml::parse_error& err) {
        std::fprintf(stderr,
                     "failed to parse profile '%s': %s\n",
                     path,
                     err.what());
        return false;
    }

    auto trainer = config["trainer"];
    profile->name = trainer["name"].value_or(profile->name);
    profile->port = trainer["port"].value_or(profile->port);
    profile->seconds = trainer["seconds"].value_or(profile->seconds);
    profile->frame_rate_hz = trainer["frame_rate_hz"].value_or(profile->frame_rate_hz);
    const std::string resolution_mode_text =
        trainer["resolution_mode"].value_or(std::string(TrainerResolutionModeName(profile->resolution_mode)));
    if (!ParseTrainerResolutionModeName(resolution_mode_text, &profile->resolution_mode)) {
        std::fprintf(stderr,
                     "profile error: trainer.resolution_mode must be legacy or gx12_2x.\n");
        return false;
    }

    auto safety = config["safety"];
    profile->stop_key = safety["stop_key"].value_or(profile->stop_key);
    profile->freeze_key = safety["freeze_key"].value_or(profile->freeze_key);
    if (!ResolveTrainerProfileKeys(profile)) {
        std::fprintf(stderr,
                     "profile error: unrecognized safety.stop_key or safety.freeze_key. Use letters, digits, Space, Esc, arrows, or F1..F24.\n");
        return false;
    }

    auto mapper = config["mapper"];
    profile->roll_gain = mapper["roll_gain"].value_or(profile->roll_gain);
    profile->pitch_gain = mapper["pitch_gain"].value_or(profile->pitch_gain);
    profile->max_output = mapper["max_output"].value_or(profile->max_output);
    profile->deadband = mapper["deadband"].value_or(profile->deadband);
    profile->expo = mapper["expo"].value_or(profile->expo);
    profile->smoothing = mapper["smoothing"].value_or(profile->smoothing);
    const bool input_filter_explicit = static_cast<bool>(mapper["input_filter"]);
    const std::string input_filter_text =
        mapper["input_filter"].value_or(std::string(InputFilterModeName(profile->input_filter)));
    if (!ParseInputFilterModeName(input_filter_text, &profile->input_filter)) {
        std::fprintf(stderr,
                     "profile error: mapper.input_filter must be off, smoothing, or one_euro.\n");
        return false;
    }
    if (!input_filter_explicit && profile->smoothing > 0.0) {
        profile->input_filter = InputFilterMode::Smoothing;
    }
    profile->one_euro_min_cutoff_hz = mapper["one_euro_min_cutoff_hz"].value_or(profile->one_euro_min_cutoff_hz);
    profile->one_euro_beta = mapper["one_euro_beta"].value_or(profile->one_euro_beta);
    profile->one_euro_dcutoff_hz = mapper["one_euro_dcutoff_hz"].value_or(profile->one_euro_dcutoff_hz);
    profile->despike_enabled = mapper["despike_enabled"].value_or(profile->despike_enabled);
    profile->despike_count_enabled = mapper["despike_count_enabled"].value_or(profile->despike_count_enabled);
    profile->despike_window = mapper["despike_window"].value_or(profile->despike_window);
    profile->despike_threshold_sigma = mapper["despike_threshold_sigma"].value_or(profile->despike_threshold_sigma);
    const bool output_curve_explicit = static_cast<bool>(mapper["output_curve"]);
    const std::string output_curve_text =
        mapper["output_curve"].value_or(std::string(OutputCurveModeName(profile->output_curve)));
    if (!ParseOutputCurveModeName(output_curve_text, &profile->output_curve)) {
        std::fprintf(stderr,
                     "profile error: mapper.output_curve must be expo, nodes, or actual.\n");
        return false;
    }
    profile->actual_center = mapper["actual_center"].value_or(profile->actual_center);
    profile->actual_max = mapper["actual_max"].value_or(profile->actual_max);
    profile->actual_expo = mapper["actual_expo"].value_or(profile->actual_expo);
    const std::string position_model_text =
        mapper["position_model"].value_or(std::string(PositionModelName(profile->position_model)));
    if (!ParsePositionModelName(position_model_text, &profile->position_model)) {
        std::fprintf(stderr,
                     "profile error: mapper.position_model must be integrator or dynamic_gimbal.\n");
        return false;
    }
    profile->gimbal_frequency_hz = mapper["gimbal_frequency_hz"].value_or(profile->gimbal_frequency_hz);
    profile->gimbal_damping_ratio = mapper["gimbal_damping_ratio"].value_or(profile->gimbal_damping_ratio);
    profile->gimbal_input_impulse = mapper["gimbal_input_impulse"].value_or(profile->gimbal_input_impulse);
    profile->gimbal_static_friction = mapper["gimbal_static_friction"].value_or(profile->gimbal_static_friction);
    profile->gimbal_dynamic_friction = mapper["gimbal_dynamic_friction"].value_or(profile->gimbal_dynamic_friction);
    profile->gimbal_edge_bumper = mapper["gimbal_edge_bumper"].value_or(profile->gimbal_edge_bumper);
    profile->gimbal_antiwindup_enabled = mapper["gimbal_antiwindup_enabled"].value_or(profile->gimbal_antiwindup_enabled);
    profile->gimbal_antiwindup_start = mapper["gimbal_antiwindup_start"].value_or(profile->gimbal_antiwindup_start);
    profile->gimbal_antiwindup_min_gain = mapper["gimbal_antiwindup_min_gain"].value_or(profile->gimbal_antiwindup_min_gain);
    const std::string input_gain_mode_text =
        mapper["input_gain_mode"].value_or(std::string(InputGainModeName(profile->input_gain_mode)));
    if (!ParseInputGainModeName(input_gain_mode_text, &profile->input_gain_mode)) {
        std::fprintf(stderr,
                     "profile error: mapper.input_gain_mode must be flat or adaptive.\n");
        return false;
    }
    profile->adaptive_slow_gain = mapper["adaptive_slow_gain"].value_or(profile->adaptive_slow_gain);
    profile->adaptive_fast_gain = mapper["adaptive_fast_gain"].value_or(profile->adaptive_fast_gain);
    profile->adaptive_speed_low = mapper["adaptive_speed_low"].value_or(profile->adaptive_speed_low);
    profile->adaptive_speed_high = mapper["adaptive_speed_high"].value_or(profile->adaptive_speed_high);
    profile->adaptive_curve = mapper["adaptive_curve"].value_or(profile->adaptive_curve);
    profile->adaptive_tracker_ms = mapper["adaptive_tracker_ms"].value_or(profile->adaptive_tracker_ms);
    const std::string gate_shape_text =
        mapper["gate_shape"].value_or(std::string(GateShapeName(profile->gate_shape)));
    if (!ParseGateShapeName(gate_shape_text, &profile->gate_shape)) {
        std::fprintf(stderr,
                     "profile error: mapper.gate_shape must be axis, circle, octagon, or square.\n");
        return false;
    }
    profile->diagonal_scale = mapper["diagonal_scale"].value_or(profile->diagonal_scale);
    profile->return_enabled_explicit = static_cast<bool>(mapper["return_enabled"]);
    profile->return_rate = mapper["return_rate"].value_or(profile->return_rate);
    profile->return_enabled = mapper["return_enabled"].value_or(profile->return_rate > 0.0);
    profile->return_idle_ms = mapper["return_idle_ms"].value_or(profile->return_idle_ms);
    profile->constant_return_enabled = mapper["constant_return_enabled"].value_or(profile->constant_return_enabled);
    profile->constant_return_rate = mapper["constant_return_rate"].value_or(profile->constant_return_rate);
    profile->elastic_return_enabled = mapper["elastic_return_enabled"].value_or(profile->elastic_return_enabled);
    const std::string elastic_return_mode_text =
        mapper["elastic_return_mode"].value_or(std::string(ElasticReturnModeName(profile->elastic_return_mode)));
    if (!ParseElasticReturnModeName(elastic_return_mode_text, &profile->elastic_return_mode)) {
        std::fprintf(stderr,
                     "profile error: mapper.elastic_return_mode must be linear, progressive, smoothstep, or expo.\n");
        return false;
    }
    profile->elastic_return_coefficient = mapper["elastic_return_coefficient"].value_or(profile->elastic_return_coefficient);
    profile->elastic_return_curve = mapper["elastic_return_curve"].value_or(profile->elastic_return_curve);
    profile->output_shape.enabled = mapper["output_shaping_enabled"].value_or(profile->output_shape.enabled);
    profile->return_shape.enabled = mapper["return_shaping_enabled"].value_or(profile->return_shape.enabled);
    if (!output_curve_explicit && profile->output_shape.enabled) {
        profile->output_curve = OutputCurveMode::Nodes;
    }
    auto load_shape_nodes = [](const toml::node_view<toml::node>& mapper_view,
                               const char* key,
                               StickShapeCurve* shape) {
        if (auto* arr = mapper_view[key].as_array()) {
            shape->nodes.clear();
            shape->nodes.reserve(arr->size());
            for (const auto& elem : *arr) {
                if (auto* triple = elem.as_array(); triple && triple->size() >= 3) {
                    StickShapeNode node{};
                    node.x = (*triple)[0].value_or(0.5);
                    node.y = (*triple)[1].value_or(0.5);
                    node.width = (*triple)[2].value_or(0.25);
                    shape->nodes.push_back(node);
                }
            }
        }
    };
    load_shape_nodes(mapper, "output_shape_nodes", &profile->output_shape);
    load_shape_nodes(mapper, "return_shape_nodes", &profile->return_shape);
    profile->invert_roll = mapper["invert_roll"].value_or(profile->invert_roll);
    profile->invert_pitch = mapper["invert_pitch"].value_or(profile->invert_pitch);
    profile->swap_axes = mapper["swap_axes"].value_or(profile->swap_axes);

    auto control = config["control"];
    const std::string control_mode_text = control["mode"].value_or(std::string(ControlModeName(profile->control_mode)));
    if (!ParseControlModeName(control_mode_text, &profile->control_mode)) {
        std::fprintf(stderr,
                     "profile error: control.mode must be direct_mouse or drone_mouse_aim.\n");
        return false;
    }

    auto mouse_aim = config["mouse_aim"];
    profile->mouse_aim.sensitivity_x = mouse_aim["sensitivity_x"].value_or(profile->mouse_aim.sensitivity_x);
    profile->mouse_aim.sensitivity_y = mouse_aim["sensitivity_y"].value_or(profile->mouse_aim.sensitivity_y);
    profile->mouse_aim.reticle_limit = mouse_aim["reticle_limit"].value_or(profile->mouse_aim.reticle_limit);
    profile->mouse_aim.reticle_deadband = mouse_aim["reticle_deadband"].value_or(profile->mouse_aim.reticle_deadband);
    profile->mouse_aim.reticle_return_rate = mouse_aim["reticle_return_rate"].value_or(profile->mouse_aim.reticle_return_rate);
    profile->mouse_aim.output_smoothing = mouse_aim["output_smoothing"].value_or(profile->mouse_aim.output_smoothing);
    profile->mouse_aim.roll_gain = mouse_aim["roll_gain"].value_or(profile->mouse_aim.roll_gain);
    profile->mouse_aim.yaw_gain = mouse_aim["yaw_gain"].value_or(profile->mouse_aim.yaw_gain);
    profile->mouse_aim.pitch_gain = mouse_aim["pitch_gain"].value_or(profile->mouse_aim.pitch_gain);
    profile->mouse_aim.roll_max = mouse_aim["roll_max"].value_or(profile->mouse_aim.roll_max);
    profile->mouse_aim.yaw_max = mouse_aim["yaw_max"].value_or(profile->mouse_aim.yaw_max);
    profile->mouse_aim.pitch_max = mouse_aim["pitch_max"].value_or(profile->mouse_aim.pitch_max);
    profile->mouse_aim.slew_rate = mouse_aim["slew_rate"].value_or(profile->mouse_aim.slew_rate);
    profile->mouse_aim.invert_x = mouse_aim["invert_x"].value_or(profile->mouse_aim.invert_x);
    profile->mouse_aim.invert_y = mouse_aim["invert_y"].value_or(profile->mouse_aim.invert_y);

    auto mouse_right_stick = config["mouse_right_stick"];
    profile->mouse_right_stick_enabled = mouse_right_stick["enabled"].value_or(profile->mouse_right_stick_enabled);

    auto mouse_devices = config["mouse_devices"];
    profile->mouse_right_device_token = mouse_devices["right"].value_or(profile->mouse_right_device_token);
    profile->mouse_left_device_token = mouse_devices["left"].value_or(profile->mouse_left_device_token);

    auto mouse_left = config["mouse_left_stick"];
    profile->mouse_left.enabled = mouse_left["enabled"].value_or(profile->mouse_left.enabled);
    profile->mouse_left.require_device = mouse_left["require_device"].value_or(profile->mouse_left.require_device);
    profile->mouse_left.invert_throttle = mouse_left["invert_throttle"].value_or(profile->mouse_left.invert_throttle);
    profile->mouse_left.invert_yaw = mouse_left["invert_yaw"].value_or(profile->mouse_left.invert_yaw);
    profile->mouse_left.swap_axes = mouse_left["swap_axes"].value_or(profile->mouse_left.swap_axes);
    profile->mouse_left.throttle_rate = mouse_left["throttle_rate"].value_or(profile->mouse_left.throttle_rate);
    profile->mouse_left.throttle_return_enabled = mouse_left["throttle_return_enabled"].value_or(profile->mouse_left.throttle_return_enabled);
    profile->mouse_left.throttle_return_rate = mouse_left["throttle_return_rate"].value_or(profile->mouse_left.throttle_return_rate);
    profile->mouse_left.yaw_pulse = mouse_left["yaw_pulse"].value_or(profile->mouse_left.yaw_pulse);
    profile->mouse_left.yaw_gain = mouse_left["yaw_gain"].value_or(profile->mouse_left.yaw_gain);
    profile->mouse_left.yaw_deadband = mouse_left["yaw_deadband"].value_or(profile->mouse_left.yaw_deadband);
    profile->mouse_left.yaw_smoothing = mouse_left["yaw_smoothing"].value_or(profile->mouse_left.yaw_smoothing);
    profile->mouse_left.yaw_slew_rate = mouse_left["yaw_slew_rate"].value_or(profile->mouse_left.yaw_slew_rate);
    const bool legacy_yaw_mapper_shaping =
        mouse_left["yaw_mapper_shaping_enabled"].value_or(false);
    const bool modern_yaw_shaping_present =
        static_cast<bool>(mouse_left["yaw_shaping_enabled"]) ||
        static_cast<bool>(mouse_left["yaw_input_filter"]) ||
        static_cast<bool>(mouse_left["yaw_despike_enabled"]) ||
        static_cast<bool>(mouse_left["yaw_output_curve"]) ||
        static_cast<bool>(mouse_left["yaw_expo"]) ||
        static_cast<bool>(mouse_left["yaw_position_model"]) ||
        static_cast<bool>(mouse_left["yaw_input_gain_mode"]) ||
        static_cast<bool>(mouse_left["yaw_gate_shape"]) ||
        static_cast<bool>(mouse_left["yaw_output_shaping_enabled"]) ||
        static_cast<bool>(mouse_left["yaw_return_shaping_enabled"]);
    profile->mouse_left.yaw_shaping_enabled =
        mouse_left["yaw_shaping_enabled"].value_or(legacy_yaw_mapper_shaping ||
                                                   profile->mouse_left.yaw_shaping_enabled);
    const bool yaw_input_filter_explicit = static_cast<bool>(mouse_left["yaw_input_filter"]);
    const std::string mouse_left_yaw_input_filter =
        mouse_left["yaw_input_filter"].value_or(std::string(InputFilterModeName(profile->mouse_left.yaw_input_filter)));
    if (!ParseInputFilterModeName(mouse_left_yaw_input_filter,
                                  &profile->mouse_left.yaw_input_filter)) {
        std::fprintf(stderr,
                     "profile error: mouse_left_stick.yaw_input_filter must be off, smoothing, or one_euro.\n");
        return false;
    }
    if (!yaw_input_filter_explicit && profile->mouse_left.yaw_smoothing > 0.0) {
        profile->mouse_left.yaw_input_filter = InputFilterMode::Smoothing;
    }
    profile->mouse_left.yaw_one_euro_min_cutoff_hz =
        mouse_left["yaw_one_euro_min_cutoff_hz"].value_or(profile->mouse_left.yaw_one_euro_min_cutoff_hz);
    profile->mouse_left.yaw_one_euro_beta =
        mouse_left["yaw_one_euro_beta"].value_or(profile->mouse_left.yaw_one_euro_beta);
    profile->mouse_left.yaw_one_euro_dcutoff_hz =
        mouse_left["yaw_one_euro_dcutoff_hz"].value_or(profile->mouse_left.yaw_one_euro_dcutoff_hz);
    profile->mouse_left.yaw_despike_enabled =
        mouse_left["yaw_despike_enabled"].value_or(profile->mouse_left.yaw_despike_enabled);
    profile->mouse_left.yaw_despike_count_enabled =
        mouse_left["yaw_despike_count_enabled"].value_or(profile->mouse_left.yaw_despike_count_enabled);
    profile->mouse_left.yaw_despike_window =
        mouse_left["yaw_despike_window"].value_or(profile->mouse_left.yaw_despike_window);
    profile->mouse_left.yaw_despike_threshold_sigma =
        mouse_left["yaw_despike_threshold_sigma"].value_or(profile->mouse_left.yaw_despike_threshold_sigma);
    const bool yaw_output_curve_explicit = static_cast<bool>(mouse_left["yaw_output_curve"]);
    const std::string mouse_left_yaw_output_curve =
        mouse_left["yaw_output_curve"].value_or(std::string(OutputCurveModeName(profile->mouse_left.yaw_output_curve)));
    if (!ParseOutputCurveModeName(mouse_left_yaw_output_curve,
                                  &profile->mouse_left.yaw_output_curve)) {
        std::fprintf(stderr,
                     "profile error: mouse_left_stick.yaw_output_curve must be expo, nodes, or actual.\n");
        return false;
    }
    profile->mouse_left.yaw_expo =
        mouse_left["yaw_expo"].value_or(profile->mouse_left.yaw_expo);
    profile->mouse_left.yaw_actual_center =
        mouse_left["yaw_actual_center"].value_or(profile->mouse_left.yaw_actual_center);
    profile->mouse_left.yaw_actual_max =
        mouse_left["yaw_actual_max"].value_or(profile->mouse_left.yaw_actual_max);
    profile->mouse_left.yaw_actual_expo =
        mouse_left["yaw_actual_expo"].value_or(profile->mouse_left.yaw_actual_expo);
    const std::string mouse_left_yaw_position_model =
        mouse_left["yaw_position_model"].value_or(std::string(PositionModelName(profile->mouse_left.yaw_position_model)));
    if (!ParsePositionModelName(mouse_left_yaw_position_model,
                                &profile->mouse_left.yaw_position_model)) {
        std::fprintf(stderr,
                     "profile error: mouse_left_stick.yaw_position_model must be integrator or dynamic_gimbal.\n");
        return false;
    }
    profile->mouse_left.yaw_gimbal_frequency_hz =
        mouse_left["yaw_gimbal_frequency_hz"].value_or(profile->mouse_left.yaw_gimbal_frequency_hz);
    profile->mouse_left.yaw_gimbal_damping_ratio =
        mouse_left["yaw_gimbal_damping_ratio"].value_or(profile->mouse_left.yaw_gimbal_damping_ratio);
    profile->mouse_left.yaw_gimbal_input_impulse =
        mouse_left["yaw_gimbal_input_impulse"].value_or(profile->mouse_left.yaw_gimbal_input_impulse);
    profile->mouse_left.yaw_gimbal_static_friction =
        mouse_left["yaw_gimbal_static_friction"].value_or(profile->mouse_left.yaw_gimbal_static_friction);
    profile->mouse_left.yaw_gimbal_dynamic_friction =
        mouse_left["yaw_gimbal_dynamic_friction"].value_or(profile->mouse_left.yaw_gimbal_dynamic_friction);
    profile->mouse_left.yaw_gimbal_edge_bumper =
        mouse_left["yaw_gimbal_edge_bumper"].value_or(profile->mouse_left.yaw_gimbal_edge_bumper);
    profile->mouse_left.yaw_gimbal_antiwindup_enabled =
        mouse_left["yaw_gimbal_antiwindup_enabled"].value_or(profile->mouse_left.yaw_gimbal_antiwindup_enabled);
    profile->mouse_left.yaw_gimbal_antiwindup_start =
        mouse_left["yaw_gimbal_antiwindup_start"].value_or(profile->mouse_left.yaw_gimbal_antiwindup_start);
    profile->mouse_left.yaw_gimbal_antiwindup_min_gain =
        mouse_left["yaw_gimbal_antiwindup_min_gain"].value_or(profile->mouse_left.yaw_gimbal_antiwindup_min_gain);
    const std::string mouse_left_yaw_input_gain_mode =
        mouse_left["yaw_input_gain_mode"].value_or(std::string(InputGainModeName(profile->mouse_left.yaw_input_gain_mode)));
    if (!ParseInputGainModeName(mouse_left_yaw_input_gain_mode,
                                &profile->mouse_left.yaw_input_gain_mode)) {
        std::fprintf(stderr,
                     "profile error: mouse_left_stick.yaw_input_gain_mode must be flat or adaptive.\n");
        return false;
    }
    profile->mouse_left.yaw_adaptive_slow_gain =
        mouse_left["yaw_adaptive_slow_gain"].value_or(profile->mouse_left.yaw_adaptive_slow_gain);
    profile->mouse_left.yaw_adaptive_fast_gain =
        mouse_left["yaw_adaptive_fast_gain"].value_or(profile->mouse_left.yaw_adaptive_fast_gain);
    profile->mouse_left.yaw_adaptive_speed_low =
        mouse_left["yaw_adaptive_speed_low"].value_or(profile->mouse_left.yaw_adaptive_speed_low);
    profile->mouse_left.yaw_adaptive_speed_high =
        mouse_left["yaw_adaptive_speed_high"].value_or(profile->mouse_left.yaw_adaptive_speed_high);
    profile->mouse_left.yaw_adaptive_curve =
        mouse_left["yaw_adaptive_curve"].value_or(profile->mouse_left.yaw_adaptive_curve);
    profile->mouse_left.yaw_adaptive_tracker_ms =
        mouse_left["yaw_adaptive_tracker_ms"].value_or(profile->mouse_left.yaw_adaptive_tracker_ms);
    const std::string mouse_left_yaw_gate_shape =
        mouse_left["yaw_gate_shape"].value_or(std::string(GateShapeName(profile->mouse_left.yaw_gate_shape)));
    if (!ParseGateShapeName(mouse_left_yaw_gate_shape,
                            &profile->mouse_left.yaw_gate_shape)) {
        std::fprintf(stderr,
                     "profile error: mouse_left_stick.yaw_gate_shape must be axis, circle, octagon, or square.\n");
        return false;
    }
    profile->mouse_left.yaw_diagonal_scale =
        mouse_left["yaw_diagonal_scale"].value_or(profile->mouse_left.yaw_diagonal_scale);
    profile->mouse_left.yaw_return_enabled = mouse_left["yaw_return_enabled"].value_or(profile->mouse_left.yaw_return_enabled);
    profile->mouse_left.yaw_return_rate = mouse_left["yaw_return_rate"].value_or(profile->mouse_left.yaw_return_rate);
    profile->mouse_left.yaw_return_idle_ms = mouse_left["yaw_return_idle_ms"].value_or(profile->mouse_left.yaw_return_idle_ms);
    profile->mouse_left.yaw_constant_return_enabled = mouse_left["yaw_constant_return_enabled"].value_or(profile->mouse_left.yaw_constant_return_enabled);
    profile->mouse_left.yaw_constant_return_rate = mouse_left["yaw_constant_return_rate"].value_or(profile->mouse_left.yaw_constant_return_rate);
    profile->mouse_left.yaw_elastic_return_enabled = mouse_left["yaw_elastic_return_enabled"].value_or(profile->mouse_left.yaw_elastic_return_enabled);
    const std::string mouse_left_yaw_elastic_mode =
        mouse_left["yaw_elastic_return_mode"].value_or(std::string(ElasticReturnModeName(profile->mouse_left.yaw_elastic_return_mode)));
    if (!ParseElasticReturnModeName(mouse_left_yaw_elastic_mode,
                                    &profile->mouse_left.yaw_elastic_return_mode)) {
        std::fprintf(stderr,
                     "profile error: mouse_left_stick.yaw_elastic_return_mode must be linear, progressive, smoothstep, or expo.\n");
        return false;
    }
    profile->mouse_left.yaw_elastic_return_coefficient =
        mouse_left["yaw_elastic_return_coefficient"].value_or(profile->mouse_left.yaw_elastic_return_coefficient);
    profile->mouse_left.yaw_elastic_return_curve =
        mouse_left["yaw_elastic_return_curve"].value_or(profile->mouse_left.yaw_elastic_return_curve);
    profile->mouse_left.yaw_output_shape.enabled =
        mouse_left["yaw_output_shaping_enabled"].value_or(profile->mouse_left.yaw_output_shape.enabled);
    profile->mouse_left.yaw_return_shape.enabled =
        mouse_left["yaw_return_shaping_enabled"].value_or(profile->mouse_left.yaw_return_shape.enabled);
    if (!yaw_output_curve_explicit && profile->mouse_left.yaw_output_shape.enabled) {
        profile->mouse_left.yaw_output_curve = OutputCurveMode::Nodes;
    }
    load_shape_nodes(mouse_left, "yaw_output_shape_nodes", &profile->mouse_left.yaw_output_shape);
    load_shape_nodes(mouse_left, "yaw_return_shape_nodes", &profile->mouse_left.yaw_return_shape);

    if (legacy_yaw_mapper_shaping && !modern_yaw_shaping_present) {
        profile->mouse_left.yaw_input_filter = profile->input_filter;
        profile->mouse_left.yaw_one_euro_min_cutoff_hz = profile->one_euro_min_cutoff_hz;
        profile->mouse_left.yaw_one_euro_beta = profile->one_euro_beta;
        profile->mouse_left.yaw_one_euro_dcutoff_hz = profile->one_euro_dcutoff_hz;
        profile->mouse_left.yaw_despike_enabled = profile->despike_enabled;
        profile->mouse_left.yaw_despike_count_enabled = profile->despike_count_enabled;
        profile->mouse_left.yaw_despike_window = profile->despike_window;
        profile->mouse_left.yaw_despike_threshold_sigma = profile->despike_threshold_sigma;
        profile->mouse_left.yaw_output_curve = profile->output_curve;
        profile->mouse_left.yaw_expo = profile->expo;
        profile->mouse_left.yaw_actual_center = profile->actual_center;
        profile->mouse_left.yaw_actual_max = profile->actual_max;
        profile->mouse_left.yaw_actual_expo = profile->actual_expo;
        profile->mouse_left.yaw_position_model = profile->position_model;
        profile->mouse_left.yaw_gimbal_frequency_hz = profile->gimbal_frequency_hz;
        profile->mouse_left.yaw_gimbal_damping_ratio = profile->gimbal_damping_ratio;
        profile->mouse_left.yaw_gimbal_input_impulse = profile->gimbal_input_impulse;
        profile->mouse_left.yaw_gimbal_static_friction = profile->gimbal_static_friction;
        profile->mouse_left.yaw_gimbal_dynamic_friction = profile->gimbal_dynamic_friction;
        profile->mouse_left.yaw_gimbal_edge_bumper = profile->gimbal_edge_bumper;
        profile->mouse_left.yaw_gimbal_antiwindup_enabled = profile->gimbal_antiwindup_enabled;
        profile->mouse_left.yaw_gimbal_antiwindup_start = profile->gimbal_antiwindup_start;
        profile->mouse_left.yaw_gimbal_antiwindup_min_gain = profile->gimbal_antiwindup_min_gain;
        profile->mouse_left.yaw_input_gain_mode = profile->input_gain_mode;
        profile->mouse_left.yaw_adaptive_slow_gain = profile->adaptive_slow_gain;
        profile->mouse_left.yaw_adaptive_fast_gain = profile->adaptive_fast_gain;
        profile->mouse_left.yaw_adaptive_speed_low = profile->adaptive_speed_low;
        profile->mouse_left.yaw_adaptive_speed_high = profile->adaptive_speed_high;
        profile->mouse_left.yaw_adaptive_curve = profile->adaptive_curve;
        profile->mouse_left.yaw_adaptive_tracker_ms = profile->adaptive_tracker_ms;
        profile->mouse_left.yaw_gate_shape = profile->gate_shape;
        profile->mouse_left.yaw_diagonal_scale = profile->diagonal_scale;
        profile->mouse_left.yaw_return_enabled = profile->return_enabled;
        profile->mouse_left.yaw_return_rate = profile->return_rate;
        profile->mouse_left.yaw_return_idle_ms = profile->return_idle_ms;
        profile->mouse_left.yaw_constant_return_enabled = profile->constant_return_enabled;
        profile->mouse_left.yaw_constant_return_rate = profile->constant_return_rate;
        profile->mouse_left.yaw_elastic_return_enabled = profile->elastic_return_enabled;
        profile->mouse_left.yaw_elastic_return_mode = profile->elastic_return_mode;
        profile->mouse_left.yaw_elastic_return_coefficient = profile->elastic_return_coefficient;
        profile->mouse_left.yaw_elastic_return_curve = profile->elastic_return_curve;
        profile->mouse_left.yaw_output_shape = profile->output_shape;
        profile->mouse_left.yaw_return_shape = profile->return_shape;
    }

    auto right_mouse_left = config["right_mouse_left_stick"];
    profile->right_mouse_left.enabled =
        right_mouse_left["enabled"].value_or(profile->right_mouse_left.enabled);
    profile->right_mouse_left.invert_throttle =
        right_mouse_left["invert_throttle"].value_or(profile->right_mouse_left.invert_throttle);
    profile->right_mouse_left.invert_yaw =
        right_mouse_left["invert_yaw"].value_or(profile->right_mouse_left.invert_yaw);
    profile->right_mouse_left.swap_axes =
        right_mouse_left["swap_axes"].value_or(profile->right_mouse_left.swap_axes);
    profile->right_mouse_left.throttle_step =
        right_mouse_left["throttle_step"].value_or(profile->right_mouse_left.throttle_step);
    profile->right_mouse_left.throttle_button_rate =
        right_mouse_left["throttle_button_rate"].value_or(profile->right_mouse_left.throttle_button_rate);
    profile->right_mouse_left.throttle_return_enabled =
        right_mouse_left["throttle_return_enabled"].value_or(profile->right_mouse_left.throttle_return_enabled);
    profile->right_mouse_left.throttle_return_rate =
        right_mouse_left["throttle_return_rate"].value_or(profile->right_mouse_left.throttle_return_rate);
    profile->right_mouse_left.yaw_pulse =
        right_mouse_left["yaw_pulse"].value_or(profile->right_mouse_left.yaw_pulse);
    profile->right_mouse_left.yaw_scroll_step =
        right_mouse_left["yaw_scroll_step"].value_or(profile->right_mouse_left.yaw_scroll_step);
    profile->right_mouse_left.yaw_slew_rate =
        right_mouse_left["yaw_slew_rate"].value_or(profile->right_mouse_left.yaw_slew_rate);

    auto keyboard = config["keyboard_left_stick"];
    profile->keyboard_left.enabled = keyboard["enabled"].value_or(profile->keyboard_left.enabled);
    profile->keyboard_left.block_selected_keys = keyboard["block_selected_keys"].value_or(profile->keyboard_left.block_selected_keys);
    const std::string keyboard_input_source = keyboard["input_source"].value_or(std::string(KeyboardInputSourceName(profile->keyboard_left.input_source)));
    if (!ParseKeyboardInputSourceName(keyboard_input_source, &profile->keyboard_left.input_source)) {
        std::fprintf(stderr,
                     "profile error: keyboard_left_stick.input_source must be gameinput, wooting_analog, or auto.\n");
        return false;
    }
    profile->keyboard_left.require_analog = keyboard["require_analog"].value_or(profile->keyboard_left.require_analog);
    profile->keyboard_left.throttle_up_key = keyboard["throttle_up_key"].value_or(profile->keyboard_left.throttle_up_key);
    profile->keyboard_left.throttle_down_key = keyboard["throttle_down_key"].value_or(profile->keyboard_left.throttle_down_key);
    profile->keyboard_left.yaw_left_key = keyboard["yaw_left_key"].value_or(profile->keyboard_left.yaw_left_key);
    profile->keyboard_left.yaw_right_key = keyboard["yaw_right_key"].value_or(profile->keyboard_left.yaw_right_key);
    profile->keyboard_left.throttle_cut_key = keyboard["throttle_cut_key"].value_or(profile->keyboard_left.throttle_cut_key);
    profile->keyboard_left.analog_keycode_mode = keyboard["analog_keycode_mode"].value_or(profile->keyboard_left.analog_keycode_mode);
    profile->keyboard_left.throttle_rate = keyboard["throttle_rate"].value_or(profile->keyboard_left.throttle_rate);
    profile->keyboard_left.throttle_return_enabled = keyboard["throttle_return_enabled"].value_or(profile->keyboard_left.throttle_return_enabled);
    profile->keyboard_left.throttle_return_rate = keyboard["throttle_return_rate"].value_or(profile->keyboard_left.throttle_return_rate);
    profile->keyboard_left.yaw_pulse = keyboard["yaw_pulse"].value_or(profile->keyboard_left.yaw_pulse);
    profile->keyboard_left.yaw_slew_rate = keyboard["yaw_slew_rate"].value_or(profile->keyboard_left.yaw_slew_rate);
    profile->keyboard_left.analog_deadzone = keyboard["analog_deadzone"].value_or(profile->keyboard_left.analog_deadzone);
    profile->keyboard_left.analog_curve = keyboard["analog_curve"].value_or(profile->keyboard_left.analog_curve);
    profile->keyboard_left.analog_min = keyboard["analog_min"].value_or(profile->keyboard_left.analog_min);
    profile->keyboard_left.analog_max = keyboard["analog_max"].value_or(profile->keyboard_left.analog_max);
    profile->keyboard_left.invert_yaw = keyboard["invert_yaw"].value_or(profile->keyboard_left.invert_yaw);
    if (!ResolveKeyboardLeftStickKeys(&profile->keyboard_left)) {
        std::fprintf(stderr,
                     "profile error: unrecognized keyboard_left_stick key name. Use letters, digits, Space, Esc, arrows, or F1..F24.\n");
        return false;
    }

    if (!ResolveTrainerProfileKeys(profile)) {
        std::fprintf(stderr,
                     "profile error: unrecognized safety.stop_key or safety.freeze_key. Use letters, digits, Space, Esc, arrows, or F1..F24.\n");
        return false;
    }

    auto logging = config["logging"];
    profile->log_csv = logging["csv"].value_or(profile->log_csv);
    profile->log_path = logging["path"].value_or(profile->log_path);
    profile->log_every_n_frames = logging["every_n_frames"].value_or(profile->log_every_n_frames);

    return ValidateTrainerProfile(*profile);
}

void PrintTrainerProfile(const TrainerProfile& profile, bool guided) {
    std::printf("\n--%s: profile=%s name='%s'\n",
                guided ? "tune" : "trainer-profile",
                profile.source_file.c_str(),
                profile.name.c_str());
    if (profile.seconds == kTrainerProfileIndefiniteSeconds) {
        std::printf("  port=%s duration=indefinite frame_rate=%d Hz\n",
                    profile.port.c_str(),
                    profile.frame_rate_hz);
    } else {
        std::printf("  port=%s duration=%d sec frame_rate=%d Hz\n",
                    profile.port.c_str(),
                    profile.seconds,
                    profile.frame_rate_hz);
    }
    std::printf("  trainer: resolution_mode=%s%s\n",
                TrainerResolutionModeName(profile.resolution_mode),
                (profile.resolution_mode == TrainerResolutionMode::Gx12_2x &&
                 !kGx12Resolution2xBuild) ? " (unavailable in this build)" : "");
    std::printf("  safety: stop_key=%s freeze_key=%s\n",
                profile.stop_key.c_str(),
                profile.freeze_key.c_str());
    const int mapper_rate_hz = std::min(profile.frame_rate_hz, kTrainerMapperReferenceHz);
    const double gain_scale = TrainerRateGainScale(mapper_rate_hz);
    std::printf("  mapper: roll_gain=%.3f pitch_gain=%.3f mapper_rate=%d Hz effective_gain=%.3fx max=%d deadband=%d expo=%.3f smoothing=%.3f input_filter=%s idle_return=%s idle_rate=%.1f/s idle_ms=%.1f constant_return=%s constant_rate=%.1f/s elastic_return=%s elastic_mode=%s elastic_coeff=%.3f/s elastic_curve=%.3f\n",
                profile.roll_gain,
                profile.pitch_gain,
                mapper_rate_hz,
                gain_scale,
                profile.max_output,
                profile.deadband,
                profile.expo,
                profile.smoothing,
                InputFilterModeName(profile.input_filter),
                profile.return_enabled ? "true" : "false",
                profile.return_rate,
                profile.return_idle_ms,
                profile.constant_return_enabled ? "true" : "false",
                profile.constant_return_rate,
                profile.elastic_return_enabled ? "true" : "false",
                ElasticReturnModeName(profile.elastic_return_mode),
                profile.elastic_return_coefficient,
                profile.elastic_return_curve);
    std::printf("  mapper_filter: despike=%s count=%s window=%d threshold_sigma=%.3f one_euro_min=%.3fHz beta=%.3f dcutoff=%.3fHz\n",
                profile.despike_enabled ? "true" : "false",
                profile.despike_count_enabled ? "true" : "false",
                profile.despike_window,
                profile.despike_threshold_sigma,
                profile.one_euro_min_cutoff_hz,
                profile.one_euro_beta,
                profile.one_euro_dcutoff_hz);
    std::printf("  mapper_shape: output_curve=%s actual_center=%.3f actual_max=%.3f actual_expo=%.3f output_shaping=%s nodes=[%s] return_shaping=%s nodes=[%s]\n",
                OutputCurveModeName(profile.output_curve),
                profile.actual_center,
                profile.actual_max,
                profile.actual_expo,
                profile.output_shape.enabled ? "true" : "false",
                DescribeStickShapeNodes(profile.output_shape).c_str(),
                profile.return_shape.enabled ? "true" : "false",
                DescribeStickShapeNodes(profile.return_shape).c_str());
    std::printf("  mapper_model: position_model=%s gimbal_freq=%.3fHz damping=%.3f impulse=%.3f static_friction=%.3f dynamic_friction=%.3f edge_bumper=%.3f antiwindup=%s start=%.3f min_gain=%.3f input_gain=%s slow=%.3f fast=%.3f speed_low=%.1f speed_high=%.1f curve=%.3f tracker_ms=%.1f gate=%s diagonal_scale=%.3f\n",
                PositionModelName(profile.position_model),
                profile.gimbal_frequency_hz,
                profile.gimbal_damping_ratio,
                profile.gimbal_input_impulse,
                profile.gimbal_static_friction,
                profile.gimbal_dynamic_friction,
                profile.gimbal_edge_bumper,
                profile.gimbal_antiwindup_enabled ? "true" : "false",
                profile.gimbal_antiwindup_start,
                profile.gimbal_antiwindup_min_gain,
                InputGainModeName(profile.input_gain_mode),
                profile.adaptive_slow_gain,
                profile.adaptive_fast_gain,
                profile.adaptive_speed_low,
                profile.adaptive_speed_high,
                profile.adaptive_curve,
                profile.adaptive_tracker_ms,
                GateShapeName(profile.gate_shape),
                profile.diagonal_scale);
    std::printf("  axes: invert_roll=%s invert_pitch=%s swap_axes=%s\n",
                profile.invert_roll ? "true" : "false",
                profile.invert_pitch ? "true" : "false",
                profile.swap_axes ? "true" : "false");
    std::printf("  control: mode=%s\n", ControlModeName(profile.control_mode));
    if (profile.control_mode == ControlMode::DroneMouseAim) {
        std::printf("  mouse_aim: sens=(%.3f,%.3f) reticle_limit=%.1f deadband=%.1f return=%.1f/s smoothing=%.3f gains roll/yaw/pitch=%.3f/%.3f/%.3f max=%d/%d/%d slew=%.1f/s invert_x=%s invert_y=%s\n",
                    profile.mouse_aim.sensitivity_x,
                    profile.mouse_aim.sensitivity_y,
                    profile.mouse_aim.reticle_limit,
                    profile.mouse_aim.reticle_deadband,
                    profile.mouse_aim.reticle_return_rate,
                    profile.mouse_aim.output_smoothing,
                    profile.mouse_aim.roll_gain,
                    profile.mouse_aim.yaw_gain,
                    profile.mouse_aim.pitch_gain,
                    profile.mouse_aim.roll_max,
                    profile.mouse_aim.yaw_max,
                    profile.mouse_aim.pitch_max,
                    profile.mouse_aim.slew_rate,
                    profile.mouse_aim.invert_x ? "true" : "false",
                    profile.mouse_aim.invert_y ? "true" : "false");
    }
    std::printf("  mouse_right_stick: enabled=%s\n",
                profile.mouse_right_stick_enabled ? "true" : "false");
    if (profile.mouse_left.enabled || !profile.mouse_left_device_token.empty() ||
        profile.mouse_right_device_token != "auto") {
        std::printf("  mouse_devices: right=%s left=%s\n",
                    profile.mouse_right_device_token.c_str(),
                    profile.mouse_left_device_token.empty() ? "(unset)" : profile.mouse_left_device_token.c_str());
    }
    if (profile.mouse_left.enabled) {
        std::printf("  mouse_left_stick: enabled=true require_device=%s throttle_sens=%.1f/s return=%s %.1f/s yaw_sens=%.3f max_yaw=%d deadband=%d smoothing=%.3f yaw_response=%.1f/s yaw_shaping=%s yaw_idle_return=%s %.1f/s idle_ms=%.1f yaw_constant_return=%s %.1f/s yaw_elastic_return=%s mode=%s coeff=%.3f/s curve=%.3f invert_throttle=%s invert_yaw=%s swap_axes=%s\n",
                    profile.mouse_left.require_device ? "true" : "false",
                    profile.mouse_left.throttle_rate,
                    profile.mouse_left.throttle_return_enabled ? "true" : "false",
                    profile.mouse_left.throttle_return_rate,
                    profile.mouse_left.yaw_gain,
                    profile.mouse_left.yaw_pulse,
                    profile.mouse_left.yaw_deadband,
                    profile.mouse_left.yaw_smoothing,
                    profile.mouse_left.yaw_slew_rate,
                    profile.mouse_left.yaw_shaping_enabled ? "true" : "false",
                    profile.mouse_left.yaw_return_enabled ? "true" : "false",
                    profile.mouse_left.yaw_return_rate,
                    profile.mouse_left.yaw_return_idle_ms,
                    profile.mouse_left.yaw_constant_return_enabled ? "true" : "false",
                    profile.mouse_left.yaw_constant_return_rate,
                    profile.mouse_left.yaw_elastic_return_enabled ? "true" : "false",
                    ElasticReturnModeName(profile.mouse_left.yaw_elastic_return_mode),
                    profile.mouse_left.yaw_elastic_return_coefficient,
                    profile.mouse_left.yaw_elastic_return_curve,
                    profile.mouse_left.invert_throttle ? "true" : "false",
                    profile.mouse_left.invert_yaw ? "true" : "false",
                    profile.mouse_left.swap_axes ? "true" : "false");
        if (profile.mouse_left.yaw_shaping_enabled) {
            std::printf("  mouse_left_yaw_filter: input_filter=%s despike=%s count=%s window=%d threshold_sigma=%.3f one_euro_min=%.3fHz beta=%.3f dcutoff=%.3fHz\n",
                        InputFilterModeName(profile.mouse_left.yaw_input_filter),
                        profile.mouse_left.yaw_despike_enabled ? "true" : "false",
                        profile.mouse_left.yaw_despike_count_enabled ? "true" : "false",
                        profile.mouse_left.yaw_despike_window,
                        profile.mouse_left.yaw_despike_threshold_sigma,
                        profile.mouse_left.yaw_one_euro_min_cutoff_hz,
                        profile.mouse_left.yaw_one_euro_beta,
                        profile.mouse_left.yaw_one_euro_dcutoff_hz);
            std::printf("  mouse_left_yaw_shape: output_curve=%s expo=%.3f actual_center=%.3f actual_max=%.3f actual_expo=%.3f output_nodes=[%s] return_nodes=[%s]\n",
                        OutputCurveModeName(profile.mouse_left.yaw_output_curve),
                        profile.mouse_left.yaw_expo,
                        profile.mouse_left.yaw_actual_center,
                        profile.mouse_left.yaw_actual_max,
                        profile.mouse_left.yaw_actual_expo,
                        DescribeStickShapeNodes(profile.mouse_left.yaw_output_shape).c_str(),
                        DescribeStickShapeNodes(profile.mouse_left.yaw_return_shape).c_str());
            std::printf("  mouse_left_yaw_model: position_model=%s input_gain=%s gate=%s diagonal_scale=%.3f gimbal_freq=%.3fHz damping=%.3f antiwindup=%s start=%.3f min_gain=%.3f adaptive_slow=%.3f fast=%.3f speed=%.1f..%.1f curve=%.3f tracker=%.1fms\n",
                        PositionModelName(profile.mouse_left.yaw_position_model),
                        InputGainModeName(profile.mouse_left.yaw_input_gain_mode),
                        GateShapeName(profile.mouse_left.yaw_gate_shape),
                        profile.mouse_left.yaw_diagonal_scale,
                        profile.mouse_left.yaw_gimbal_frequency_hz,
                        profile.mouse_left.yaw_gimbal_damping_ratio,
                        profile.mouse_left.yaw_gimbal_antiwindup_enabled ? "true" : "false",
                        profile.mouse_left.yaw_gimbal_antiwindup_start,
                        profile.mouse_left.yaw_gimbal_antiwindup_min_gain,
                        profile.mouse_left.yaw_adaptive_slow_gain,
                        profile.mouse_left.yaw_adaptive_fast_gain,
                        profile.mouse_left.yaw_adaptive_speed_low,
                        profile.mouse_left.yaw_adaptive_speed_high,
                        profile.mouse_left.yaw_adaptive_curve,
                        profile.mouse_left.yaw_adaptive_tracker_ms);
        }
    }
    if (profile.right_mouse_left.enabled) {
        std::printf("  right_mouse_left_stick: enabled=true mapping=%s throttle_step=%.1f throttle_button_rate=%.1f/s return=%s %.1f/s yaw_max=%d yaw_scroll_step=%.1f yaw_response=%.1f/s invert_throttle=%s invert_yaw=%s\n",
                    profile.right_mouse_left.swap_axes
                        ? "mouse4/mouse5=throttle scroll=yaw"
                        : "mouse4/mouse5=yaw scroll=throttle",
                    profile.right_mouse_left.throttle_step,
                    profile.right_mouse_left.throttle_button_rate,
                    profile.right_mouse_left.throttle_return_enabled ? "true" : "false",
                    profile.right_mouse_left.throttle_return_rate,
                    profile.right_mouse_left.yaw_pulse,
                    profile.right_mouse_left.yaw_scroll_step,
                    profile.right_mouse_left.yaw_slew_rate,
                    profile.right_mouse_left.invert_throttle ? "true" : "false",
                    profile.right_mouse_left.invert_yaw ? "true" : "false");
    }
    if (profile.keyboard_left.enabled) {
        std::printf("  keyboard_left_stick: enabled=true source=%s require_analog=%s block_selected_keys=%s throttle=%s/%s cut=%s throttle_speed=%.1f/s return=%s %.1f/s yaw=%s/%s max_yaw=%d yaw_response=%.1f/s invert_yaw=%s\n",
                    KeyboardInputSourceName(profile.keyboard_left.input_source),
                    profile.keyboard_left.require_analog ? "true" : "false",
                    profile.keyboard_left.block_selected_keys ? "true" : "false",
                    profile.keyboard_left.throttle_up_key.c_str(),
                    profile.keyboard_left.throttle_down_key.c_str(),
                    profile.keyboard_left.throttle_cut_key.c_str(),
                    profile.keyboard_left.throttle_rate,
                    profile.keyboard_left.throttle_return_enabled ? "true" : "false",
                    profile.keyboard_left.throttle_return_rate,
                    profile.keyboard_left.yaw_left_key.c_str(),
                    profile.keyboard_left.yaw_right_key.c_str(),
                    profile.keyboard_left.yaw_pulse,
                    profile.keyboard_left.yaw_slew_rate,
                    profile.keyboard_left.invert_yaw ? "true" : "false");
        if (profile.keyboard_left.input_source != KeyboardLeftStickProfile::InputSource::GameInput) {
            std::printf("  keyboard_left_stick analog: mode=%s deadzone=%.3f curve=%.3f min=%.3f max=%.3f\n",
                        profile.keyboard_left.analog_keycode_mode.c_str(),
                        profile.keyboard_left.analog_deadzone,
                        profile.keyboard_left.analog_curve,
                        profile.keyboard_left.analog_min,
                        profile.keyboard_left.analog_max);
        }
    } else if (profile.control_mode == ControlMode::DroneMouseAim) {
        std::printf("  keyboard_left_stick: enabled=false; ch3 stays low-throttle safety and ch4 uses reticle-aim yaw\n");
    } else {
        std::printf("  keyboard_left_stick: enabled=false; ch3 stays low-throttle safety and ch4 stays centered\n");
    }
    if (profile.log_csv) {
        std::printf("  csv_log=%s every_n_frames=%d\n",
                    profile.log_path.c_str(),
                    profile.log_every_n_frames);
    } else {
        std::printf("  csv_log=off\n");
    }
}

void PrintElasticPreview(const TrainerProfile& profile) {
    constexpr int kBarWidth = 44;
    const int mapper_rate_hz = std::min(profile.frame_rate_hz, kTrainerMapperReferenceHz);
    const double coefficient = profile.elastic_return_enabled ? profile.elastic_return_coefficient : 0.0;
    const double full_rate = ShapedElasticReturnRatePerSecond(profile.max_output,
                                                              profile.max_output,
                                                              coefficient,
                                                              profile.elastic_return_mode,
                                                              profile.elastic_return_curve,
                                                              profile.return_shape);

    std::printf("\n--elastic-preview: profile=%s name='%s'\n",
                profile.source_file.c_str(),
                profile.name.c_str());
    std::printf("  elastic_return=%s mode=%s coefficient=%.3f/s curve=%.3f max=%d mapper_rate=%d Hz\n",
                profile.elastic_return_enabled ? "true" : "false",
                ElasticReturnModeName(profile.elastic_return_mode),
                profile.elastic_return_coefficient,
                profile.elastic_return_curve,
                profile.max_output,
                mapper_rate_hz);
    std::printf("  output_shaping=%s nodes=[%s] return_shaping=%s nodes=[%s]\n",
                profile.output_shape.enabled ? "true" : "false",
                DescribeStickShapeNodes(profile.output_shape).c_str(),
                profile.return_shape.enabled ? "true" : "false",
                DescribeStickShapeNodes(profile.return_shape).c_str());
    if (!profile.elastic_return_enabled || coefficient <= 0.0) {
        std::printf("  note: elastic return is disabled or coefficient is zero; the curve below is flat.\n");
    }

    std::printf("\n  Pull strength by current virtual stick deflection\n");
    std::printf("  deflect  return/s  per tick  curve\n");
    const double rate_scale = std::max(1.0, full_rate);
    for (int i = 0; i <= 8; ++i) {
        const double norm = static_cast<double>(i) / 8.0;
        const double position = norm * static_cast<double>(profile.max_output);
        const double rate = ShapedElasticReturnRatePerSecond(position,
                                                             profile.max_output,
                                                             coefficient,
                                                             profile.elastic_return_mode,
                                                             profile.elastic_return_curve,
                                                             profile.return_shape);
        const int bar_count = static_cast<int>(std::lround(ClampDouble(rate / rate_scale, 0.0, 1.0) * kBarWidth));
        const std::string bar(static_cast<size_t>(bar_count), '#');
        std::printf("  %5.0f%%  %8.1f  %8.3f  |%-*s|\n",
                    norm * 100.0,
                    rate,
                    rate / static_cast<double>(mapper_rate_hz),
                    kBarWidth,
                    bar.c_str());
    }

    std::printf("\n  Mode comparison at the same coefficient/curve\n");
    std::printf("  mode          25%%/s    50%%/s    75%%/s   100%%/s\n");
    const std::array<ElasticReturnMode, 4> modes = {
        ElasticReturnMode::Linear,
        ElasticReturnMode::Progressive,
        ElasticReturnMode::Smoothstep,
        ElasticReturnMode::Expo,
    };
    for (ElasticReturnMode mode : modes) {
        std::printf("  %-11s", ElasticReturnModeName(mode));
        for (double norm : {0.25, 0.50, 0.75, 1.00}) {
            const double rate = ElasticReturnRatePerSecond(norm * static_cast<double>(profile.max_output),
                                                           profile.max_output,
                                                           coefficient,
                                                           mode,
                                                           profile.elastic_return_curve);
            std::printf(" %8.1f", rate);
        }
        std::printf("\n");
    }

    std::printf("\n  No-input decay from full stick using selected elastic mode\n");
    std::printf("  time_ms  position  visual\n");
    double position = static_cast<double>(profile.max_output);
    const double dt = 1.0 / static_cast<double>(mapper_rate_hz);
    int next_sample_ms = 0;
    const int total_ticks = mapper_rate_hz;
    for (int tick = 0; tick <= total_ticks; ++tick) {
        const int elapsed_ms = static_cast<int>(std::lround(1000.0 * static_cast<double>(tick) /
                                                            static_cast<double>(mapper_rate_hz)));
        if (elapsed_ms >= next_sample_ms || tick == total_ticks) {
            const double norm = ClampDouble(position / static_cast<double>(profile.max_output), 0.0, 1.0);
            const int bar_count = static_cast<int>(std::lround(norm * kBarWidth));
            const std::string bar(static_cast<size_t>(bar_count), '#');
            std::printf("  %7d  %8.1f  |%-*s|\n",
                        elapsed_ms,
                        position,
                        kBarWidth,
                        bar.c_str());
            next_sample_ms += 100;
        }
        if (tick == total_ticks || position <= 0.0) {
            continue;
        }
        const double step = ShapedElasticReturnRatePerSecond(position,
                                                             profile.max_output,
                                                             coefficient,
                                                             profile.elastic_return_mode,
                                                             profile.elastic_return_curve,
                                                             profile.return_shape) *
                            dt;
        position = std::max(0.0, position - step);
    }
}

void PrintGimbalPreview(const TrainerProfile& profile) {
    constexpr int kBarWidth = 42;
    const int mapper_rate_hz = std::min(profile.frame_rate_hz, kTrainerMapperReferenceHz);
    const double mapper_dt = 1.0 / static_cast<double>(mapper_rate_hz);
    const double gain_scale = TrainerRateGainScale(mapper_rate_hz);

    std::printf("\n--gimbal-preview: profile=%s name='%s'\n",
                profile.source_file.c_str(),
                profile.name.c_str());
    std::printf("  model=%s gimbal_freq=%.3fHz damping=%.3f impulse=%.3f static_friction=%.3f dynamic_friction=%.3f edge_bumper=%.3f\n",
                PositionModelName(profile.position_model),
                profile.gimbal_frequency_hz,
                profile.gimbal_damping_ratio,
                profile.gimbal_input_impulse,
                profile.gimbal_static_friction,
                profile.gimbal_dynamic_friction,
                profile.gimbal_edge_bumper);
    std::printf("  input_gain=%s slow=%.3f fast=%.3f speed_low=%.1f speed_high=%.1f curve=%.3f tracker_ms=%.1f\n",
                InputGainModeName(profile.input_gain_mode),
                profile.adaptive_slow_gain,
                profile.adaptive_fast_gain,
                profile.adaptive_speed_low,
                profile.adaptive_speed_high,
                profile.adaptive_curve,
                profile.adaptive_tracker_ms);
    std::printf("  gate=%s diagonal_scale=%.3f max=%d mapper_rate=%dHz\n",
                GateShapeName(profile.gate_shape),
                profile.diagonal_scale,
                profile.max_output,
                mapper_rate_hz);

    std::printf("\n  Adaptive speed -> gain\n");
    std::printf("  counts/s       gain  visual\n");
    const double speeds[] = {
        0.0,
        profile.adaptive_speed_low * 0.5,
        profile.adaptive_speed_low,
        (profile.adaptive_speed_low + profile.adaptive_speed_high) * 0.5,
        profile.adaptive_speed_high,
        profile.adaptive_speed_high * 1.5,
    };
    const double gain_scale_max = std::max(1.0, std::max(profile.adaptive_slow_gain, profile.adaptive_fast_gain));
    for (double speed : speeds) {
        const double gain = profile.input_gain_mode == InputGainMode::Adaptive
                                ? AdaptiveInputGainFromTrackedSpeed(speed, profile)
                                : 1.0;
        const int bar_count = static_cast<int>(std::lround(ClampDouble(gain / gain_scale_max, 0.0, 1.0) *
                                                           static_cast<double>(kBarWidth)));
        const std::string bar(static_cast<size_t>(bar_count), '#');
        std::printf("  %8.1f   %8.3f  |%-*s|\n", speed, gain, kBarWidth, bar.c_str());
    }

    std::printf("\n  Radial gate examples before -> after\n");
    std::printf("  roll_in pitch_in   roll_out pitch_out  scale\n");
    const double max_value = static_cast<double>(profile.max_output);
    const std::array<std::pair<double, double>, 5> gate_samples = {{
        {max_value, 0.0},
        {max_value, max_value * 0.5},
        {max_value, max_value},
        {max_value * 0.75, max_value * 0.75},
        {max_value * 1.1, max_value * 0.9},
    }};
    for (const auto& sample : gate_samples) {
        double roll = sample.first;
        double pitch = sample.second;
        double gate_scale = 1.0;
        ApplyRadialGate(&roll, &pitch, profile, &gate_scale);
        std::printf("  %7.1f %8.1f   %8.1f %9.1f  %.3f\n",
                    sample.first,
                    sample.second,
                    roll,
                    pitch,
                    gate_scale);
    }

    std::printf("\n  Synthetic right-stick response: 40 ms roll flick, then release\n");
    std::printf("  time_ms dx filt gain gate aw pos vel output\n");
    double roll_position = 0.0;
    double pitch_position = 0.0;
    ElasticAxisState roll_state;
    ElasticAxisState pitch_state;
    RightStickSharedState shared_state;
    int roll_pulse = 0;
    int pitch_pulse = 0;
    int last_motion_tick = -1000000;
    const int total_ticks = static_cast<int>(std::lround(mapper_rate_hz * 1.2));
    const int sample_ticks = std::max(1, mapper_rate_hz / 20);
    const int idle_ticks = profile.return_idle_ms <= 0.0
                               ? 0
                               : static_cast<int>(std::ceil((profile.return_idle_ms / 1000.0) *
                                                            static_cast<double>(mapper_rate_hz)));
    for (int tick = 0; tick <= total_ticks; ++tick) {
        const double dx = tick < std::max(1, mapper_rate_hz / 25) ? 8.0 : 0.0;
        const double dy = 0.0;
        const bool mouse_moved = dx != 0.0 || dy != 0.0;
        if (mouse_moved) {
            last_motion_tick = tick;
        }
        const bool apply_idle_return = profile.return_enabled &&
                                       profile.return_rate > 0.0 &&
                                       !mouse_moved &&
                                       (tick - last_motion_tick) >= idle_ticks;
        const double combined_return_step =
            (apply_idle_return ? profile.return_rate / static_cast<double>(mapper_rate_hz) : 0.0) +
            (profile.constant_return_enabled
                 ? profile.constant_return_rate / static_cast<double>(mapper_rate_hz)
                 : 0.0);
        const double elastic_return_coefficient =
            profile.elastic_return_enabled ? profile.elastic_return_coefficient : 0.0;
        ShapeRightStickPositionPulses(dx,
                                      dy,
                                      gain_scale,
                                      combined_return_step,
                                      elastic_return_coefficient,
                                      mapper_dt,
                                      combined_return_step > 0.0 ||
                                          elastic_return_coefficient > 0.0,
                                      &roll_position,
                                      &pitch_position,
                                      &roll_state,
                                      &pitch_state,
                                      &shared_state,
                                      profile,
                                      &roll_pulse,
                                      &pitch_pulse);
        (void)pitch_pulse;
        if (tick % sample_ticks == 0 || tick == total_ticks) {
            const int elapsed_ms = static_cast<int>(std::lround(1000.0 * static_cast<double>(tick) /
                                                                static_cast<double>(mapper_rate_hz)));
            std::printf("  %7d %2.0f %4.1f %4.2f %4.2f %2s %5.1f %7.1f %6d\n",
                        elapsed_ms,
                        dx,
                        shared_state.filtered_roll_source,
                        shared_state.adaptive_gain,
                        shared_state.gate_scale,
                        shared_state.gimbal_antiwindup_active ? "Y" : "-",
                        roll_position,
                        roll_state.velocity,
                        roll_pulse);
        }
    }
}

void PrintMouseSpikeTest(const TrainerProfile& profile) {
    std::printf("\n--mouse-spike-test: profile=%s name='%s'\n",
                profile.source_file.c_str(),
                profile.name.c_str());
    TrainerProfile test_profile = profile;
    test_profile.despike_enabled = true;
    test_profile.despike_count_enabled = true;
    if (test_profile.despike_window < 3 || (test_profile.despike_window % 2) == 0) {
        test_profile.despike_window = 5;
    }
    RightStickSharedState state;
    std::printf("  despike=%s window=%d threshold_sigma=%.3f\n",
                profile.despike_enabled ? "true" : "forced-for-test",
                test_profile.despike_window,
                test_profile.despike_threshold_sigma);
    std::printf("  sample raw_dx filtered_dx verdict\n");
    const std::array<double, 13> samples = {{
        2.0, 2.0, 1.0, 2.0, 1.0, 2.0, 48.0, 2.0, 1.0, 2.0, 2.0, 1.0, 2.0,
    }};
    bool spike_removed = false;
    for (size_t i = 0; i < samples.size(); ++i) {
        double dx = samples[i];
        double dy = 0.0;
        ApplyRightStickInputPreprocessors(&dx,
                                          &dy,
                                          0.001,
                                          test_profile,
                                          &state);
        if (i == 6U) {
            spike_removed = std::abs(dx) < 10.0;
        }
        std::printf("  %6zu %6.1f %11.1f %s\n",
                    i,
                    samples[i],
                    dx,
                    (i == 6U ? (spike_removed ? "removed" : "passed") : ""));
    }
    std::printf("  result=%s despike_total=%llu despike_10s=%llu\n",
                spike_removed ? "PASS" : "FAIL",
                static_cast<unsigned long long>(state.despike_total_count),
                static_cast<unsigned long long>(state.despike_recent_count));
}

void PrintInputFilterPreview(const TrainerProfile& profile) {
    std::printf("\n--input-filter-preview: profile=%s name='%s'\n",
                profile.source_file.c_str(),
                profile.name.c_str());
    std::printf("  one_euro_min=%.3fHz beta=%.3f dcutoff=%.3fHz smoothing=%.3f\n",
                profile.one_euro_min_cutoff_hz,
                profile.one_euro_beta,
                profile.one_euro_dcutoff_hz,
                profile.smoothing);
    std::printf("  tick raw off smoothing one_euro\n");

    TrainerProfile off_profile = profile;
    TrainerProfile smoothing_profile = profile;
    TrainerProfile euro_profile = profile;
    off_profile.input_filter = InputFilterMode::Off;
    smoothing_profile.input_filter = InputFilterMode::Smoothing;
    euro_profile.input_filter = InputFilterMode::OneEuro;

    double smoothing_value = 0.0;
    RightStickSharedState euro_state;
    const double dt = 0.001;
    for (int tick = 0; tick <= 80; tick += 4) {
        const double step = tick >= 20 ? 16.0 : 0.0;
        const double sine = std::sin(static_cast<double>(tick) * 0.45) * 1.5;
        const double raw = step + sine;
        const double off = raw;
        const double smooth = (smoothing_profile.smoothing * smoothing_value) +
                              ((1.0 - smoothing_profile.smoothing) * raw);
        smoothing_value = smooth;
        double euro = raw;
        double dy = 0.0;
        ApplyRightStickInputPreprocessors(&euro, &dy, dt, euro_profile, &euro_state);
        std::printf("  %4d %6.2f %6.2f %9.2f %8.2f\n",
                    tick,
                    raw,
                    off,
                    smooth,
                    euro);
    }
}

void PrintOutputCurvePreview(const TrainerProfile& profile) {
    std::printf("\n--output-curve-preview: profile=%s name='%s'\n",
                profile.source_file.c_str(),
                profile.name.c_str());
    std::printf("  expo=%.3f actual_center=%.3f actual_max=%.3f actual_expo=%.3f nodes=%s [%s]\n",
                profile.expo,
                profile.actual_center,
                profile.actual_max,
                profile.actual_expo,
                profile.output_shape.enabled ? "on" : "off",
                DescribeStickShapeNodes(profile.output_shape).c_str());
    std::printf("  in expo nodes actual\n");
    TrainerProfile expo_profile = profile;
    TrainerProfile nodes_profile = profile;
    TrainerProfile actual_profile = profile;
    expo_profile.output_curve = OutputCurveMode::Expo;
    nodes_profile.output_curve = OutputCurveMode::Nodes;
    actual_profile.output_curve = OutputCurveMode::Actual;
    for (int i = 0; i <= 10; ++i) {
        const double input = static_cast<double>(profile.max_output) * static_cast<double>(i) / 10.0;
        const double expo = ShapeTrainerAxis(input, expo_profile);
        const double nodes = ShapeTrainerAxis(input, nodes_profile);
        const double actual = ShapeTrainerAxis(input, actual_profile);
        std::printf("  %.2f %5.1f %5.1f %6.1f\n",
                    static_cast<double>(i) / 10.0,
                    expo,
                    nodes,
                    actual);
    }
}

std::string KeyboardBlockSignature(const KeyboardLeftStickProfile& keyboard) {
    if (!keyboard.enabled || !keyboard.block_selected_keys) {
        return "off";
    }
    char text[96];
    std::snprintf(text,
                  sizeof(text),
                  "%d:%d:%d:%d:%d",
                  keyboard.throttle_up_vk,
                  keyboard.throttle_down_vk,
                  keyboard.throttle_cut_vk,
                  keyboard.yaw_left_vk,
                  keyboard.yaw_right_vk);
    return text;
}

bool KeyboardWantsWooting(const KeyboardLeftStickProfile& keyboard) {
    return keyboard.enabled &&
           (keyboard.input_source == KeyboardLeftStickProfile::InputSource::WootingAnalog ||
            keyboard.input_source == KeyboardLeftStickProfile::InputSource::Auto);
}

bool EnsureWootingForKeyboard(const KeyboardLeftStickProfile& keyboard,
                              WootingAnalogSdk* sdk,
                              bool* active,
                              bool print_status) {
    if (active) {
        *active = false;
    }
    if (!KeyboardWantsWooting(keyboard)) {
        return true;
    }

    WootingAnalogKeycodeMode mode = WootingAnalogKeycodeMode::VirtualKeyTranslate;
    if (!ParseWootingAnalogKeycodeMode(keyboard.analog_keycode_mode, &mode)) {
        std::fprintf(stderr,
                     "keyboard_left_stick.analog_keycode_mode is invalid: %s\n",
                     keyboard.analog_keycode_mode.c_str());
        return false;
    }

    std::string error;
    if (!sdk || !sdk->Initialise(mode, &error)) {
        if (keyboard.require_analog ||
            keyboard.input_source == KeyboardLeftStickProfile::InputSource::WootingAnalog) {
            std::fprintf(stderr,
                         "keyboard_left_stick Wooting Analog init failed: %s.\n",
                         error.c_str());
            return false;
        }
        if (print_status) {
            std::printf("keyboard_left_stick analog=unavailable fallback=gameinput reason=%s\n",
                        error.c_str());
        }
        return true;
    }

    if (sdk->DeviceCount() <= 0) {
        if (keyboard.require_analog ||
            keyboard.input_source == KeyboardLeftStickProfile::InputSource::WootingAnalog) {
            std::fprintf(stderr,
                         "keyboard_left_stick Wooting Analog init failed: SDK loaded but no analog devices were reported.\n");
            return false;
        }
        if (print_status) {
            std::printf("keyboard_left_stick analog=unavailable fallback=gameinput reason=no-devices\n");
        }
        return true;
    }

    if (active) {
        *active = true;
    }
    if (print_status) {
        std::printf("keyboard_left_stick analog=active dll=%s devices=%d mode=%s\n",
                    sdk->DllName().c_str(),
                    sdk->DeviceCount(),
                    keyboard.analog_keycode_mode.c_str());
    }
    return true;
}

void PrintGuidedTuningNotes(const TrainerProfile& profile) {
    std::printf("\nGuided setup checks:\n");
    std::printf("  1. GX12 SYS -> Hardware -> USB-VCP should be SBUS Trainer.\n");
    std::printf("  2. Model Trainer Mode should be Master/Serial.\n");
    if (profile.control_mode == ControlMode::DroneMouseAim) {
        std::printf("  3. SYS -> Trainer should use Ail=REPL, Ele=REPL, Thr=OFF, Rud=ADD.\n");
        std::printf("     Rud=ADD gives additive yaw: physical left stick yaw plus reticle-aim yaw.\n");
        std::printf("  4. Model mixes should keep local throttle/yaw inputs; trainer ADD is applied before the Rud input reaches the mix.\n");
    } else {
        std::printf("  3. SYS -> Trainer should replace Ail/Ele only; set Thr/Rud trainer modes OFF.\n");
        std::printf("  4. Model mixes should keep local throttle/yaw on I2/I3 and route TR1/TR2 only for mouse axes.\n");
    }
    if (profile.keyboard_left.enabled || profile.right_mouse_left.enabled) {
        std::printf("     Button/keyboard left-stick profiles are sim-only: route trainer Thr/Rud only for VelociDrone tests.\n");
    }
    if (profile.mouse_left.enabled) {
        std::printf("     Second-mouse left-stick profiles are sim-only: route trainer Thr/Rud only for VelociDrone tests.\n");
    }
    if (profile.control_mode == ControlMode::DroneMouseAim) {
        std::printf("     Reticle Aim is open-loop and sim-first: use channel monitor / VelociDrone before RF use.\n");
    }
    std::printf("  5. On SYS -> Trainer, expect RX near %d and Mix near 1000 when frame_rate >= 1000.\n",
                profile.frame_rate_hz);
    std::printf("  %s stops and sends neutral SBUS.\n\n", profile.stop_key.c_str());
}

bool OpenCsvLog(const TrainerProfile& profile, std::ofstream* csv) {
    if (!profile.log_csv || profile.log_path.empty()) {
        return false;
    }

    std::error_code ec;
    const std::filesystem::path log_path(profile.log_path);
    if (log_path.has_parent_path()) {
        std::filesystem::create_directories(log_path.parent_path(), ec);
        if (ec) {
            std::fprintf(stderr,
                         "warning: could not create log directory '%s': %s\n",
                         log_path.parent_path().string().c_str(),
                         ec.message().c_str());
        }
    }

    csv->open(log_path, std::ios::out | std::ios::trunc);
    if (!*csv) {
        std::fprintf(stderr,
                     "warning: could not open CSV log '%s'. Continuing without CSV.\n",
                     profile.log_path.c_str());
        return false;
    }

    *csv << "time_s,frame,mouse_events,input_aux,keyboard_events,dx,dy,mouse_raw_dx,mouse_raw_dy,mouse_filtered_dx,mouse_filtered_dy,despike_count_10s,despike_count_total,left_dx,left_dy,left_yaw_raw,left_yaw_filtered,left_yaw_despike_count_10s,left_yaw_despike_count_total,left_yaw_position_model,left_yaw_adaptive_gain,left_yaw_gate_scale,left_yaw_gimbal_antiwindup_active,position_model,adaptive_gain,gate_scale,gimbal_antiwindup_active,roll_position,pitch_position,roll_velocity,pitch_velocity,roll,pitch,throttle,yaw,aim_x,aim_y,analog_throttle_up,analog_throttle_down,analog_yaw_left,analog_yaw_right,analog_cut,late_us\n";
    return true;
}

int RunTrainerProfile(const TrainerProfile& initial_profile, bool guided, bool live_reload) {
    TrainerProfile active_profile = initial_profile;
    g_stop_virtual_key.store(active_profile.stop_key_vk, std::memory_order_release);
    g_freeze_virtual_key.store(active_profile.freeze_key_vk, std::memory_order_release);
    const std::string profile_path = initial_profile.source_file;
    const std::string port_name = ResolveTrainerPortName(initial_profile.port.c_str());
    const std::string path = WindowsComPath(port_name.c_str());
    if (path.empty()) {
        std::fprintf(stderr, "trainer profile requires a COM port, for example COM3, or port=\"auto\".\n");
        return 2;
    }

    PrintTrainerProfile(active_profile, guided);
    if (guided) {
        PrintGuidedTuningNotes(active_profile);
    }
    if (active_profile.resolution_mode == TrainerResolutionMode::Gx12_2x &&
        !kGx12Resolution2xBuild) {
        std::fprintf(stderr,
                     "trainer profile error: gx12_2x requires a 2x-capable gx12mouse build.\n");
        return 2;
    }
    if (live_reload) {
        std::printf("  live_reload=true; mapper and keyboard edits to this TOML apply while running. Port/log changes still require restart.\n");
    }

    HANDLE launcher_stop_event = CreateEventW(nullptr, TRUE, FALSE, kLauncherStopEventName);
    if (launcher_stop_event == nullptr) {
        std::fprintf(stderr,
                     "warning: launcher stop event unavailable: le=%lu\n",
                     static_cast<unsigned long>(GetLastError()));
    }

    HANDLE serial = CreateFileA(path.c_str(),
                                GENERIC_WRITE,
                                0,
                                nullptr,
                                OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL,
                                nullptr);
    if (serial == INVALID_HANDLE_VALUE) {
        std::fprintf(stderr,
                     "CreateFile(%s) failed: le=%lu\n",
                     path.c_str(),
                     static_cast<unsigned long>(GetLastError()));
        if (launcher_stop_event) CloseHandle(launcher_stop_event);
        return 1;
    }

    DCB dcb{};
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(serial, &dcb)) {
        std::fprintf(stderr, "GetCommState failed: le=%lu\n", static_cast<unsigned long>(GetLastError()));
        CloseHandle(serial);
        if (launcher_stop_event) CloseHandle(launcher_stop_event);
        return 1;
    }
    dcb.BaudRate = CBR_115200;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    if (!SetCommState(serial, &dcb)) {
        std::fprintf(stderr, "SetCommState failed: le=%lu\n", static_cast<unsigned long>(GetLastError()));
        CloseHandle(serial);
        if (launcher_stop_event) CloseHandle(launcher_stop_event);
        return 1;
    }

    COMMTIMEOUTS timeouts{};
    timeouts.WriteTotalTimeoutConstant = 20;
    timeouts.WriteTotalTimeoutMultiplier = 1;
    SetCommTimeouts(serial, &timeouts);

    std::ofstream csv;
    const bool csv_open = OpenCsvLog(active_profile, &csv);

    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

#if defined(GX12_HAS_GAMEINPUT)
    GameInputMouseCapture gameinput_capture;
    SelectedKeyboardBlocker key_blocker;
    WootingAnalogSdk wooting_sdk;
    bool wooting_active = false;
    if (!StartGameInputMouseCapture(&gameinput_capture,
                                    live_reload || active_profile.keyboard_left.enabled,
                                    active_profile.mouse_right_device_token,
                                    active_profile.mouse_left.enabled ? active_profile.mouse_left_device_token : "")) {
        CloseHandle(serial);
        if (launcher_stop_event) CloseHandle(launcher_stop_event);
        return 1;
    }
    if (!EnsureWootingForKeyboard(active_profile.keyboard_left,
                                  &wooting_sdk,
                                  &wooting_active,
                                  true)) {
        StopGameInputMouseCapture(&gameinput_capture);
        CloseHandle(serial);
        if (launcher_stop_event) CloseHandle(launcher_stop_event);
        return 1;
    }
    std::string keyboard_block_signature = KeyboardBlockSignature(active_profile.keyboard_left);
    if (active_profile.keyboard_left.enabled && active_profile.keyboard_left.block_selected_keys) {
        key_blocker.Start(active_profile.keyboard_left);
    }
    if (active_profile.mouse_left.enabled) {
        std::printf("input_capture=gameinput-background dual-mouse. right=%s left=%s. Keep VelociDrone foreground; %s toggles cursor lock; %s stops and sends neutral SBUS.\n",
                    active_profile.mouse_right_device_token.c_str(),
                    active_profile.mouse_left_device_token.c_str(),
                    active_profile.freeze_key.c_str(),
                    active_profile.stop_key.c_str());
    } else if (active_profile.right_mouse_left.enabled) {
        std::printf("input_capture=gameinput-background right-mouse buttons+scroll. right=%s mapping=%s. Keep VelociDrone foreground; %s toggles cursor lock; %s stops and sends neutral SBUS.\n",
                    active_profile.mouse_right_device_token.c_str(),
                    active_profile.right_mouse_left.swap_axes
                        ? "mouse4/mouse5=throttle scroll=yaw"
                        : "mouse4/mouse5=yaw scroll=throttle",
                    active_profile.freeze_key.c_str(),
                    active_profile.stop_key.c_str());
    } else if (active_profile.keyboard_left.enabled) {
        std::printf("input_capture=gameinput-background mouse+keyboard. keyboard_source=%s%s. Keep VelociDrone foreground; key_block=%s; %s toggles cursor lock; %s stops and sends neutral SBUS.\n",
                    KeyboardInputSourceName(active_profile.keyboard_left.input_source),
                    wooting_active ? "(active)" : "",
                    active_profile.keyboard_left.block_selected_keys ? "selected" : "off",
                    active_profile.freeze_key.c_str(),
                    active_profile.stop_key.c_str());
    } else {
        std::printf("mouse_capture=gameinput-background. Keep VelociDrone foreground; %s toggles cursor lock; %s stops and sends neutral SBUS.\n",
                    active_profile.freeze_key.c_str(),
                    active_profile.stop_key.c_str());
    }
#else
    if (active_profile.keyboard_left.enabled || active_profile.right_mouse_left.enabled) {
        std::fprintf(stderr,
                     "keyboard/right-mouse left-stick sources require a Microsoft.GameInput build; this binary only has foreground Raw Input.\n");
        CloseHandle(serial);
        if (launcher_stop_event) CloseHandle(launcher_stop_event);
        return 2;
    }
    HINSTANCE instance = GetModuleHandleW(nullptr);
    g_mouse_stats = MouseRateStats{};
    g_mouse_rate_stop.store(false, std::memory_order_release);

    HWND hwnd = CreateMouseRateWindow(instance, MouseRateMode::Foreground);
    if (!hwnd) {
        std::fprintf(stderr, "No HWND available for trainer profile mode.\n");
        CloseHandle(serial);
        if (launcher_stop_event) CloseHandle(launcher_stop_event);
        return 1;
    }

    if (!RegisterMouseRawInput(hwnd, MouseRateMode::Foreground)) {
        DestroyWindow(hwnd);
        CloseHandle(serial);
        if (launcher_stop_event) CloseHandle(launcher_stop_event);
        return 1;
    }
    std::printf("mouse_capture=foreground-rawinput. Focus the GX12 Mouse Rate Probe window; %s toggles cursor lock; %s stops.\n",
                active_profile.freeze_key.c_str(),
                active_profile.stop_key.c_str());
#endif

    if (!guided) {
        if (active_profile.control_mode == ControlMode::DroneMouseAim) {
            std::printf("control_mode=drone_mouse_aim: mouse moves a bounded aim reticle; reticle error drives roll/pitch/yaw. Throttle is not automated.\n");
            std::printf("For physical-stick additive yaw, set GX12 SYS -> Trainer Rud=ADD, not REPL; keep Thr=OFF.\n");
        }
        if (active_profile.mouse_left.enabled) {
            std::printf("Sends SBUS ch1/ch2 from right mouse and ch3/ch4 from left mouse; remaining trainer channels centered. Sim-only.\n\n");
        } else if (active_profile.right_mouse_left.enabled) {
            std::printf("Sends SBUS ch1/ch2 from right mouse and ch3/ch4 from right mouse Mouse4/Mouse5 + scroll; remaining trainer channels centered. Sim-only.\n\n");
        } else if (active_profile.keyboard_left.enabled) {
            std::printf("Sends SBUS ch1=roll, ch2=pitch, ch3=keyboard throttle, ch4=keyboard yaw; remaining trainer channels centered. Sim-only.\n\n");
        } else if (active_profile.control_mode == ControlMode::DroneMouseAim) {
            std::printf("Sends SBUS ch1=reticle-aim roll, ch2=reticle-aim pitch, ch3=low-throttle safety, ch4=reticle-aim yaw; remaining trainer channels centered.\n\n");
        } else {
            std::printf("Sends SBUS ch1=roll, ch2=pitch, ch3=low-throttle safety, ch4=center; remaining trainer channels centered.\n\n");
        }
    }

    using clock = std::chrono::steady_clock;
    const auto start = clock::now();
    const bool indefinite = active_profile.seconds == kTrainerProfileIndefiniteSeconds;
    const auto end = start + std::chrono::seconds(active_profile.seconds);
    auto next_frame = start;
    int current_frame_rate_hz = active_profile.frame_rate_hz;
    auto frame_period = std::chrono::microseconds(1000000 / current_frame_rate_hz);
    int mapper_rate_hz = std::min(current_frame_rate_hz, kTrainerMapperReferenceHz);
    auto next_mapper = start;
    auto mapper_period = std::chrono::microseconds(1000000 / mapper_rate_hz);
    auto return_idle_period = std::chrono::duration<double, std::milli>(active_profile.return_idle_ms);
    auto left_yaw_return_idle_period =
        std::chrono::duration<double, std::milli>(active_profile.mouse_left.yaw_return_idle_ms);
    double gain_scale = TrainerRateGainScale(mapper_rate_hz);
    double return_step = active_profile.return_rate / static_cast<double>(mapper_rate_hz);
    auto next_print = start + std::chrono::milliseconds(250);
    auto next_profile_reload_check = start + std::chrono::milliseconds(250);
    std::filesystem::file_time_type last_profile_write{};
    if (live_reload && !profile_path.empty()) {
        std::error_code ec;
        last_profile_write = std::filesystem::last_write_time(profile_path, ec);
        if (ec) {
            std::fprintf(stderr,
                         "warning: profile live reload cannot stat '%s': %s\n",
                         profile_path.c_str(),
                         ec.message().c_str());
        }
    }
    auto last_mouse_motion = start;
    auto last_left_yaw_motion = start;
#if defined(GX12_HAS_GAMEINPUT)
    GameInputMouseRateSnapshot last_mapper = SnapshotGameInputStats(gameinput_capture.stats);
    GameInputMouseRateSnapshot last_print = last_mapper;
    GameInputMouseRoleSnapshot last_right_mapper = SnapshotGameInputRole(gameinput_capture.stats.right_mouse);
    GameInputMouseRoleSnapshot last_left_mapper = SnapshotGameInputRole(gameinput_capture.stats.left_mouse);
    GameInputMouseRoleSnapshot last_right_print = last_right_mapper;
    GameInputMouseRoleSnapshot last_left_print = last_left_mapper;
#else
    MouseRateStats last_mapper = g_mouse_stats;
    MouseRateStats last_print = g_mouse_stats;
#endif
    uint32_t frames = 0;
    uint32_t mapper_updates = 0;
    uint64_t late_frames = 0;
    int64_t max_late_us = 0;
    int last_roll = 0;
    int last_pitch = 0;
    int last_throttle = QuantizeTrainerProfileOutput(
        static_cast<double>(TrainerProfileLowValue()),
        active_profile.resolution_mode);
    int last_yaw = 0;
    int64_t last_mapper_dx = 0;
    int64_t last_mapper_dy = 0;
    int64_t last_right_mapper_wheel_y = 0;
    int64_t last_left_mapper_dx = 0;
    int64_t last_left_mapper_dy = 0;
    double filtered_roll = 0.0;
    double filtered_pitch = 0.0;
    double filtered_left_yaw = 0.0;
    double left_yaw_position = 0.0;
    double left_yaw_dummy_position = 0.0;
    ElasticAxisState roll_elastic_state;
    ElasticAxisState pitch_elastic_state;
    ElasticAxisState left_yaw_elastic_state;
    ElasticAxisState left_yaw_dummy_state;
    RightStickSharedState right_stick_state;
    RightStickSharedState left_yaw_mapper_state;
    MouseAimState mouse_aim_state;
    double keyboard_throttle = static_cast<double>(TrainerProfileLowValue());
    double keyboard_yaw = 0.0;
    double right_mouse_left_yaw_target = 0.0;
    double right_mouse_left_yaw_output = 0.0;
    AnalogKeyDepths last_analog_depths;
    bool last_freeze_down = false;

    bool stop_requested = false;
    while (!stop_requested && (indefinite || clock::now() < end)) {
        if (LauncherStopRequested(launcher_stop_event)) {
            std::printf("\nlauncher requested profile switch; stopping and sending neutral SBUS.\n");
            stop_requested = true;
            break;
        }
#if defined(GX12_HAS_GAMEINPUT)
        if (StopKeyDown()) {
            stop_requested = true;
            break;
        }
        {
            const GameInputMouseRateSnapshot current = SnapshotGameInputStats(gameinput_capture.stats);
            const bool freeze_down = FreezeKeyDown();
            if (freeze_down && !last_freeze_down) {
                ToggleCursorLock();
            }
            last_freeze_down = freeze_down;
        }
#else
        if (g_mouse_rate_stop.load(std::memory_order_acquire)) {
            stop_requested = true;
            break;
        }
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
#endif

        const auto now = clock::now();
        if (live_reload && now >= next_profile_reload_check && !profile_path.empty()) {
            std::error_code ec;
            const auto write_time = std::filesystem::last_write_time(profile_path, ec);
            if (!ec && write_time != last_profile_write) {
                TrainerProfile reloaded;
                if (LoadTrainerProfile(profile_path.c_str(), &reloaded)) {
                    if (reloaded.resolution_mode == TrainerResolutionMode::Gx12_2x &&
                        !kGx12Resolution2xBuild) {
                        std::fprintf(stderr,
                                     "\nprofile_reload=failed; gx12_2x requires a 2x-capable gx12mouse build.\n");
                        last_profile_write = write_time;
                        continue;
                    }
                    const bool port_changed = reloaded.port != active_profile.port;
                    const bool log_changed = reloaded.log_path != active_profile.log_path ||
                                             reloaded.log_csv != active_profile.log_csv ||
                                             reloaded.log_every_n_frames != active_profile.log_every_n_frames;
                    const bool control_changed = reloaded.control_mode != active_profile.control_mode;
                    const bool resolution_changed =
                        reloaded.resolution_mode != active_profile.resolution_mode;
                    const bool right_mapper_model_changed =
                        reloaded.position_model != active_profile.position_model ||
                        reloaded.input_gain_mode != active_profile.input_gain_mode ||
                        reloaded.gate_shape != active_profile.gate_shape;
                    const bool left_yaw_mapper_model_changed =
                        reloaded.mouse_left.yaw_shaping_enabled != active_profile.mouse_left.yaw_shaping_enabled ||
                        reloaded.mouse_left.yaw_position_model != active_profile.mouse_left.yaw_position_model ||
                        reloaded.mouse_left.yaw_input_filter != active_profile.mouse_left.yaw_input_filter ||
                        reloaded.mouse_left.yaw_input_gain_mode != active_profile.mouse_left.yaw_input_gain_mode ||
                        reloaded.mouse_left.yaw_gate_shape != active_profile.mouse_left.yaw_gate_shape;
                    const bool right_mouse_left_changed =
                        reloaded.right_mouse_left.enabled != active_profile.right_mouse_left.enabled ||
                        reloaded.right_mouse_left.swap_axes != active_profile.right_mouse_left.swap_axes ||
                        reloaded.right_mouse_left.yaw_pulse != active_profile.right_mouse_left.yaw_pulse ||
                        reloaded.right_mouse_left.yaw_slew_rate != active_profile.right_mouse_left.yaw_slew_rate;
                    active_profile = reloaded;
                    g_stop_virtual_key.store(active_profile.stop_key_vk, std::memory_order_release);
                    g_freeze_virtual_key.store(active_profile.freeze_key_vk, std::memory_order_release);
#if defined(GX12_HAS_GAMEINPUT)
                    gameinput_capture.stats.right_device_token =
                        active_profile.mouse_right_device_token.empty() ? "auto" : active_profile.mouse_right_device_token;
                    gameinput_capture.stats.left_device_token =
                        active_profile.mouse_left.enabled ? active_profile.mouse_left_device_token : "";
#endif
                    if (control_changed || right_mapper_model_changed || resolution_changed) {
                        filtered_roll = 0.0;
                        filtered_pitch = 0.0;
                        roll_elastic_state = ElasticAxisState{};
                        pitch_elastic_state = ElasticAxisState{};
                        right_stick_state = RightStickSharedState{};
                        mouse_aim_state = MouseAimState{};
                        last_roll = 0;
                        last_pitch = 0;
                        last_yaw = 0;
                    }
                    if (left_yaw_mapper_model_changed) {
                        filtered_left_yaw = 0.0;
                        left_yaw_position = 0.0;
                        left_yaw_dummy_position = 0.0;
                        left_yaw_elastic_state = ElasticAxisState{};
                        left_yaw_dummy_state = ElasticAxisState{};
                        left_yaw_mapper_state = RightStickSharedState{};
                        if (active_profile.control_mode == ControlMode::DirectMouse) {
                            last_yaw = 0;
                        }
                    }
                    if (right_mouse_left_changed) {
                        right_mouse_left_yaw_target = 0.0;
                        right_mouse_left_yaw_output = 0.0;
                        if (active_profile.control_mode == ControlMode::DirectMouse) {
                            last_yaw = 0;
                        }
                    }
                    if (active_profile.frame_rate_hz != current_frame_rate_hz) {
                        current_frame_rate_hz = active_profile.frame_rate_hz;
                        frame_period = std::chrono::microseconds(1000000 / current_frame_rate_hz);
                        mapper_rate_hz = std::min(current_frame_rate_hz, kTrainerMapperReferenceHz);
                        mapper_period = std::chrono::microseconds(1000000 / mapper_rate_hz);
                        next_frame = now + frame_period;
                        next_mapper = now + mapper_period;
                    }
                    return_idle_period = std::chrono::duration<double, std::milli>(active_profile.return_idle_ms);
                    left_yaw_return_idle_period =
                        std::chrono::duration<double, std::milli>(active_profile.mouse_left.yaw_return_idle_ms);
                    gain_scale = TrainerRateGainScale(mapper_rate_hz);
                    return_step = active_profile.return_rate / static_cast<double>(mapper_rate_hz);
#if defined(GX12_HAS_GAMEINPUT)
                    if (!EnsureWootingForKeyboard(active_profile.keyboard_left,
                                                  &wooting_sdk,
                                                  &wooting_active,
                                                  true)) {
                        std::fprintf(stderr,
                                     "\nprofile_reload=failed; analog source unavailable, stopping safely.\n");
                        stop_requested = true;
                        break;
                    }
                    const std::string new_block_signature = KeyboardBlockSignature(active_profile.keyboard_left);
                    const bool should_restart_blocker =
                        active_profile.keyboard_left.enabled &&
                        active_profile.keyboard_left.block_selected_keys;
                    if (new_block_signature != keyboard_block_signature || should_restart_blocker) {
                        key_blocker.Stop();
                        if (should_restart_blocker) {
                            key_blocker.Start(active_profile.keyboard_left);
                        }
                        keyboard_block_signature = new_block_signature;
                    }
#endif
                    if (!active_profile.keyboard_left.enabled &&
                        !active_profile.mouse_left.enabled &&
                        !active_profile.right_mouse_left.enabled) {
                        keyboard_throttle = static_cast<double>(TrainerProfileLowValue());
                        keyboard_yaw = 0.0;
                        right_mouse_left_yaw_target = 0.0;
                        right_mouse_left_yaw_output = 0.0;
                        last_throttle = QuantizeTrainerProfileOutput(
                            keyboard_throttle,
                            active_profile.resolution_mode);
                        last_yaw = 0;
                    }
                    std::printf("\nprofile_reload=ok frame_rate=%d mode=%s resolution=%s mouse_stick=%s mouse_left=%s right_mouse_left=%s keyboard=%s block_keys=%s%s%s\n",
                                active_profile.frame_rate_hz,
                                ControlModeName(active_profile.control_mode),
                                TrainerResolutionModeName(active_profile.resolution_mode),
                                active_profile.mouse_right_stick_enabled ? "on" : "off",
                                active_profile.mouse_left.enabled ? "on" : "off",
                                active_profile.right_mouse_left.enabled ? "on" : "off",
                                active_profile.keyboard_left.enabled ? "on" : "off",
                                active_profile.keyboard_left.block_selected_keys ? "on" : "off",
                                port_changed ? " note=port-change-requires-restart" : "",
                                log_changed ? " note=log-change-requires-restart" : "");
                    std::fflush(stdout);
                    last_profile_write = write_time;
                } else {
                    std::fprintf(stderr,
                                 "\nprofile_reload=failed; keeping previous valid settings.\n");
                }
            }
            do {
                next_profile_reload_check += std::chrono::milliseconds(250);
            } while (next_profile_reload_check <= now);
        }

        if (now >= next_mapper) {
#if defined(GX12_HAS_GAMEINPUT)
            const GameInputMouseRateSnapshot current = SnapshotGameInputStats(gameinput_capture.stats);
            const GameInputMouseRoleSnapshot current_right = SnapshotGameInputRole(gameinput_capture.stats.right_mouse);
            const GameInputMouseRoleSnapshot current_left = SnapshotGameInputRole(gameinput_capture.stats.left_mouse);
            last_mapper_dx = current_right.dx_sum - last_right_mapper.dx_sum;
            last_mapper_dy = current_right.dy_sum - last_right_mapper.dy_sum;
            last_right_mapper_wheel_y =
                current_right.wheel_y_sum - last_right_mapper.wheel_y_sum;
            last_left_mapper_dx = current_left.dx_sum - last_left_mapper.dx_sum;
            last_left_mapper_dy = current_left.dy_sum - last_left_mapper.dy_sum;
            last_right_mapper = current_right;
            last_left_mapper = current_left;
#else
            const MouseRateStats current = g_mouse_stats;
            last_mapper_dx = current.dx_sum - last_mapper.dx_sum;
            last_mapper_dy = current.dy_sum - last_mapper.dy_sum;
            last_right_mapper_wheel_y = 0;
            last_left_mapper_dx = 0;
            last_left_mapper_dy = 0;
#endif
            last_mapper = current;

            if (active_profile.mouse_right_stick_enabled) {
                if (active_profile.control_mode == ControlMode::DroneMouseAim) {
                    const int64_t aim_dx = active_profile.swap_axes ? -last_mapper_dy : last_mapper_dx;
                    const int64_t aim_dy = active_profile.swap_axes ? last_mapper_dx : last_mapper_dy;
                    UpdateDroneMouseAim(aim_dx,
                                        aim_dy,
                                        1.0 / static_cast<double>(mapper_rate_hz),
                                        active_profile.mouse_aim,
                                        &mouse_aim_state,
                                        &last_roll,
                                        &last_pitch,
                                        &last_yaw);
                } else {
                    mouse_aim_state = MouseAimState{};
                    const double roll_source = active_profile.swap_axes ? static_cast<double>(-last_mapper_dy) : static_cast<double>(last_mapper_dx);
                    const double pitch_source = active_profile.swap_axes ? static_cast<double>(last_mapper_dx) : static_cast<double>(-last_mapper_dy);
                    const double mapper_dt = 1.0 / static_cast<double>(mapper_rate_hz);
                    const bool use_position_mapper = RightStickNeedsPositionMapper(active_profile);
                    if (use_position_mapper) {
                        const bool mouse_moved = (last_mapper_dx != 0 || last_mapper_dy != 0);
                        if (mouse_moved) {
                            last_mouse_motion = now;
                        }
                        const bool apply_idle_return = active_profile.return_enabled &&
                                                       active_profile.return_rate > 0.0 &&
                                                       !mouse_moved &&
                                                       (active_profile.return_idle_ms <= 0.0 ||
                                                        now - last_mouse_motion >= return_idle_period);
                        const double combined_return_step =
                            (apply_idle_return ? return_step : 0.0) +
                            (active_profile.constant_return_enabled
                                 ? active_profile.constant_return_rate / static_cast<double>(mapper_rate_hz)
                                 : 0.0);
                        const double elastic_return_coefficient =
                            active_profile.elastic_return_enabled
                                ? active_profile.elastic_return_coefficient
                                : 0.0;
                        ShapeRightStickPositionPulses(roll_source,
                                                      pitch_source,
                                                      gain_scale,
                                                      combined_return_step,
                                                      elastic_return_coefficient,
                                                      mapper_dt,
                                                      combined_return_step > 0.0 ||
                                                          elastic_return_coefficient > 0.0,
                                                      &filtered_roll,
                                                      &filtered_pitch,
                                                      &roll_elastic_state,
                                                      &pitch_elastic_state,
                                                      &right_stick_state,
                                                      active_profile,
                                                      &last_roll,
                                                      &last_pitch);
                    } else {
                        double filtered_roll_source = roll_source;
                        double filtered_pitch_source = pitch_source;
                        ApplyRightStickInputPreprocessors(&filtered_roll_source,
                                                          &filtered_pitch_source,
                                                          mapper_dt,
                                                          active_profile,
                                                          &right_stick_state);
                        const double input_gain = UpdateAdaptiveInputGain(filtered_roll_source,
                                                                          filtered_pitch_source,
                                                                          mapper_dt,
                                                                          active_profile,
                                                                          &right_stick_state);
                        last_roll = ShapeTrainerPulse(filtered_roll_source,
                                                      active_profile.roll_gain * gain_scale * input_gain,
                                                      active_profile.invert_roll,
                                                      &filtered_roll,
                                                      active_profile);
                        last_pitch = ShapeTrainerPulse(filtered_pitch_source,
                                                       active_profile.pitch_gain * gain_scale * input_gain,
                                                       active_profile.invert_pitch,
                                                       &filtered_pitch,
                                                       active_profile);
                    }
                }
            } else {
                filtered_roll = 0.0;
                filtered_pitch = 0.0;
                roll_elastic_state = ElasticAxisState{};
                pitch_elastic_state = ElasticAxisState{};
                right_stick_state = RightStickSharedState{};
                mouse_aim_state = MouseAimState{};
                last_roll = 0;
                last_pitch = 0;
                if (active_profile.control_mode == ControlMode::DroneMouseAim) {
                    last_yaw = 0;
                }
            }

            if (active_profile.mouse_left.enabled) {
#if defined(GX12_HAS_GAMEINPUT)
                const double mapper_dt = 1.0 / static_cast<double>(mapper_rate_hz);
                const double throttle_source = active_profile.mouse_left.swap_axes
                    ? static_cast<double>(last_left_mapper_dx)
                    : static_cast<double>(-last_left_mapper_dy);
                double throttle_axis = throttle_source;
                if (active_profile.mouse_left.invert_throttle) {
                    throttle_axis = -throttle_axis;
                }
                keyboard_throttle = ClampDouble(keyboard_throttle +
                                                throttle_axis * active_profile.mouse_left.throttle_rate,
                                                static_cast<double>(TrainerProfileLowValue()),
                                                static_cast<double>(kTrainerProfileDomainLimit));
                if (active_profile.mouse_left.throttle_return_enabled &&
                    std::abs(throttle_axis) < 0.001) {
                    keyboard_throttle = MoveTowardDouble(keyboard_throttle,
                                                        static_cast<double>(TrainerProfileLowValue()),
                                                        active_profile.mouse_left.throttle_return_rate *
                                                        mapper_dt);
                }
                last_throttle = QuantizeTrainerProfileOutput(keyboard_throttle,
                                                             active_profile.resolution_mode);

                const double raw_yaw_source = active_profile.mouse_left.swap_axes
                    ? static_cast<double>(last_left_mapper_dy)
                    : static_cast<double>(last_left_mapper_dx);
                double yaw_source = raw_yaw_source;
                if (!active_profile.mouse_left.yaw_shaping_enabled &&
                    active_profile.mouse_left.invert_yaw) {
                    yaw_source = -yaw_source;
                }
                const bool left_yaw_moved = std::abs(raw_yaw_source) > 0.001;
                if (left_yaw_moved) {
                    last_left_yaw_motion = now;
                }
                if (active_profile.mouse_left.yaw_shaping_enabled) {
                    const bool apply_left_yaw_idle_return =
                        active_profile.mouse_left.yaw_return_enabled &&
                        active_profile.mouse_left.yaw_return_rate > 0.0 &&
                        !left_yaw_moved &&
                        (active_profile.mouse_left.yaw_return_idle_ms <= 0.0 ||
                         now - last_left_yaw_motion >= left_yaw_return_idle_period);
                    const double combined_return_step =
                        (apply_left_yaw_idle_return
                             ? active_profile.mouse_left.yaw_return_rate / static_cast<double>(mapper_rate_hz)
                             : 0.0) +
                        (active_profile.mouse_left.yaw_constant_return_enabled
                             ? active_profile.mouse_left.yaw_constant_return_rate / static_cast<double>(mapper_rate_hz)
                             : 0.0);
                    const double elastic_return_coefficient =
                        active_profile.mouse_left.yaw_elastic_return_enabled
                            ? active_profile.mouse_left.yaw_elastic_return_coefficient
                            : 0.0;
                    last_yaw = ShapeMouseLeftYawWithMapper(raw_yaw_source,
                                                           gain_scale,
                                                           combined_return_step,
                                                           elastic_return_coefficient,
                                                           mapper_dt,
                                                           combined_return_step > 0.0 ||
                                                               elastic_return_coefficient > 0.0,
                                                           &left_yaw_position,
                                                           &left_yaw_dummy_position,
                                                           &left_yaw_elastic_state,
                                                           &left_yaw_dummy_state,
                                                           &left_yaw_mapper_state,
                                                           active_profile,
                                                           &filtered_left_yaw);
                    keyboard_yaw = left_yaw_position;
                } else {
                    keyboard_yaw = ClampDouble(keyboard_yaw + yaw_source * active_profile.mouse_left.yaw_gain,
                                               -static_cast<double>(active_profile.mouse_left.yaw_pulse),
                                               static_cast<double>(active_profile.mouse_left.yaw_pulse));
                    if (std::abs(keyboard_yaw) <= static_cast<double>(active_profile.mouse_left.yaw_deadband)) {
                        keyboard_yaw = 0.0;
                    }
                    const bool apply_left_yaw_idle_return =
                        active_profile.mouse_left.yaw_return_enabled &&
                        active_profile.mouse_left.yaw_return_rate > 0.0 &&
                        !left_yaw_moved &&
                        (active_profile.mouse_left.yaw_return_idle_ms <= 0.0 ||
                         now - last_left_yaw_motion >= left_yaw_return_idle_period);
                    double left_yaw_return_step =
                        (apply_left_yaw_idle_return
                             ? active_profile.mouse_left.yaw_return_rate * mapper_dt
                             : 0.0) +
                        (active_profile.mouse_left.yaw_constant_return_enabled
                             ? active_profile.mouse_left.yaw_constant_return_rate * mapper_dt
                             : 0.0);
                    if (active_profile.mouse_left.yaw_elastic_return_enabled) {
                        left_yaw_return_step +=
                            ElasticReturnRatePerSecond(std::abs(keyboard_yaw),
                                                       active_profile.mouse_left.yaw_pulse,
                                                       active_profile.mouse_left.yaw_elastic_return_coefficient,
                                                       active_profile.mouse_left.yaw_elastic_return_mode,
                                                       active_profile.mouse_left.yaw_elastic_return_curve) *
                            mapper_dt;
                    }
                    if (left_yaw_return_step > 0.0) {
                        keyboard_yaw = MoveTowardDouble(keyboard_yaw, 0.0, left_yaw_return_step);
                    }
                    double yaw_target = keyboard_yaw;
                    if (active_profile.mouse_left.yaw_smoothing > 0.0) {
                        yaw_target = (active_profile.mouse_left.yaw_smoothing * filtered_left_yaw) +
                                     ((1.0 - active_profile.mouse_left.yaw_smoothing) * yaw_target);
                    }
                    const double yaw_output = active_profile.mouse_left.yaw_slew_rate <= 0.0
                        ? yaw_target
                        : MoveTowardDouble(filtered_left_yaw,
                                           yaw_target,
                                           active_profile.mouse_left.yaw_slew_rate * mapper_dt);
                    filtered_left_yaw = yaw_output;
                    last_yaw = QuantizeTrainerProfileOutput(yaw_output,
                                                            active_profile.resolution_mode);
                }
#endif
            } else if (active_profile.right_mouse_left.enabled) {
#if defined(GX12_HAS_GAMEINPUT)
                const auto& source = active_profile.right_mouse_left;
                const double mapper_dt = 1.0 / static_cast<double>(mapper_rate_hz);
                const double wheel_axis_raw =
                    NormalizeGameInputWheelDelta(last_right_mapper_wheel_y);
                const double button_axis_raw =
                    static_cast<double>(GameInputMouse4Mouse5Axis(current_right.buttons));

                if (source.swap_axes) {
                    double throttle_axis = button_axis_raw;
                    if (source.invert_throttle) {
                        throttle_axis = -throttle_axis;
                    }
                    keyboard_throttle = ClampDouble(keyboard_throttle +
                                                    throttle_axis *
                                                    source.throttle_button_rate *
                                                    mapper_dt,
                                                    static_cast<double>(TrainerProfileLowValue()),
                                                    static_cast<double>(kTrainerProfileDomainLimit));
                    if (source.throttle_return_enabled &&
                        std::abs(throttle_axis) < 0.001) {
                        keyboard_throttle = MoveTowardDouble(keyboard_throttle,
                                                            static_cast<double>(TrainerProfileLowValue()),
                                                            source.throttle_return_rate *
                                                            mapper_dt);
                    }

                    double yaw_wheel_axis = wheel_axis_raw;
                    if (source.invert_yaw) {
                        yaw_wheel_axis = -yaw_wheel_axis;
                    }
                    if (std::abs(yaw_wheel_axis) > 0.001) {
                        right_mouse_left_yaw_target =
                            ClampDouble(right_mouse_left_yaw_target +
                                            yaw_wheel_axis * source.yaw_scroll_step,
                                        -static_cast<double>(source.yaw_pulse),
                                        static_cast<double>(source.yaw_pulse));
                    }
                } else {
                    double throttle_wheel_axis = wheel_axis_raw;
                    if (source.invert_throttle) {
                        throttle_wheel_axis = -throttle_wheel_axis;
                    }
                    if (std::abs(throttle_wheel_axis) > 0.001) {
                        keyboard_throttle =
                            ClampDouble(keyboard_throttle +
                                            throttle_wheel_axis * source.throttle_step,
                                        static_cast<double>(TrainerProfileLowValue()),
                                        static_cast<double>(kTrainerProfileDomainLimit));
                    } else if (source.throttle_return_enabled) {
                        keyboard_throttle = MoveTowardDouble(keyboard_throttle,
                                                            static_cast<double>(TrainerProfileLowValue()),
                                                            source.throttle_return_rate *
                                                            mapper_dt);
                    }

                    double yaw_button_axis = button_axis_raw;
                    if (source.invert_yaw) {
                        yaw_button_axis = -yaw_button_axis;
                    }
                    right_mouse_left_yaw_target =
                        yaw_button_axis * static_cast<double>(source.yaw_pulse);
                }

                last_throttle = QuantizeTrainerProfileOutput(keyboard_throttle,
                                                             active_profile.resolution_mode);
                right_mouse_left_yaw_output = source.yaw_slew_rate <= 0.0
                    ? right_mouse_left_yaw_target
                    : MoveTowardDouble(right_mouse_left_yaw_output,
                                       right_mouse_left_yaw_target,
                                       source.yaw_slew_rate * mapper_dt);
                keyboard_yaw = right_mouse_left_yaw_output;
                last_yaw = QuantizeTrainerProfileOutput(right_mouse_left_yaw_output,
                                                        active_profile.resolution_mode);
#endif
            } else if (active_profile.keyboard_left.enabled) {
#if defined(GX12_HAS_GAMEINPUT)
                const auto selected_key_down = [&gameinput_capture](int vk) {
                    return AnyExpandedKeyDown(vk, [&gameinput_capture](int expanded_vk) {
                        return GameInputKeyDown(gameinput_capture.stats, expanded_vk) ||
                               BlockedKeyboardKeyDown(expanded_vk);
                    });
                };
                last_analog_depths = {};
                const bool use_analog = wooting_active && KeyboardWantsWooting(active_profile.keyboard_left);
                if (use_analog) {
                    last_analog_depths = ReadWootingKeyDepths(&wooting_sdk, active_profile.keyboard_left);
                    if (active_profile.keyboard_left.require_analog && last_analog_depths.errors > 0) {
                        std::fprintf(stderr,
                                     "\nWooting analog read failed with require_analog=true; stopping safely.\n");
                        stop_requested = true;
                        break;
                    }
                }
                const bool throttle_up_digital = selected_key_down(active_profile.keyboard_left.throttle_up_vk);
                const bool throttle_down_digital = selected_key_down(active_profile.keyboard_left.throttle_down_vk);
                const bool throttle_cut = selected_key_down(active_profile.keyboard_left.throttle_cut_vk);
                const double mapper_dt = 1.0 / static_cast<double>(mapper_rate_hz);
                const bool analog_cut = use_analog && last_analog_depths.throttle_cut >= 0.50;
                if (throttle_cut || analog_cut) {
                    keyboard_throttle = static_cast<double>(TrainerProfileLowValue());
                } else {
                    const double throttle_axis = use_analog
                        ? (last_analog_depths.throttle_up - last_analog_depths.throttle_down)
                        : static_cast<double>((throttle_up_digital ? 1 : 0) - (throttle_down_digital ? 1 : 0));
                    keyboard_throttle = ClampDouble(keyboard_throttle +
                                                    throttle_axis *
                                                    active_profile.keyboard_left.throttle_rate *
                                                    mapper_dt,
                                                    static_cast<double>(TrainerProfileLowValue()),
                                                    static_cast<double>(kTrainerProfileDomainLimit));
                    if (active_profile.keyboard_left.throttle_return_enabled &&
                        std::abs(throttle_axis) < 0.001) {
                        keyboard_throttle = MoveTowardDouble(keyboard_throttle,
                                                            static_cast<double>(TrainerProfileLowValue()),
                                                            active_profile.keyboard_left.throttle_return_rate *
                                                            mapper_dt);
                    }
                }

                last_throttle = QuantizeTrainerProfileOutput(keyboard_throttle,
                                                             active_profile.resolution_mode);
                if (active_profile.control_mode == ControlMode::DirectMouse) {
                    const bool yaw_left_digital = selected_key_down(active_profile.keyboard_left.yaw_left_vk);
                    const bool yaw_right_digital = selected_key_down(active_profile.keyboard_left.yaw_right_vk);
                    double yaw_axis = use_analog
                        ? (last_analog_depths.yaw_right - last_analog_depths.yaw_left)
                        : static_cast<double>((yaw_right_digital ? 1 : 0) - (yaw_left_digital ? 1 : 0));
                    if (active_profile.keyboard_left.invert_yaw) {
                        yaw_axis = -yaw_axis;
                    }
                    const double yaw_target = yaw_axis * static_cast<double>(active_profile.keyboard_left.yaw_pulse);
                    keyboard_yaw = MoveTowardDouble(keyboard_yaw,
                                                    yaw_target,
                                                    active_profile.keyboard_left.yaw_slew_rate * mapper_dt);
                    last_yaw = QuantizeTrainerProfileOutput(keyboard_yaw,
                                                            active_profile.resolution_mode);
                }
#endif
            } else if (!active_profile.mouse_left.enabled) {
                filtered_left_yaw = 0.0;
                left_yaw_position = 0.0;
                left_yaw_dummy_position = 0.0;
                left_yaw_elastic_state = ElasticAxisState{};
                left_yaw_dummy_state = ElasticAxisState{};
                left_yaw_mapper_state = RightStickSharedState{};
                right_mouse_left_yaw_target = 0.0;
                right_mouse_left_yaw_output = 0.0;
                last_throttle = QuantizeTrainerProfileOutput(
                    static_cast<double>(TrainerProfileLowValue()),
                    active_profile.resolution_mode);
                if (active_profile.control_mode == ControlMode::DirectMouse) {
                    last_yaw = 0;
                }
            }
            ++mapper_updates;
            do {
                next_mapper += mapper_period;
            } while (next_mapper <= now);
        }

        if (now >= next_frame) {
            const int64_t late_us = std::chrono::duration_cast<std::chrono::microseconds>(now - next_frame).count();
            if (late_us > frame_period.count()) {
                ++late_frames;
            }
            max_late_us = std::max(max_late_us, late_us);

#if defined(GX12_HAS_GAMEINPUT)
            const GameInputMouseRateSnapshot current = SnapshotGameInputStats(gameinput_capture.stats);
            const uint64_t current_events_total = current.callbacks;
            const uint64_t current_aux_total = current.mouse_states;
            const uint64_t current_keyboard_total = current.keyboard_callbacks;
#else
            const MouseRateStats current = g_mouse_stats;
            const uint64_t current_events_total = current.mouse_event_count;
            const uint64_t current_aux_total = current.wm_input_count;
            const uint64_t current_keyboard_total = 0;
#endif

            uint8_t frame[25];
            std::array<int, kSbusChannels> trainer_pulses{};
            trainer_pulses[0] = last_roll;
            trainer_pulses[1] = last_pitch;
            trainer_pulses[2] = last_throttle;
            trainer_pulses[3] = last_yaw;
            BuildSbusFrame(trainer_pulses,
                           TrainerActiveFlags(active_profile),
                           active_profile.resolution_mode,
                           frame);

            DWORD written = 0;
            if (!WriteFile(serial, frame, sizeof(frame), &written, nullptr) ||
                written != sizeof(frame)) {
                std::fprintf(stderr,
                             "WriteFile failed after %lu frame(s): written=%lu le=%lu\n",
                             static_cast<unsigned long>(frames),
                             static_cast<unsigned long>(written),
                             static_cast<unsigned long>(GetLastError()));
#if defined(GX12_HAS_GAMEINPUT)
                ReleaseCursorClip(false);
                StopGameInputMouseCapture(&gameinput_capture);
#else
                DestroyWindow(hwnd);
#endif
                CloseHandle(serial);
                if (launcher_stop_event) CloseHandle(launcher_stop_event);
                return 1;
            }

            ++frames;
            if (csv_open && (frames % static_cast<uint32_t>(active_profile.log_every_n_frames) == 0U)) {
                csv << std::chrono::duration<double>(now - start).count() << ','
                    << frames << ','
                    << current_events_total << ','
                    << current_aux_total << ','
                    << current_keyboard_total << ','
                    << last_mapper_dx << ','
                    << last_mapper_dy << ','
                    << right_stick_state.raw_roll_source << ','
                    << right_stick_state.raw_pitch_source << ','
                    << right_stick_state.filtered_roll_source << ','
                    << right_stick_state.filtered_pitch_source << ','
                    << right_stick_state.despike_recent_count << ','
                    << right_stick_state.despike_total_count << ','
                    << last_left_mapper_dx << ','
                    << last_left_mapper_dy << ','
                    << left_yaw_mapper_state.raw_pitch_source << ','
                    << left_yaw_mapper_state.filtered_pitch_source << ','
                    << left_yaw_mapper_state.despike_recent_count << ','
                    << left_yaw_mapper_state.despike_total_count << ','
                    << PositionModelName(active_profile.mouse_left.yaw_position_model) << ','
                    << left_yaw_mapper_state.adaptive_gain << ','
                    << left_yaw_mapper_state.gate_scale << ','
                    << (left_yaw_mapper_state.gimbal_antiwindup_active ? 1 : 0) << ','
                    << PositionModelName(active_profile.position_model) << ','
                    << right_stick_state.adaptive_gain << ','
                    << right_stick_state.gate_scale << ','
                    << (right_stick_state.gimbal_antiwindup_active ? 1 : 0) << ','
                    << filtered_roll << ','
                    << filtered_pitch << ','
                    << roll_elastic_state.velocity << ','
                    << pitch_elastic_state.velocity << ','
                    << last_roll << ','
                    << last_pitch << ','
                    << last_throttle << ','
                    << last_yaw << ','
                    << mouse_aim_state.reticle_x << ','
                    << mouse_aim_state.reticle_y << ','
                    << last_analog_depths.throttle_up << ','
                    << last_analog_depths.throttle_down << ','
                    << last_analog_depths.yaw_left << ','
                    << last_analog_depths.yaw_right << ','
                    << last_analog_depths.throttle_cut << ','
                    << late_us << '\n';
            }
            do {
                next_frame += frame_period;
            } while (next_frame <= now);
        }

        if (now >= next_print) {
#if defined(GX12_HAS_GAMEINPUT)
            const GameInputMouseRateSnapshot current = SnapshotGameInputStats(gameinput_capture.stats);
            const GameInputMouseRoleSnapshot current_right = SnapshotGameInputRole(gameinput_capture.stats.right_mouse);
            const GameInputMouseRoleSnapshot current_left = SnapshotGameInputRole(gameinput_capture.stats.left_mouse);
            const uint64_t events = current.callbacks - last_print.callbacks;
            const uint64_t aux = current.mouse_states - last_print.mouse_states;
            const uint64_t keyboard_events = current.keyboard_callbacks - last_print.keyboard_callbacks;
            const int64_t dx = current_right.dx_sum - last_right_print.dx_sum;
            const int64_t dy = current_right.dy_sum - last_right_print.dy_sum;
            const int64_t left_dx = current_left.dx_sum - last_left_print.dx_sum;
            const int64_t left_dy = current_left.dy_sum - last_left_print.dy_sum;
            const uint64_t errors = current.errors - last_print.errors;
#else
            const MouseRateStats current = g_mouse_stats;
            const uint64_t events = current.mouse_event_count - last_print.mouse_event_count;
            const uint64_t aux = current.wm_input_count - last_print.wm_input_count;
            const uint64_t keyboard_events = 0;
            const int64_t dx = current.dx_sum - last_print.dx_sum;
            const int64_t dy = current.dy_sum - last_print.dy_sum;
            const int64_t left_dx = 0;
            const int64_t left_dy = 0;
            const uint64_t errors = current.data_error_count - last_print.data_error_count;
#endif
            constexpr double kWindowSeconds = 0.250;
            std::printf("[%.3fs] frames=%5lu map=%5lu mouse=%7.1f Hz key=%5llu aux=%5llu rdx=%+7lld rdy=%+7lld ldx=%+7lld ldy=%+7lld roll=%+4d pitch=%+4d thr=%+4d yaw=%+4d despike=%llu/10s total=%llu left_despike=%llu/10s total=%llu aim=%+5.0f/%+5.0f analog=%.2f/%.2f/%.2f/%.2f cut=%.2f late=%llu max_late_us=%lld err=%llu\n",
                        std::chrono::duration<double>(now - start).count(),
                        static_cast<unsigned long>(frames),
                        static_cast<unsigned long>(mapper_updates),
                        static_cast<double>(events) / kWindowSeconds,
                        static_cast<unsigned long long>(keyboard_events),
                        static_cast<unsigned long long>(aux),
                        static_cast<long long>(dx),
                        static_cast<long long>(dy),
                        static_cast<long long>(left_dx),
                        static_cast<long long>(left_dy),
                        last_roll,
                        last_pitch,
                        last_throttle,
                        last_yaw,
                        static_cast<unsigned long long>(right_stick_state.despike_recent_count),
                        static_cast<unsigned long long>(right_stick_state.despike_total_count),
                        static_cast<unsigned long long>(left_yaw_mapper_state.despike_recent_count),
                        static_cast<unsigned long long>(left_yaw_mapper_state.despike_total_count),
                        mouse_aim_state.reticle_x,
                        mouse_aim_state.reticle_y,
                        last_analog_depths.throttle_up,
                        last_analog_depths.throttle_down,
                        last_analog_depths.yaw_left,
                        last_analog_depths.yaw_right,
                        last_analog_depths.throttle_cut,
                        static_cast<unsigned long long>(late_frames),
                        static_cast<long long>(max_late_us),
                        static_cast<unsigned long long>(errors));
            std::fflush(stdout);
            last_print = current;
#if defined(GX12_HAS_GAMEINPUT)
            last_right_print = current_right;
            last_left_print = current_left;
#endif
            next_print += std::chrono::milliseconds(250);
        }

#if defined(GX12_HAS_GAMEINPUT)
        Sleep(0);
#else
        MsgWaitForMultipleObjectsEx(0,
                                    nullptr,
                                    0,
                                    QS_ALLINPUT,
                                    MWMO_INPUTAVAILABLE);
#endif
    }

    uint8_t neutral[25];
    std::array<int, kSbusChannels> inactive_pulses{};
    inactive_pulses[2] = kSafeThrottleLow;
    BuildSbusFrame(inactive_pulses, kSbusTrainerMaskMarker, neutral);
    DWORD written = 0;
    (void)WriteFile(serial, neutral, sizeof(neutral), &written, nullptr);

#if defined(GX12_HAS_GAMEINPUT)
    const GameInputMouseRateSnapshot final_stats = SnapshotGameInputStats(gameinput_capture.stats);
    const uint64_t final_events = final_stats.callbacks;
    const int64_t final_dx = final_stats.dx_sum;
    const int64_t final_dy = final_stats.dy_sum;
    const unsigned long final_error = final_stats.errors > 0 ? 1UL : 0UL;
#else
    const MouseRateStats final_stats = g_mouse_stats;
    const uint64_t final_events = final_stats.mouse_event_count;
    const int64_t final_dx = final_stats.dx_sum;
    const int64_t final_dy = final_stats.dy_sum;
    const unsigned long final_error = final_stats.last_error;
#endif
    const double elapsed = std::chrono::duration<double>(clock::now() - start).count();
    std::printf("\nsummary: frames=%lu elapsed=%.3fs actual_frame_rate=%.1f Hz mouse_events=%llu avg_mouse_rate=%.1f Hz dx=%+lld dy=%+lld despike_total=%llu late_frames=%llu max_late_us=%lld last_error=%lu\n",
                static_cast<unsigned long>(frames),
                elapsed,
                elapsed > 0.0 ? static_cast<double>(frames) / elapsed : 0.0,
                static_cast<unsigned long long>(final_events),
                elapsed > 0.0 ? static_cast<double>(final_events) / elapsed : 0.0,
                static_cast<long long>(final_dx),
                static_cast<long long>(final_dy),
                static_cast<unsigned long long>(right_stick_state.despike_total_count),
                static_cast<unsigned long long>(late_frames),
                static_cast<long long>(max_late_us),
                final_error);

    if (guided) {
        std::printf("\nTuning notes:\n");
        std::printf("  - If the radio RX number is below %d, lower frame_rate_hz or check USB-VCP mode.\n",
                    active_profile.frame_rate_hz);
        std::printf("  - If Mix is near 1000, the radio-side trainer sampling ceiling is behaving as expected.\n");
        std::printf("  - Raise gain for faster response, raise expo for softer center, enable return for spring recentering.\n");
    }

    if (csv_open) {
        csv.flush();
        std::printf("csv log: %s\n", active_profile.log_path.c_str());
    }

#if defined(GX12_HAS_GAMEINPUT)
    ReleaseCursorClip(false);
    key_blocker.Stop();
    StopGameInputMouseCapture(&gameinput_capture);
#else
    if (IsWindow(hwnd)) {
        DestroyWindow(hwnd);
    }
#endif
    CloseHandle(serial);
    if (launcher_stop_event) CloseHandle(launcher_stop_event);
    return 0;
}

int RunMouseAimDryRun(const TrainerProfile& profile, int seconds) {
    g_stop_virtual_key.store(profile.stop_key_vk, std::memory_order_release);
    g_freeze_virtual_key.store(profile.freeze_key_vk, std::memory_order_release);
    if (profile.control_mode != ControlMode::DroneMouseAim) {
        std::fprintf(stderr, "--mouse-aim-dry-run requires control.mode=\"drone_mouse_aim\".\n");
        return 2;
    }
    if (seconds <= 0 || seconds > 60) {
        std::fprintf(stderr, "--mouse-aim-dry-run duration must be 1..60 seconds.\n");
        return 2;
    }

#if defined(GX12_HAS_GAMEINPUT)
    GameInputMouseCapture gameinput_capture;
    if (!StartGameInputMouseCapture(&gameinput_capture, false)) {
        return 1;
    }

    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    const int mapper_rate_hz = std::min(profile.frame_rate_hz, kTrainerMapperReferenceHz);
    const auto mapper_period = std::chrono::microseconds(1000000 / mapper_rate_hz);
    std::printf("\n--mouse-aim-dry-run: profile=%s duration=%d sec mapper_rate=%d Hz\n",
                profile.source_file.c_str(),
                seconds,
                mapper_rate_hz);
    std::printf("No serial output is opened. Move the mouse; %s stops early.\n\n",
                profile.stop_key.c_str());

    using clock = std::chrono::steady_clock;
    const auto start = clock::now();
    const auto end = start + std::chrono::seconds(seconds);
    auto next_mapper = start;
    auto next_print = start + std::chrono::milliseconds(250);
    GameInputMouseRateSnapshot last_mapper = SnapshotGameInputStats(gameinput_capture.stats);
    GameInputMouseRateSnapshot last_print = last_mapper;
    MouseAimState aim_state;
    int roll = 0;
    int pitch = 0;
    int yaw = 0;
    uint32_t mapper_updates = 0;

    while (clock::now() < end) {
        if (StopKeyDown()) {
            break;
        }

        const auto now = clock::now();
        if (now >= next_mapper) {
            const GameInputMouseRateSnapshot current = SnapshotGameInputStats(gameinput_capture.stats);
            const int64_t dx = current.dx_sum - last_mapper.dx_sum;
            const int64_t dy = current.dy_sum - last_mapper.dy_sum;
            last_mapper = current;
            const int64_t aim_dx = profile.swap_axes ? -dy : dx;
            const int64_t aim_dy = profile.swap_axes ? dx : dy;
            UpdateDroneMouseAim(aim_dx,
                                aim_dy,
                                1.0 / static_cast<double>(mapper_rate_hz),
                                profile.mouse_aim,
                                &aim_state,
                                &roll,
                                &pitch,
                                &yaw);
            ++mapper_updates;
            do {
                next_mapper += mapper_period;
            } while (next_mapper <= now);
        }

        if (now >= next_print) {
            const GameInputMouseRateSnapshot current = SnapshotGameInputStats(gameinput_capture.stats);
            const uint64_t events = current.callbacks - last_print.callbacks;
            const int64_t dx = current.dx_sum - last_print.dx_sum;
            const int64_t dy = current.dy_sum - last_print.dy_sum;
            constexpr double kWindowSeconds = 0.250;
            std::printf("[%.3fs] map=%5lu mouse=%7.1f Hz dx=%+7lld dy=%+7lld aim=%+6.1f/%+6.1f roll=%+4d pitch=%+4d yaw=%+4d\n",
                        std::chrono::duration<double>(now - start).count(),
                        static_cast<unsigned long>(mapper_updates),
                        static_cast<double>(events) / kWindowSeconds,
                        static_cast<long long>(dx),
                        static_cast<long long>(dy),
                        aim_state.reticle_x,
                        aim_state.reticle_y,
                        roll,
                        pitch,
                        yaw);
            std::fflush(stdout);
            last_print = current;
            next_print += std::chrono::milliseconds(250);
        }
        Sleep(0);
    }

    const GameInputMouseRateSnapshot final_stats = SnapshotGameInputStats(gameinput_capture.stats);
    const double elapsed = std::chrono::duration<double>(clock::now() - start).count();
    std::printf("\nsummary: elapsed=%.3fs mouse_events=%llu avg_mouse_rate=%.1f Hz dx=%+lld dy=%+lld final_aim=%+.1f/%+.1f roll=%+d pitch=%+d yaw=%+d\n",
                elapsed,
                static_cast<unsigned long long>(final_stats.callbacks),
                elapsed > 0.0 ? static_cast<double>(final_stats.callbacks) / elapsed : 0.0,
                static_cast<long long>(final_stats.dx_sum),
                static_cast<long long>(final_stats.dy_sum),
                aim_state.reticle_x,
                aim_state.reticle_y,
                roll,
                pitch,
                yaw);
    StopGameInputMouseCapture(&gameinput_capture);
    return 0;
#else
    std::fprintf(stderr, "--mouse-aim-dry-run requires a Microsoft.GameInput build.\n");
    return 1;
#endif
}

int RunMouseLeftDryRun(const TrainerProfile& profile, int seconds) {
    g_stop_virtual_key.store(profile.stop_key_vk, std::memory_order_release);
    g_freeze_virtual_key.store(profile.freeze_key_vk, std::memory_order_release);
    if (!profile.mouse_left.enabled) {
        std::fprintf(stderr, "--mouse-left-dry-run requires mouse_left_stick.enabled=true.\n");
        return 2;
    }
    if (seconds <= 0 || seconds > 60) {
        std::fprintf(stderr, "--mouse-left-dry-run duration must be 1..60 seconds.\n");
        return 2;
    }

#if defined(GX12_HAS_GAMEINPUT)
    GameInputMouseCapture gameinput_capture;
    if (!StartGameInputMouseCapture(&gameinput_capture,
                                    false,
                                    profile.mouse_right_device_token,
                                    profile.mouse_left_device_token)) {
        return 1;
    }

    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    const int mapper_rate_hz = std::min(profile.frame_rate_hz, kTrainerMapperReferenceHz);
    const auto mapper_period = std::chrono::microseconds(1000000 / mapper_rate_hz);
    std::printf("\n--mouse-left-dry-run: profile=%s duration=%d sec mapper_rate=%d Hz resolution_mode=%s\n",
                profile.source_file.c_str(),
                seconds,
                mapper_rate_hz,
                TrainerResolutionModeName(profile.resolution_mode));
    std::printf("No serial output is opened. right=%s left=%s. %s stops early.\n\n",
                profile.mouse_right_device_token.c_str(),
                profile.mouse_left_device_token.c_str(),
                profile.stop_key.c_str());

    using clock = std::chrono::steady_clock;
    const auto start = clock::now();
    const auto end = start + std::chrono::seconds(seconds);
    auto next_mapper = start;
    auto next_print = start + std::chrono::milliseconds(250);
    GameInputMouseRoleSnapshot last_right_mapper = SnapshotGameInputRole(gameinput_capture.stats.right_mouse);
    GameInputMouseRoleSnapshot last_left_mapper = SnapshotGameInputRole(gameinput_capture.stats.left_mouse);
    GameInputMouseRoleSnapshot last_right_print = last_right_mapper;
    GameInputMouseRoleSnapshot last_left_print = last_left_mapper;
    double filtered_roll = 0.0;
    double filtered_pitch = 0.0;
    double filtered_left_yaw = 0.0;
    double left_yaw_position = 0.0;
    double left_yaw_dummy_position = 0.0;
    ElasticAxisState roll_elastic_state;
    ElasticAxisState pitch_elastic_state;
    ElasticAxisState left_yaw_elastic_state;
    ElasticAxisState left_yaw_dummy_state;
    RightStickSharedState right_stick_state;
    RightStickSharedState left_yaw_mapper_state;
    double throttle = static_cast<double>(kSafeThrottleLow);
    double yaw = 0.0;
    int roll_pulse = 0;
    int pitch_pulse = 0;
    int throttle_pulse = QuantizeTrainerProfileOutput(
        static_cast<double>(TrainerProfileLowValue()),
        profile.resolution_mode);
    int yaw_pulse = 0;
    uint32_t mapper_updates = 0;
    const double gain_scale = TrainerRateGainScale(mapper_rate_hz);
    const double mapper_dt = 1.0 / static_cast<double>(mapper_rate_hz);
    auto last_mouse_motion = start;
    auto last_left_yaw_motion = start;
    const auto return_idle_period = std::chrono::duration<double, std::milli>(profile.return_idle_ms);
    const auto left_yaw_return_idle_period =
        std::chrono::duration<double, std::milli>(profile.mouse_left.yaw_return_idle_ms);
    const double return_step = profile.return_rate / static_cast<double>(mapper_rate_hz);

    while (clock::now() < end) {
        if (StopKeyDown()) {
            break;
        }
        const auto now = clock::now();
        if (now >= next_mapper) {
            const GameInputMouseRoleSnapshot right = SnapshotGameInputRole(gameinput_capture.stats.right_mouse);
            const GameInputMouseRoleSnapshot left = SnapshotGameInputRole(gameinput_capture.stats.left_mouse);
            const int64_t rdx = right.dx_sum - last_right_mapper.dx_sum;
            const int64_t rdy = right.dy_sum - last_right_mapper.dy_sum;
            const int64_t ldx = left.dx_sum - last_left_mapper.dx_sum;
            const int64_t ldy = left.dy_sum - last_left_mapper.dy_sum;
            last_right_mapper = right;
            last_left_mapper = left;

            const double roll_source = profile.swap_axes ? static_cast<double>(-rdy) : static_cast<double>(rdx);
            const double pitch_source = profile.swap_axes ? static_cast<double>(rdx) : static_cast<double>(-rdy);
            const bool use_position_mapper = RightStickNeedsPositionMapper(profile);
            if (use_position_mapper) {
                const bool mouse_moved = (rdx != 0 || rdy != 0);
                if (mouse_moved) {
                    last_mouse_motion = now;
                }
                const bool apply_idle_return = profile.return_enabled &&
                                               profile.return_rate > 0.0 &&
                                               !mouse_moved &&
                                               (profile.return_idle_ms <= 0.0 ||
                                                now - last_mouse_motion >= return_idle_period);
                const double combined_return_step =
                    (apply_idle_return ? return_step : 0.0) +
                    (profile.constant_return_enabled
                         ? profile.constant_return_rate / static_cast<double>(mapper_rate_hz)
                         : 0.0);
                const double elastic_return_coefficient =
                    profile.elastic_return_enabled ? profile.elastic_return_coefficient : 0.0;
                ShapeRightStickPositionPulses(roll_source,
                                              pitch_source,
                                              gain_scale,
                                              combined_return_step,
                                              elastic_return_coefficient,
                                              mapper_dt,
                                              combined_return_step > 0.0 ||
                                                  elastic_return_coefficient > 0.0,
                                              &filtered_roll,
                                              &filtered_pitch,
                                              &roll_elastic_state,
                                              &pitch_elastic_state,
                                              &right_stick_state,
                                              profile,
                                              &roll_pulse,
                                              &pitch_pulse);
            } else {
                double filtered_roll_source = roll_source;
                double filtered_pitch_source = pitch_source;
                ApplyRightStickInputPreprocessors(&filtered_roll_source,
                                                  &filtered_pitch_source,
                                                  mapper_dt,
                                                  profile,
                                                  &right_stick_state);
                const double input_gain = UpdateAdaptiveInputGain(filtered_roll_source,
                                                                  filtered_pitch_source,
                                                                  mapper_dt,
                                                                  profile,
                                                                  &right_stick_state);
                roll_pulse = ShapeTrainerPulse(filtered_roll_source,
                                               profile.roll_gain * gain_scale * input_gain,
                                               profile.invert_roll,
                                               &filtered_roll,
                                               profile);
                pitch_pulse = ShapeTrainerPulse(filtered_pitch_source,
                                                profile.pitch_gain * gain_scale * input_gain,
                                                profile.invert_pitch,
                                                &filtered_pitch,
                                                profile);
            }

            double throttle_axis = profile.mouse_left.swap_axes ? static_cast<double>(ldx) : static_cast<double>(-ldy);
            if (profile.mouse_left.invert_throttle) throttle_axis = -throttle_axis;
            throttle = ClampDouble(throttle + throttle_axis * profile.mouse_left.throttle_rate,
                                   static_cast<double>(TrainerProfileLowValue()),
                                   static_cast<double>(kTrainerProfileDomainLimit));
            if (profile.mouse_left.throttle_return_enabled && std::abs(throttle_axis) < 0.001) {
                throttle = MoveTowardDouble(throttle,
                                            static_cast<double>(TrainerProfileLowValue()),
                                            profile.mouse_left.throttle_return_rate * mapper_dt);
            }
            throttle_pulse = QuantizeTrainerProfileOutput(throttle,
                                                          profile.resolution_mode);

            const double raw_yaw_source = profile.mouse_left.swap_axes ? static_cast<double>(ldy) : static_cast<double>(ldx);
            double yaw_source = raw_yaw_source;
            if (!profile.mouse_left.yaw_shaping_enabled && profile.mouse_left.invert_yaw) yaw_source = -yaw_source;
            const bool left_yaw_moved = std::abs(raw_yaw_source) > 0.001;
            if (left_yaw_moved) {
                last_left_yaw_motion = now;
            }
            if (profile.mouse_left.yaw_shaping_enabled) {
                const bool apply_left_yaw_idle_return =
                    profile.mouse_left.yaw_return_enabled &&
                    profile.mouse_left.yaw_return_rate > 0.0 &&
                    !left_yaw_moved &&
                    (profile.mouse_left.yaw_return_idle_ms <= 0.0 ||
                     now - last_left_yaw_motion >= left_yaw_return_idle_period);
                const double combined_return_step =
                    (apply_left_yaw_idle_return
                         ? profile.mouse_left.yaw_return_rate / static_cast<double>(mapper_rate_hz)
                         : 0.0) +
                    (profile.mouse_left.yaw_constant_return_enabled
                         ? profile.mouse_left.yaw_constant_return_rate / static_cast<double>(mapper_rate_hz)
                         : 0.0);
                const double elastic_return_coefficient =
                    profile.mouse_left.yaw_elastic_return_enabled
                        ? profile.mouse_left.yaw_elastic_return_coefficient
                        : 0.0;
                yaw_pulse = ShapeMouseLeftYawWithMapper(raw_yaw_source,
                                                        gain_scale,
                                                        combined_return_step,
                                                        elastic_return_coefficient,
                                                        mapper_dt,
                                                        combined_return_step > 0.0 ||
                                                            elastic_return_coefficient > 0.0,
                                                        &left_yaw_position,
                                                        &left_yaw_dummy_position,
                                                        &left_yaw_elastic_state,
                                                        &left_yaw_dummy_state,
                                                        &left_yaw_mapper_state,
                                                        profile,
                                                        &filtered_left_yaw);
                yaw = left_yaw_position;
            } else {
                yaw = ClampDouble(yaw + yaw_source * profile.mouse_left.yaw_gain,
                                  -static_cast<double>(profile.mouse_left.yaw_pulse),
                                  static_cast<double>(profile.mouse_left.yaw_pulse));
                if (std::abs(yaw) <= static_cast<double>(profile.mouse_left.yaw_deadband)) {
                    yaw = 0.0;
                }
                const bool apply_left_yaw_idle_return =
                    profile.mouse_left.yaw_return_enabled &&
                    profile.mouse_left.yaw_return_rate > 0.0 &&
                    !left_yaw_moved &&
                    (profile.mouse_left.yaw_return_idle_ms <= 0.0 ||
                     now - last_left_yaw_motion >= left_yaw_return_idle_period);
                double left_yaw_return_step =
                    (apply_left_yaw_idle_return ? profile.mouse_left.yaw_return_rate * mapper_dt : 0.0) +
                    (profile.mouse_left.yaw_constant_return_enabled
                         ? profile.mouse_left.yaw_constant_return_rate * mapper_dt
                         : 0.0);
                if (profile.mouse_left.yaw_elastic_return_enabled) {
                    left_yaw_return_step +=
                        ElasticReturnRatePerSecond(std::abs(yaw),
                                                   profile.mouse_left.yaw_pulse,
                                                   profile.mouse_left.yaw_elastic_return_coefficient,
                                                   profile.mouse_left.yaw_elastic_return_mode,
                                                   profile.mouse_left.yaw_elastic_return_curve) *
                        mapper_dt;
                }
                if (left_yaw_return_step > 0.0) {
                    yaw = MoveTowardDouble(yaw, 0.0, left_yaw_return_step);
                }
                double yaw_target = yaw;
                if (profile.mouse_left.yaw_smoothing > 0.0) {
                    yaw_target = (profile.mouse_left.yaw_smoothing * filtered_left_yaw) +
                                 ((1.0 - profile.mouse_left.yaw_smoothing) * yaw_target);
                }
                const double yaw_output = profile.mouse_left.yaw_slew_rate <= 0.0
                    ? yaw_target
                    : MoveTowardDouble(filtered_left_yaw,
                                       yaw_target,
                                       profile.mouse_left.yaw_slew_rate * mapper_dt);
                filtered_left_yaw = yaw_output;
                yaw_pulse = QuantizeTrainerProfileOutput(yaw_output,
                                                         profile.resolution_mode);
            }
            ++mapper_updates;
            do {
                next_mapper += mapper_period;
            } while (next_mapper <= now);
        }

        if (now >= next_print) {
            const GameInputMouseRoleSnapshot right = SnapshotGameInputRole(gameinput_capture.stats.right_mouse);
            const GameInputMouseRoleSnapshot left = SnapshotGameInputRole(gameinput_capture.stats.left_mouse);
            const int64_t rdx = right.dx_sum - last_right_print.dx_sum;
            const int64_t rdy = right.dy_sum - last_right_print.dy_sum;
            const int64_t ldx = left.dx_sum - last_left_print.dx_sum;
            const int64_t ldy = left.dy_sum - last_left_print.dy_sum;
            std::printf("[%.3fs] map=%5lu rdx=%+7lld rdy=%+7lld ldx=%+7lld ldy=%+7lld roll=%+4d pitch=%+4d thr=%+4d yaw=%+4d\n",
                        std::chrono::duration<double>(now - start).count(),
                        static_cast<unsigned long>(mapper_updates),
                        static_cast<long long>(rdx),
                        static_cast<long long>(rdy),
                        static_cast<long long>(ldx),
                        static_cast<long long>(ldy),
                        roll_pulse,
                        pitch_pulse,
                        throttle_pulse,
                        yaw_pulse);
            std::fflush(stdout);
            last_right_print = right;
            last_left_print = left;
            next_print += std::chrono::milliseconds(250);
        }
        Sleep(0);
    }
    StopGameInputMouseCapture(&gameinput_capture);
    return 0;
#else
    std::fprintf(stderr, "--mouse-left-dry-run requires a Microsoft.GameInput build.\n");
    return 2;
#endif
}

// ---- GX12 HID diagnostics --------------------------------------------------

constexpr unsigned short kGx12VendorId  = 0x1209;
constexpr unsigned short kGx12ProductId = 0x4F54;
constexpr int kGx12HidReportSize     = 20;
constexpr int kGx12ChannelCount      = 8;
constexpr int kGx12ChannelCenter     = 1024;

struct Gx12DecodedReport {
    uint32_t buttons = 0;  // 24 bits in lower 24
    int16_t channels[kGx12ChannelCount] = {};
};

bool DecodeGx12Report(const unsigned char* data, int length, Gx12DecodedReport* out) {
    if (!data || length < kGx12HidReportSize) {
        return false;
    }
    out->buttons = static_cast<uint32_t>(data[0]) |
                   (static_cast<uint32_t>(data[1]) << 8) |
                   (static_cast<uint32_t>(data[2]) << 16);
    for (int ch = 0; ch < kGx12ChannelCount; ++ch) {
        const int byte_index = 3 + ch * 2;
        const uint16_t raw = static_cast<uint16_t>(data[byte_index]) |
                             (static_cast<uint16_t>(data[byte_index + 1]) << 8);
        out->channels[ch] = static_cast<int16_t>(static_cast<int>(raw) - kGx12ChannelCenter);
    }
    return true;
}

const HidDeviceEntry* FindGx12Hid(const std::vector<HidDeviceEntry>& devices) {
    for (const HidDeviceEntry& device : devices) {
        if (device.vendor_id == kGx12VendorId && device.product_id == kGx12ProductId) {
            return &device;
        }
    }
    return nullptr;
}

struct Gx12HidChannelGranularity {
    uint64_t samples = 0;
    int min_raw = 0;
    int max_raw = 0;
    int last_raw = kGx12ChannelCenter;
    std::array<bool, kGx12ChannelCenter * 2 + 1> seen{};

    void Record(int centered_value) {
        int raw = centered_value + kGx12ChannelCenter;
        if (raw < 0) raw = 0;
        if (raw > kGx12ChannelCenter * 2) raw = kGx12ChannelCenter * 2;

        if (samples == 0 || raw < min_raw) min_raw = raw;
        if (samples == 0 || raw > max_raw) max_raw = raw;
        seen[static_cast<size_t>(raw)] = true;
        last_raw = raw;
        ++samples;
    }

    int DistinctCount() const {
        return static_cast<int>(std::count(seen.begin(), seen.end(), true));
    }

    int AdjacentPairCount() const {
        int count = 0;
        for (size_t i = 0; i + 1 < seen.size(); ++i) {
            if (seen[i] && seen[i + 1]) {
                ++count;
            }
        }
        return count;
    }

    int MissingInsideRange() const {
        if (samples == 0 || max_raw < min_raw) {
            return 0;
        }
        int missing = 0;
        for (int raw = min_raw; raw <= max_raw; ++raw) {
            if (!seen[static_cast<size_t>(raw)]) {
                ++missing;
            }
        }
        return missing;
    }
};

int RunGx12HidCapture(int seconds, const char* csv_path) {
    if (seconds <= 0 || seconds > 60) {
        std::fprintf(stderr, "--gx12-hid-capture duration must be 1..60 seconds.\n");
        return 2;
    }

    if (hid_init() != 0) {
        std::fprintf(stderr, "hid_init failed: %s\n", HidErrorUtf8(nullptr).c_str());
        return 1;
    }

    const std::vector<HidDeviceEntry> devices = EnumerateHidDevices();
    const HidDeviceEntry* selected = FindGx12Hid(devices);
    if (!selected) {
        std::fprintf(stderr,
                     "GX12 joystick HID not found (vid=0x%04x pid=0x%04x). Run --hid-list and confirm the radio is in USB Joystick mode.\n",
                     kGx12VendorId,
                     kGx12ProductId);
        hid_exit();
        return 1;
    }

    std::ofstream csv;
    if (csv_path && csv_path[0] != '\0') {
        csv.open(csv_path, std::ios::out | std::ios::trunc);
        if (!csv) {
            std::fprintf(stderr, "failed to open CSV output: %s\n", csv_path);
            hid_exit();
            return 1;
        }
        csv << "elapsed_ms,report,buttons";
        for (int ch = 0; ch < kGx12ChannelCount; ++ch) {
            csv << ",ch" << (ch + 1) << "_raw";
        }
        csv << "\n";
    }

    std::printf("\n--gx12-hid-capture: opening GX12 HID index %d for %d second(s).\n",
                selected->index,
                seconds);
    std::printf("  vid=0x%04x pid=0x%04x usage=0x%04x:0x%04x iface=%d product='%s'\n",
                selected->vendor_id,
                selected->product_id,
                selected->usage_page,
                selected->usage,
                selected->interface_number,
                selected->product.c_str());
    if (csv_path && csv_path[0] != '\0') {
        std::printf("  csv=%s\n", csv_path);
    }

    hid_device* device = hid_open_path(selected->path.c_str());
    if (!device) {
        std::fprintf(stderr, "hid_open_path failed: %s\n", HidErrorUtf8(nullptr).c_str());
        hid_exit();
        return 1;
    }

    if (hid_set_nonblocking(device, 0) != 0) {
        std::fprintf(stderr, "hid_set_nonblocking failed: %s\n", HidErrorUtf8(device).c_str());
        hid_close(device);
        hid_exit();
        return 1;
    }

    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

    using clock = std::chrono::steady_clock;
    const auto start = clock::now();
    const auto end = start + std::chrono::seconds(seconds);
    auto next_print = start + std::chrono::milliseconds(250);
    std::array<unsigned char, 64> buffer{};
    std::array<Gx12HidChannelGranularity, kGx12ChannelCount> channels{};
    uint64_t reports = 0;
    uint64_t timeouts = 0;
    uint64_t errors = 0;
    std::string last_error;

    while (clock::now() < end) {
        const int read = hid_read_timeout(device, buffer.data(), buffer.size(), 1);
        if (read > 0) {
            Gx12DecodedReport decoded{};
            if (DecodeGx12Report(buffer.data(), read, &decoded)) {
                ++reports;
                const auto now = clock::now();
                const double elapsed_ms =
                    std::chrono::duration<double, std::milli>(now - start).count();

                if (csv) {
                    csv << elapsed_ms << "," << reports << "," << decoded.buttons;
                }
                for (int ch = 0; ch < kGx12ChannelCount; ++ch) {
                    channels[ch].Record(decoded.channels[ch]);
                    if (csv) {
                        csv << "," << channels[ch].last_raw;
                    }
                }
                if (csv) {
                    csv << "\n";
                }
            }
        } else if (read == 0) {
            ++timeouts;
        } else {
            ++errors;
            last_error = HidReadErrorUtf8(device);
            break;
        }

        const auto now = clock::now();
        if (now >= next_print) {
            const double elapsed = std::chrono::duration<double>(now - start).count();
            std::printf("[%.3fs] reports=%6llu ch1=%4d ch2=%4d ch3=%4d ch4=%4d timeout=%llu err=%llu\n",
                        elapsed,
                        static_cast<unsigned long long>(reports),
                        channels[0].last_raw,
                        channels[1].last_raw,
                        channels[2].last_raw,
                        channels[3].last_raw,
                        static_cast<unsigned long long>(timeouts),
                        static_cast<unsigned long long>(errors));
            std::fflush(stdout);
            next_print += std::chrono::milliseconds(250);
        }
    }

    const double elapsed = std::chrono::duration<double>(clock::now() - start).count();
    std::printf("\nsummary: reports=%llu elapsed=%.3fs avg_rate=%.1f Hz timeouts=%llu err=%llu\n",
                static_cast<unsigned long long>(reports),
                elapsed,
                elapsed > 0.0 ? static_cast<double>(reports) / elapsed : 0.0,
                static_cast<unsigned long long>(timeouts),
                static_cast<unsigned long long>(errors));
    for (int ch = 0; ch < kGx12ChannelCount; ++ch) {
        const Gx12HidChannelGranularity& stats = channels[ch];
        std::printf("  ch%d: raw_min=%4d raw_max=%4d distinct=%4d adjacent_pairs=%4d missing_inside=%4d\n",
                    ch + 1,
                    stats.min_raw,
                    stats.max_raw,
                    stats.DistinctCount(),
                    stats.AdjacentPairCount(),
                    stats.MissingInsideRange());
    }
    if (!last_error.empty()) {
        std::printf("last read error: %s\n", last_error.c_str());
    }

    hid_close(device);
    hid_exit();
    return errors == 0 ? 0 : 1;
}

int RunTrainer(const char* port_name, int seconds, int mouse_gain, int frame_rate_hz) {
    if (seconds <= 0 || seconds > 30) {
        std::fprintf(stderr, "--trainer duration must be 1..30 seconds.\n");
        return 2;
    }
    if (mouse_gain <= 0 || mouse_gain > 5000) {
        std::fprintf(stderr, "--trainer gain must be 1..5000.\n");
        return 2;
    }
    if (frame_rate_hz <= 0 || frame_rate_hz > 8000) {
        std::fprintf(stderr, "--trainer frame_rate_hz must be 1..8000.\n");
        return 2;
    }

    const std::string resolved_port = ResolveTrainerPortName(port_name);
    const std::string path = WindowsComPath(resolved_port.c_str());
    if (path.empty()) {
        std::fprintf(stderr, "--trainer requires a COM port, for example COM3, or auto.\n");
        return 2;
    }

    HANDLE serial = CreateFileA(path.c_str(),
                                GENERIC_WRITE,
                                0,
                                nullptr,
                                OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL,
                                nullptr);
    if (serial == INVALID_HANDLE_VALUE) {
        std::fprintf(stderr,
                     "CreateFile(%s) failed: le=%lu\n",
                     path.c_str(),
                     static_cast<unsigned long>(GetLastError()));
        return 1;
    }

    DCB dcb{};
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(serial, &dcb)) {
        std::fprintf(stderr, "GetCommState failed: le=%lu\n", static_cast<unsigned long>(GetLastError()));
        CloseHandle(serial);
        return 1;
    }
    dcb.BaudRate = CBR_115200;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    if (!SetCommState(serial, &dcb)) {
        std::fprintf(stderr, "SetCommState failed: le=%lu\n", static_cast<unsigned long>(GetLastError()));
        CloseHandle(serial);
        return 1;
    }

    COMMTIMEOUTS timeouts{};
    timeouts.WriteTotalTimeoutConstant = 20;
    timeouts.WriteTotalTimeoutMultiplier = 1;
    SetCommTimeouts(serial, &timeouts);

    HINSTANCE instance = GetModuleHandleW(nullptr);
    g_mouse_stats = MouseRateStats{};
    g_mouse_rate_stop.store(false, std::memory_order_release);

    HWND hwnd = CreateMouseRateWindow(instance, MouseRateMode::Foreground);
    if (!hwnd) {
        std::fprintf(stderr, "No HWND available for trainer mode.\n");
        CloseHandle(serial);
        return 1;
    }

    if (!RegisterMouseRawInput(hwnd, MouseRateMode::Foreground)) {
        DestroyWindow(hwnd);
        CloseHandle(serial);
        return 1;
    }

    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    std::printf("\n--trainer: port=%s duration=%d sec gain=%d frame_rate=%d Hz.\n",
                resolved_port.c_str(),
                seconds,
                mouse_gain,
                frame_rate_hz);
    std::printf("Sends SBUS ch1=roll, ch2=pitch, ch3=low-throttle safety; remaining trainer channels centered. F2 toggles cursor lock; Esc stops.\n\n");

    using clock = std::chrono::steady_clock;
    const auto start = clock::now();
    const auto end = start + std::chrono::seconds(seconds);
    auto next_frame = start;
    const auto frame_period = std::chrono::microseconds(1000000 / frame_rate_hz);
    const int mapper_rate_hz = std::min(frame_rate_hz, kTrainerMapperReferenceHz);
    auto next_mapper = start;
    const auto mapper_period = std::chrono::microseconds(1000000 / mapper_rate_hz);
    const double gain_scale = TrainerRateGainScale(mapper_rate_hz);
    auto next_print = start + std::chrono::milliseconds(250);
    MouseRateStats last_mapper = g_mouse_stats;
    MouseRateStats last_print = g_mouse_stats;
    uint32_t frames = 0;
    uint32_t mapper_updates = 0;
    int last_roll = 0;
    int last_pitch = 0;

    while (!g_mouse_rate_stop.load(std::memory_order_acquire) && clock::now() < end) {
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        const auto now = clock::now();
        if (now >= next_mapper) {
            const MouseRateStats current = g_mouse_stats;
            const int64_t dx = current.dx_sum - last_mapper.dx_sum;
            const int64_t dy = current.dy_sum - last_mapper.dy_sum;
            last_mapper = current;

            last_roll = ClampTrainerPulse(static_cast<int64_t>(std::lround(static_cast<double>(dx) * mouse_gain * gain_scale)));
            last_pitch = ClampTrainerPulse(static_cast<int64_t>(std::lround(static_cast<double>(-dy) * mouse_gain * gain_scale)));
            ++mapper_updates;
            next_mapper += mapper_period;
        }

        if (now >= next_frame) {
            uint8_t frame[25];
            BuildSbusFrame(last_roll, last_pitch, frame);

            DWORD written = 0;
            if (!WriteFile(serial, frame, sizeof(frame), &written, nullptr) ||
                written != sizeof(frame)) {
                std::fprintf(stderr,
                             "WriteFile failed after %lu frame(s): written=%lu le=%lu\n",
                             static_cast<unsigned long>(frames),
                             static_cast<unsigned long>(written),
                             static_cast<unsigned long>(GetLastError()));
                DestroyWindow(hwnd);
                CloseHandle(serial);
                return 1;
            }

            ++frames;
            next_frame += frame_period;
        }

        if (now >= next_print) {
            const MouseRateStats current = g_mouse_stats;
            const uint64_t events = current.mouse_event_count - last_print.mouse_event_count;
            const uint64_t wm = current.wm_input_count - last_print.wm_input_count;
            const int64_t dx = current.dx_sum - last_print.dx_sum;
            const int64_t dy = current.dy_sum - last_print.dy_sum;
            constexpr double kWindowSeconds = 0.250;
            std::printf("[%.3fs] frames=%5lu map=%5lu mouse=%7.1f Hz wm=%5llu dx=%+7lld dy=%+7lld roll=%+4d pitch=%+4d err=%llu\n",
                        std::chrono::duration<double>(now - start).count(),
                        static_cast<unsigned long>(frames),
                        static_cast<unsigned long>(mapper_updates),
                        static_cast<double>(events) / kWindowSeconds,
                        static_cast<unsigned long long>(wm),
                        static_cast<long long>(dx),
                        static_cast<long long>(dy),
                        last_roll,
                        last_pitch,
                        static_cast<unsigned long long>(current.data_error_count - last_print.data_error_count));
            std::fflush(stdout);
            last_print = current;
            next_print += std::chrono::milliseconds(250);
        }

        MsgWaitForMultipleObjectsEx(0,
                                    nullptr,
                                    0,
                                    QS_ALLINPUT,
                                    MWMO_INPUTAVAILABLE);
    }

    uint8_t neutral[25];
    BuildSbusFrame(0, 0, neutral);
    DWORD written = 0;
    (void)WriteFile(serial, neutral, sizeof(neutral), &written, nullptr);

    const MouseRateStats final_stats = g_mouse_stats;
    const double elapsed = std::chrono::duration<double>(clock::now() - start).count();
    std::printf("\nsummary: frames=%lu elapsed=%.3fs actual_frame_rate=%.1f Hz mouse_events=%llu avg_mouse_rate=%.1f Hz dx=%+lld dy=%+lld last_error=%lu\n",
                static_cast<unsigned long>(frames),
                elapsed,
                elapsed > 0.0 ? static_cast<double>(frames) / elapsed : 0.0,
                static_cast<unsigned long long>(final_stats.mouse_event_count),
                elapsed > 0.0 ? static_cast<double>(final_stats.mouse_event_count) / elapsed : 0.0,
                static_cast<long long>(final_stats.dx_sum),
                static_cast<long long>(final_stats.dy_sum),
                static_cast<unsigned long>(final_stats.last_error));

    if (IsWindow(hwnd)) {
        DestroyWindow(hwnd);
    }
    CloseHandle(serial);
    return 0;
}

uint8_t TrainerSweepActiveFlags(TrainerResolutionMode resolution_mode) {
    if (resolution_mode == TrainerResolutionMode::Gx12_2x) {
        return static_cast<uint8_t>(kSbusTrainerMaskMarker |
                                    kSbusTrainerMaskRightActive |
                                    kSbusTrainerResolution2x);
    }
    return 0;
}

void BuildTrainerSweepFrame(int roll,
                            int pitch,
                            TrainerResolutionMode resolution_mode,
                            uint8_t* frame) {
    std::array<int, kSbusChannels> pulses{};
    pulses[0] = roll;
    pulses[1] = pitch;
    pulses[2] = kSafeThrottleLow;
    BuildSbusFrame(pulses, TrainerSweepActiveFlags(resolution_mode), resolution_mode, frame);
}

int RunTrainerSweep(const char* port_name,
                    int seconds,
                    int amplitude,
                    int frame_rate_hz,
                    TrainerResolutionMode resolution_mode) {
    if (seconds <= 0 || seconds > 60) {
        std::fprintf(stderr, "--trainer-sweep duration must be 1..60 seconds.\n");
        return 2;
    }
    if (resolution_mode == TrainerResolutionMode::Gx12_2x && !kGx12Resolution2xBuild) {
        std::fprintf(stderr,
                     "--trainer-sweep gx12_2x requires a 2x-capable gx12mouse build.\n");
        return 2;
    }
    const int amplitude_limit = TrainerDomainLimit(resolution_mode);
    if (amplitude <= 0 || amplitude > amplitude_limit) {
        std::fprintf(stderr,
                     "--trainer-sweep amplitude must be 1..%d for resolution_mode=%s.\n",
                     amplitude_limit,
                     TrainerResolutionModeName(resolution_mode));
        return 2;
    }
    if (frame_rate_hz <= 0 || frame_rate_hz > 8000) {
        std::fprintf(stderr, "--trainer-sweep frame_rate_hz must be 1..8000.\n");
        return 2;
    }

    const std::string resolved_port = ResolveTrainerPortName(port_name);
    const std::string path = WindowsComPath(resolved_port.c_str());
    if (path.empty()) {
        std::fprintf(stderr, "--trainer-sweep requires a COM port, for example COM3, or auto.\n");
        return 2;
    }

    HANDLE serial = CreateFileA(path.c_str(),
                                GENERIC_WRITE,
                                0,
                                nullptr,
                                OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL,
                                nullptr);
    if (serial == INVALID_HANDLE_VALUE) {
        std::fprintf(stderr,
                     "CreateFile(%s) failed: le=%lu\n",
                     path.c_str(),
                     static_cast<unsigned long>(GetLastError()));
        return 1;
    }

    DCB dcb{};
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(serial, &dcb)) {
        std::fprintf(stderr, "GetCommState failed: le=%lu\n", static_cast<unsigned long>(GetLastError()));
        CloseHandle(serial);
        return 1;
    }
    dcb.BaudRate = CBR_115200;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    if (!SetCommState(serial, &dcb)) {
        std::fprintf(stderr, "SetCommState failed: le=%lu\n", static_cast<unsigned long>(GetLastError()));
        CloseHandle(serial);
        return 1;
    }

    COMMTIMEOUTS timeouts{};
    timeouts.WriteTotalTimeoutConstant = 20;
    timeouts.WriteTotalTimeoutMultiplier = 1;
    SetCommTimeouts(serial, &timeouts);

    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    std::printf("\n--trainer-sweep: port=%s duration=%d sec amplitude=%d frame_rate=%d Hz resolution_mode=%s.\n",
                resolved_port.c_str(),
                seconds,
                amplitude,
                frame_rate_hz,
                TrainerResolutionModeName(resolution_mode));
    if (resolution_mode == TrainerResolutionMode::Gx12_2x) {
        std::printf("Sends deterministic GX12 active-mask SBUS ch1/ch2 sweep; left-stick trainer channels stay inactive/physical. Press Ctrl+C to stop.\n\n");
    } else {
        std::printf("Sends deterministic SBUS ch1/ch2 sweep, ch3=low-throttle safety; remaining trainer channels centered. Press Ctrl+C to stop.\n\n");
    }

    using clock = std::chrono::steady_clock;
    const auto start = clock::now();
    const auto end = start + std::chrono::seconds(seconds);
    auto next_frame = start;
    const auto frame_period = std::chrono::microseconds(1000000 / frame_rate_hz);
    auto next_print = start + std::chrono::milliseconds(250);
    uint32_t frames = 0;
    int last_roll = 0;
    int last_pitch = 0;

    while (clock::now() < end) {
        const auto now = clock::now();
        if (now >= next_frame) {
            const double elapsed = std::chrono::duration<double>(now - start).count();
            const double phase = elapsed * 2.0 * 3.14159265358979323846;
            last_roll = static_cast<int>(std::lround(std::sin(phase * 0.50) * amplitude));
            last_pitch = static_cast<int>(std::lround(std::sin(phase * 0.25) * amplitude));

            uint8_t frame[25];
            BuildTrainerSweepFrame(last_roll, last_pitch, resolution_mode, frame);

            DWORD written = 0;
            if (!WriteFile(serial, frame, sizeof(frame), &written, nullptr) ||
                written != sizeof(frame)) {
                std::fprintf(stderr,
                             "WriteFile failed after %lu frame(s): written=%lu le=%lu\n",
                             static_cast<unsigned long>(frames),
                             static_cast<unsigned long>(written),
                             static_cast<unsigned long>(GetLastError()));
                CloseHandle(serial);
                return 1;
            }

            ++frames;
            next_frame += frame_period;
        }

        if (now >= next_print) {
            std::printf("[%.3fs] frames=%5lu roll=%+4d pitch=%+4d\n",
                        std::chrono::duration<double>(now - start).count(),
                        static_cast<unsigned long>(frames),
                        last_roll,
                        last_pitch);
            std::fflush(stdout);
            next_print += std::chrono::milliseconds(250);
        }

        Sleep(0);
    }

    uint8_t neutral[25];
    BuildTrainerSweepFrame(0, 0, resolution_mode, neutral);
    DWORD written = 0;
    (void)WriteFile(serial, neutral, sizeof(neutral), &written, nullptr);

    const double elapsed = std::chrono::duration<double>(clock::now() - start).count();
    std::printf("\nsummary: frames=%lu elapsed=%.3fs actual_frame_rate=%.1f Hz\n",
                static_cast<unsigned long>(frames),
                elapsed,
                elapsed > 0.0 ? static_cast<double>(frames) / elapsed : 0.0);

    CloseHandle(serial);
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    PrintBanner();

    if (argc == 1) {
        return 0;
    }

    if (std::strcmp(argv[1], "--help") == 0 ||
        std::strcmp(argv[1], "-h") == 0) {
        PrintUsage();
        return 0;
    }

    if (std::strcmp(argv[1], "--mouse-rate") == 0) {
        int seconds = 5;
        MouseRateMode mode = MouseRateMode::Sink;
        if (argc >= 3) {
            seconds = ParsePositiveSeconds(argv[2], seconds);
            if (seconds < 0) {
                std::fprintf(stderr, "invalid --mouse-rate duration: %s\n", argv[2]);
                return 2;
            }
        }
        if (argc >= 4 && !ParseMouseRateMode(argv[3], &mode)) {
            std::fprintf(stderr, "invalid --mouse-rate mode: %s\n", argv[3]);
            std::fprintf(stderr, "valid modes: sink, foreground, overlay, overlay-hold\n");
            return 2;
        }
        if (argc > 4) {
            std::fprintf(stderr, "too many arguments for --mouse-rate.\n");
            return 2;
        }
        return RunMouseRate(seconds, mode);
    }

    if (std::strcmp(argv[1], "--mouse-rate-gameinput") == 0) {
        int seconds = 10;
        if (argc >= 3) {
            seconds = ParsePositiveSeconds(argv[2], seconds);
            if (seconds < 0) {
                std::fprintf(stderr, "invalid --mouse-rate-gameinput duration: %s\n", argv[2]);
                return 2;
            }
        }
        if (argc > 3) {
            std::fprintf(stderr, "too many arguments for --mouse-rate-gameinput.\n");
            return 2;
        }
        return RunMouseRateGameInput(seconds);
    }

    if (std::strcmp(argv[1], "--mouse-devices-gameinput") == 0) {
        int seconds = 10;
        if (argc >= 3) {
            seconds = ParsePositiveSeconds(argv[2], seconds);
            if (seconds < 0) {
                std::fprintf(stderr, "invalid --mouse-devices-gameinput duration: %s\n", argv[2]);
                return 2;
            }
        }
        if (argc > 3) {
            std::fprintf(stderr, "too many arguments for --mouse-devices-gameinput.\n");
            return 2;
        }
        return RunMouseDevicesGameInput(seconds);
    }

    if (std::strcmp(argv[1], "--keyboard-rate-gameinput") == 0) {
        int seconds = 10;
        if (argc >= 3) {
            seconds = ParsePositiveIntLimit(argv[2], seconds, 30);
            if (seconds <= 0) {
                std::fprintf(stderr, "invalid --keyboard-rate-gameinput duration: %s\n", argv[2]);
                return 2;
            }
        }
        if (argc > 3) {
            std::fprintf(stderr, "too many arguments for --keyboard-rate-gameinput.\n");
            return 2;
        }
        return RunKeyboardRateGameInput(seconds);
    }

    if (std::strcmp(argv[1], "--wooting-rate") == 0) {
        int seconds = 10;
        int first_key_arg = 2;
        if (argc >= 3) {
            const int parsed_seconds = ParsePositiveIntLimit(argv[2], -1, 30);
            if (parsed_seconds > 0) {
                seconds = parsed_seconds;
                first_key_arg = 3;
            }
        }
        std::vector<std::string> keys;
        for (int i = first_key_arg; i < argc; ++i) {
            keys.emplace_back(argv[i]);
        }
        return RunWootingRate(seconds, keys);
    }

    if (std::strcmp(argv[1], "--hid-list") == 0) {
        if (argc > 2) {
            std::fprintf(stderr, "too many arguments for --hid-list.\n");
            return 2;
        }
        return RunHidList();
    }

    if (std::strcmp(argv[1], "--hid-rate") == 0) {
        int seconds = 5;
        int index = -1;
        if (argc >= 3) {
            seconds = ParsePositiveSeconds(argv[2], seconds);
            if (seconds < 0) {
                std::fprintf(stderr, "invalid --hid-rate duration: %s\n", argv[2]);
                return 2;
            }
        }
        if (argc >= 4) {
            index = ParseNonNegativeInt(argv[3], index);
            if (index < 0) {
                std::fprintf(stderr, "invalid --hid-rate device index: %s\n", argv[3]);
                return 2;
            }
        }
        if (argc > 4) {
            std::fprintf(stderr, "too many arguments for --hid-rate.\n");
            return 2;
        }
        return RunHidRate(seconds, index);
    }

    if (std::strcmp(argv[1], "--gx12-hid-capture") == 0) {
        int seconds = 10;
        const char* csv_path = nullptr;
        if (argc >= 3) {
            seconds = ParsePositiveSeconds(argv[2], seconds);
            if (seconds < 0) {
                std::fprintf(stderr, "invalid --gx12-hid-capture duration: %s\n", argv[2]);
                return 2;
            }
        }
        if (argc >= 4) {
            csv_path = argv[3];
        }
        if (argc > 4) {
            std::fprintf(stderr, "too many arguments for --gx12-hid-capture.\n");
            return 2;
        }
        return RunGx12HidCapture(seconds, csv_path);
    }

    if (std::strcmp(argv[1], "--trainer") == 0) {
        if (argc < 3) {
            std::fprintf(stderr, "--trainer requires a COM port, for example COM3.\n");
            return 2;
        }

        int seconds = 10;
        int mouse_gain = 50;
        int frame_rate_hz = 1000;
        if (argc >= 4) {
            seconds = ParsePositiveSeconds(argv[3], seconds);
            if (seconds < 0) {
                std::fprintf(stderr, "invalid --trainer duration: %s\n", argv[3]);
                return 2;
            }
        }
        if (argc >= 5) {
            mouse_gain = ParsePositiveIntLimit(argv[4], mouse_gain, 5000);
            if (mouse_gain < 0) {
                std::fprintf(stderr, "invalid --trainer gain: %s\n", argv[4]);
                return 2;
            }
        }
        if (argc >= 6) {
            frame_rate_hz = ParseTrainerFrameRate(argv[5], frame_rate_hz);
            if (frame_rate_hz < 0) {
                std::fprintf(stderr, "invalid --trainer frame_rate_hz: %s\n", argv[5]);
                return 2;
            }
        }
        if (argc > 6) {
            std::fprintf(stderr, "too many arguments for --trainer.\n");
            return 2;
        }
        return RunTrainer(argv[2], seconds, mouse_gain, frame_rate_hz);
    }

    if (std::strcmp(argv[1], "--trainer-sweep") == 0) {
        if (argc < 3) {
            std::fprintf(stderr, "--trainer-sweep requires a COM port, for example COM3.\n");
            return 2;
        }

        int seconds = 20;
        int amplitude = 400;
        int frame_rate_hz = 1000;
        TrainerResolutionMode resolution_mode = TrainerResolutionMode::Legacy;
        if (argc >= 4) {
            seconds = ParsePositiveSeconds(argv[3], seconds);
            if (seconds < 0) {
                std::fprintf(stderr, "invalid --trainer-sweep duration: %s\n", argv[3]);
                return 2;
            }
        }
        if (argc >= 5) {
            amplitude = ParsePositiveIntLimit(argv[4], amplitude, 1024);
            if (amplitude < 0) {
                std::fprintf(stderr, "invalid --trainer-sweep amplitude: %s\n", argv[4]);
                return 2;
            }
        }
        if (argc >= 6) {
            frame_rate_hz = ParseTrainerFrameRate(argv[5], frame_rate_hz);
            if (frame_rate_hz < 0) {
                std::fprintf(stderr, "invalid --trainer-sweep frame_rate_hz: %s\n", argv[5]);
                return 2;
            }
        }
        if (argc >= 7) {
            if (!ParseTrainerResolutionModeName(argv[6], &resolution_mode)) {
                std::fprintf(stderr, "invalid --trainer-sweep resolution mode: %s\n", argv[6]);
                std::fprintf(stderr, "valid modes: legacy, gx12_2x\n");
                return 2;
            }
        }
        if (argc > 7) {
            std::fprintf(stderr, "too many arguments for --trainer-sweep.\n");
            return 2;
        }
        return RunTrainerSweep(argv[2], seconds, amplitude, frame_rate_hz, resolution_mode);
    }

    if (std::strcmp(argv[1], "--trainer-resolution-self-test") == 0) {
        if (argc != 2) {
            std::fprintf(stderr, "too many arguments for --trainer-resolution-self-test.\n");
            return 2;
        }
        return RunTrainerResolutionSelfTest();
    }

    if (std::strcmp(argv[1], "--trainer-profile") == 0 ||
        std::strcmp(argv[1], "--tune") == 0) {
        if (argc < 3) {
            std::fprintf(stderr, "%s requires a profile file, for example profiles\\whoop-soft.toml.\n", argv[1]);
            return 2;
        }

        TrainerProfile profile;
        if (!LoadTrainerProfile(argv[2], &profile)) {
            return 2;
        }
        bool live_reload = false;
        bool saw_duration = false;
        for (int i = 3; i < argc; ++i) {
            if (_stricmp(argv[i], "live") == 0 || _stricmp(argv[i], "--live") == 0) {
                live_reload = true;
                continue;
            }
            if (saw_duration) {
                std::fprintf(stderr, "duplicate %s duration override: %s\n", argv[1], argv[i]);
                return 2;
            }
            profile.seconds = ParsePositiveIntLimit(argv[i],
                                                    profile.seconds,
                                                    kTrainerProfileMaxSeconds);
            if (profile.seconds <= 0) {
                std::fprintf(stderr, "invalid %s duration override: %s\n", argv[1], argv[i]);
                return 2;
            }
            saw_duration = true;
            if (!ValidateTrainerProfile(profile)) {
                return 2;
            }
        }
        const bool guided = std::strcmp(argv[1], "--tune") == 0;
        return RunTrainerProfile(profile, guided, live_reload);
    }

    if (std::strcmp(argv[1], "--mouse-aim-dry-run") == 0) {
        if (argc < 3) {
            std::fprintf(stderr, "--mouse-aim-dry-run requires a profile file, for example profiles\\mouse-aim-whoop.toml.\n");
            return 2;
        }

        TrainerProfile profile;
        if (!LoadTrainerProfile(argv[2], &profile)) {
            return 2;
        }
        int seconds = 10;
        if (argc >= 4) {
            seconds = ParsePositiveIntLimit(argv[3], seconds, 60);
            if (seconds <= 0) {
                std::fprintf(stderr, "invalid --mouse-aim-dry-run duration: %s\n", argv[3]);
                return 2;
            }
        }
        if (argc > 4) {
            std::fprintf(stderr, "too many arguments for --mouse-aim-dry-run.\n");
            return 2;
        }
        return RunMouseAimDryRun(profile, seconds);
    }

    if (std::strcmp(argv[1], "--mouse-left-dry-run") == 0) {
        if (argc < 3) {
            std::fprintf(stderr, "--mouse-left-dry-run requires a profile file, for example profiles\\whoop-linear-dual-mouse.toml.\n");
            return 2;
        }

        TrainerProfile profile;
        if (!LoadTrainerProfile(argv[2], &profile)) {
            return 2;
        }
        int seconds = 10;
        if (argc >= 4) {
            seconds = ParsePositiveIntLimit(argv[3], seconds, 60);
            if (seconds <= 0) {
                std::fprintf(stderr, "invalid --mouse-left-dry-run duration: %s\n", argv[3]);
                return 2;
            }
        }
        if (argc > 4) {
            std::fprintf(stderr, "too many arguments for --mouse-left-dry-run.\n");
            return 2;
        }
        return RunMouseLeftDryRun(profile, seconds);
    }

    if (std::strcmp(argv[1], "--elastic-preview") == 0) {
        if (argc < 3) {
            std::fprintf(stderr, "--elastic-preview requires a profile file, for example profiles\\whoop-linear.toml.\n");
            return 2;
        }
        if (argc > 3) {
            std::fprintf(stderr, "too many arguments for --elastic-preview.\n");
            return 2;
        }

        TrainerProfile profile;
        if (!LoadTrainerProfile(argv[2], &profile)) {
            return 2;
        }
        PrintElasticPreview(profile);
        return 0;
    }

    if (std::strcmp(argv[1], "--gimbal-preview") == 0) {
        if (argc < 3) {
            std::fprintf(stderr, "--gimbal-preview requires a profile file, for example profiles\\whoop-linear.toml.\n");
            return 2;
        }
        if (argc > 3) {
            std::fprintf(stderr, "too many arguments for --gimbal-preview.\n");
            return 2;
        }
        TrainerProfile profile;
        if (!LoadTrainerProfile(argv[2], &profile)) {
            return 1;
        }
        PrintGimbalPreview(profile);
        return 0;
    }

    if (std::strcmp(argv[1], "--mouse-spike-test") == 0) {
        if (argc < 3) {
            std::fprintf(stderr, "--mouse-spike-test requires a profile file, for example profiles\\whoop-linear.toml.\n");
            return 2;
        }
        if (argc > 3) {
            std::fprintf(stderr, "too many arguments for --mouse-spike-test.\n");
            return 2;
        }
        TrainerProfile profile;
        if (!LoadTrainerProfile(argv[2], &profile)) {
            return 1;
        }
        PrintMouseSpikeTest(profile);
        return 0;
    }

    if (std::strcmp(argv[1], "--input-filter-preview") == 0) {
        if (argc < 3) {
            std::fprintf(stderr, "--input-filter-preview requires a profile file, for example profiles\\whoop-linear.toml.\n");
            return 2;
        }
        if (argc > 3) {
            std::fprintf(stderr, "too many arguments for --input-filter-preview.\n");
            return 2;
        }
        TrainerProfile profile;
        if (!LoadTrainerProfile(argv[2], &profile)) {
            return 1;
        }
        PrintInputFilterPreview(profile);
        return 0;
    }

    if (std::strcmp(argv[1], "--output-curve-preview") == 0) {
        if (argc < 3) {
            std::fprintf(stderr, "--output-curve-preview requires a profile file, for example profiles\\whoop-linear.toml.\n");
            return 2;
        }
        if (argc > 3) {
            std::fprintf(stderr, "too many arguments for --output-curve-preview.\n");
            return 2;
        }
        TrainerProfile profile;
        if (!LoadTrainerProfile(argv[2], &profile)) {
            return 1;
        }
        PrintOutputCurvePreview(profile);
        return 0;
    }

    if (std::strcmp(argv[1], "--show-profile") == 0) {
        if (argc < 3) {
            std::fprintf(stderr, "--show-profile requires a profile file, for example profiles\\whoop-soft.toml.\n");
            return 2;
        }
        if (argc > 3) {
            std::fprintf(stderr, "too many arguments for --show-profile.\n");
            return 2;
        }

        TrainerProfile profile;
        if (!LoadTrainerProfile(argv[2], &profile)) {
            return 2;
        }
        PrintTrainerProfile(profile, false);
        return 0;
    }

    std::fprintf(stderr, "unknown arg: %s\n", argv[1]);
    PrintUsage();
    return 2;
}

