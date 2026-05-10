#include <chrono>
#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <mutex>
#include <sstream>
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
    std::printf("  gx12mouse --trainer-profile FILE [sec|live] [--recording PATH --record-toggle=KEY|Mouse4 --record-duration=sec --record-overwrite --runtime-control PATH] [--bind|--bind-block KEY CHANNELS RECORDING ...]\n");
    std::printf("                                  Send mouse-derived SBUS using a TOML tuning profile\n");
    std::printf("                                  Add 'live' to reload mapper/keyboard edits; recording and playback bind hotkeys run inside this trainer process\n");
    std::printf("                                  Integrated PC-mouse playback replays recorded mapper ticks; --bind-block ignores live input on the bind channels\n");
    std::printf("  gx12mouse --trainer-record FILE RECORDING [sec|live] [--record-toggle=KEY|Mouse4 --record-overwrite --runtime-control PATH]\n");
    std::printf("                                  Record input tracks in memory and save the CSV on a background thread when capture stops; optional key toggles capture\n");
    std::printf("  gx12mouse --trainer-playback RECORDING [COM|auto] [once|loop] [--channels=trainer_right|ail,ele|thr,rud|radio_thr,radio_rud|gimbals] [--trigger=KEY|Mouse4]\n");
    std::printf("                                  Replay selected recorded channels through composite trainer SBUS\n");
    std::printf("  gx12mouse --trainer-playback-bank [COM|auto] [once|loop] --bind|--bind-block KEY CHANNELS RECORDING ...\n");
    std::printf("                                  Arm up to 12 hotkey playback bindings on one trainer serial port\n");
    std::printf("  gx12mouse --recording-info RECORDING\n");
    std::printf("                                  Print recording metadata, duration, and available tracks\n");
    std::printf("  gx12mouse --recording-determinism-audit RECORDING PROFILE [CHANNELS] [--timed-runs=N]\n");
    std::printf("                                  Offline audit for real recording replay repeatability; no serial port is opened\n");
    std::printf("  gx12mouse --recording-self-test\n");
    std::printf("                                  Validate recording parser, channel masks, and trigger parsing\n");
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
    if (!data || !out || length < kGx12HidReportSize) {
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

struct HidReportTiming {
    uint64_t intervals = 0;
    uint64_t sum_interval_us = 0;
    uint64_t min_interval_us = 0;
    uint64_t max_interval_us = 0;
    std::chrono::steady_clock::time_point last_report_time{};
    bool has_last_report_time = false;

    uint64_t Record(std::chrono::steady_clock::time_point now) {
        if (!has_last_report_time) {
            last_report_time = now;
            has_last_report_time = true;
            return 0;
        }

        const auto delta = std::chrono::duration_cast<std::chrono::microseconds>(
                               now - last_report_time)
                               .count();
        const uint64_t interval_us = delta > 0 ? static_cast<uint64_t>(delta) : 0;
        if (intervals == 0 || interval_us < min_interval_us) {
            min_interval_us = interval_us;
        }
        if (interval_us > max_interval_us) {
            max_interval_us = interval_us;
        }
        sum_interval_us += interval_us;
        ++intervals;
        last_report_time = now;
        return interval_us;
    }

    double AverageIntervalUs() const {
        return intervals > 0
                   ? static_cast<double>(sum_interval_us) / static_cast<double>(intervals)
                   : 0.0;
    }
};

struct HidRateStats {
    uint64_t reports = 0;
    uint64_t timeouts = 0;
    uint64_t errors = 0;
    uint64_t bytes = 0;
    int min_len = 0;
    int max_len = 0;
    int last_len = 0;
    std::array<unsigned char, 16> last_prefix{};
    HidReportTiming timing;
};

void RecordHidReport(HidRateStats* stats,
                     const unsigned char* data,
                     int length,
                     std::chrono::steady_clock::time_point now) {
    ++stats->reports;
    stats->bytes += static_cast<uint64_t>(length);
    stats->last_len = length;
    stats->timing.Record(now);
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
                        double window_seconds,
                        double elapsed_seconds) {
    const uint64_t reports = current.reports - previous.reports;
    const uint64_t timeouts = current.timeouts - previous.timeouts;
    const uint64_t errors = current.errors - previous.errors;
    const uint64_t bytes = current.bytes - previous.bytes;
    const double hz = window_seconds > 0.0
                          ? static_cast<double>(reports) / window_seconds
                          : 0.0;
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
    auto last_print_time = start;
    HidRateStats stats;
    HidRateStats last_print;
    std::array<unsigned char, 1024> buffer{};
    std::string last_error;

    while (clock::now() < end) {
        const int read = hid_read_timeout(device, buffer.data(), buffer.size(), 1);
        const auto now = clock::now();
        if (read > 0) {
            RecordHidReport(&stats, buffer.data(), read, now);
        } else if (read == 0) {
            ++stats.timeouts;
        } else {
            ++stats.errors;
            last_error = HidReadErrorUtf8(device);
            break;
        }

        if (now >= next_print) {
            const double window_seconds =
                std::chrono::duration<double>(now - last_print_time).count();
            PrintHidRateSample(last_print,
                               stats,
                               window_seconds,
                               std::chrono::duration<double>(now - start).count());
            std::fflush(stdout);
            last_print = stats;
            last_print_time = now;
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
    if (stats.timing.intervals > 0) {
        std::printf("report_interval: avg=%.3f ms min=%.3f ms max=%.3f ms samples=%llu\n",
                    stats.timing.AverageIntervalUs() / 1000.0,
                    static_cast<double>(stats.timing.min_interval_us) / 1000.0,
                    static_cast<double>(stats.timing.max_interval_us) / 1000.0,
                    static_cast<unsigned long long>(stats.timing.intervals));
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

enum class ElasticReturnMetric {
    Axis,
    DiagonalNormalized,
};

enum class ElasticReturnActivation {
    Always,
    WhileMoving,
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
    double elastic_return_idle_seconds = 1000000.0;
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
    ElasticReturnMetric elastic_return_metric = ElasticReturnMetric::Axis;
    ElasticReturnActivation elastic_return_activation = ElasticReturnActivation::Always;
    double elastic_return_coefficient = 0.0;
    double elastic_return_idle_coefficient = 0.0;
    double elastic_return_taper_ms = 0.0;
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

const char* ElasticReturnMetricName(ElasticReturnMetric metric) {
    switch (metric) {
    case ElasticReturnMetric::Axis:
        return "axis";
    case ElasticReturnMetric::DiagonalNormalized:
        return "diagonal_normalized";
    }
    return "axis";
}

bool ParseElasticReturnMetricName(const std::string& text, ElasticReturnMetric* metric) {
    const std::string value = ToLowerAscii(text);
    if (value.empty() || value == "axis" || value == "per_axis" || value == "legacy") {
        *metric = ElasticReturnMetric::Axis;
        return true;
    }
    if (value == "diagonal_normalized" ||
        value == "diagonal-normalized" ||
        value == "diag_normalized" ||
        value == "vector" ||
        value == "radial") {
        *metric = ElasticReturnMetric::DiagonalNormalized;
        return true;
    }
    return false;
}

const char* ElasticReturnActivationName(ElasticReturnActivation activation) {
    switch (activation) {
    case ElasticReturnActivation::Always:
        return "always";
    case ElasticReturnActivation::WhileMoving:
        return "while_moving";
    }
    return "always";
}

bool ParseElasticReturnActivationName(const std::string& text,
                                      ElasticReturnActivation* activation) {
    const std::string value = ToLowerAscii(text);
    if (value.empty() || value == "always" || value == "continuous" ||
        value == "legacy") {
        *activation = ElasticReturnActivation::Always;
        return true;
    }
    if (value == "while_moving" ||
        value == "while-moving" ||
        value == "moving" ||
        value == "input_gated" ||
        value == "input-gated" ||
        value == "motion_gated" ||
        value == "motion-gated" ||
        value == "clutched") {
        *activation = ElasticReturnActivation::WhileMoving;
        return true;
    }
    return false;
}

double EffectiveElasticReturnCoefficient(const TrainerProfile& profile,
                                         double elastic_return_coefficient,
                                         bool mouse_moved,
                                         double dt,
                                         RightStickSharedState* state) {
    elastic_return_coefficient = std::max(0.0, elastic_return_coefficient);
    if (elastic_return_coefficient <= 0.0) {
        return 0.0;
    }
    if (profile.elastic_return_activation == ElasticReturnActivation::Always) {
        if (state && mouse_moved) {
            state->elastic_return_idle_seconds = 0.0;
        }
        return elastic_return_coefficient;
    }

    if (mouse_moved) {
        if (state) {
            state->elastic_return_idle_seconds = 0.0;
        }
        return elastic_return_coefficient;
    }

    double idle_seconds = 1000000.0;
    if (state) {
        state->elastic_return_idle_seconds =
            std::min(1000000.0,
                     state->elastic_return_idle_seconds + std::max(0.0, dt));
        idle_seconds = state->elastic_return_idle_seconds;
    }

    const double target_coefficient =
        ClampDouble(profile.elastic_return_idle_coefficient,
                    0.0,
                    elastic_return_coefficient);
    const double taper_seconds = std::max(0.0, profile.elastic_return_taper_ms) / 1000.0;
    if (taper_seconds <= 0.0) {
        return target_coefficient;
    }

    const double t = ClampDouble(idle_seconds / taper_seconds, 0.0, 1.0);
    return LerpDouble(elastic_return_coefficient, target_coefficient, t);
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

void ApplyRightStickDiagonalNormalizedReturn(double return_step,
                                             double elastic_return_coefficient,
                                             ElasticReturnMode elastic_return_mode,
                                             double elastic_return_curve,
                                             double dt,
                                             bool apply_return,
                                             double* roll_position,
                                             double* pitch_position,
                                             const TrainerProfile& profile) {
    if (!apply_return || !roll_position || !pitch_position) {
        return;
    }

    const double roll = *roll_position;
    const double pitch = *pitch_position;
    const double radius = std::sqrt((roll * roll) + (pitch * pitch));
    if (radius <= 0.0) {
        return;
    }

    const double basis = std::max(std::abs(roll), std::abs(pitch));
    double step = return_step;
    step += ShapedElasticReturnRatePerSecond(basis,
                                             profile.max_output,
                                             elastic_return_coefficient,
                                             elastic_return_mode,
                                             elastic_return_curve,
                                             profile.return_shape) *
            dt;
    if (step <= 0.0) {
        return;
    }
    if (step >= radius) {
        *roll_position = 0.0;
        *pitch_position = 0.0;
        return;
    }

    const double scale = (radius - step) / radius;
    *roll_position = roll * scale;
    *pitch_position = pitch * scale;
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
    const bool mouse_moved = std::abs(roll_source) > 0.0 ||
                             std::abs(pitch_source) > 0.0;
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

    const double active_elastic_return_coefficient =
        (apply_return &&
         elastic_return_coefficient > 0.0)
            ? EffectiveElasticReturnCoefficient(profile,
                                                elastic_return_coefficient,
                                                mouse_moved,
                                                dt,
                                                shared_state)
            : 0.0;
    const bool apply_any_return = apply_return &&
                                  (return_step > 0.0 ||
                                   active_elastic_return_coefficient > 0.0);

    if (apply_any_return &&
        profile.elastic_return_metric == ElasticReturnMetric::DiagonalNormalized) {
        ApplyRightStickDiagonalNormalizedReturn(return_step,
                                                active_elastic_return_coefficient,
                                                profile.elastic_return_mode,
                                                profile.elastic_return_curve,
                                                dt,
                                                true,
                                                roll_position,
                                                pitch_position,
                                                profile);
    } else if (apply_any_return) {
        ApplyRightStickAxisReturn(return_step,
                                  active_elastic_return_coefficient,
                                  profile.elastic_return_mode,
                                  profile.elastic_return_curve,
                                  dt,
                                  true,
                                  roll_position,
                                  profile);
        ApplyRightStickAxisReturn(return_step,
                                  active_elastic_return_coefficient,
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
    if (profile.elastic_return_idle_coefficient < 0.0 ||
        profile.elastic_return_idle_coefficient > 100.0) {
        std::fprintf(stderr, "profile error: mapper.elastic_return_idle_coefficient must be 0..100 per second.\n");
        return false;
    }
    if (profile.elastic_return_taper_ms < 0.0 || profile.elastic_return_taper_ms > 5000.0) {
        std::fprintf(stderr, "profile error: mapper.elastic_return_taper_ms must be 0..5000 milliseconds.\n");
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
    const std::string elastic_return_metric_text =
        mapper["elastic_return_metric"].value_or(std::string(ElasticReturnMetricName(profile->elastic_return_metric)));
    if (!ParseElasticReturnMetricName(elastic_return_metric_text, &profile->elastic_return_metric)) {
        std::fprintf(stderr,
                     "profile error: mapper.elastic_return_metric must be axis or diagonal_normalized.\n");
        return false;
    }
    const std::string elastic_return_activation_text =
        mapper["elastic_return_activation"].value_or(
            std::string(ElasticReturnActivationName(profile->elastic_return_activation)));
    if (!ParseElasticReturnActivationName(elastic_return_activation_text,
                                          &profile->elastic_return_activation)) {
        std::fprintf(stderr,
                     "profile error: mapper.elastic_return_activation must be always or while_moving.\n");
        return false;
    }
    profile->elastic_return_coefficient = mapper["elastic_return_coefficient"].value_or(profile->elastic_return_coefficient);
    profile->elastic_return_idle_coefficient =
        mapper["elastic_return_idle_coefficient"].value_or(profile->elastic_return_idle_coefficient);
    profile->elastic_return_taper_ms =
        mapper["elastic_return_taper_ms"].value_or(profile->elastic_return_taper_ms);
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
    std::printf("  mapper: roll_gain=%.3f pitch_gain=%.3f mapper_rate=%d Hz effective_gain=%.3fx max=%d deadband=%d expo=%.3f smoothing=%.3f input_filter=%s idle_return=%s idle_rate=%.1f/s idle_ms=%.1f constant_return=%s constant_rate=%.1f/s elastic_return=%s elastic_mode=%s elastic_metric=%s elastic_activation=%s elastic_coeff=%.3f/s elastic_idle_coeff=%.3f/s elastic_taper_ms=%.1f elastic_curve=%.3f\n",
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
                ElasticReturnMetricName(profile.elastic_return_metric),
                ElasticReturnActivationName(profile.elastic_return_activation),
                profile.elastic_return_coefficient,
                profile.elastic_return_idle_coefficient,
                profile.elastic_return_taper_ms,
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
    std::printf("  elastic_return=%s mode=%s metric=%s activation=%s coefficient=%.3f/s idle_coefficient=%.3f/s taper_ms=%.1f curve=%.3f max=%d mapper_rate=%d Hz\n",
                profile.elastic_return_enabled ? "true" : "false",
                ElasticReturnModeName(profile.elastic_return_mode),
                ElasticReturnMetricName(profile.elastic_return_metric),
                ElasticReturnActivationName(profile.elastic_return_activation),
                profile.elastic_return_coefficient,
                profile.elastic_return_idle_coefficient,
                profile.elastic_return_taper_ms,
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

    const double dt = 1.0 / static_cast<double>(mapper_rate_hz);
    std::printf("\n  Selected metric return step examples at 50%% deflection\n");
    std::printf("  position        roll_step  pitch_step  vector_step\n");
    const double half = 0.5 * static_cast<double>(profile.max_output);
    const std::array<std::pair<double, double>, 3> metric_samples = {{
        {half, 0.0},
        {half, half},
        {half, half * 0.5},
    }};
    for (const auto& sample : metric_samples) {
        double roll = sample.first;
        double pitch = sample.second;
        if (profile.elastic_return_metric == ElasticReturnMetric::DiagonalNormalized) {
            ApplyRightStickDiagonalNormalizedReturn(0.0,
                                                    coefficient,
                                                    profile.elastic_return_mode,
                                                    profile.elastic_return_curve,
                                                    dt,
                                                    true,
                                                    &roll,
                                                    &pitch,
                                                    profile);
        } else {
            ApplyRightStickAxisReturn(0.0,
                                      coefficient,
                                      profile.elastic_return_mode,
                                      profile.elastic_return_curve,
                                      dt,
                                      true,
                                      &roll,
                                      profile);
            ApplyRightStickAxisReturn(0.0,
                                      coefficient,
                                      profile.elastic_return_mode,
                                      profile.elastic_return_curve,
                                      dt,
                                      true,
                                      &pitch,
                                      profile);
        }
        const double roll_step = sample.first - roll;
        const double pitch_step = sample.second - pitch;
        const double vector_step = std::sqrt((roll_step * roll_step) + (pitch_step * pitch_step));
        std::printf("  %6.1f,%6.1f  %9.3f  %10.3f  %11.3f\n",
                    sample.first,
                    sample.second,
                    roll_step,
                    pitch_step,
                    vector_step);
    }

    std::printf("\n  No-input decay from full stick using selected elastic mode/activation\n");
    std::printf("  time_ms  coeff/s  position  visual\n");
    double position = static_cast<double>(profile.max_output);
    RightStickSharedState preview_state;
    preview_state.elastic_return_idle_seconds = 0.0;
    int next_sample_ms = 0;
    const int total_ticks = mapper_rate_hz;
    for (int tick = 0; tick <= total_ticks; ++tick) {
        const double effective_coefficient =
            EffectiveElasticReturnCoefficient(profile,
                                              coefficient,
                                              false,
                                              tick == 0 ? 0.0 : dt,
                                              &preview_state);
        const int elapsed_ms = static_cast<int>(std::lround(1000.0 * static_cast<double>(tick) /
                                                            static_cast<double>(mapper_rate_hz)));
        if (elapsed_ms >= next_sample_ms || tick == total_ticks) {
            const double norm = ClampDouble(position / static_cast<double>(profile.max_output), 0.0, 1.0);
            const int bar_count = static_cast<int>(std::lround(norm * kBarWidth));
            const std::string bar(static_cast<size_t>(bar_count), '#');
            std::printf("  %7d  %7.3f  %8.1f  |%-*s|\n",
                        elapsed_ms,
                        effective_coefficient,
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
                                                             effective_coefficient,
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

struct TrainerCsvLogWriter {
    struct Row {
        double time_s = 0.0;
        uint32_t frame = 0;
        uint64_t mouse_events = 0;
        uint64_t input_aux = 0;
        uint64_t keyboard_events = 0;
        int64_t dx = 0;
        int64_t dy = 0;
        double mouse_raw_dx = 0.0;
        double mouse_raw_dy = 0.0;
        double mouse_filtered_dx = 0.0;
        double mouse_filtered_dy = 0.0;
        uint64_t despike_recent_count = 0;
        uint64_t despike_total_count = 0;
        int64_t left_dx = 0;
        int64_t left_dy = 0;
        double left_yaw_raw = 0.0;
        double left_yaw_filtered = 0.0;
        uint64_t left_despike_recent_count = 0;
        uint64_t left_despike_total_count = 0;
        PositionModel left_yaw_position_model = PositionModel::Integrator;
        double left_yaw_adaptive_gain = 1.0;
        double left_yaw_gate_scale = 1.0;
        bool left_yaw_gimbal_antiwindup_active = false;
        PositionModel position_model = PositionModel::Integrator;
        double adaptive_gain = 1.0;
        double gate_scale = 1.0;
        bool gimbal_antiwindup_active = false;
        double roll_position = 0.0;
        double pitch_position = 0.0;
        double roll_velocity = 0.0;
        double pitch_velocity = 0.0;
        int roll = 0;
        int pitch = 0;
        int throttle = 0;
        int yaw = 0;
        double aim_x = 0.0;
        double aim_y = 0.0;
        double analog_throttle_up = 0.0;
        double analog_throttle_down = 0.0;
        double analog_yaw_left = 0.0;
        double analog_yaw_right = 0.0;
        double analog_cut = 0.0;
        int64_t late_us = 0;
    };

    std::ofstream csv;
    std::string path;
    std::mutex mutex;
    std::condition_variable cv;
    std::deque<Row> queue;
    std::thread worker;
    bool open = false;
    bool stop_requested = false;
    uint64_t queued_rows = 0;
    uint64_t dropped_rows = 0;

    ~TrainerCsvLogWriter() {
        Close();
    }

    bool Open(const TrainerProfile& profile) {
        if (!profile.log_csv || profile.log_path.empty()) {
            return false;
        }

        Close();
        path = profile.log_path;
        queued_rows = 0;
        dropped_rows = 0;
        {
            std::lock_guard<std::mutex> lock(mutex);
            queue.clear();
            stop_requested = false;
        }

        std::error_code ec;
        const std::filesystem::path log_path(path);
        if (log_path.has_parent_path()) {
            std::filesystem::create_directories(log_path.parent_path(), ec);
            if (ec) {
                std::fprintf(stderr,
                             "warning: could not create log directory '%s': %s\n",
                             log_path.parent_path().string().c_str(),
                             ec.message().c_str());
            }
        }

        csv.open(log_path, std::ios::out | std::ios::trunc);
        if (!csv) {
            std::fprintf(stderr,
                         "warning: could not open CSV log '%s'. Continuing without CSV.\n",
                         path.c_str());
            return false;
        }

        csv << "time_s,frame,mouse_events,input_aux,keyboard_events,dx,dy,mouse_raw_dx,mouse_raw_dy,mouse_filtered_dx,mouse_filtered_dy,despike_count_10s,despike_count_total,left_dx,left_dy,left_yaw_raw,left_yaw_filtered,left_yaw_despike_count_10s,left_yaw_despike_count_total,left_yaw_position_model,left_yaw_adaptive_gain,left_yaw_gate_scale,left_yaw_gimbal_antiwindup_active,position_model,adaptive_gain,gate_scale,gimbal_antiwindup_active,roll_position,pitch_position,roll_velocity,pitch_velocity,roll,pitch,throttle,yaw,aim_x,aim_y,analog_throttle_up,analog_throttle_down,analog_yaw_left,analog_yaw_right,analog_cut,late_us\n";
        open = true;
        worker = std::thread([this]() {
            WorkerLoop();
        });
        return true;
    }

    void Close() {
        if (worker.joinable()) {
            {
                std::lock_guard<std::mutex> lock(mutex);
                stop_requested = true;
            }
            cv.notify_one();
            worker.join();
        }
        if (csv.is_open()) {
            csv.flush();
            csv.close();
        }
        open = false;
        {
            std::lock_guard<std::mutex> lock(mutex);
            queue.clear();
            stop_requested = false;
        }
    }

    bool IsOpen() const {
        return open;
    }

    uint64_t DroppedRows() const {
        return dropped_rows;
    }

    void Enqueue(Row row) {
        if (!open) {
            return;
        }

        constexpr size_t kMaxQueuedRows = 65536;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (stop_requested || queue.size() >= kMaxQueuedRows) {
                ++dropped_rows;
                return;
            }
            queue.push_back(row);
        }
        ++queued_rows;
        cv.notify_one();
    }

    void WorkerLoop() {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);

        for (;;) {
            std::deque<Row> local;
            {
                std::unique_lock<std::mutex> lock(mutex);
                cv.wait(lock, [&]() {
                    return stop_requested || !queue.empty();
                });
                local.swap(queue);
                if (local.empty() && stop_requested) {
                    break;
                }
            }

            for (const Row& row : local) {
                WriteRow(row);
            }
        }
    }

    void WriteRow(const Row& row) {
        csv << row.time_s << ','
            << row.frame << ','
            << row.mouse_events << ','
            << row.input_aux << ','
            << row.keyboard_events << ','
            << row.dx << ','
            << row.dy << ','
            << row.mouse_raw_dx << ','
            << row.mouse_raw_dy << ','
            << row.mouse_filtered_dx << ','
            << row.mouse_filtered_dy << ','
            << row.despike_recent_count << ','
            << row.despike_total_count << ','
            << row.left_dx << ','
            << row.left_dy << ','
            << row.left_yaw_raw << ','
            << row.left_yaw_filtered << ','
            << row.left_despike_recent_count << ','
            << row.left_despike_total_count << ','
            << PositionModelName(row.left_yaw_position_model) << ','
            << row.left_yaw_adaptive_gain << ','
            << row.left_yaw_gate_scale << ','
            << (row.left_yaw_gimbal_antiwindup_active ? 1 : 0) << ','
            << PositionModelName(row.position_model) << ','
            << row.adaptive_gain << ','
            << row.gate_scale << ','
            << (row.gimbal_antiwindup_active ? 1 : 0) << ','
            << row.roll_position << ','
            << row.pitch_position << ','
            << row.roll_velocity << ','
            << row.pitch_velocity << ','
            << row.roll << ','
            << row.pitch << ','
            << row.throttle << ','
            << row.yaw << ','
            << row.aim_x << ','
            << row.aim_y << ','
            << row.analog_throttle_up << ','
            << row.analog_throttle_down << ','
            << row.analog_yaw_left << ','
            << row.analog_yaw_right << ','
            << row.analog_cut << ','
            << row.late_us << '\n';
    }
};

constexpr int kRecordingSchemaVersion = 1;
constexpr const char* kRecordingMagic = "gx12rec_csv";

std::string TrimAscii(std::string value) {
    size_t first = 0;
    while (first < value.size() &&
           (value[first] == ' ' || value[first] == '\t' ||
            value[first] == '\r' || value[first] == '\n')) {
        ++first;
    }
    size_t last = value.size();
    while (last > first &&
           (value[last - 1] == ' ' || value[last - 1] == '\t' ||
            value[last - 1] == '\r' || value[last - 1] == '\n')) {
        --last;
    }
    return value.substr(first, last - first);
}

std::vector<std::string> SplitSimpleCsvLine(const std::string& line) {
    std::vector<std::string> fields;
    size_t start = 0;
    while (start <= line.size()) {
        const size_t comma = line.find(',', start);
        if (comma == std::string::npos) {
            fields.push_back(line.substr(start));
            break;
        }
        fields.push_back(line.substr(start, comma - start));
        start = comma + 1;
    }
    return fields;
}

std::string MetadataValue(std::string value) {
    for (char& ch : value) {
        if (ch == '\r' || ch == '\n') {
            ch = ' ';
        }
    }
    return value;
}

std::string HexUint64(uint64_t value) {
    std::ostringstream out;
    out << std::uppercase << std::hex << std::setw(16) << std::setfill('0') << value;
    return out.str();
}

std::string Fnv1aFileHashHex(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return "";
    }
    uint64_t hash = 1469598103934665603ULL;
    char buffer[4096];
    while (file) {
        file.read(buffer, sizeof(buffer));
        const std::streamsize got = file.gcount();
        for (std::streamsize i = 0; i < got; ++i) {
            hash ^= static_cast<unsigned char>(buffer[i]);
            hash *= 1099511628211ULL;
        }
    }
    return HexUint64(hash);
}

int64_t UnixTimeMillisecondsNow() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

struct RecordingStartState {
    bool right_mapper_available = false;
    double right_roll_value = 0.0;
    double right_pitch_value = 0.0;
    double right_roll_velocity = 0.0;
    double right_pitch_velocity = 0.0;
    int right_roll_pulse = 0;
    int right_pitch_pulse = 0;

    bool mouse_left_available = false;
    double mouse_left_throttle_value = 0.0;
    double mouse_left_yaw_value = 0.0;
    double mouse_left_yaw_filtered = 0.0;
    double mouse_left_yaw_position = 0.0;
    double mouse_left_yaw_dummy_position = 0.0;
    double mouse_left_yaw_velocity = 0.0;
    double mouse_left_yaw_dummy_velocity = 0.0;
    int mouse_left_throttle_pulse = 0;
    int mouse_left_yaw_pulse = 0;

    bool right_mouse_left_available = false;
    double right_mouse_left_throttle_value = 0.0;
    double right_mouse_left_yaw_target = 0.0;
    double right_mouse_left_yaw_output = 0.0;
    int right_mouse_left_throttle_pulse = 0;
    int right_mouse_left_yaw_pulse = 0;
};

struct RecordingCsvWriter;

struct Gx12RecordingHidCapture {
    bool hid_initialized = false;
    bool available = false;
    bool failed = false;
    hid_device* device = nullptr;
    int index = -1;
    unsigned short vendor_id = 0;
    unsigned short product_id = 0;
    unsigned short usage_page = 0;
    unsigned short usage = 0;
    int interface_number = -1;
    std::string product;
    std::array<unsigned char, 64> buffer{};
    Gx12DecodedReport last_report{};
    bool last_valid = false;
    int64_t last_elapsed_us = 0;
    uint64_t reports = 0;
    uint64_t decode_errors = 0;
    uint64_t read_errors = 0;
    std::string last_error;

    bool Start();
    void Poll(RecordingCsvWriter* writer, std::chrono::steady_clock::time_point start);
    void Stop();
};

struct RecordingCsvWriter {
    enum class RowKind {
        Sample,
        Hid
    };

    struct QueuedRow {
        RowKind kind = RowKind::Sample;
        int64_t time_us = 0;
        uint32_t frame = 0;
        uint32_t mapper_tick = 0;
        uint64_t input_events = 0;
        uint64_t input_aux = 0;
        uint64_t keyboard_events = 0;
        int64_t right_dx = 0;
        int64_t right_dy = 0;
        int64_t right_wheel_x = 0;
        int64_t right_wheel_y = 0;
        uint32_t right_buttons = 0;
        int64_t left_dx = 0;
        int64_t left_dy = 0;
        int64_t left_wheel_x = 0;
        int64_t left_wheel_y = 0;
        uint32_t left_buttons = 0;
        std::array<int, kSbusChannels> final_channels{};
        uint64_t hid_reports = 0;
        bool hid_valid = false;
        uint32_t hid_buttons = 0;
        std::array<int, kGx12ChannelCount> hid_channels{};
        uint8_t trainer_flags = 0;
        int64_t late_us = 0;
    };

    struct CommitSnapshot {
        std::string path;
        std::string selected_path;
        std::vector<std::string> metadata_lines;
        std::vector<QueuedRow> rows;
        uint64_t sample_rows = 0;
        uint64_t hid_rows = 0;
        bool was_open = false;
    };

    struct CommitResult {
        std::string path;
        std::string selected_path;
        uint64_t sample_rows = 0;
        uint64_t hid_rows = 0;
        bool attempted = false;
        bool ok = true;
        uintmax_t bytes = 0;
        std::string error;
    };

    std::string path;
    uint64_t sample_rows = 0;
    uint64_t hid_rows = 0;
    bool open = false;
    bool last_commit_attempted = false;
    bool last_commit_ok = false;
    uintmax_t last_commit_bytes = 0;
    std::string last_commit_error;
    std::vector<std::string> metadata_lines;
    std::vector<QueuedRow> buffered_rows;

    ~RecordingCsvWriter() {
        Close();
    }

    bool IsOpen() const {
        return open;
    }

    static const char* RowKindName(RowKind kind) {
        return kind == RowKind::Hid ? "hid" : "sample";
    }

    static void WriteQueuedRowToCsv(std::ofstream& csv, const QueuedRow& row) {
        csv << RowKindName(row.kind) << ','
            << row.time_us << ','
            << row.frame << ','
            << row.mapper_tick << ','
            << row.input_events << ','
            << row.input_aux << ','
            << row.keyboard_events << ','
            << row.right_dx << ','
            << row.right_dy << ','
            << row.right_wheel_x << ','
            << row.right_wheel_y << ','
            << row.right_buttons << ','
            << row.left_dx << ','
            << row.left_dy << ','
            << row.left_wheel_x << ','
            << row.left_wheel_y << ','
            << row.left_buttons;
        for (int ch = 0; ch < kSbusChannels; ++ch) {
            csv << ',' << row.final_channels[static_cast<size_t>(ch)];
        }
        csv << ',' << row.hid_reports
            << ',' << (row.hid_valid ? 1 : 0)
            << ',' << row.hid_buttons;
        for (int ch = 0; ch < kGx12ChannelCount; ++ch) {
            csv << ',' << row.hid_channels[static_cast<size_t>(ch)];
        }
        csv << ',' << static_cast<unsigned int>(row.trainer_flags)
            << ',' << row.late_us << '\n';
    }

    static bool SnapshotNeedsCommit(const CommitSnapshot& snapshot) {
        return snapshot.was_open &&
               (!snapshot.rows.empty() || snapshot.metadata_lines.size() > 1);
    }

    static CommitResult CommitSnapshotToDisk(CommitSnapshot snapshot) {
        CommitResult result;
        result.path = snapshot.path;
        result.selected_path = snapshot.selected_path;
        result.sample_rows = snapshot.sample_rows;
        result.hid_rows = snapshot.hid_rows;

        if (!SnapshotNeedsCommit(snapshot)) {
            return result;
        }

        result.attempted = true;
        std::ofstream csv(snapshot.path, std::ios::out | std::ios::trunc);
        if (!csv) {
            result.ok = false;
            result.error = "open failed";
            std::fprintf(stderr, "failed to write recording output: %s\n", snapshot.path.c_str());
            return result;
        }

        for (const std::string& line : snapshot.metadata_lines) {
            csv << line;
        }
        for (const QueuedRow& row : snapshot.rows) {
            WriteQueuedRowToCsv(csv, row);
        }
        csv.flush();
        if (!csv) {
            result.ok = false;
            result.error = "write/flush failed";
            std::fprintf(stderr, "failed to flush recording output: %s\n", snapshot.path.c_str());
        }
        csv.close();
        if (!csv) {
            result.ok = false;
            if (result.error.empty()) {
                result.error = "close failed";
            }
            std::fprintf(stderr, "failed to close recording output: %s\n", snapshot.path.c_str());
        }
        if (result.ok) {
            std::error_code ec;
            result.bytes = std::filesystem::file_size(snapshot.path, ec);
            if (ec) {
                result.bytes = 0;
            }
        }
        return result;
    }

    void ApplyCommitResult(const CommitResult& result) {
        last_commit_attempted = result.attempted;
        last_commit_ok = result.ok;
        last_commit_bytes = result.bytes;
        last_commit_error = result.error;
    }

    CommitSnapshot TakeCommitSnapshot() {
        CommitSnapshot snapshot;
        snapshot.path = path;
        snapshot.metadata_lines = std::move(metadata_lines);
        snapshot.rows = std::move(buffered_rows);
        snapshot.sample_rows = sample_rows;
        snapshot.hid_rows = hid_rows;
        snapshot.was_open = open;

        open = false;
        metadata_lines.clear();
        buffered_rows.clear();
        return snapshot;
    }

    bool BufferRow(QueuedRow&& row) {
        if (!open) {
            return false;
        }
        buffered_rows.push_back(std::move(row));
        return true;
    }

    bool BufferMetadata(std::string line) {
        if (!open) {
            return false;
        }
        metadata_lines.push_back(std::move(line));
        return true;
    }

    void WriteStartState(const RecordingStartState& state) {
        if (!open) {
            return;
        }

        std::ostringstream metadata;
        metadata << std::setprecision(17);
        metadata << "# playback_start_right_mapper_state="
                 << (state.right_mapper_available ? "true" : "false") << "\n";
        if (state.right_mapper_available) {
            metadata << "# playback_start_right_roll_value=" << state.right_roll_value << "\n";
            metadata << "# playback_start_right_pitch_value=" << state.right_pitch_value << "\n";
            metadata << "# playback_start_right_roll_velocity=" << state.right_roll_velocity << "\n";
            metadata << "# playback_start_right_pitch_velocity=" << state.right_pitch_velocity << "\n";
            metadata << "# playback_start_right_roll_pulse=" << state.right_roll_pulse << "\n";
            metadata << "# playback_start_right_pitch_pulse=" << state.right_pitch_pulse << "\n";
        }
        metadata << "# playback_start_mouse_left_state="
                 << (state.mouse_left_available ? "true" : "false") << "\n";
        if (state.mouse_left_available) {
            metadata << "# playback_start_mouse_left_throttle_value=" << state.mouse_left_throttle_value << "\n";
            metadata << "# playback_start_mouse_left_yaw_value=" << state.mouse_left_yaw_value << "\n";
            metadata << "# playback_start_mouse_left_yaw_filtered=" << state.mouse_left_yaw_filtered << "\n";
            metadata << "# playback_start_mouse_left_yaw_position=" << state.mouse_left_yaw_position << "\n";
            metadata << "# playback_start_mouse_left_yaw_dummy_position=" << state.mouse_left_yaw_dummy_position << "\n";
            metadata << "# playback_start_mouse_left_yaw_velocity=" << state.mouse_left_yaw_velocity << "\n";
            metadata << "# playback_start_mouse_left_yaw_dummy_velocity=" << state.mouse_left_yaw_dummy_velocity << "\n";
            metadata << "# playback_start_mouse_left_throttle_pulse=" << state.mouse_left_throttle_pulse << "\n";
            metadata << "# playback_start_mouse_left_yaw_pulse=" << state.mouse_left_yaw_pulse << "\n";
        }
        metadata << "# playback_start_right_mouse_left_state="
                 << (state.right_mouse_left_available ? "true" : "false") << "\n";
        if (state.right_mouse_left_available) {
            metadata << "# playback_start_right_mouse_left_throttle_value=" << state.right_mouse_left_throttle_value << "\n";
            metadata << "# playback_start_right_mouse_left_yaw_target=" << state.right_mouse_left_yaw_target << "\n";
            metadata << "# playback_start_right_mouse_left_yaw_output=" << state.right_mouse_left_yaw_output << "\n";
            metadata << "# playback_start_right_mouse_left_throttle_pulse=" << state.right_mouse_left_throttle_pulse << "\n";
            metadata << "# playback_start_right_mouse_left_yaw_pulse=" << state.right_mouse_left_yaw_pulse << "\n";
        }
        BufferMetadata(metadata.str());
    }

    bool Open(const TrainerProfile& profile,
              const char* recording_path,
              int mapper_rate_hz,
              const Gx12RecordingHidCapture& hid_capture) {
        if (!recording_path || recording_path[0] == '\0') {
            std::fprintf(stderr, "recording path must not be empty.\n");
            return false;
        }

        Close();
        sample_rows = 0;
        hid_rows = 0;
        last_commit_attempted = false;
        last_commit_ok = false;
        last_commit_bytes = 0;
        last_commit_error.clear();
        metadata_lines.clear();
        buffered_rows.clear();
        path = recording_path;
        std::error_code ec;
        const std::filesystem::path output_path(path);
        if (output_path.has_parent_path()) {
            std::filesystem::create_directories(output_path.parent_path(), ec);
            if (ec) {
                std::fprintf(stderr,
                             "failed to create recording directory '%s': %s\n",
                             output_path.parent_path().string().c_str(),
                             ec.message().c_str());
                return false;
            }
        }

        std::ostringstream header;
        header << "# " << kRecordingMagic << "=" << kRecordingSchemaVersion << "\n";
        header << "# app_version=" << GX12_APP_VERSION << "\n";
        header << "# created_unix_ms=" << UnixTimeMillisecondsNow() << "\n";
        header << "# profile_name=" << MetadataValue(profile.name) << "\n";
        header << "# profile_path=" << MetadataValue(profile.source_file) << "\n";
        header << "# profile_fnv1a64=" << Fnv1aFileHashHex(profile.source_file) << "\n";
        header << "# trainer_frame_rate_hz=" << profile.frame_rate_hz << "\n";
        header << "# mapper_rate_hz=" << mapper_rate_hz << "\n";
        header << "# trainer_resolution_mode=" << TrainerResolutionModeName(profile.resolution_mode) << "\n";
        header << "# right_mouse_device_token=" << MetadataValue(profile.mouse_right_device_token) << "\n";
        header << "# left_mouse_device_token=" << MetadataValue(profile.mouse_left_device_token) << "\n";
        header << "# recording_buffer=memory\n";
        header << "# hid_available=" << (hid_capture.available ? "true" : "false") << "\n";
        if (hid_capture.available) {
            header << "# hid_index=" << hid_capture.index << "\n";
            header << "# hid_vid=0x" << std::uppercase << std::hex << std::setw(4) << std::setfill('0')
                << hid_capture.vendor_id << "\n";
            header << "# hid_pid=0x" << std::uppercase << std::hex << std::setw(4) << std::setfill('0')
                << hid_capture.product_id << std::dec << std::setfill(' ') << "\n";
            header << "# hid_usage=0x" << std::uppercase << std::hex << std::setw(4) << std::setfill('0')
                << hid_capture.usage_page << ":0x" << std::setw(4) << hid_capture.usage
                << std::dec << std::setfill(' ') << "\n";
            header << "# hid_interface=" << hid_capture.interface_number << "\n";
            header << "# hid_product=" << MetadataValue(hid_capture.product) << "\n";
        }
        header << "# playback_note=recordings store all available tracks; playback channel masks decide what is automated\n";

        header << "row_type,time_us,frame,mapper_tick,input_events,input_aux,keyboard_events,"
               "right_dx,right_dy,right_wheel_x,right_wheel_y,right_buttons,"
               "left_dx,left_dy,left_wheel_x,left_wheel_y,left_buttons";
        for (int ch = 0; ch < kSbusChannels; ++ch) {
            header << ",final_ch" << (ch + 1);
        }
        header << ",hid_reports,hid_valid,hid_buttons";
        for (int ch = 0; ch < kGx12ChannelCount; ++ch) {
            header << ",hid_ch" << (ch + 1);
        }
        header << ",trainer_flags,late_us\n";
        metadata_lines.push_back(header.str());
        buffered_rows.reserve(static_cast<size_t>(
            std::max(1024, profile.frame_rate_hz * std::max(1, profile.seconds > 0 ? profile.seconds : 30))));
        open = true;
        return true;
    }

    CommitSnapshot CloseToSnapshot() {
        last_commit_attempted = false;
        last_commit_ok = false;
        last_commit_bytes = 0;
        last_commit_error.clear();
        return TakeCommitSnapshot();
    }

    bool Close() {
        CommitResult result = CommitSnapshotToDisk(CloseToSnapshot());
        ApplyCommitResult(result);
        return result.ok;
    }

    void WriteRow(RowKind row_type,
                  int64_t time_us,
                  uint32_t frame,
                  uint32_t mapper_tick,
                  uint64_t input_events,
                  uint64_t input_aux,
                  uint64_t keyboard_events,
                  int64_t right_dx,
                  int64_t right_dy,
                  int64_t right_wheel_x,
                  int64_t right_wheel_y,
                  uint32_t right_buttons,
                  int64_t left_dx,
                  int64_t left_dy,
                  int64_t left_wheel_x,
                  int64_t left_wheel_y,
                  uint32_t left_buttons,
                  const std::array<int, kSbusChannels>& final_channels,
                  const Gx12DecodedReport* hid_report,
                  bool hid_valid,
                  uint64_t hid_reports,
                  uint8_t trainer_flags,
                  int64_t late_us) {
        if (!open) {
            return;
        }

        QueuedRow row;
        row.kind = row_type;
        row.time_us = time_us;
        row.frame = frame;
        row.mapper_tick = mapper_tick;
        row.input_events = input_events;
        row.input_aux = input_aux;
        row.keyboard_events = keyboard_events;
        row.right_dx = right_dx;
        row.right_dy = right_dy;
        row.right_wheel_x = right_wheel_x;
        row.right_wheel_y = right_wheel_y;
        row.right_buttons = right_buttons;
        row.left_dx = left_dx;
        row.left_dy = left_dy;
        row.left_wheel_x = left_wheel_x;
        row.left_wheel_y = left_wheel_y;
        row.left_buttons = left_buttons;
        row.final_channels = final_channels;
        row.hid_reports = hid_reports;
        row.hid_valid = hid_valid;
        row.hid_buttons = hid_valid && hid_report ? hid_report->buttons : 0;
        if (hid_valid && hid_report) {
            for (int ch = 0; ch < kGx12ChannelCount; ++ch) {
                row.hid_channels[static_cast<size_t>(ch)] = hid_report->channels[ch];
            }
        }
        row.trainer_flags = trainer_flags;
        row.late_us = late_us;

        if (!BufferRow(std::move(row))) {
            return;
        }
        if (row_type == RowKind::Sample) {
            ++sample_rows;
        } else {
            ++hid_rows;
        }
    }

    void WriteSample(int64_t time_us,
                     uint32_t frame,
                     uint32_t mapper_tick,
                     uint64_t input_events,
                     uint64_t input_aux,
                     uint64_t keyboard_events,
                     int64_t right_dx,
                     int64_t right_dy,
                     int64_t right_wheel_x,
                     int64_t right_wheel_y,
                     uint32_t right_buttons,
                     int64_t left_dx,
                     int64_t left_dy,
                     int64_t left_wheel_x,
                     int64_t left_wheel_y,
                     uint32_t left_buttons,
                     const std::array<int, kSbusChannels>& final_channels,
                     const Gx12RecordingHidCapture& hid_capture,
                     uint8_t trainer_flags,
                     int64_t late_us) {
        WriteRow(RowKind::Sample,
                 time_us,
                 frame,
                 mapper_tick,
                 input_events,
                 input_aux,
                 keyboard_events,
                 right_dx,
                 right_dy,
                 right_wheel_x,
                 right_wheel_y,
                 right_buttons,
                 left_dx,
                 left_dy,
                 left_wheel_x,
                 left_wheel_y,
                 left_buttons,
                 final_channels,
                 hid_capture.last_valid ? &hid_capture.last_report : nullptr,
                 hid_capture.last_valid,
                 hid_capture.reports,
                 trainer_flags,
                 late_us);
    }

    void WriteHidRow(int64_t time_us, uint64_t report_index, const Gx12DecodedReport& decoded) {
        std::array<int, kSbusChannels> final_channels{};
        WriteRow(RowKind::Hid,
                 time_us,
                 0,
                 0,
                 0,
                 0,
                 0,
                 0,
                 0,
                 0,
                 0,
                 0,
                 0,
                 0,
                 0,
                 0,
                 0,
                 final_channels,
                 &decoded,
                 true,
                 report_index,
                 0,
                 0);
    }
};

struct RecordingCommitQueue {
    std::mutex mutex;
    std::condition_variable cv;
    std::deque<RecordingCsvWriter::CommitSnapshot> pending;
    std::deque<RecordingCsvWriter::CommitResult> completed;
    std::thread worker;
    bool stop_requested = false;

    ~RecordingCommitQueue() {
        Stop();
    }

    void Start() {
        if (worker.joinable()) {
            return;
        }
        {
            std::lock_guard<std::mutex> lock(mutex);
            stop_requested = false;
        }
        worker = std::thread([this]() {
            WorkerLoop();
        });
    }

    void Enqueue(RecordingCsvWriter::CommitSnapshot snapshot) {
        if (!snapshot.was_open) {
            return;
        }
        Start();
        {
            std::lock_guard<std::mutex> lock(mutex);
            pending.push_back(std::move(snapshot));
        }
        cv.notify_one();
    }

    std::vector<RecordingCsvWriter::CommitResult> DrainCompleted() {
        std::vector<RecordingCsvWriter::CommitResult> results;
        std::lock_guard<std::mutex> lock(mutex);
        while (!completed.empty()) {
            results.push_back(std::move(completed.front()));
            completed.pop_front();
        }
        return results;
    }

    void Stop() {
        if (!worker.joinable()) {
            return;
        }
        {
            std::lock_guard<std::mutex> lock(mutex);
            stop_requested = true;
        }
        cv.notify_one();
        worker.join();
    }

    void WorkerLoop() {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);

        for (;;) {
            RecordingCsvWriter::CommitSnapshot snapshot;
            {
                std::unique_lock<std::mutex> lock(mutex);
                cv.wait(lock, [&]() {
                    return stop_requested || !pending.empty();
                });
                if (pending.empty() && stop_requested) {
                    break;
                }
                snapshot = std::move(pending.front());
                pending.pop_front();
            }

            RecordingCsvWriter::CommitResult result =
                RecordingCsvWriter::CommitSnapshotToDisk(std::move(snapshot));
            {
                std::lock_guard<std::mutex> lock(mutex);
                completed.push_back(std::move(result));
            }
        }
    }
};

bool Gx12RecordingHidCapture::Start() {
    if (hid_init() != 0) {
        last_error = HidErrorUtf8(nullptr);
        return false;
    }
    hid_initialized = true;

    const std::vector<HidDeviceEntry> devices = EnumerateHidDevices();
    const HidDeviceEntry* selected = FindGx12Hid(devices);
    if (!selected) {
        last_error = "GX12 joystick HID not found";
        Stop();
        return false;
    }

    device = hid_open_path(selected->path.c_str());
    if (!device) {
        last_error = HidErrorUtf8(nullptr);
        Stop();
        return false;
    }
    if (hid_set_nonblocking(device, 1) != 0) {
        last_error = HidErrorUtf8(device);
        Stop();
        return false;
    }

    available = true;
    index = selected->index;
    vendor_id = selected->vendor_id;
    product_id = selected->product_id;
    usage_page = selected->usage_page;
    usage = selected->usage;
    interface_number = selected->interface_number;
    product = selected->product;
    return true;
}

void Gx12RecordingHidCapture::Poll(RecordingCsvWriter* writer,
                                   std::chrono::steady_clock::time_point start) {
    if (!available || !device || failed) {
        return;
    }

    for (int drained = 0; drained < 16; ++drained) {
        const int read = hid_read_timeout(device, buffer.data(), buffer.size(), 0);
        const auto now = std::chrono::steady_clock::now();
        if (read > 0) {
            Gx12DecodedReport decoded{};
            if (!DecodeGx12Report(buffer.data(), read, &decoded)) {
                ++decode_errors;
                continue;
            }
            ++reports;
            last_report = decoded;
            last_valid = true;
            last_elapsed_us =
                std::chrono::duration_cast<std::chrono::microseconds>(now - start).count();
            if (writer) {
                writer->WriteHidRow(last_elapsed_us, reports, decoded);
            }
            continue;
        }
        if (read == 0) {
            break;
        }

        ++read_errors;
        last_error = HidReadErrorUtf8(device);
        failed = true;
        break;
    }
}

void Gx12RecordingHidCapture::Stop() {
    if (device) {
        hid_close(device);
        device = nullptr;
    }
    if (hid_initialized) {
        hid_exit();
        hid_initialized = false;
    }
    available = false;
}

struct RecordingMetadata {
    int schema = 0;
    std::string app_version;
    std::string profile_name;
    std::string profile_path;
    std::string profile_hash;
    std::string recording_buffer = "unknown";
    int trainer_frame_rate_hz = 0;
    int mapper_rate_hz = 0;
    TrainerResolutionMode resolution_mode = TrainerResolutionMode::Legacy;
    bool hid_available_header = false;
    RecordingStartState start_state;
};

struct RecordingSample {
    int64_t time_us = 0;
    uint32_t frame = 0;
    uint32_t mapper_tick = 0;
    int64_t right_dx = 0;
    int64_t right_dy = 0;
    int64_t right_wheel_x = 0;
    int64_t right_wheel_y = 0;
    uint32_t right_buttons = 0;
    int64_t left_dx = 0;
    int64_t left_dy = 0;
    int64_t left_wheel_x = 0;
    int64_t left_wheel_y = 0;
    uint32_t left_buttons = 0;
    std::array<int, kSbusChannels> final_channels{};
    std::array<int, kGx12ChannelCount> hid_channels{};
    bool hid_valid = false;
    uint64_t hid_reports = 0;
    uint32_t hid_buttons = 0;
    uint8_t trainer_flags = 0;
    int64_t late_us = 0;
};

struct LoadedRecording {
    RecordingMetadata metadata;
    std::vector<RecordingSample> samples;
    uint64_t hid_rows = 0;
    bool has_hid_samples = false;
};

struct PlaybackChannelMask {
    std::array<bool, kSbusChannels> enabled{};
    std::array<bool, kSbusChannels> use_hid{};
    std::array<bool, kSbusChannels> use_pc_input{};

    bool Any() const {
        return std::any_of(enabled.begin(), enabled.end(), [](bool value) { return value; });
    }

    bool UsesRightStick() const {
        return enabled[0] || enabled[1];
    }

    bool UsesLeftStick() const {
        return enabled[2] || enabled[3];
    }

    bool UsesThrottleOrYaw() const {
        return enabled[2] || enabled[3];
    }

    bool UsesHidRightStick() const {
        return (enabled[0] && use_hid[0]) || (enabled[1] && use_hid[1]);
    }
};

struct PlaybackTrigger {
    int virtual_key = 0;
    std::string label = "immediate";

    bool Immediate() const {
        return virtual_key == 0;
    }
};

constexpr size_t kMaxPlaybackBankSlots = 12;

struct PlaybackBankSlotSpec {
    std::string recording_path;
    PlaybackChannelMask mask;
    PlaybackTrigger trigger;
    bool block_live_input = false;
};

struct PlaybackBankSlot {
    PlaybackBankSlotSpec spec;
    LoadedRecording recording;
    std::vector<size_t> mapper_tick_sample_indices;
    bool loaded = false;
};

struct PlaybackInputInjection {
    int64_t right_dx = 0;
    int64_t right_dy = 0;
    int64_t right_wheel_x = 0;
    int64_t right_wheel_y = 0;
    uint32_t right_buttons = 0;
    bool right_buttons_valid = false;
    int64_t left_dx = 0;
    int64_t left_dy = 0;
    int64_t left_wheel_x = 0;
    int64_t left_wheel_y = 0;
    uint32_t left_buttons = 0;
    bool left_buttons_valid = false;
    uint32_t mapper_ticks = 0;

    bool Any() const {
        return mapper_ticks > 0 ||
               right_dx != 0 ||
               right_dy != 0 ||
               right_wheel_x != 0 ||
               right_wheel_y != 0 ||
               right_buttons_valid ||
               left_dx != 0 ||
               left_dy != 0 ||
               left_wheel_x != 0 ||
               left_wheel_y != 0 ||
               left_buttons_valid;
    }
};

struct RightStickPlaybackState {
    double roll_value = 0.0;
    double pitch_value = 0.0;
    ElasticAxisState roll_elastic_state;
    ElasticAxisState pitch_elastic_state;
    RightStickSharedState shared_state;
    int roll_pulse = 0;
    int pitch_pulse = 0;
    uint32_t idle_ticks_without_motion = 0;
};

RightStickPlaybackState MakeRightStickPlaybackStateFromRecording(
    const RecordingStartState& start_state) {
    RightStickPlaybackState state;
    if (!start_state.right_mapper_available) {
        return state;
    }
    state.roll_value = start_state.right_roll_value;
    state.pitch_value = start_state.right_pitch_value;
    state.roll_elastic_state.velocity = start_state.right_roll_velocity;
    state.pitch_elastic_state.velocity = start_state.right_pitch_velocity;
    state.roll_pulse = start_state.right_roll_pulse;
    state.pitch_pulse = start_state.right_pitch_pulse;
    return state;
}

RecordingStartState MakeRecordingStartStateFromRightStick(
    double roll_value,
    double pitch_value,
    const ElasticAxisState& roll_elastic_state,
    const ElasticAxisState& pitch_elastic_state,
    int roll_pulse,
    int pitch_pulse) {
    RecordingStartState state;
    state.right_mapper_available = true;
    state.right_roll_value = roll_value;
    state.right_pitch_value = pitch_value;
    state.right_roll_velocity = roll_elastic_state.velocity;
    state.right_pitch_velocity = pitch_elastic_state.velocity;
    state.right_roll_pulse = roll_pulse;
    state.right_pitch_pulse = pitch_pulse;
    return state;
}

void StepRightStickPlaybackState(const TrainerProfile& profile,
                                 int64_t dx,
                                 int64_t dy,
                                 int mapper_rate_hz,
                                 RightStickPlaybackState* state) {
    if (!state || mapper_rate_hz <= 0) {
        return;
    }

    const double mapper_dt = 1.0 / static_cast<double>(mapper_rate_hz);
    const double gain_scale = TrainerRateGainScale(mapper_rate_hz);
    const double roll_source = profile.swap_axes
        ? static_cast<double>(-dy)
        : static_cast<double>(dx);
    const double pitch_source = profile.swap_axes
        ? static_cast<double>(dx)
        : static_cast<double>(-dy);
    const bool use_position_mapper = RightStickNeedsPositionMapper(profile);
    if (use_position_mapper) {
        const bool mouse_moved = dx != 0 || dy != 0;
        if (mouse_moved) {
            state->idle_ticks_without_motion = 0;
        } else if (state->idle_ticks_without_motion < std::numeric_limits<uint32_t>::max()) {
            ++state->idle_ticks_without_motion;
        }
        const double idle_ms =
            static_cast<double>(state->idle_ticks_without_motion) *
            1000.0 /
            static_cast<double>(mapper_rate_hz);
        const bool apply_idle_return =
            profile.return_enabled &&
            profile.return_rate > 0.0 &&
            !mouse_moved &&
            (profile.return_idle_ms <= 0.0 || idle_ms >= profile.return_idle_ms);
        const double combined_return_step =
            (apply_idle_return
                 ? profile.return_rate / static_cast<double>(mapper_rate_hz)
                 : 0.0) +
            (profile.constant_return_enabled
                 ? profile.constant_return_rate / static_cast<double>(mapper_rate_hz)
                 : 0.0);
        const double elastic_return_coefficient =
            profile.elastic_return_enabled
                ? profile.elastic_return_coefficient
                : 0.0;
        ShapeRightStickPositionPulses(roll_source,
                                      pitch_source,
                                      gain_scale,
                                      combined_return_step,
                                      elastic_return_coefficient,
                                      mapper_dt,
                                      combined_return_step > 0.0 ||
                                          elastic_return_coefficient > 0.0,
                                      &state->roll_value,
                                      &state->pitch_value,
                                      &state->roll_elastic_state,
                                      &state->pitch_elastic_state,
                                      &state->shared_state,
                                      profile,
                                      &state->roll_pulse,
                                      &state->pitch_pulse);
    } else {
        double filtered_roll_source = roll_source;
        double filtered_pitch_source = pitch_source;
        ApplyRightStickInputPreprocessors(&filtered_roll_source,
                                          &filtered_pitch_source,
                                          mapper_dt,
                                          profile,
                                          &state->shared_state);
        const double input_gain = UpdateAdaptiveInputGain(filtered_roll_source,
                                                          filtered_pitch_source,
                                                          mapper_dt,
                                                          profile,
                                                          &state->shared_state);
        state->roll_pulse = ShapeTrainerPulse(filtered_roll_source,
                                              profile.roll_gain * gain_scale * input_gain,
                                              profile.invert_roll,
                                              &state->roll_value,
                                              profile);
        state->pitch_pulse = ShapeTrainerPulse(filtered_pitch_source,
                                               profile.pitch_gain * gain_scale * input_gain,
                                               profile.invert_pitch,
                                               &state->pitch_value,
                                               profile);
    }
}

struct LeftStickPlaybackState {
    double throttle_value = 0.0;
    double mouse_left_yaw_value = 0.0;
    double mouse_left_yaw_filtered = 0.0;
    double mouse_left_yaw_position = 0.0;
    double mouse_left_yaw_dummy_position = 0.0;
    ElasticAxisState mouse_left_yaw_elastic_state;
    ElasticAxisState mouse_left_yaw_dummy_state;
    RightStickSharedState mouse_left_yaw_mapper_state;
    uint32_t mouse_left_idle_ticks_without_motion = 0;
    double right_mouse_left_yaw_target = 0.0;
    double right_mouse_left_yaw_output = 0.0;
    int throttle_pulse = 0;
    int yaw_pulse = 0;
};

LeftStickPlaybackState MakeLeftStickPlaybackStateFromRecording(
    const TrainerProfile& profile,
    const RecordingStartState& start_state) {
    LeftStickPlaybackState state;
    state.throttle_value = static_cast<double>(TrainerProfileLowValue());
    state.throttle_pulse = QuantizeTrainerProfileOutput(
        state.throttle_value,
        profile.resolution_mode);
    state.yaw_pulse = 0;

    if (profile.mouse_left.enabled && start_state.mouse_left_available) {
        state.throttle_value = start_state.mouse_left_throttle_value;
        state.mouse_left_yaw_value = start_state.mouse_left_yaw_value;
        state.mouse_left_yaw_filtered = start_state.mouse_left_yaw_filtered;
        state.mouse_left_yaw_position = start_state.mouse_left_yaw_position;
        state.mouse_left_yaw_dummy_position = start_state.mouse_left_yaw_dummy_position;
        state.mouse_left_yaw_elastic_state.velocity =
            start_state.mouse_left_yaw_velocity;
        state.mouse_left_yaw_dummy_state.velocity =
            start_state.mouse_left_yaw_dummy_velocity;
        state.throttle_pulse = start_state.mouse_left_throttle_pulse;
        state.yaw_pulse = start_state.mouse_left_yaw_pulse;
    } else if (profile.right_mouse_left.enabled &&
               start_state.right_mouse_left_available) {
        state.throttle_value = start_state.right_mouse_left_throttle_value;
        state.right_mouse_left_yaw_target =
            start_state.right_mouse_left_yaw_target;
        state.right_mouse_left_yaw_output =
            start_state.right_mouse_left_yaw_output;
        state.throttle_pulse = start_state.right_mouse_left_throttle_pulse;
        state.yaw_pulse = start_state.right_mouse_left_yaw_pulse;
    }
    return state;
}

void StepLeftStickPlaybackState(const TrainerProfile& profile,
                                const PlaybackInputInjection& injection,
                                int mapper_rate_hz,
                                LeftStickPlaybackState* state) {
    if (!state || mapper_rate_hz <= 0) {
        return;
    }

    const double mapper_dt = 1.0 / static_cast<double>(mapper_rate_hz);
    const double gain_scale = TrainerRateGainScale(mapper_rate_hz);

    if (profile.mouse_left.enabled) {
        const double throttle_source = profile.mouse_left.swap_axes
            ? static_cast<double>(injection.left_dx)
            : static_cast<double>(-injection.left_dy);
        double throttle_axis = throttle_source;
        if (profile.mouse_left.invert_throttle) {
            throttle_axis = -throttle_axis;
        }
        state->throttle_value = ClampDouble(
            state->throttle_value + throttle_axis * profile.mouse_left.throttle_rate,
            static_cast<double>(TrainerProfileLowValue()),
            static_cast<double>(kTrainerProfileDomainLimit));
        if (profile.mouse_left.throttle_return_enabled &&
            std::abs(throttle_axis) < 0.001) {
            state->throttle_value = MoveTowardDouble(
                state->throttle_value,
                static_cast<double>(TrainerProfileLowValue()),
                profile.mouse_left.throttle_return_rate * mapper_dt);
        }
        state->throttle_pulse = QuantizeTrainerProfileOutput(
            state->throttle_value,
            profile.resolution_mode);

        const double raw_yaw_source = profile.mouse_left.swap_axes
            ? static_cast<double>(injection.left_dy)
            : static_cast<double>(injection.left_dx);
        double yaw_source = raw_yaw_source;
        if (!profile.mouse_left.yaw_shaping_enabled &&
            profile.mouse_left.invert_yaw) {
            yaw_source = -yaw_source;
        }
        const bool left_yaw_moved = std::abs(raw_yaw_source) > 0.001;
        if (left_yaw_moved) {
            state->mouse_left_idle_ticks_without_motion = 0;
        } else if (state->mouse_left_idle_ticks_without_motion <
                   std::numeric_limits<uint32_t>::max()) {
            ++state->mouse_left_idle_ticks_without_motion;
        }
        const double idle_ms =
            static_cast<double>(state->mouse_left_idle_ticks_without_motion) *
            1000.0 /
            static_cast<double>(mapper_rate_hz);

        if (profile.mouse_left.yaw_shaping_enabled) {
            const bool apply_left_yaw_idle_return =
                profile.mouse_left.yaw_return_enabled &&
                profile.mouse_left.yaw_return_rate > 0.0 &&
                !left_yaw_moved &&
                (profile.mouse_left.yaw_return_idle_ms <= 0.0 ||
                 idle_ms >= profile.mouse_left.yaw_return_idle_ms);
            const double combined_return_step =
                (apply_left_yaw_idle_return
                     ? profile.mouse_left.yaw_return_rate /
                           static_cast<double>(mapper_rate_hz)
                     : 0.0) +
                (profile.mouse_left.yaw_constant_return_enabled
                     ? profile.mouse_left.yaw_constant_return_rate /
                           static_cast<double>(mapper_rate_hz)
                     : 0.0);
            const double elastic_return_coefficient =
                profile.mouse_left.yaw_elastic_return_enabled
                    ? profile.mouse_left.yaw_elastic_return_coefficient
                    : 0.0;
            state->yaw_pulse = ShapeMouseLeftYawWithMapper(
                raw_yaw_source,
                gain_scale,
                combined_return_step,
                elastic_return_coefficient,
                mapper_dt,
                combined_return_step > 0.0 ||
                    elastic_return_coefficient > 0.0,
                &state->mouse_left_yaw_position,
                &state->mouse_left_yaw_dummy_position,
                &state->mouse_left_yaw_elastic_state,
                &state->mouse_left_yaw_dummy_state,
                &state->mouse_left_yaw_mapper_state,
                profile,
                &state->mouse_left_yaw_filtered);
            state->mouse_left_yaw_value = state->mouse_left_yaw_position;
        } else {
            state->mouse_left_yaw_value = ClampDouble(
                state->mouse_left_yaw_value +
                    yaw_source * profile.mouse_left.yaw_gain,
                -static_cast<double>(profile.mouse_left.yaw_pulse),
                static_cast<double>(profile.mouse_left.yaw_pulse));
            if (std::abs(state->mouse_left_yaw_value) <=
                static_cast<double>(profile.mouse_left.yaw_deadband)) {
                state->mouse_left_yaw_value = 0.0;
            }
            const bool apply_left_yaw_idle_return =
                profile.mouse_left.yaw_return_enabled &&
                profile.mouse_left.yaw_return_rate > 0.0 &&
                !left_yaw_moved &&
                (profile.mouse_left.yaw_return_idle_ms <= 0.0 ||
                 idle_ms >= profile.mouse_left.yaw_return_idle_ms);
            double left_yaw_return_step =
                (apply_left_yaw_idle_return
                     ? profile.mouse_left.yaw_return_rate * mapper_dt
                     : 0.0) +
                (profile.mouse_left.yaw_constant_return_enabled
                     ? profile.mouse_left.yaw_constant_return_rate * mapper_dt
                     : 0.0);
            if (profile.mouse_left.yaw_elastic_return_enabled) {
                left_yaw_return_step +=
                    ElasticReturnRatePerSecond(
                        std::abs(state->mouse_left_yaw_value),
                        profile.mouse_left.yaw_pulse,
                        profile.mouse_left.yaw_elastic_return_coefficient,
                        profile.mouse_left.yaw_elastic_return_mode,
                        profile.mouse_left.yaw_elastic_return_curve) *
                    mapper_dt;
            }
            if (left_yaw_return_step > 0.0) {
                state->mouse_left_yaw_value = MoveTowardDouble(
                    state->mouse_left_yaw_value,
                    0.0,
                    left_yaw_return_step);
            }
            double yaw_target = state->mouse_left_yaw_value;
            if (profile.mouse_left.yaw_smoothing > 0.0) {
                yaw_target =
                    (profile.mouse_left.yaw_smoothing *
                     state->mouse_left_yaw_filtered) +
                    ((1.0 - profile.mouse_left.yaw_smoothing) * yaw_target);
            }
            const double yaw_output = profile.mouse_left.yaw_slew_rate <= 0.0
                ? yaw_target
                : MoveTowardDouble(state->mouse_left_yaw_filtered,
                                   yaw_target,
                                   profile.mouse_left.yaw_slew_rate * mapper_dt);
            state->mouse_left_yaw_filtered = yaw_output;
            state->yaw_pulse = QuantizeTrainerProfileOutput(
                yaw_output,
                profile.resolution_mode);
        }
        return;
    }

    if (profile.right_mouse_left.enabled) {
        const auto& source = profile.right_mouse_left;
#if defined(GX12_HAS_GAMEINPUT)
        const double wheel_axis_raw =
            NormalizeGameInputWheelDelta(injection.right_wheel_y);
        const uint32_t buttons =
            injection.right_buttons_valid ? injection.right_buttons : 0;
        const double button_axis_raw =
            static_cast<double>(GameInputMouse4Mouse5Axis(buttons));
#else
        const double wheel_axis_raw = 0.0;
        const double button_axis_raw = 0.0;
#endif

        if (source.swap_axes) {
            double throttle_axis = button_axis_raw;
            if (source.invert_throttle) {
                throttle_axis = -throttle_axis;
            }
            state->throttle_value = ClampDouble(
                state->throttle_value +
                    throttle_axis * source.throttle_button_rate * mapper_dt,
                static_cast<double>(TrainerProfileLowValue()),
                static_cast<double>(kTrainerProfileDomainLimit));
            if (source.throttle_return_enabled &&
                std::abs(throttle_axis) < 0.001) {
                state->throttle_value = MoveTowardDouble(
                    state->throttle_value,
                    static_cast<double>(TrainerProfileLowValue()),
                    source.throttle_return_rate * mapper_dt);
            }

            double yaw_wheel_axis = wheel_axis_raw;
            if (source.invert_yaw) {
                yaw_wheel_axis = -yaw_wheel_axis;
            }
            if (std::abs(yaw_wheel_axis) > 0.001) {
                state->right_mouse_left_yaw_target =
                    ClampDouble(state->right_mouse_left_yaw_target +
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
                state->throttle_value =
                    ClampDouble(state->throttle_value +
                                    throttle_wheel_axis * source.throttle_step,
                                static_cast<double>(TrainerProfileLowValue()),
                                static_cast<double>(kTrainerProfileDomainLimit));
            } else if (source.throttle_return_enabled) {
                state->throttle_value = MoveTowardDouble(
                    state->throttle_value,
                    static_cast<double>(TrainerProfileLowValue()),
                    source.throttle_return_rate * mapper_dt);
            }

            double yaw_button_axis = button_axis_raw;
            if (source.invert_yaw) {
                yaw_button_axis = -yaw_button_axis;
            }
            state->right_mouse_left_yaw_target =
                yaw_button_axis * static_cast<double>(source.yaw_pulse);
        }

        state->throttle_pulse = QuantizeTrainerProfileOutput(
            state->throttle_value,
            profile.resolution_mode);
        state->right_mouse_left_yaw_output = source.yaw_slew_rate <= 0.0
            ? state->right_mouse_left_yaw_target
            : MoveTowardDouble(state->right_mouse_left_yaw_output,
                               state->right_mouse_left_yaw_target,
                               source.yaw_slew_rate * mapper_dt);
        state->yaw_pulse = QuantizeTrainerProfileOutput(
            state->right_mouse_left_yaw_output,
            profile.resolution_mode);
    }
}

bool LoadRecordingFile(const char* path, LoadedRecording* recording, std::string* error);
bool ValidatePlaybackRecordingForCurrentBuild(const LoadedRecording& recording,
                                              const char* recording_path);
int ParseTriggerVirtualKeyName(const std::string& text);
bool ParsePlaybackTrigger(const std::string& text, PlaybackTrigger* trigger);
bool ParsePlaybackChannelMask(const std::string& text, PlaybackChannelMask* mask);
bool PlaybackTriggerPressed(const PlaybackTrigger& trigger);
std::vector<size_t> BuildPlaybackMapperTickSampleIndices(const LoadedRecording& recording);
bool PlaybackMaskUsesOnlyRecordedOverlay(const PlaybackChannelMask& mask);
int PlaybackFramesPerRecordedMapperTick(const TrainerProfile& profile,
                                        const LoadedRecording& recording);
uint8_t PlaybackActiveFlags(const PlaybackChannelMask& mask, TrainerResolutionMode mode);
bool ResolveIntegratedPlaybackMaskForProfile(PlaybackChannelMask* mask,
                                             const TrainerProfile& profile,
                                             const LoadedRecording& recording,
                                             const std::string& recording_path,
                                             bool verbose);
std::array<int, kSbusChannels> BuildPlaybackPulses(const RecordingSample& sample,
                                                   const PlaybackChannelMask& mask,
                                                   TrainerResolutionMode mode);
std::string DescribePlaybackChannelMask(const PlaybackChannelMask& mask);
bool PlaybackMaskAllEnabledChannelsUseInputInjection(const PlaybackChannelMask& mask,
                                                     const TrainerProfile& profile);
PlaybackInputInjection ConsumePlaybackInputInjection(const PlaybackBankSlot& slot,
                                                     const TrainerProfile& profile,
                                                     int64_t playback_elapsed_us,
                                                     int64_t playback_base_us,
                                                     size_t* sample_index,
                                                     bool* have_mapper_tick,
                                                     uint32_t* last_mapper_tick);
void ApplyPlaybackInputInjection(const PlaybackInputInjection& injection,
                                 int64_t* right_dx,
                                 int64_t* right_dy,
                                 int64_t* right_wheel_x,
                                 int64_t* right_wheel_y,
                                 uint32_t* right_buttons,
                                 int64_t* left_dx,
                                 int64_t* left_dy,
                                 int64_t* left_wheel_x,
                                 int64_t* left_wheel_y,
                                 uint32_t* left_buttons);
void ClearPlaybackLiveInputForMask(const PlaybackBankSlotSpec& spec,
                                   const TrainerProfile& profile,
                                   int64_t* right_dx,
                                   int64_t* right_dy,
                                   int64_t* right_wheel_y,
                                   uint32_t* right_buttons,
                                   int64_t* left_dx,
                                   int64_t* left_dy);
bool PlaybackChannelUsesInputInjection(const PlaybackChannelMask& mask,
                                       const TrainerProfile& profile,
                                       int channel_index);

struct TrainerRecordingOptions {
    std::string path;
    bool start_immediately = false;
    bool overwrite_existing = false;
    int toggle_key_vk = 0;
    std::string toggle_key_label;
    int max_seconds = 0;

    bool Enabled() const {
        return !path.empty();
    }

    bool ToggleMode() const {
        return Enabled() && toggle_key_vk > 0;
    }
};

struct TrainerRuntimeControlOptions {
    std::string path;

    bool Enabled() const {
        return !path.empty();
    }
};

struct TrainerPlaybackBankOptions {
    bool loop = false;
    std::vector<PlaybackBankSlotSpec> specs;

    bool Enabled() const {
        return !specs.empty();
    }
};

struct TrainerRuntimeControlSnapshot {
    TrainerRecordingOptions recording;
    TrainerPlaybackBankOptions playback_bank;
};

bool RecordingToggleKeyDown(const TrainerRecordingOptions& options) {
    if (options.toggle_key_vk <= 0) {
        return false;
    }
    return AnyExpandedKeyDown(options.toggle_key_vk, [](int vk) {
        return vk > 0 && (GetAsyncKeyState(vk) & 0x8000) != 0;
    });
}

std::vector<std::string> SplitTabLine(const std::string& line) {
    std::vector<std::string> fields;
    size_t start = 0;
    while (start <= line.size()) {
        const size_t tab = line.find('\t', start);
        if (tab == std::string::npos) {
            fields.push_back(line.substr(start));
            break;
        }
        fields.push_back(line.substr(start, tab - start));
        start = tab + 1;
    }
    return fields;
}

bool ParseRuntimeControlBool(const std::string& text, bool* value) {
    if (!value) {
        return false;
    }
    const std::string lowered = ToLowerAscii(TrimAscii(text));
    if (lowered == "1" || lowered == "true" || lowered == "yes" || lowered == "on") {
        *value = true;
        return true;
    }
    if (lowered.empty() || lowered == "0" || lowered == "false" || lowered == "no" || lowered == "off") {
        *value = false;
        return true;
    }
    return false;
}

bool SetRuntimeControlRecordToggle(const std::string& text, TrainerRecordingOptions* recording) {
    if (!recording) {
        return false;
    }
    const std::string trimmed = TrimAscii(text);
    const std::string lowered = ToLowerAscii(trimmed);
    if (lowered.empty() || lowered == "off" || lowered == "none" || lowered == "immediate") {
        recording->toggle_key_vk = 0;
        recording->toggle_key_label.clear();
        return true;
    }
    const int vk = ParseTriggerVirtualKeyName(trimmed);
    if (vk <= 0) {
        return false;
    }
    recording->toggle_key_vk = vk;
    recording->toggle_key_label = trimmed;
    return true;
}

bool LoadTrainerRuntimeControlFile(const std::string& path,
                                   TrainerRuntimeControlSnapshot* snapshot,
                                   std::string* error) {
    if (!snapshot) {
        if (error) *error = "runtime control output is null";
        return false;
    }
    std::ifstream file(path);
    if (!file) {
        if (error) *error = "failed to open runtime control file";
        return false;
    }

    TrainerRuntimeControlSnapshot parsed;
    parsed.recording.start_immediately = true;
    std::string line;
    int line_number = 0;
    while (std::getline(file, line)) {
        ++line_number;
        const std::string trimmed = TrimAscii(line);
        if (trimmed.empty() || trimmed[0] == '#') {
            continue;
        }

        std::vector<std::string> fields = SplitTabLine(line);
        if (fields.empty()) {
            continue;
        }
        const std::string key = ToLowerAscii(TrimAscii(fields[0]));
        if (key == "recording_path") {
            if (fields.size() < 2) {
                if (error) *error = "line " + std::to_string(line_number) + " missing recording path";
                return false;
            }
            parsed.recording.path = TrimAscii(fields[1]);
            continue;
        }
        if (key == "record_duration") {
            if (fields.size() < 2) {
                if (error) *error = "line " + std::to_string(line_number) + " missing record duration";
                return false;
            }
            const std::string value = TrimAscii(fields[1]);
            char* end = nullptr;
            const long seconds = std::strtol(value.c_str(), &end, 10);
            if (value.empty() || !end || *end != '\0' || seconds < 0 || seconds > kTrainerProfileMaxSeconds) {
                if (error) *error = "line " + std::to_string(line_number) + " has invalid record duration";
                return false;
            }
            parsed.recording.max_seconds = static_cast<int>(seconds);
            continue;
        }
        if (key == "record_toggle") {
            if (fields.size() < 2 || !SetRuntimeControlRecordToggle(fields[1], &parsed.recording)) {
                if (error) *error = "line " + std::to_string(line_number) + " has invalid record toggle";
                return false;
            }
            continue;
        }
        if (key == "record_overwrite") {
            if (fields.size() < 2 ||
                !ParseRuntimeControlBool(fields[1], &parsed.recording.overwrite_existing)) {
                if (error) *error = "line " + std::to_string(line_number) + " has invalid overwrite value";
                return false;
            }
            continue;
        }
        if (key == "playback_loop") {
            if (fields.size() < 2 ||
                !ParseRuntimeControlBool(fields[1], &parsed.playback_bank.loop)) {
                if (error) *error = "line " + std::to_string(line_number) + " has invalid playback loop value";
                return false;
            }
            continue;
        }
        if (key == "bind" || key == "bind_block" || key == "bind-block" ||
            key == "bind_block_live" || key == "bind-block-live") {
            if (fields.size() < 4) {
                if (error) *error = "line " + std::to_string(line_number) + " bind needs trigger, channels, and recording";
                return false;
            }
            if (parsed.playback_bank.specs.size() >= kMaxPlaybackBankSlots) {
                if (error) *error = "too many playback binds in runtime control file";
                return false;
            }
            PlaybackBankSlotSpec spec;
            spec.block_live_input = key != "bind";
            if (!ParsePlaybackTrigger(fields[1], &spec.trigger) || spec.trigger.Immediate()) {
                if (error) *error = "line " + std::to_string(line_number) + " has invalid bind trigger";
                return false;
            }
            if (!ParsePlaybackChannelMask(fields[2], &spec.mask) || !spec.mask.Any()) {
                if (error) *error = "line " + std::to_string(line_number) + " has invalid bind channels";
                return false;
            }
            spec.recording_path = TrimAscii(fields[3]);
            if (spec.recording_path.empty()) {
                if (error) *error = "line " + std::to_string(line_number) + " bind recording path is empty";
                return false;
            }
            if (fields.size() >= 5 &&
                !ParseRuntimeControlBool(fields[4], &spec.block_live_input)) {
                if (error) *error = "line " + std::to_string(line_number) + " has invalid bind live-input block value";
                return false;
            }
            parsed.playback_bank.specs.push_back(std::move(spec));
            continue;
        }

        if (error) *error = "line " + std::to_string(line_number) + " has unknown key '" + key + "'";
        return false;
    }

    parsed.recording.start_immediately = !parsed.recording.ToggleMode();
    *snapshot = std::move(parsed);
    return true;
}

std::string FormatClipIndex(int clip_index) {
    std::ostringstream stream;
    stream << std::setw(3) << std::setfill('0') << clip_index;
    return stream.str();
}

std::string MakeRecordingClipPath(const std::string& base_path, int clip_index) {
    if (clip_index <= 1 && !std::filesystem::exists(base_path)) {
        return base_path;
    }

    const std::filesystem::path path(base_path);
    const std::filesystem::path parent = path.parent_path();
    const std::string filename = path.filename().string();
    const std::string lowered = ToLowerAscii(filename);
    const std::string double_extension = ".gx12rec.csv";
    const bool has_recording_extension =
        lowered.size() >= double_extension.size() &&
        lowered.compare(lowered.size() - double_extension.size(),
                        double_extension.size(),
                        double_extension) == 0;
    const std::string stem = has_recording_extension
        ? filename.substr(0, filename.size() - double_extension.size())
        : path.stem().string();
    const std::string extension = has_recording_extension
        ? double_extension
        : path.extension().string();

    for (int candidate_index = std::max(1, clip_index); candidate_index < 10000; ++candidate_index) {
        std::filesystem::path candidate = parent / (stem + "-" + FormatClipIndex(candidate_index) + extension);
        if (!std::filesystem::exists(candidate)) {
            return candidate.string();
        }
    }
    return (parent / (stem + "-" + std::to_string(clip_index) + extension)).string();
}

int RunTrainerProfile(const TrainerProfile& initial_profile,
                      bool guided,
                      bool live_reload,
                      const TrainerRecordingOptions& recording_options = TrainerRecordingOptions{},
                      const TrainerPlaybackBankOptions& playback_bank_options = TrainerPlaybackBankOptions{},
                      const TrainerRuntimeControlOptions& runtime_control_options = TrainerRuntimeControlOptions{}) {
    TrainerProfile active_profile = initial_profile;
    TrainerRecordingOptions active_recording_options = recording_options;
    TrainerPlaybackBankOptions active_playback_bank_options = playback_bank_options;
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

    TrainerCsvLogWriter csv_log;
    const bool csv_open = csv_log.Open(active_profile);
    const bool recording_hid_enabled = active_recording_options.Enabled();
    Gx12RecordingHidCapture recording_hid;
    RecordingCsvWriter recording_writer;
    RecordingCommitQueue recording_commits;
    bool recording_writer_open = false;
    uint64_t recording_total_clips = 0;
    uint64_t recording_failed_clips = 0;
    uint64_t recording_total_sample_rows = 0;
    uint64_t recording_total_hid_rows = 0;
    std::string last_recording_path;
    if (recording_hid_enabled) {
        recording_commits.Start();
        if (!recording_hid.Start()) {
            std::fprintf(stderr,
                         "warning: GX12 HID recording track unavailable: %s. Recording will continue without HID channels.\n",
                         recording_hid.last_error.empty() ? "unknown error" : recording_hid.last_error.c_str());
        }
        if (active_recording_options.ToggleMode()) {
            std::printf("recording_armed=%s key=%s max_seconds=%d multi_clip=true overwrite=%s buffer=memory commit=background tracks=trainer,mouse,keyboard%s\n",
                        active_recording_options.path.c_str(),
                        active_recording_options.toggle_key_label.c_str(),
                        active_recording_options.max_seconds,
                        active_recording_options.overwrite_existing ? "true" : "false",
                        recording_hid.available ? ",gx12_hid" : "");
        } else {
            const int initial_mapper_rate_hz =
                std::min(active_profile.frame_rate_hz, kTrainerMapperReferenceHz);
            if (!recording_writer.Open(active_profile,
                                       active_recording_options.path.c_str(),
                                       initial_mapper_rate_hz,
                                       recording_hid)) {
                recording_hid.Stop();
                CloseHandle(serial);
                if (launcher_stop_event) CloseHandle(launcher_stop_event);
                return 1;
            }
            recording_writer_open = true;
            last_recording_path = recording_writer.path;
            std::printf("recording=%s buffer=memory commit=background tracks=trainer,mouse,keyboard%s\n",
                        active_recording_options.path.c_str(),
                        recording_hid.available ? ",gx12_hid" : "");
        }
    }
    if (runtime_control_options.Enabled()) {
        std::printf("runtime_control=%s reload=250ms\n", runtime_control_options.path.c_str());
    }

    std::vector<PlaybackBankSlot> playback_slots;
    std::vector<bool> playback_previous_down;
    struct PendingRecordingCommitPath {
        std::string finished_path_lower;
        std::string selected_path_lower;
    };
    std::vector<PendingRecordingCommitPath> pending_recording_commit_paths;
    auto recording_commit_pending_for_path = [&](const std::string& path_to_check) {
        const std::string lowered = ToLowerAscii(path_to_check);
        return std::find_if(pending_recording_commit_paths.begin(),
                            pending_recording_commit_paths.end(),
                            [&](const PendingRecordingCommitPath& pending) {
                                return lowered == pending.finished_path_lower ||
                                       lowered == pending.selected_path_lower;
                            }) != pending_recording_commit_paths.end();
    };
    auto remove_pending_recording_commit_path = [&](const std::string& finished_path) {
        const std::string lowered = ToLowerAscii(finished_path);
        auto it = std::find_if(pending_recording_commit_paths.begin(),
                               pending_recording_commit_paths.end(),
                               [&](const PendingRecordingCommitPath& pending) {
                                   return lowered == pending.finished_path_lower;
                               });
        if (it != pending_recording_commit_paths.end()) {
            pending_recording_commit_paths.erase(it);
        }
    };
    auto load_playback_slot_recording = [&](PlaybackBankSlot* slot, bool print_error) -> bool {
        if (!slot) {
            return false;
        }
        if (recording_commit_pending_for_path(slot->spec.recording_path)) {
            if (print_error) {
                std::fprintf(stderr,
                             "trainer profile playback bind not ready for %s: recording save still in progress\n",
                             slot->spec.recording_path.c_str());
            }
            return false;
        }
        if (slot->loaded) {
            return true;
        }

        std::string error;
        LoadedRecording loaded;
        if (!LoadRecordingFile(slot->spec.recording_path.c_str(), &loaded, &error)) {
            if (print_error) {
                std::fprintf(stderr,
                             "trainer profile playback bind not ready for %s: %s\n",
                             slot->spec.recording_path.c_str(),
                             error.c_str());
            }
            return false;
        }
        if (!ValidatePlaybackRecordingForCurrentBuild(loaded, slot->spec.recording_path.c_str())) {
            return false;
        }
        if (loaded.metadata.resolution_mode != active_profile.resolution_mode) {
            std::fprintf(stderr,
                         "trainer profile playback bind resolution mismatch for %s: recording=%s profile=%s\n",
                         slot->spec.recording_path.c_str(),
                         TrainerResolutionModeName(loaded.metadata.resolution_mode),
                         TrainerResolutionModeName(active_profile.resolution_mode));
            return false;
        }
        slot->recording = std::move(loaded);
        slot->mapper_tick_sample_indices =
            BuildPlaybackMapperTickSampleIndices(slot->recording);
        slot->loaded = true;
        return true;
    };

    auto rebuild_playback_slots = [&](const TrainerPlaybackBankOptions& options,
                                      const char* source_label) -> bool {
        if (!options.Enabled()) {
            playback_slots.clear();
            playback_previous_down.clear();
            return true;
        }
        if (options.specs.size() > kMaxPlaybackBankSlots) {
            std::fprintf(stderr,
                         "trainer profile playback bank supports at most %zu binding slots.\n",
                         kMaxPlaybackBankSlots);
            return false;
        }
        std::vector<PlaybackBankSlot> rebuilt;
        rebuilt.reserve(options.specs.size());
        for (const PlaybackBankSlotSpec& spec : options.specs) {
            if (spec.trigger.Immediate()) {
                std::fprintf(stderr,
                             "trainer profile playback bind for %s needs a hotkey trigger.\n",
                             spec.recording_path.c_str());
                return false;
            }
            if (!spec.mask.Any()) {
                std::fprintf(stderr,
                             "trainer profile playback bind for %s needs at least one channel.\n",
                             spec.recording_path.c_str());
                return false;
            }

            PlaybackBankSlot slot;
            slot.spec = spec;
            if (std::filesystem::exists(slot.spec.recording_path)) {
                (void)load_playback_slot_recording(&slot, true);
            }
            rebuilt.push_back(std::move(slot));
        }

        playback_slots = std::move(rebuilt);
        playback_previous_down.assign(playback_slots.size(), false);
        std::printf("playback_bank_integrated=true source=%s slots=%zu mode=%s\n",
                    source_label ? source_label : "command",
                    playback_slots.size(),
                    options.loop ? "loop" : "once");
        for (size_t index = 0; index < playback_slots.size(); ++index) {
            const PlaybackBankSlot& slot = playback_slots[index];
            const std::string sample_count = slot.loaded
                ? std::to_string(slot.recording.samples.size())
                : "pending";
            std::printf("  [%zu] key=%s channels=%s live_input=%s samples=%s recording=%s\n",
                        index + 1,
                        slot.spec.trigger.label.c_str(),
                        DescribePlaybackChannelMask(slot.spec.mask).c_str(),
                        slot.spec.block_live_input ? "blocked" : "pass",
                        sample_count.c_str(),
                        slot.spec.recording_path.c_str());
            if (slot.spec.mask.UsesThrottleOrYaw()) {
                std::printf("      WARNING: bind includes recorded throttle/yaw; keep this sim/bench-only.\n");
            }
            if (slot.loaded && slot.spec.mask.UsesHidRightStick() && !slot.recording.has_hid_samples) {
                std::printf("      WARNING: radio right-gimbal playback requested, but recording has no HID samples; ch1/ch2 will fall back to final trainer rows.\n");
            }
        }
        return true;
    };

    if (!rebuild_playback_slots(active_playback_bank_options, "command")) {
        recording_writer.Close();
        recording_hid.Stop();
        CloseHandle(serial);
        if (launcher_stop_event) CloseHandle(launcher_stop_event);
        return 2;
    }

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
        recording_hid.Stop();
        CloseHandle(serial);
        if (launcher_stop_event) CloseHandle(launcher_stop_event);
        return 1;
    }
    if (!EnsureWootingForKeyboard(active_profile.keyboard_left,
                                  &wooting_sdk,
                                  &wooting_active,
                                  true)) {
        StopGameInputMouseCapture(&gameinput_capture);
        recording_hid.Stop();
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
        recording_hid.Stop();
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
        recording_hid.Stop();
        CloseHandle(serial);
        if (launcher_stop_event) CloseHandle(launcher_stop_event);
        return 1;
    }

    if (!RegisterMouseRawInput(hwnd, MouseRateMode::Foreground)) {
        DestroyWindow(hwnd);
        recording_hid.Stop();
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
    auto next_recording_hid_poll = start;
    const auto recording_hid_poll_period = std::chrono::milliseconds(1);
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
    auto next_runtime_control_check = start + std::chrono::milliseconds(250);
    std::filesystem::file_time_type last_runtime_control_write{};
    if (runtime_control_options.Enabled()) {
        std::error_code ec;
        last_runtime_control_write = std::filesystem::last_write_time(runtime_control_options.path, ec);
        if (ec) {
            std::fprintf(stderr,
                         "warning: runtime control cannot stat '%s': %s\n",
                         runtime_control_options.path.c_str(),
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
    int64_t last_right_mapper_wheel_x = 0;
    int64_t last_right_mapper_wheel_y = 0;
    int64_t last_left_mapper_dx = 0;
    int64_t last_left_mapper_dy = 0;
    int64_t last_left_mapper_wheel_x = 0;
    int64_t last_left_mapper_wheel_y = 0;
    uint32_t last_right_mapper_buttons = 0;
    uint32_t last_left_mapper_buttons = 0;
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
    bool last_record_toggle_down = false;
    bool recording_active = recording_writer_open && active_recording_options.start_immediately;
    bool recording_finished = false;
    int recording_clip_index = recording_writer_open ? 1 : 0;
    std::string recording_clip_base_path = recording_writer_open ? active_recording_options.path : "";
    bool recording_start_state_written = false;
    auto recording_started_at = start;
    auto recording_deadline = clock::time_point::max();
    bool playback_active = false;
    size_t playback_slot_index = 0;
    auto normalize_runtime_path_key = [](const std::string& raw_path) -> std::string {
        std::error_code ec;
        std::filesystem::path path(raw_path);
        std::filesystem::path absolute_path = std::filesystem::absolute(path, ec);
        if (ec) {
            absolute_path = path;
        }
        return ToLowerAscii(absolute_path.lexically_normal().string());
    };
    auto playback_binds_include_path = [&](const std::string& raw_path) -> bool {
        const std::string target = normalize_runtime_path_key(raw_path);
        if (target.empty()) {
            return false;
        }
        for (const PlaybackBankSlot& slot : playback_slots) {
            if (normalize_runtime_path_key(slot.spec.recording_path) == target) {
                return true;
            }
        }
        return false;
    };

    auto capture_recording_start_state = [&]() -> RecordingStartState {
        RecordingStartState state =
            MakeRecordingStartStateFromRightStick(filtered_roll,
                                                  filtered_pitch,
                                                  roll_elastic_state,
                                                  pitch_elastic_state,
                                                  last_roll,
                                                  last_pitch);
        if (active_profile.mouse_left.enabled) {
            state.mouse_left_available = true;
            state.mouse_left_throttle_value = keyboard_throttle;
            state.mouse_left_yaw_value = keyboard_yaw;
            state.mouse_left_yaw_filtered = filtered_left_yaw;
            state.mouse_left_yaw_position = left_yaw_position;
            state.mouse_left_yaw_dummy_position = left_yaw_dummy_position;
            state.mouse_left_yaw_velocity = left_yaw_elastic_state.velocity;
            state.mouse_left_yaw_dummy_velocity = left_yaw_dummy_state.velocity;
            state.mouse_left_throttle_pulse = last_throttle;
            state.mouse_left_yaw_pulse = last_yaw;
        } else if (active_profile.right_mouse_left.enabled) {
            state.right_mouse_left_available = true;
            state.right_mouse_left_throttle_value = keyboard_throttle;
            state.right_mouse_left_yaw_target = right_mouse_left_yaw_target;
            state.right_mouse_left_yaw_output = right_mouse_left_yaw_output;
            state.right_mouse_left_throttle_pulse = last_throttle;
            state.right_mouse_left_yaw_pulse = last_yaw;
        }
        return state;
    };

    auto open_next_recording_clip = [&](bool preopen) -> bool {
        if (!active_recording_options.Enabled() || recording_writer_open) {
            return true;
        }
        if (recording_clip_base_path != active_recording_options.path) {
            recording_clip_base_path = active_recording_options.path;
            recording_clip_index = 0;
        }
        ++recording_clip_index;
        const std::string clip_path = active_recording_options.ToggleMode()
            ? (active_recording_options.overwrite_existing
                   ? active_recording_options.path
                   : MakeRecordingClipPath(active_recording_options.path,
                                           recording_clip_index))
            : active_recording_options.path;
        if (preopen &&
            active_recording_options.ToggleMode() &&
            active_recording_options.overwrite_existing &&
            playback_binds_include_path(clip_path)) {
            std::printf("recording_preopen_skipped=playback-conflict path=%s\n",
                        clip_path.c_str());
            return true;
        }
        if (!recording_writer.Open(active_profile,
                                   clip_path.c_str(),
                                   mapper_rate_hz,
                                   recording_hid)) {
            return false;
        }
        recording_writer_open = true;
        recording_start_state_written = false;
        last_recording_path = recording_writer.path;
        return true;
    };

    if (recording_writer_open) {
        recording_writer.WriteStartState(capture_recording_start_state());
        recording_start_state_written = true;
    } else if (active_recording_options.Enabled() &&
               active_recording_options.ToggleMode()) {
        if (!open_next_recording_clip(true)) {
#if defined(GX12_HAS_GAMEINPUT)
            key_blocker.Stop();
            StopGameInputMouseCapture(&gameinput_capture);
#else
            if (IsWindow(hwnd)) {
                DestroyWindow(hwnd);
            }
#endif
            csv_log.Close();
            recording_hid.Stop();
            CloseHandle(serial);
            if (launcher_stop_event) CloseHandle(launcher_stop_event);
            return 1;
        }
        if (recording_writer_open) {
            std::printf("recording_preopened=%s buffer=memory commit=background\n",
                        recording_writer.path.c_str());
        }
    }

    auto begin_recording_clip = [&](clock::time_point now) -> bool {
        if (!active_recording_options.Enabled() || recording_active) {
            return true;
        }
        if (!active_recording_options.ToggleMode() && recording_finished) {
            return true;
        }
        if (!recording_writer_open) {
            if (!open_next_recording_clip(false)) {
                return false;
            }
        }
        if (!recording_start_state_written) {
            recording_writer.WriteStartState(capture_recording_start_state());
            recording_start_state_written = true;
        }
        recording_active = true;
        recording_started_at = now;
        recording_deadline = active_recording_options.max_seconds > 0
            ? now + std::chrono::seconds(active_recording_options.max_seconds)
            : clock::time_point::max();
        std::printf("\nrecording started: %s buffer=memory commit=background\n",
                    recording_writer.path.c_str());
        std::fflush(stdout);
        return true;
    };

    auto finish_recording_clip = [&](clock::time_point now, const char* reason) {
        if (!active_recording_options.Enabled() || !recording_active) {
            return;
        }
        recording_active = false;
        if (!active_recording_options.ToggleMode()) {
            recording_finished = true;
        }
        const std::string finished_path = recording_writer.path;
        const uint64_t clip_sample_rows = recording_writer.sample_rows;
        const uint64_t clip_hid_rows = recording_writer.hid_rows;
        RecordingCsvWriter::CommitSnapshot commit_snapshot =
            recording_writer.CloseToSnapshot();
        commit_snapshot.selected_path = active_recording_options.path;
        recording_writer_open = false;
        recording_start_state_written = false;
        recording_total_sample_rows += clip_sample_rows;
        recording_total_hid_rows += clip_hid_rows;
        last_recording_path = finished_path;
        const double clip_seconds =
            std::chrono::duration<double>(now - recording_started_at).count();
        std::printf("\nrecording stopped (%s): %s duration=%.3fs samples=%llu hid_rows=%llu\n",
                    reason,
                    finished_path.c_str(),
                    clip_seconds,
                    static_cast<unsigned long long>(clip_sample_rows),
                    static_cast<unsigned long long>(clip_hid_rows));
        const std::string finished_path_lower = ToLowerAscii(finished_path);
        const std::string selected_path_lower = ToLowerAscii(active_recording_options.path);
        for (size_t slot_index = 0; slot_index < playback_slots.size(); ++slot_index) {
            if (playback_active && slot_index == playback_slot_index) {
                continue;
            }
            PlaybackBankSlot& slot = playback_slots[slot_index];
            const std::string slot_path_lower = ToLowerAscii(slot.spec.recording_path);
            if (slot_path_lower == finished_path_lower || slot_path_lower == selected_path_lower) {
                slot.loaded = false;
                slot.recording = LoadedRecording{};
                slot.mapper_tick_sample_indices.clear();
            }
        }
        pending_recording_commit_paths.push_back(
            PendingRecordingCommitPath{finished_path_lower, selected_path_lower});
        recording_commits.Enqueue(std::move(commit_snapshot));
        std::printf("recording save queued: %s buffer=memory commit=background samples=%llu hid_rows=%llu\n",
                    finished_path.c_str(),
                    static_cast<unsigned long long>(clip_sample_rows),
                    static_cast<unsigned long long>(clip_hid_rows));
        std::fflush(stdout);
    };

    auto handle_recording_commit_result =
        [&](const RecordingCsvWriter::CommitResult& result) {
            remove_pending_recording_commit_path(result.path);
            const bool usable_clip = result.ok && result.attempted && result.sample_rows > 0;
            if (usable_clip) {
                ++recording_total_clips;
            } else {
                ++recording_failed_clips;
            }
            if (usable_clip) {
                std::printf("recording committed: %s buffer=memory commit=background bytes=%llu\n",
                            result.path.c_str(),
                            static_cast<unsigned long long>(result.bytes));
            } else if (!result.ok) {
                std::fprintf(stderr,
                             "recording commit failed: %s buffer=memory commit=background error=%s\n",
                             result.path.c_str(),
                             result.error.empty() ? "unknown" : result.error.c_str());
            } else if (result.sample_rows == 0) {
                std::fprintf(stderr,
                             "recording discarded: %s buffer=memory commit=background samples=0\n",
                             result.path.c_str());
            }
            std::fflush(stdout);
            if (!usable_clip) {
                return;
            }

            const std::string finished_path_lower = ToLowerAscii(result.path);
            const std::string selected_path_lower = ToLowerAscii(result.selected_path);
            for (size_t slot_index = 0; slot_index < playback_slots.size(); ++slot_index) {
                if (playback_active && slot_index == playback_slot_index) {
                    continue;
                }
                PlaybackBankSlot& slot = playback_slots[slot_index];
                const std::string slot_path_lower = ToLowerAscii(slot.spec.recording_path);
                if (slot_path_lower == finished_path_lower ||
                    slot_path_lower == selected_path_lower) {
                    slot.loaded = false;
                    slot.recording = LoadedRecording{};
                    slot.mapper_tick_sample_indices.clear();
                    (void)load_playback_slot_recording(&slot, false);
                }
            }
        };

    auto drain_recording_commit_results = [&]() {
        std::vector<RecordingCsvWriter::CommitResult> results =
            recording_commits.DrainCompleted();
        for (const RecordingCsvWriter::CommitResult& result : results) {
            handle_recording_commit_result(result);
        }
    };

    if (recording_active && active_recording_options.max_seconds > 0) {
        recording_deadline = start + std::chrono::seconds(active_recording_options.max_seconds);
    }

    size_t playback_sample_index = 0;
    size_t playback_input_sample_index = 0;
    size_t playback_mapper_tick_index = 0;
    int playback_mapper_tick_frame = 0;
    bool playback_input_have_mapper_tick = false;
    uint32_t playback_input_last_mapper_tick = 0;
    auto playback_started_at = start;
    int64_t playback_base_us = 0;
    RightStickPlaybackState playback_right_state;
    LeftStickPlaybackState playback_left_state;
    uint64_t integrated_playback_frames = 0;
    uint64_t integrated_playback_input_ticks = 0;
    uint64_t integrated_playback_starts = 0;

    auto playback_slot_uses_frame_clocked_input = [&](const PlaybackBankSlot& slot) {
        const bool right =
            PlaybackChannelUsesInputInjection(slot.spec.mask, active_profile, 0) ||
            PlaybackChannelUsesInputInjection(slot.spec.mask, active_profile, 1);
        const bool left =
            PlaybackChannelUsesInputInjection(slot.spec.mask, active_profile, 2) ||
            PlaybackChannelUsesInputInjection(slot.spec.mask, active_profile, 3);
        return right || left;
    };

    auto playback_slot_uses_normalized_mapper_clock = [&](const PlaybackBankSlot& slot) {
        return (PlaybackMaskUsesOnlyRecordedOverlay(slot.spec.mask) ||
                playback_slot_uses_frame_clocked_input(slot)) &&
               PlaybackFramesPerRecordedMapperTick(active_profile, slot.recording) > 0 &&
               !slot.mapper_tick_sample_indices.empty();
    };

    auto playback_slot_covers_primary_controls = [&](const PlaybackBankSlot& slot) {
        return slot.spec.mask.enabled[0] &&
               slot.spec.mask.enabled[1] &&
               slot.spec.mask.enabled[2] &&
               slot.spec.mask.enabled[3];
    };

    auto timing_sensitive_playback_active = [&]() {
        return playback_active &&
               playback_slot_index < playback_slots.size() &&
               playback_slot_uses_normalized_mapper_clock(
                   playback_slots[playback_slot_index]);
    };

    auto playback_can_skip_live_mapper = [&]() {
        return timing_sensitive_playback_active() &&
               playback_slot_covers_primary_controls(playback_slots[playback_slot_index]);
    };

    auto reset_integrated_playback_progress = [&](const PlaybackBankSlot& slot) {
        playback_sample_index = 0;
        playback_input_sample_index = 0;
        playback_mapper_tick_index = 0;
        playback_mapper_tick_frame = 0;
        playback_input_have_mapper_tick = false;
        playback_input_last_mapper_tick = 0;
        playback_base_us = slot.recording.samples.empty() ? 0 : slot.recording.samples.front().time_us;
        playback_right_state =
            MakeRightStickPlaybackStateFromRecording(slot.recording.metadata.start_state);
        playback_left_state =
            MakeLeftStickPlaybackStateFromRecording(active_profile,
                                                    slot.recording.metadata.start_state);
    };

    auto finish_integrated_playback = [&](const char* reason) {
        if (!playback_active || playback_slot_index >= playback_slots.size()) {
            playback_active = false;
            return;
        }
        const PlaybackBankSlot& slot = playback_slots[playback_slot_index];
        std::printf("playback bind %zu end: frames=%llu input_ticks=%llu reason=%s\n",
                    playback_slot_index + 1,
                    static_cast<unsigned long long>(integrated_playback_frames),
                    static_cast<unsigned long long>(integrated_playback_input_ticks),
                    reason);
        std::fflush(stdout);
        playback_active = false;
        playback_sample_index = 0;
        playback_input_sample_index = 0;
        playback_mapper_tick_index = 0;
        playback_mapper_tick_frame = 0;
        playback_input_have_mapper_tick = false;
        playback_input_last_mapper_tick = 0;
        playback_base_us = 0;
        (void)slot;
    };

    auto start_integrated_playback = [&](size_t index, clock::time_point now) {
        if (index >= playback_slots.size()) {
            return;
        }
        PlaybackBankSlot& slot = playback_slots[index];
        if (!load_playback_slot_recording(&slot, true)) {
            return;
        }
        if (slot.recording.samples.empty()) {
            std::fprintf(stderr,
                         "trainer profile playback bind has no samples: %s\n",
                         slot.spec.recording_path.c_str());
            return;
        }
        if (slot.recording.metadata.resolution_mode != active_profile.resolution_mode) {
            std::fprintf(stderr,
                         "trainer profile playback bind resolution mismatch for %s: recording=%s profile=%s\n",
                         slot.spec.recording_path.c_str(),
                         TrainerResolutionModeName(slot.recording.metadata.resolution_mode),
                         TrainerResolutionModeName(active_profile.resolution_mode));
            return;
        }
        if (!ResolveIntegratedPlaybackMaskForProfile(&slot.spec.mask,
                                                     active_profile,
                                                     slot.recording,
                                                     slot.spec.recording_path,
                                                     true)) {
            return;
        }
        playback_active = true;
        playback_slot_index = index;
        playback_started_at = now;
        reset_integrated_playback_progress(slot);
        integrated_playback_frames = 0;
        integrated_playback_input_ticks = 0;
        ++integrated_playback_starts;
        const bool normalized_clock =
            playback_slot_uses_normalized_mapper_clock(slot);
        const int normalized_frames_per_tick = normalized_clock
            ? PlaybackFramesPerRecordedMapperTick(active_profile, slot.recording)
            : 0;
        const std::string playback_clock_label = normalized_clock
            ? ("mapper-normalized/" + std::to_string(normalized_frames_per_tick))
            : "sample-rows";
        std::printf("playback bind %zu start: key=%s recording=%s channels=%s live_input=%s clock=%s\n",
                    index + 1,
                    slot.spec.trigger.label.c_str(),
                    slot.spec.recording_path.c_str(),
                    DescribePlaybackChannelMask(slot.spec.mask).c_str(),
                    slot.spec.block_live_input ? "blocked" : "pass",
                    playback_clock_label.c_str());
        std::fflush(stdout);
    };

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
        if (!timing_sensitive_playback_active()) {
            drain_recording_commit_results();
        }
        if (runtime_control_options.Enabled() &&
            !timing_sensitive_playback_active() &&
            now >= next_runtime_control_check) {
            std::error_code ec;
            const auto write_time = std::filesystem::last_write_time(runtime_control_options.path, ec);
            if (!ec && write_time != last_runtime_control_write) {
                TrainerRuntimeControlSnapshot snapshot;
                std::string error;
                if (LoadTrainerRuntimeControlFile(runtime_control_options.path, &snapshot, &error)) {
                    if (recording_active && snapshot.recording.path.empty()) {
                        finish_recording_clip(now, "settings");
                    }
                    if (playback_active) {
                        finish_integrated_playback("settings");
                    }
                    if (rebuild_playback_slots(snapshot.playback_bank, "control")) {
                        const bool record_path_changed = active_recording_options.path != snapshot.recording.path;
                        const bool record_key_changed =
                            active_recording_options.toggle_key_vk != snapshot.recording.toggle_key_vk;
                        active_recording_options = snapshot.recording;
                        active_playback_bank_options = snapshot.playback_bank;
                        if (record_path_changed) {
                            recording_finished = false;
                            if (!recording_active && recording_writer_open) {
                                recording_writer.Close();
                                recording_writer_open = false;
                                recording_start_state_written = false;
                            }
                        }
                        if (record_key_changed) {
                            last_record_toggle_down = false;
                        }
                        if (active_recording_options.Enabled() &&
                            !active_recording_options.ToggleMode() &&
                            !recording_active &&
                            !recording_finished &&
                            !begin_recording_clip(now)) {
                            stop_requested = true;
                            break;
                        }
                        std::printf("runtime_control_reload=ok recording=%s overwrite=%s binds=%zu loop=%s\n",
                                    active_recording_options.path.empty() ? "(off)" : active_recording_options.path.c_str(),
                                    active_recording_options.overwrite_existing ? "true" : "false",
                                    active_playback_bank_options.specs.size(),
                                    active_playback_bank_options.loop ? "true" : "false");
                        std::fflush(stdout);
                        last_runtime_control_write = write_time;
                    } else {
                        std::fprintf(stderr,
                                     "runtime_control_reload=failed; keeping previous playback binds: %s\n",
                                     runtime_control_options.path.c_str());
                        last_runtime_control_write = write_time;
                    }
                } else {
                    std::fprintf(stderr,
                                 "runtime_control_reload=failed; keeping previous settings: %s\n",
                                 error.c_str());
                    last_runtime_control_write = write_time;
                }
            } else if (ec && last_runtime_control_write != std::filesystem::file_time_type{}) {
                std::fprintf(stderr,
                             "runtime_control_reload=missing; keeping previous settings: %s\n",
                             ec.message().c_str());
                last_runtime_control_write = std::filesystem::file_time_type{};
            }
            do {
                next_runtime_control_check += std::chrono::milliseconds(250);
            } while (next_runtime_control_check <= now);
        }
        if (active_recording_options.Enabled() && active_recording_options.ToggleMode()) {
            const bool record_toggle_down = RecordingToggleKeyDown(active_recording_options);
            if (record_toggle_down && !last_record_toggle_down) {
                if (!recording_active) {
                    if (!begin_recording_clip(now)) {
                        stop_requested = true;
                        break;
                    }
                } else if (recording_active) {
                    finish_recording_clip(now, "key");
                }
            }
            last_record_toggle_down = record_toggle_down;
        }
        if (active_recording_options.Enabled() &&
            recording_active &&
            !timing_sensitive_playback_active() &&
            (!recording_finished || active_recording_options.ToggleMode()) &&
            now >= next_recording_hid_poll) {
            recording_hid.Poll(&recording_writer, start);
            do {
                next_recording_hid_poll += recording_hid_poll_period;
            } while (next_recording_hid_poll <= now);
        }
        if (recording_active && now >= recording_deadline) {
            finish_recording_clip(now, "duration");
        }
        if (!playback_slots.empty()) {
            for (size_t index = 0; index < playback_slots.size(); ++index) {
                const bool down = PlaybackTriggerPressed(playback_slots[index].spec.trigger);
                if (!down) {
                    playback_previous_down[index] = false;
                    continue;
                }
                if (playback_previous_down[index]) {
                    continue;
                }

                playback_previous_down[index] = true;
                if (playback_active) {
                    if (index == playback_slot_index) {
                        finish_integrated_playback("bind");
                    } else {
                        finish_integrated_playback("switch");
                        start_integrated_playback(index, now);
                    }
                } else {
                    start_integrated_playback(index, now);
                }
                break;
            }
        }
        if (live_reload &&
            !timing_sensitive_playback_active() &&
            now >= next_profile_reload_check &&
            !profile_path.empty()) {
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
                        reloaded.gate_shape != active_profile.gate_shape ||
                        reloaded.elastic_return_metric != active_profile.elastic_return_metric ||
                        reloaded.elastic_return_activation != active_profile.elastic_return_activation;
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
            last_right_mapper_wheel_x =
                current_right.wheel_x_sum - last_right_mapper.wheel_x_sum;
            last_right_mapper_wheel_y =
                current_right.wheel_y_sum - last_right_mapper.wheel_y_sum;
            last_left_mapper_dx = current_left.dx_sum - last_left_mapper.dx_sum;
            last_left_mapper_dy = current_left.dy_sum - last_left_mapper.dy_sum;
            last_left_mapper_wheel_x =
                current_left.wheel_x_sum - last_left_mapper.wheel_x_sum;
            last_left_mapper_wheel_y =
                current_left.wheel_y_sum - last_left_mapper.wheel_y_sum;
            last_right_mapper_buttons = current_right.buttons;
            last_left_mapper_buttons = current_left.buttons;
            last_right_mapper = current_right;
            last_left_mapper = current_left;
#else
            const MouseRateStats current = g_mouse_stats;
            last_mapper_dx = current.dx_sum - last_mapper.dx_sum;
            last_mapper_dy = current.dy_sum - last_mapper.dy_sum;
            last_right_mapper_wheel_x = 0;
            last_right_mapper_wheel_y = 0;
            last_left_mapper_dx = 0;
            last_left_mapper_dy = 0;
            last_left_mapper_wheel_x = 0;
            last_left_mapper_wheel_y = 0;
            last_right_mapper_buttons = 0;
            last_left_mapper_buttons = 0;
#endif
            last_mapper = current;

            if (playback_can_skip_live_mapper()) {
                last_mapper_dx = 0;
                last_mapper_dy = 0;
                last_right_mapper_wheel_x = 0;
                last_right_mapper_wheel_y = 0;
                last_left_mapper_dx = 0;
                last_left_mapper_dy = 0;
                last_left_mapper_wheel_x = 0;
                last_left_mapper_wheel_y = 0;
                last_right_mapper_buttons = 0;
                last_left_mapper_buttons = 0;
                last_analog_depths = {};
            } else {
            if (playback_active && playback_slot_index < playback_slots.size()) {
                const PlaybackBankSlot& slot = playback_slots[playback_slot_index];
                if (slot.spec.block_live_input) {
                    ClearPlaybackLiveInputForMask(slot.spec,
                                                  active_profile,
                                                  &last_mapper_dx,
                                                  &last_mapper_dy,
                                                  &last_right_mapper_wheel_y,
                                                  &last_right_mapper_buttons,
                                                  &last_left_mapper_dx,
                                                  &last_left_mapper_dy);
                }
            }

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
                    static_cast<double>(GameInputMouse4Mouse5Axis(last_right_mapper_buttons));

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
            uint8_t trainer_flags = TrainerActiveFlags(active_profile);
            bool finish_playback_after_frame = false;
            if (playback_active && playback_slot_index < playback_slots.size()) {
                PlaybackBankSlot& slot = playback_slots[playback_slot_index];
                const std::vector<RecordingSample>& samples = slot.recording.samples;
                const bool normalized_mapper_clock =
                    playback_slot_uses_normalized_mapper_clock(slot);
                if (normalized_mapper_clock) {
                    const std::vector<size_t>& tick_indices =
                        slot.mapper_tick_sample_indices;
                    if (playback_mapper_tick_index >= tick_indices.size()) {
                        if (active_playback_bank_options.loop) {
                            playback_started_at = now;
                            reset_integrated_playback_progress(slot);
                        } else {
                            finish_integrated_playback("end");
                        }
                    }
                    if (playback_active &&
                        playback_mapper_tick_index < tick_indices.size()) {
                        const int frames_per_tick =
                            PlaybackFramesPerRecordedMapperTick(active_profile,
                                                                slot.recording);
                        const size_t sample_index =
                            tick_indices[playback_mapper_tick_index];
                        const RecordingSample& playback_sample = samples[sample_index];
                        if (playback_slot_uses_frame_clocked_input(slot) &&
                            playback_mapper_tick_frame == 0) {
                            const bool playback_uses_right_input =
                                PlaybackChannelUsesInputInjection(slot.spec.mask, active_profile, 0) ||
                                PlaybackChannelUsesInputInjection(slot.spec.mask, active_profile, 1);
                            const bool playback_uses_left_input =
                                PlaybackChannelUsesInputInjection(slot.spec.mask, active_profile, 2) ||
                                PlaybackChannelUsesInputInjection(slot.spec.mask, active_profile, 3);
                            while (playback_input_sample_index < samples.size() &&
                                   (!playback_input_have_mapper_tick ||
                                    playback_input_last_mapper_tick != playback_sample.mapper_tick)) {
                                const size_t before_index = playback_input_sample_index;
                                const PlaybackInputInjection injection =
                                    ConsumePlaybackInputInjection(slot,
                                                                  active_profile,
                                                                  0,
                                                                  playback_base_us,
                                                                  &playback_input_sample_index,
                                                                  &playback_input_have_mapper_tick,
                                                                  &playback_input_last_mapper_tick);
                                if (injection.mapper_ticks == 0 ||
                                    playback_input_sample_index == before_index) {
                                    break;
                                }
                                if (playback_uses_right_input) {
                                    StepRightStickPlaybackState(active_profile,
                                                                injection.right_dx,
                                                                injection.right_dy,
                                                                mapper_rate_hz,
                                                                &playback_right_state);
                                }
                                if (playback_uses_left_input) {
                                    StepLeftStickPlaybackState(active_profile,
                                                               injection,
                                                               mapper_rate_hz,
                                                               &playback_left_state);
                                }
                                integrated_playback_input_ticks += injection.mapper_ticks;
                                if (playback_input_last_mapper_tick == playback_sample.mapper_tick) {
                                    break;
                                }
                            }
                        }
                        const std::array<int, kSbusChannels> playback_pulses =
                            BuildPlaybackPulses(playback_sample,
                                                slot.spec.mask,
                                                active_profile.resolution_mode);
                        for (int ch = 0; ch < kSbusChannels; ++ch) {
                            if (!slot.spec.mask.enabled[static_cast<size_t>(ch)]) {
                                continue;
                            }
                            if (PlaybackChannelUsesInputInjection(slot.spec.mask, active_profile, ch)) {
                                if (ch == 0) {
                                    trainer_pulses[0] = slot.spec.block_live_input
                                        ? playback_right_state.roll_pulse
                                        : ClampTrainerOutput(
                                              static_cast<int64_t>(trainer_pulses[0]) +
                                                  playback_right_state.roll_pulse,
                                              active_profile.resolution_mode);
                                } else if (ch == 1) {
                                    trainer_pulses[1] = slot.spec.block_live_input
                                        ? playback_right_state.pitch_pulse
                                        : ClampTrainerOutput(
                                              static_cast<int64_t>(trainer_pulses[1]) +
                                                  playback_right_state.pitch_pulse,
                                              active_profile.resolution_mode);
                                } else if (ch == 2) {
                                    trainer_pulses[2] = slot.spec.block_live_input
                                        ? playback_left_state.throttle_pulse
                                        : ClampTrainerOutput(
                                              static_cast<int64_t>(trainer_pulses[2]) +
                                                  playback_left_state.throttle_pulse -
                                                  TrainerLowValue(active_profile.resolution_mode),
                                              active_profile.resolution_mode);
                                } else if (ch == 3) {
                                    trainer_pulses[3] = slot.spec.block_live_input
                                        ? playback_left_state.yaw_pulse
                                        : ClampTrainerOutput(
                                              static_cast<int64_t>(trainer_pulses[3]) +
                                                  playback_left_state.yaw_pulse,
                                              active_profile.resolution_mode);
                                }
                            } else {
                                trainer_pulses[static_cast<size_t>(ch)] =
                                    playback_pulses[static_cast<size_t>(ch)];
                            }
                        }
                        trainer_flags |= PlaybackActiveFlags(slot.spec.mask, active_profile.resolution_mode);
                        ++integrated_playback_frames;
                        ++playback_mapper_tick_frame;
                        if (playback_mapper_tick_frame >= frames_per_tick) {
                            playback_mapper_tick_frame = 0;
                            ++playback_mapper_tick_index;
                        }
                        if (playback_mapper_tick_index >= tick_indices.size()) {
                            finish_playback_after_frame = true;
                        }
                    }
                } else {
                    if (playback_sample_index >= samples.size()) {
                        if (active_playback_bank_options.loop) {
                            playback_started_at = now;
                            reset_integrated_playback_progress(slot);
                        } else {
                            finish_integrated_playback("end");
                        }
                    }
                    if (playback_active && playback_sample_index < samples.size()) {
                        const RecordingSample& playback_sample = samples[playback_sample_index];
                        if (playback_slot_uses_frame_clocked_input(slot)) {
                            const bool playback_uses_right_input =
                                PlaybackChannelUsesInputInjection(slot.spec.mask, active_profile, 0) ||
                                PlaybackChannelUsesInputInjection(slot.spec.mask, active_profile, 1);
                            const bool playback_uses_left_input =
                                PlaybackChannelUsesInputInjection(slot.spec.mask, active_profile, 2) ||
                                PlaybackChannelUsesInputInjection(slot.spec.mask, active_profile, 3);
                            while (playback_input_sample_index < samples.size() &&
                                   (!playback_input_have_mapper_tick ||
                                    playback_input_last_mapper_tick != playback_sample.mapper_tick)) {
                                const size_t before_index = playback_input_sample_index;
                                const PlaybackInputInjection injection =
                                    ConsumePlaybackInputInjection(slot,
                                                                  active_profile,
                                                                  0,
                                                                  playback_base_us,
                                                                  &playback_input_sample_index,
                                                                  &playback_input_have_mapper_tick,
                                                                  &playback_input_last_mapper_tick);
                                if (injection.mapper_ticks == 0 ||
                                    playback_input_sample_index == before_index) {
                                    break;
                                }
                                if (playback_uses_right_input) {
                                    StepRightStickPlaybackState(active_profile,
                                                                injection.right_dx,
                                                                injection.right_dy,
                                                                mapper_rate_hz,
                                                                &playback_right_state);
                                }
                                if (playback_uses_left_input) {
                                    StepLeftStickPlaybackState(active_profile,
                                                               injection,
                                                               mapper_rate_hz,
                                                               &playback_left_state);
                                }
                                integrated_playback_input_ticks += injection.mapper_ticks;
                                if (playback_input_last_mapper_tick == playback_sample.mapper_tick) {
                                    break;
                                }
                            }
                            if (PlaybackChannelUsesInputInjection(slot.spec.mask, active_profile, 0)) {
                                trainer_pulses[0] = slot.spec.block_live_input
                                    ? playback_right_state.roll_pulse
                                    : ClampTrainerOutput(
                                          static_cast<int64_t>(trainer_pulses[0]) +
                                              playback_right_state.roll_pulse,
                                          active_profile.resolution_mode);
                            }
                            if (PlaybackChannelUsesInputInjection(slot.spec.mask, active_profile, 1)) {
                                trainer_pulses[1] = slot.spec.block_live_input
                                    ? playback_right_state.pitch_pulse
                                    : ClampTrainerOutput(
                                          static_cast<int64_t>(trainer_pulses[1]) +
                                              playback_right_state.pitch_pulse,
                                          active_profile.resolution_mode);
                            }
                            if (PlaybackChannelUsesInputInjection(slot.spec.mask, active_profile, 2)) {
                                trainer_pulses[2] = slot.spec.block_live_input
                                    ? playback_left_state.throttle_pulse
                                    : ClampTrainerOutput(
                                          static_cast<int64_t>(trainer_pulses[2]) +
                                              playback_left_state.throttle_pulse -
                                              TrainerLowValue(active_profile.resolution_mode),
                                          active_profile.resolution_mode);
                            }
                            if (PlaybackChannelUsesInputInjection(slot.spec.mask, active_profile, 3)) {
                                trainer_pulses[3] = slot.spec.block_live_input
                                    ? playback_left_state.yaw_pulse
                                    : ClampTrainerOutput(
                                          static_cast<int64_t>(trainer_pulses[3]) +
                                              playback_left_state.yaw_pulse,
                                          active_profile.resolution_mode);
                            }
                        }

                        const std::array<int, kSbusChannels> playback_pulses =
                            BuildPlaybackPulses(playback_sample,
                                                slot.spec.mask,
                                                active_profile.resolution_mode);
                        for (int ch = 0; ch < kSbusChannels; ++ch) {
                            if (slot.spec.mask.enabled[static_cast<size_t>(ch)] &&
                                !PlaybackChannelUsesInputInjection(slot.spec.mask, active_profile, ch)) {
                                trainer_pulses[static_cast<size_t>(ch)] =
                                    playback_pulses[static_cast<size_t>(ch)];
                            }
                        }
                        trainer_flags |= PlaybackActiveFlags(slot.spec.mask, active_profile.resolution_mode);
                        ++integrated_playback_frames;
                        ++playback_sample_index;
                        if (playback_sample_index >= samples.size()) {
                            finish_playback_after_frame = true;
                        }
                    }
                }
            }
            BuildSbusFrame(trainer_pulses,
                           trainer_flags,
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
                recording_hid.Stop();
                CloseHandle(serial);
                if (launcher_stop_event) CloseHandle(launcher_stop_event);
                return 1;
            }

            ++frames;
            if (recording_active && recording_writer.IsOpen()) {
                const int64_t time_us =
                    std::chrono::duration_cast<std::chrono::microseconds>(now - start).count();
                recording_writer.WriteSample(time_us,
                                             frames,
                                             mapper_updates,
                                             current_events_total,
                                             current_aux_total,
                                             current_keyboard_total,
                                             last_mapper_dx,
                                             last_mapper_dy,
                                             last_right_mapper_wheel_x,
                                             last_right_mapper_wheel_y,
                                             last_right_mapper_buttons,
                                             last_left_mapper_dx,
                                             last_left_mapper_dy,
                                             last_left_mapper_wheel_x,
                                             last_left_mapper_wheel_y,
                                             last_left_mapper_buttons,
                                             trainer_pulses,
                                             recording_hid,
                                             trainer_flags,
                                             late_us);
            }
            if (csv_open &&
                !timing_sensitive_playback_active() &&
                (frames % static_cast<uint32_t>(active_profile.log_every_n_frames) == 0U)) {
                TrainerCsvLogWriter::Row log_row;
                log_row.time_s = std::chrono::duration<double>(now - start).count();
                log_row.frame = frames;
                log_row.mouse_events = current_events_total;
                log_row.input_aux = current_aux_total;
                log_row.keyboard_events = current_keyboard_total;
                log_row.dx = last_mapper_dx;
                log_row.dy = last_mapper_dy;
                log_row.mouse_raw_dx = right_stick_state.raw_roll_source;
                log_row.mouse_raw_dy = right_stick_state.raw_pitch_source;
                log_row.mouse_filtered_dx = right_stick_state.filtered_roll_source;
                log_row.mouse_filtered_dy = right_stick_state.filtered_pitch_source;
                log_row.despike_recent_count = right_stick_state.despike_recent_count;
                log_row.despike_total_count = right_stick_state.despike_total_count;
                log_row.left_dx = last_left_mapper_dx;
                log_row.left_dy = last_left_mapper_dy;
                log_row.left_yaw_raw = left_yaw_mapper_state.raw_pitch_source;
                log_row.left_yaw_filtered = left_yaw_mapper_state.filtered_pitch_source;
                log_row.left_despike_recent_count = left_yaw_mapper_state.despike_recent_count;
                log_row.left_despike_total_count = left_yaw_mapper_state.despike_total_count;
                log_row.left_yaw_position_model = active_profile.mouse_left.yaw_position_model;
                log_row.left_yaw_adaptive_gain = left_yaw_mapper_state.adaptive_gain;
                log_row.left_yaw_gate_scale = left_yaw_mapper_state.gate_scale;
                log_row.left_yaw_gimbal_antiwindup_active =
                    left_yaw_mapper_state.gimbal_antiwindup_active;
                log_row.position_model = active_profile.position_model;
                log_row.adaptive_gain = right_stick_state.adaptive_gain;
                log_row.gate_scale = right_stick_state.gate_scale;
                log_row.gimbal_antiwindup_active =
                    right_stick_state.gimbal_antiwindup_active;
                log_row.roll_position = filtered_roll;
                log_row.pitch_position = filtered_pitch;
                log_row.roll_velocity = roll_elastic_state.velocity;
                log_row.pitch_velocity = pitch_elastic_state.velocity;
                log_row.roll = last_roll;
                log_row.pitch = last_pitch;
                log_row.throttle = last_throttle;
                log_row.yaw = last_yaw;
                log_row.aim_x = mouse_aim_state.reticle_x;
                log_row.aim_y = mouse_aim_state.reticle_y;
                log_row.analog_throttle_up = last_analog_depths.throttle_up;
                log_row.analog_throttle_down = last_analog_depths.throttle_down;
                log_row.analog_yaw_left = last_analog_depths.yaw_left;
                log_row.analog_yaw_right = last_analog_depths.yaw_right;
                log_row.analog_cut = last_analog_depths.throttle_cut;
                log_row.late_us = late_us;
                csv_log.Enqueue(log_row);
            }
            do {
                next_frame += frame_period;
            } while (next_frame <= now);
            if (finish_playback_after_frame && playback_active &&
                playback_slot_index < playback_slots.size()) {
                PlaybackBankSlot& slot = playback_slots[playback_slot_index];
                if (active_playback_bank_options.loop) {
                    playback_started_at = now;
                    reset_integrated_playback_progress(slot);
                } else {
                    finish_integrated_playback("end");
                }
            }
        }

        if (!timing_sensitive_playback_active() && now >= next_print) {
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
        } else if (timing_sensitive_playback_active() && now >= next_print) {
            do {
                next_print += std::chrono::milliseconds(250);
            } while (next_print <= now);
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

    if (playback_active) {
        finish_integrated_playback(stop_requested ? "stop" : "end");
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
        const uint64_t dropped_csv_rows = csv_log.DroppedRows();
        csv_log.Close();
        std::printf("csv log: %s dropped_rows=%llu\n",
                    active_profile.log_path.c_str(),
                    static_cast<unsigned long long>(dropped_csv_rows));
    }
    if (recording_active) {
        finish_recording_clip(clock::now(), stop_requested ? "stop" : "end");
    }
    if (active_recording_options.Enabled()) {
        recording_writer.Close();
        recording_commits.Stop();
        drain_recording_commit_results();
        std::printf("recording: base=%s clips=%llu failed_clips=%llu samples=%llu hid_rows=%llu hid_reports=%llu hid_decode_errors=%llu hid_read_errors=%llu last=%s\n",
                    active_recording_options.path.c_str(),
                    static_cast<unsigned long long>(recording_total_clips),
                    static_cast<unsigned long long>(recording_failed_clips),
                    static_cast<unsigned long long>(recording_total_sample_rows),
                    static_cast<unsigned long long>(recording_total_hid_rows),
                    static_cast<unsigned long long>(recording_hid.reports),
                    static_cast<unsigned long long>(recording_hid.decode_errors),
                    static_cast<unsigned long long>(recording_hid.read_errors),
                    last_recording_path.empty() ? "(none)" : last_recording_path.c_str());
        if (!recording_hid.last_error.empty() && recording_hid.read_errors > 0) {
            std::printf("recording_hid_last_error: %s\n", recording_hid.last_error.c_str());
        }
    }
    if (!playback_slots.empty()) {
        std::printf("playback_bank_integrated: starts=%llu frames=%llu slots=%zu\n",
                    static_cast<unsigned long long>(integrated_playback_starts),
                    static_cast<unsigned long long>(integrated_playback_frames),
                    playback_slots.size());
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
    recording_hid.Stop();
    CloseHandle(serial);
    if (launcher_stop_event) CloseHandle(launcher_stop_event);
    return 0;
}

constexpr size_t kRecordingColRowType = 0;
constexpr size_t kRecordingColTimeUs = 1;
constexpr size_t kRecordingColFrame = 2;
constexpr size_t kRecordingColMapperTick = 3;
constexpr size_t kRecordingColInputEvents = 4;
constexpr size_t kRecordingColInputAux = 5;
constexpr size_t kRecordingColKeyboardEvents = 6;
constexpr size_t kRecordingColRightDx = 7;
constexpr size_t kRecordingColRightDy = 8;
constexpr size_t kRecordingColRightWheelX = 9;
constexpr size_t kRecordingColRightWheelY = 10;
constexpr size_t kRecordingColRightButtons = 11;
constexpr size_t kRecordingColLeftDx = 12;
constexpr size_t kRecordingColLeftDy = 13;
constexpr size_t kRecordingColLeftWheelX = 14;
constexpr size_t kRecordingColLeftWheelY = 15;
constexpr size_t kRecordingColLeftButtons = 16;
constexpr size_t kRecordingColFinalFirst = 17;
constexpr size_t kRecordingColHidReports = kRecordingColFinalFirst + kSbusChannels;
constexpr size_t kRecordingColHidValid = kRecordingColHidReports + 1;
constexpr size_t kRecordingColHidButtons = kRecordingColHidReports + 2;
constexpr size_t kRecordingColHidFirst = kRecordingColHidReports + 3;
constexpr size_t kRecordingColTrainerFlags = kRecordingColHidFirst + kGx12ChannelCount;
constexpr size_t kRecordingColLateUs = kRecordingColTrainerFlags + 1;
constexpr size_t kRecordingMinColumns = kRecordingColLateUs + 1;

bool ParseInt64Strict(const std::string& text, int64_t* value) {
    if (!value) {
        return false;
    }
    const std::string trimmed = TrimAscii(text);
    if (trimmed.empty()) {
        return false;
    }
    char* end = nullptr;
    const long long parsed = std::strtoll(trimmed.c_str(), &end, 10);
    if (!end || *end != '\0') {
        return false;
    }
    *value = static_cast<int64_t>(parsed);
    return true;
}

bool ParseUInt64Strict(const std::string& text, uint64_t* value) {
    if (!value) {
        return false;
    }
    const std::string trimmed = TrimAscii(text);
    if (trimmed.empty() || trimmed[0] == '-') {
        return false;
    }
    char* end = nullptr;
    const unsigned long long parsed = std::strtoull(trimmed.c_str(), &end, 10);
    if (!end || *end != '\0') {
        return false;
    }
    *value = static_cast<uint64_t>(parsed);
    return true;
}

bool ParseDoubleStrict(const std::string& text, double* value) {
    if (!value) {
        return false;
    }
    const std::string trimmed = TrimAscii(text);
    if (trimmed.empty()) {
        return false;
    }
    char* end = nullptr;
    const double parsed = std::strtod(trimmed.c_str(), &end);
    if (!end || *end != '\0' || !std::isfinite(parsed)) {
        return false;
    }
    *value = parsed;
    return true;
}

int64_t FieldToInt64(const std::vector<std::string>& fields, size_t index, int64_t fallback = 0) {
    if (index >= fields.size()) {
        return fallback;
    }
    int64_t value = 0;
    return ParseInt64Strict(fields[index], &value) ? value : fallback;
}

int FieldToInt(const std::vector<std::string>& fields, size_t index, int fallback = 0) {
    const int64_t value = FieldToInt64(fields, index, fallback);
    if (value < static_cast<int64_t>(std::numeric_limits<int>::min())) {
        return std::numeric_limits<int>::min();
    }
    if (value > static_cast<int64_t>(std::numeric_limits<int>::max())) {
        return std::numeric_limits<int>::max();
    }
    return static_cast<int>(value);
}

uint64_t FieldToUInt64(const std::vector<std::string>& fields, size_t index, uint64_t fallback = 0) {
    if (index >= fields.size()) {
        return fallback;
    }
    uint64_t value = 0;
    return ParseUInt64Strict(fields[index], &value) ? value : fallback;
}

void ApplyRecordingMetadataLine(const std::string& line, RecordingMetadata* metadata) {
    if (!metadata || line.empty() || line[0] != '#') {
        return;
    }
    std::string body = TrimAscii(line.substr(1));
    const size_t equals = body.find('=');
    if (equals == std::string::npos) {
        return;
    }
    const std::string key = TrimAscii(body.substr(0, equals));
    const std::string value = TrimAscii(body.substr(equals + 1));
    if (key == kRecordingMagic) {
        int64_t schema = 0;
        if (ParseInt64Strict(value, &schema)) {
            metadata->schema = static_cast<int>(schema);
        }
    } else if (key == "app_version") {
        metadata->app_version = value;
    } else if (key == "profile_name") {
        metadata->profile_name = value;
    } else if (key == "profile_path") {
        metadata->profile_path = value;
    } else if (key == "profile_fnv1a64") {
        metadata->profile_hash = value;
    } else if (key == "recording_buffer") {
        metadata->recording_buffer = value.empty() ? "unknown" : value;
    } else if (key == "trainer_frame_rate_hz") {
        int64_t parsed = 0;
        if (ParseInt64Strict(value, &parsed)) {
            metadata->trainer_frame_rate_hz = static_cast<int>(parsed);
        }
    } else if (key == "mapper_rate_hz") {
        int64_t parsed = 0;
        if (ParseInt64Strict(value, &parsed)) {
            metadata->mapper_rate_hz = static_cast<int>(parsed);
        }
    } else if (key == "trainer_resolution_mode") {
        TrainerResolutionMode mode = TrainerResolutionMode::Legacy;
        if (ParseTrainerResolutionModeName(value, &mode)) {
            metadata->resolution_mode = mode;
        }
    } else if (key == "hid_available") {
        const std::string lowered = ToLowerAscii(value);
        metadata->hid_available_header = lowered == "true" || lowered == "1" || lowered == "yes";
    } else if (key == "playback_start_right_mapper_state") {
        const std::string lowered = ToLowerAscii(value);
        metadata->start_state.right_mapper_available =
            lowered == "true" || lowered == "1" || lowered == "yes";
    } else if (key == "playback_start_right_roll_value") {
        (void)ParseDoubleStrict(value, &metadata->start_state.right_roll_value);
    } else if (key == "playback_start_right_pitch_value") {
        (void)ParseDoubleStrict(value, &metadata->start_state.right_pitch_value);
    } else if (key == "playback_start_right_roll_velocity") {
        (void)ParseDoubleStrict(value, &metadata->start_state.right_roll_velocity);
    } else if (key == "playback_start_right_pitch_velocity") {
        (void)ParseDoubleStrict(value, &metadata->start_state.right_pitch_velocity);
    } else if (key == "playback_start_right_roll_pulse") {
        int64_t parsed = 0;
        if (ParseInt64Strict(value, &parsed)) {
            metadata->start_state.right_roll_pulse = static_cast<int>(parsed);
        }
    } else if (key == "playback_start_right_pitch_pulse") {
        int64_t parsed = 0;
        if (ParseInt64Strict(value, &parsed)) {
            metadata->start_state.right_pitch_pulse = static_cast<int>(parsed);
        }
    } else if (key == "playback_start_mouse_left_state") {
        const std::string lowered = ToLowerAscii(value);
        metadata->start_state.mouse_left_available =
            lowered == "true" || lowered == "1" || lowered == "yes";
    } else if (key == "playback_start_mouse_left_throttle_value") {
        (void)ParseDoubleStrict(value, &metadata->start_state.mouse_left_throttle_value);
    } else if (key == "playback_start_mouse_left_yaw_value") {
        (void)ParseDoubleStrict(value, &metadata->start_state.mouse_left_yaw_value);
    } else if (key == "playback_start_mouse_left_yaw_filtered") {
        (void)ParseDoubleStrict(value, &metadata->start_state.mouse_left_yaw_filtered);
    } else if (key == "playback_start_mouse_left_yaw_position") {
        (void)ParseDoubleStrict(value, &metadata->start_state.mouse_left_yaw_position);
    } else if (key == "playback_start_mouse_left_yaw_dummy_position") {
        (void)ParseDoubleStrict(value, &metadata->start_state.mouse_left_yaw_dummy_position);
    } else if (key == "playback_start_mouse_left_yaw_velocity") {
        (void)ParseDoubleStrict(value, &metadata->start_state.mouse_left_yaw_velocity);
    } else if (key == "playback_start_mouse_left_yaw_dummy_velocity") {
        (void)ParseDoubleStrict(value, &metadata->start_state.mouse_left_yaw_dummy_velocity);
    } else if (key == "playback_start_mouse_left_throttle_pulse") {
        int64_t parsed = 0;
        if (ParseInt64Strict(value, &parsed)) {
            metadata->start_state.mouse_left_throttle_pulse = static_cast<int>(parsed);
        }
    } else if (key == "playback_start_mouse_left_yaw_pulse") {
        int64_t parsed = 0;
        if (ParseInt64Strict(value, &parsed)) {
            metadata->start_state.mouse_left_yaw_pulse = static_cast<int>(parsed);
        }
    } else if (key == "playback_start_right_mouse_left_state") {
        const std::string lowered = ToLowerAscii(value);
        metadata->start_state.right_mouse_left_available =
            lowered == "true" || lowered == "1" || lowered == "yes";
    } else if (key == "playback_start_right_mouse_left_throttle_value") {
        (void)ParseDoubleStrict(value, &metadata->start_state.right_mouse_left_throttle_value);
    } else if (key == "playback_start_right_mouse_left_yaw_target") {
        (void)ParseDoubleStrict(value, &metadata->start_state.right_mouse_left_yaw_target);
    } else if (key == "playback_start_right_mouse_left_yaw_output") {
        (void)ParseDoubleStrict(value, &metadata->start_state.right_mouse_left_yaw_output);
    } else if (key == "playback_start_right_mouse_left_throttle_pulse") {
        int64_t parsed = 0;
        if (ParseInt64Strict(value, &parsed)) {
            metadata->start_state.right_mouse_left_throttle_pulse = static_cast<int>(parsed);
        }
    } else if (key == "playback_start_right_mouse_left_yaw_pulse") {
        int64_t parsed = 0;
        if (ParseInt64Strict(value, &parsed)) {
            metadata->start_state.right_mouse_left_yaw_pulse = static_cast<int>(parsed);
        }
    }
}

bool LoadRecordingFile(const char* path, LoadedRecording* recording, std::string* error) {
    if (!path || path[0] == '\0') {
        if (error) *error = "recording path is empty";
        return false;
    }
    if (!recording) {
        if (error) *error = "internal recording output is null";
        return false;
    }

    std::ifstream file(path);
    if (!file) {
        if (error) *error = std::string("failed to open recording: ") + path;
        return false;
    }

    LoadedRecording loaded;
    std::string line;
    uint64_t line_number = 0;
    while (std::getline(file, line)) {
        ++line_number;
        if (line.empty()) {
            continue;
        }
        if (line[0] == '#') {
            ApplyRecordingMetadataLine(line, &loaded.metadata);
            continue;
        }

        std::vector<std::string> fields = SplitSimpleCsvLine(line);
        if (fields.empty() || fields[kRecordingColRowType] == "row_type") {
            continue;
        }
        if (fields.size() < kRecordingMinColumns) {
            if (error) {
                *error = "recording row " + std::to_string(line_number) +
                         " has too few columns";
            }
            return false;
        }

        const std::string row_type = fields[kRecordingColRowType];
        if (row_type == "hid") {
            ++loaded.hid_rows;
            continue;
        }
        if (row_type != "sample") {
            continue;
        }

        RecordingSample sample;
        int64_t parsed = 0;
        if (!ParseInt64Strict(fields[kRecordingColTimeUs], &sample.time_us)) {
            if (error) {
                *error = "recording row " + std::to_string(line_number) +
                         " has invalid time_us";
            }
            return false;
        }
        if (ParseInt64Strict(fields[kRecordingColFrame], &parsed)) {
            sample.frame = static_cast<uint32_t>(std::max<int64_t>(0, parsed));
        }
        if (ParseInt64Strict(fields[kRecordingColMapperTick], &parsed)) {
            sample.mapper_tick = static_cast<uint32_t>(std::max<int64_t>(0, parsed));
        }
        sample.right_dx = FieldToInt64(fields, kRecordingColRightDx);
        sample.right_dy = FieldToInt64(fields, kRecordingColRightDy);
        sample.right_wheel_x = FieldToInt64(fields, kRecordingColRightWheelX);
        sample.right_wheel_y = FieldToInt64(fields, kRecordingColRightWheelY);
        sample.right_buttons =
            static_cast<uint32_t>(FieldToUInt64(fields, kRecordingColRightButtons) & 0xFFFFFFFFULL);
        sample.left_dx = FieldToInt64(fields, kRecordingColLeftDx);
        sample.left_dy = FieldToInt64(fields, kRecordingColLeftDy);
        sample.left_wheel_x = FieldToInt64(fields, kRecordingColLeftWheelX);
        sample.left_wheel_y = FieldToInt64(fields, kRecordingColLeftWheelY);
        sample.left_buttons =
            static_cast<uint32_t>(FieldToUInt64(fields, kRecordingColLeftButtons) & 0xFFFFFFFFULL);
        for (int ch = 0; ch < kSbusChannels; ++ch) {
            sample.final_channels[static_cast<size_t>(ch)] =
                FieldToInt(fields, kRecordingColFinalFirst + static_cast<size_t>(ch));
        }
        sample.hid_reports = FieldToUInt64(fields, kRecordingColHidReports);
        sample.hid_valid = FieldToInt(fields, kRecordingColHidValid) != 0;
        sample.hid_buttons = static_cast<uint32_t>(FieldToUInt64(fields, kRecordingColHidButtons));
        for (int ch = 0; ch < kGx12ChannelCount; ++ch) {
            sample.hid_channels[static_cast<size_t>(ch)] =
                FieldToInt(fields, kRecordingColHidFirst + static_cast<size_t>(ch));
        }
        sample.trainer_flags = static_cast<uint8_t>(FieldToUInt64(fields, kRecordingColTrainerFlags) & 0xFFU);
        sample.late_us = FieldToInt(fields, kRecordingColLateUs);
        loaded.has_hid_samples = loaded.has_hid_samples || sample.hid_valid;
        loaded.samples.push_back(sample);
    }

    if (loaded.metadata.schema != kRecordingSchemaVersion) {
        if (error) {
            *error = "unsupported or missing recording schema version";
        }
        return false;
    }
    if (loaded.samples.empty()) {
        if (error) {
            *error = "recording has no sample rows";
        }
        return false;
    }

    *recording = std::move(loaded);
    return true;
}

std::vector<size_t> BuildPlaybackMapperTickSampleIndices(const LoadedRecording& recording) {
    std::vector<size_t> indices;
    indices.reserve(recording.samples.size());
    bool have_tick = false;
    uint32_t last_tick = 0;
    for (size_t index = 0; index < recording.samples.size(); ++index) {
        const uint32_t tick = recording.samples[index].mapper_tick;
        if (!have_tick || tick != last_tick) {
            indices.push_back(index);
            have_tick = true;
            last_tick = tick;
        } else if (!indices.empty()) {
            indices.back() = index;
        }
    }
    return indices;
}

bool PlaybackMaskUsesOnlyRecordedOverlay(const PlaybackChannelMask& mask) {
    bool any_enabled = false;
    for (int ch = 0; ch < kSbusChannels; ++ch) {
        const size_t index = static_cast<size_t>(ch);
        if (!mask.enabled[index]) {
            continue;
        }
        any_enabled = true;
        if (mask.use_pc_input[index]) {
            return false;
        }
    }
    return any_enabled;
}

int PlaybackFramesPerRecordedMapperTick(const TrainerProfile& profile,
                                        const LoadedRecording& recording) {
    const int mapper_rate_hz = recording.metadata.mapper_rate_hz > 0
        ? recording.metadata.mapper_rate_hz
        : std::min(profile.frame_rate_hz, kTrainerMapperReferenceHz);
    if (profile.frame_rate_hz <= 0 ||
        mapper_rate_hz <= 0 ||
        profile.frame_rate_hz % mapper_rate_hz != 0) {
        return 0;
    }
    return std::max(1, profile.frame_rate_hz / mapper_rate_hz);
}

int HidCenteredToTrainerOutput(int centered, TrainerResolutionMode mode) {
    if (mode == TrainerResolutionMode::Gx12_2x) {
        return ClampTrainerResx(centered);
    }
    return ClampTrainerPulse(RoundScaleSigned(centered, 1, 2));
}

void ClearPlaybackChannelMask(PlaybackChannelMask* mask) {
    if (!mask) {
        return;
    }
    mask->enabled.fill(false);
    mask->use_hid.fill(false);
    mask->use_pc_input.fill(false);
}

void EnablePlaybackChannel(PlaybackChannelMask* mask, int channel_1_based) {
    if (mask && channel_1_based >= 1 && channel_1_based <= kSbusChannels) {
        const size_t index = static_cast<size_t>(channel_1_based - 1);
        mask->enabled[index] = true;
        mask->use_hid[index] = false;
        mask->use_pc_input[index] = channel_1_based <= 4;
    }
}

void EnablePlaybackPcInputChannel(PlaybackChannelMask* mask, int channel_1_based) {
    if (mask && channel_1_based >= 1 && channel_1_based <= kSbusChannels) {
        const size_t index = static_cast<size_t>(channel_1_based - 1);
        mask->enabled[index] = true;
        mask->use_hid[index] = false;
        mask->use_pc_input[index] = true;
    }
}

void EnablePlaybackFinalChannel(PlaybackChannelMask* mask, int channel_1_based) {
    if (mask && channel_1_based >= 1 && channel_1_based <= kSbusChannels) {
        const size_t index = static_cast<size_t>(channel_1_based - 1);
        mask->enabled[index] = true;
        mask->use_hid[index] = false;
        mask->use_pc_input[index] = false;
    }
}

void EnablePlaybackHidChannel(PlaybackChannelMask* mask, int channel_1_based) {
    if (mask && channel_1_based >= 1 && channel_1_based <= kSbusChannels) {
        const size_t index = static_cast<size_t>(channel_1_based - 1);
        mask->enabled[index] = true;
        mask->use_pc_input[index] = false;
        mask->use_hid[index] = false;
        if (channel_1_based <= kGx12ChannelCount) {
            mask->use_hid[index] = true;
        }
    }
}

void EnablePlaybackHidRange(PlaybackChannelMask* mask, int first_channel, int last_channel) {
    for (int ch = first_channel; ch <= last_channel; ++ch) {
        EnablePlaybackHidChannel(mask, ch);
    }
}

bool EnablePlaybackChannelToken(const std::string& raw_token, PlaybackChannelMask* mask) {
    if (!mask) {
        return false;
    }
    std::string token = ToLowerAscii(TrimAscii(raw_token));
    if (token.empty()) {
        return true;
    }
    if (token == "none" || token == "off") {
        ClearPlaybackChannelMask(mask);
        return true;
    }
    if (token == "right" || token == "right_stick" || token == "right-stick") {
        EnablePlaybackChannel(mask, 1);
        EnablePlaybackChannel(mask, 2);
        return true;
    }
    if (token == "trainer_right" || token == "trainer-right" ||
        token == "trainer_right_stick" || token == "trainer-right-stick" ||
        token == "final_right" || token == "final-right" ||
        token == "final_right_stick" || token == "final-right-stick" ||
        token == "recorded_right" || token == "recorded-right" ||
        token == "recorded_right_stick" || token == "recorded-right-stick" ||
        token == "trainer_output_right" || token == "trainer-output-right") {
        EnablePlaybackFinalChannel(mask, 1);
        EnablePlaybackFinalChannel(mask, 2);
        return true;
    }
    if (token == "radio_right" || token == "radio-right" ||
        token == "radio_right_stick" || token == "radio-right-stick" ||
        token == "hid_right" || token == "hid-right" ||
        token == "hid_right_stick" || token == "hid-right-stick" ||
        token == "right_gimbal" || token == "right-gimbal" ||
        token == "right_gimbals" || token == "right-gimbals" ||
        token == "gimbal_right" || token == "gimbal-right") {
        EnablePlaybackHidRange(mask, 1, 2);
        return true;
    }
    if (token == "left" || token == "left_stick" || token == "left-stick") {
        EnablePlaybackChannel(mask, 3);
        EnablePlaybackChannel(mask, 4);
        return true;
    }
    if (token == "mouse_left" || token == "mouse-left" ||
        token == "pc_left" || token == "pc-left" ||
        token == "input_left" || token == "input-left" ||
        token == "mouse_left_stick" || token == "mouse-left-stick" ||
        token == "pc_left_stick" || token == "pc-left-stick" ||
        token == "left_input" || token == "left-input") {
        EnablePlaybackPcInputChannel(mask, 3);
        EnablePlaybackPcInputChannel(mask, 4);
        return true;
    }
    if (token == "radio_left" || token == "radio-left" ||
        token == "radio_left_stick" || token == "radio-left-stick" ||
        token == "hid_left" || token == "hid-left" ||
        token == "hid_left_stick" || token == "hid-left-stick" ||
        token == "left_gimbal" || token == "left-gimbal" ||
        token == "left_gimbals" || token == "left-gimbals" ||
        token == "gimbal_left" || token == "gimbal-left") {
        EnablePlaybackHidRange(mask, 3, 4);
        return true;
    }
    if (token == "gimbals" || token == "sticks_hid" || token == "sticks-hid" ||
        token == "radio" || token == "radio_sticks" || token == "radio-sticks" ||
        token == "radio_gimbals" || token == "radio-gimbals" ||
        token == "hid" || token == "hid_sticks" || token == "hid-sticks" ||
        token == "hid_gimbals" || token == "hid-gimbals") {
        EnablePlaybackHidRange(mask, 1, 4);
        return true;
    }
    if (token == "sticks" || token == "all" || token == "ch1-4") {
        for (int ch = 1; ch <= 4; ++ch) {
            EnablePlaybackChannel(mask, ch);
        }
        return true;
    }
    if (token == "all8" || token == "ch1-8") {
        for (int ch = 1; ch <= 8; ++ch) {
            EnablePlaybackChannel(mask, ch);
        }
        return true;
    }
    if (token == "ail" || token == "aileron" || token == "roll") {
        EnablePlaybackChannel(mask, 1);
        return true;
    }
    if (token == "trainer_ail" || token == "trainer-ail" ||
        token == "trainer_aileron" || token == "trainer-aileron" ||
        token == "trainer_roll" || token == "trainer-roll" ||
        token == "final_ail" || token == "final-ail" ||
        token == "final_aileron" || token == "final-aileron" ||
        token == "final_roll" || token == "final-roll" ||
        token == "recorded_ail" || token == "recorded-ail" ||
        token == "recorded_aileron" || token == "recorded-aileron" ||
        token == "recorded_roll" || token == "recorded-roll") {
        EnablePlaybackFinalChannel(mask, 1);
        return true;
    }
    if (token == "radio_ail" || token == "radio-ail" ||
        token == "radio_aileron" || token == "radio-aileron" ||
        token == "radio_roll" || token == "radio-roll" ||
        token == "hid_ail" || token == "hid-ail" ||
        token == "hid_aileron" || token == "hid-aileron" ||
        token == "hid_roll" || token == "hid-roll" ||
        token == "gimbal_ail" || token == "gimbal-ail" ||
        token == "gimbal_roll" || token == "gimbal-roll") {
        EnablePlaybackHidChannel(mask, 1);
        return true;
    }
    if (token == "ele" || token == "elev" || token == "elevator" || token == "pitch") {
        EnablePlaybackChannel(mask, 2);
        return true;
    }
    if (token == "trainer_ele" || token == "trainer-ele" ||
        token == "trainer_elev" || token == "trainer-elev" ||
        token == "trainer_elevator" || token == "trainer-elevator" ||
        token == "trainer_pitch" || token == "trainer-pitch" ||
        token == "final_ele" || token == "final-ele" ||
        token == "final_elev" || token == "final-elev" ||
        token == "final_elevator" || token == "final-elevator" ||
        token == "final_pitch" || token == "final-pitch" ||
        token == "recorded_ele" || token == "recorded-ele" ||
        token == "recorded_elev" || token == "recorded-elev" ||
        token == "recorded_elevator" || token == "recorded-elevator" ||
        token == "recorded_pitch" || token == "recorded-pitch") {
        EnablePlaybackFinalChannel(mask, 2);
        return true;
    }
    if (token == "radio_ele" || token == "radio-ele" ||
        token == "radio_elev" || token == "radio-elev" ||
        token == "radio_elevator" || token == "radio-elevator" ||
        token == "radio_pitch" || token == "radio-pitch" ||
        token == "hid_ele" || token == "hid-ele" ||
        token == "hid_elev" || token == "hid-elev" ||
        token == "hid_elevator" || token == "hid-elevator" ||
        token == "hid_pitch" || token == "hid-pitch" ||
        token == "gimbal_ele" || token == "gimbal-ele" ||
        token == "gimbal_pitch" || token == "gimbal-pitch") {
        EnablePlaybackHidChannel(mask, 2);
        return true;
    }
    if (token == "trainer_thr" || token == "trainer-thr" ||
        token == "trainer_throttle" || token == "trainer-throttle" ||
        token == "final_thr" || token == "final-thr" ||
        token == "final_throttle" || token == "final-throttle" ||
        token == "recorded_thr" || token == "recorded-thr" ||
        token == "recorded_throttle" || token == "recorded-throttle") {
        EnablePlaybackFinalChannel(mask, 3);
        return true;
    }
    if (token == "radio_thr" || token == "radio-thr" ||
        token == "radio_throttle" || token == "radio-throttle" ||
        token == "hid_thr" || token == "hid-thr" ||
        token == "hid_throttle" || token == "hid-throttle" ||
        token == "gimbal_thr" || token == "gimbal-thr" ||
        token == "gimbal_throttle" || token == "gimbal-throttle") {
        EnablePlaybackHidChannel(mask, 3);
        return true;
    }
    if (token == "thr" || token == "throttle") {
        EnablePlaybackChannel(mask, 3);
        return true;
    }
    if (token == "mouse_thr" || token == "mouse-thr" ||
        token == "mouse_throttle" || token == "mouse-throttle" ||
        token == "pc_thr" || token == "pc-thr" ||
        token == "pc_throttle" || token == "pc-throttle" ||
        token == "input_thr" || token == "input-thr" ||
        token == "input_throttle" || token == "input-throttle") {
        EnablePlaybackPcInputChannel(mask, 3);
        return true;
    }
    if (token == "trainer_rud" || token == "trainer-rud" ||
        token == "trainer_rudder" || token == "trainer-rudder" ||
        token == "trainer_yaw" || token == "trainer-yaw" ||
        token == "final_rud" || token == "final-rud" ||
        token == "final_rudder" || token == "final-rudder" ||
        token == "final_yaw" || token == "final-yaw" ||
        token == "recorded_rud" || token == "recorded-rud" ||
        token == "recorded_rudder" || token == "recorded-rudder" ||
        token == "recorded_yaw" || token == "recorded-yaw") {
        EnablePlaybackFinalChannel(mask, 4);
        return true;
    }
    if (token == "radio_rud" || token == "radio-rud" ||
        token == "radio_rudder" || token == "radio-rudder" ||
        token == "radio_yaw" || token == "radio-yaw" ||
        token == "hid_rud" || token == "hid-rud" ||
        token == "hid_rudder" || token == "hid-rudder" ||
        token == "hid_yaw" || token == "hid-yaw" ||
        token == "gimbal_rud" || token == "gimbal-rud" ||
        token == "gimbal_rudder" || token == "gimbal-rudder" ||
        token == "gimbal_yaw" || token == "gimbal-yaw") {
        EnablePlaybackHidChannel(mask, 4);
        return true;
    }
    if (token == "rud" || token == "rudder" || token == "yaw") {
        EnablePlaybackChannel(mask, 4);
        return true;
    }
    if (token == "mouse_rud" || token == "mouse-rud" ||
        token == "mouse_rudder" || token == "mouse-rudder" ||
        token == "mouse_yaw" || token == "mouse-yaw" ||
        token == "pc_rud" || token == "pc-rud" ||
        token == "pc_rudder" || token == "pc-rudder" ||
        token == "pc_yaw" || token == "pc-yaw" ||
        token == "input_rud" || token == "input-rud" ||
        token == "input_rudder" || token == "input-rudder" ||
        token == "input_yaw" || token == "input-yaw") {
        EnablePlaybackPcInputChannel(mask, 4);
        return true;
    }
    if (token.rfind("aux", 0) == 0 && token.size() > 3) {
        int64_t aux = 0;
        if (ParseInt64Strict(token.substr(3), &aux) && aux >= 1 && aux <= 12) {
            EnablePlaybackChannel(mask, static_cast<int>(aux + 4));
            return true;
        }
    }
    static constexpr const char* kHidChannelPrefixes[] = {
        "radio_ch", "radio-ch", "hid_ch", "hid-ch", "gimbal_ch", "gimbal-ch"
    };
    for (const char* prefix : kHidChannelPrefixes) {
        const size_t prefix_len = std::strlen(prefix);
        if (token.rfind(prefix, 0) == 0 && token.size() > prefix_len) {
            int64_t channel = 0;
            if (ParseInt64Strict(token.substr(prefix_len), &channel) &&
                channel >= 1 && channel <= kGx12ChannelCount) {
                EnablePlaybackHidChannel(mask, static_cast<int>(channel));
                return true;
            }
        }
    }
    if (token.rfind("ch", 0) == 0 && token.size() > 2) {
        int64_t channel = 0;
        if (ParseInt64Strict(token.substr(2), &channel) &&
            channel >= 1 && channel <= kSbusChannels) {
            EnablePlaybackChannel(mask, static_cast<int>(channel));
            return true;
        }
    }
    int64_t channel = 0;
    if (ParseInt64Strict(token, &channel) && channel >= 1 && channel <= kSbusChannels) {
        EnablePlaybackChannel(mask, static_cast<int>(channel));
        return true;
    }
    return false;
}

bool ParsePlaybackChannelMask(const std::string& text, PlaybackChannelMask* mask) {
    if (!mask) {
        return false;
    }
    std::string normalized = text;
    for (char& ch : normalized) {
        if (ch == '+' || ch == ';' || ch == '|') {
            ch = ',';
        }
    }
    for (const std::string& token : SplitSimpleCsvLine(normalized)) {
        if (!EnablePlaybackChannelToken(token, mask)) {
            return false;
        }
    }
    return true;
}

PlaybackChannelMask DefaultPlaybackChannelMask() {
    PlaybackChannelMask mask;
    EnablePlaybackFinalChannel(&mask, 1);
    EnablePlaybackFinalChannel(&mask, 2);
    return mask;
}

std::string DescribePlaybackChannelMask(const PlaybackChannelMask& mask) {
    static constexpr const char* pc_names[4] = {"ail", "ele", "thr", "rud"};
    static constexpr const char* final_names[4] = {"trainer_ail", "trainer_ele", "trainer_thr", "trainer_rud"};
    static constexpr const char* hid_names[4] = {"radio_ail", "radio_ele", "radio_thr", "radio_rud"};
    std::string out;
    for (int ch = 0; ch < kSbusChannels; ++ch) {
        if (!mask.enabled[static_cast<size_t>(ch)]) {
            continue;
        }
        if (!out.empty()) {
            out += ",";
        }
        if (ch < 4) {
            const size_t index = static_cast<size_t>(ch);
            if (mask.use_hid[index]) {
                out += hid_names[ch];
            } else if (mask.use_pc_input[index]) {
                out += pc_names[ch];
            } else {
                out += final_names[ch];
            }
        } else {
            out += "ch" + std::to_string(ch + 1);
        }
    }
    return out.empty() ? "none" : out;
}

bool ParsePlaybackBindOptionName(const std::string& lowered, bool* block_live_input) {
    if (!block_live_input) {
        return false;
    }
    if (lowered == "--bind" || lowered == "--slot") {
        *block_live_input = false;
        return true;
    }
    if (lowered == "--bind-block" ||
        lowered == "--bind-block-live" ||
        lowered == "--bind-block-input" ||
        lowered == "--bind-block-radio" ||
        lowered == "--slot-block" ||
        lowered == "--slot-block-live") {
        *block_live_input = true;
        return true;
    }
    return false;
}

int ParseTriggerVirtualKeyName(const std::string& text) {
    const std::string token = ToLowerAscii(TrimAscii(text));
    if (token.empty() || token == "none" || token == "off" || token == "immediate") {
        return 0;
    }
    if (token == "mouse1" || token == "mouse_left" || token == "left_mouse" || token == "lbutton") {
        return VK_LBUTTON;
    }
    if (token == "mouse2" || token == "mouse_right" || token == "right_mouse" || token == "rbutton") {
        return VK_RBUTTON;
    }
    if (token == "mouse3" || token == "mouse_middle" || token == "middle_mouse" || token == "mbutton") {
        return VK_MBUTTON;
    }
    if (token == "mouse4" || token == "xbutton1") {
        return VK_XBUTTON1;
    }
    if (token == "mouse5" || token == "xbutton2") {
        return VK_XBUTTON2;
    }
    return ParseVirtualKeyName(token);
}

bool ParsePlaybackTrigger(const std::string& text, PlaybackTrigger* trigger) {
    if (!trigger) {
        return false;
    }
    const std::string token = ToLowerAscii(TrimAscii(text));
    if (token.empty() || token == "none" || token == "off" || token == "immediate") {
        *trigger = PlaybackTrigger{};
        return true;
    }
    const int vk = ParseTriggerVirtualKeyName(text);
    if (vk <= 0) {
        return false;
    }
    trigger->virtual_key = vk;
    trigger->label = TrimAscii(text);
    return true;
}

bool PlaybackTriggerPressed(const PlaybackTrigger& trigger) {
    return !trigger.Immediate() &&
           (GetAsyncKeyState(trigger.virtual_key) & 0x8000) != 0;
}

int WaitForPlaybackTrigger(const PlaybackTrigger& trigger, HANDLE launcher_stop_event) {
    if (trigger.Immediate()) {
        return 0;
    }
    std::printf("playback armed; press %s to start, Esc to cancel.\n", trigger.label.c_str());
    std::fflush(stdout);
    while (true) {
        if (LauncherStopRequested(launcher_stop_event) || StopKeyDown()) {
            return 1;
        }
        if (PlaybackTriggerPressed(trigger)) {
            while (PlaybackTriggerPressed(trigger)) {
                Sleep(5);
            }
            return 0;
        }
        Sleep(5);
    }
}

bool LooksLikeComPortArgument(const std::string& text) {
    if (_stricmp(text.c_str(), "auto") == 0) {
        return true;
    }
    return text.size() >= 4 &&
           (text[0] == 'C' || text[0] == 'c') &&
           (text[1] == 'O' || text[1] == 'o') &&
           (text[2] == 'M' || text[2] == 'm');
}

bool ConfigureTrainerSerial(HANDLE serial) {
    DCB dcb{};
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(serial, &dcb)) {
        std::fprintf(stderr, "GetCommState failed: le=%lu\n", static_cast<unsigned long>(GetLastError()));
        return false;
    }
    dcb.BaudRate = CBR_115200;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    if (!SetCommState(serial, &dcb)) {
        std::fprintf(stderr, "SetCommState failed: le=%lu\n", static_cast<unsigned long>(GetLastError()));
        return false;
    }

    COMMTIMEOUTS timeouts{};
    timeouts.WriteTotalTimeoutConstant = 20;
    timeouts.WriteTotalTimeoutMultiplier = 1;
    SetCommTimeouts(serial, &timeouts);
    return true;
}

HANDLE OpenTrainerSerialForPlayback(const std::string& requested_port) {
    const std::string resolved_port = ResolveTrainerPortName(requested_port.c_str());
    const std::string path = WindowsComPath(resolved_port.c_str());
    if (path.empty()) {
        std::fprintf(stderr, "--trainer-playback requires a COM port, for example COM3, or auto.\n");
        return INVALID_HANDLE_VALUE;
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
        return INVALID_HANDLE_VALUE;
    }
    if (!ConfigureTrainerSerial(serial)) {
        CloseHandle(serial);
        return INVALID_HANDLE_VALUE;
    }
    return serial;
}

bool WriteTrainerFrame(HANDLE serial, const uint8_t* frame, size_t frame_size) {
    DWORD written = 0;
    return WriteFile(serial,
                     frame,
                     static_cast<DWORD>(frame_size),
                     &written,
                     nullptr) &&
           written == frame_size;
}

bool SendPlaybackNeutral(HANDLE serial, TrainerResolutionMode mode) {
    uint8_t neutral[kSbusFrameSize];
    std::array<int, kSbusChannels> pulses{};
    pulses[2] = TrainerLowValue(mode);
    BuildSbusFrame(pulses, kSbusTrainerMaskMarker, mode, neutral);
    return WriteTrainerFrame(serial, neutral, sizeof(neutral));
}

uint8_t PlaybackActiveFlags(const PlaybackChannelMask& mask, TrainerResolutionMode mode) {
    uint8_t flags = kSbusTrainerMaskMarker;
    if (mask.UsesRightStick()) {
        flags |= kSbusTrainerMaskRightActive;
    }
    if (mask.UsesLeftStick()) {
        flags |= kSbusTrainerMaskLeftActive;
    }
    if (mode == TrainerResolutionMode::Gx12_2x) {
        flags |= kSbusTrainerResolution2x;
    }
    return flags;
}

bool PlaybackPlainChannelEnabled(const PlaybackChannelMask& mask, int channel_index) {
    return channel_index >= 0 &&
           channel_index < kSbusChannels &&
           mask.enabled[static_cast<size_t>(channel_index)] &&
           !mask.use_hid[static_cast<size_t>(channel_index)] &&
           mask.use_pc_input[static_cast<size_t>(channel_index)];
}

bool PlaybackChannelUsesInputInjection(const PlaybackChannelMask& mask,
                                       const TrainerProfile& profile,
                                       int channel_index) {
    if (!PlaybackPlainChannelEnabled(mask, channel_index)) {
        return false;
    }
    if (channel_index == 0 || channel_index == 1) {
        return profile.mouse_right_stick_enabled;
    }
    if (channel_index == 2 || channel_index == 3) {
        return profile.mouse_left.enabled || profile.right_mouse_left.enabled;
    }
    return false;
}

const char* PrimaryPlaybackChannelName(int channel_index) {
    static constexpr const char* names[] = {"ail", "ele", "thr", "rud"};
    return channel_index >= 0 && channel_index < 4 ? names[channel_index] : "ch";
}

const char* PrimaryPlaybackHidChannelName(int channel_index) {
    static constexpr const char* names[] = {
        "radio_ail", "radio_ele", "radio_thr", "radio_rud"
    };
    return channel_index >= 0 && channel_index < 4 ? names[channel_index] : "radio_ch";
}

const char* PrimaryPlaybackFinalChannelName(int channel_index) {
    static constexpr const char* names[] = {
        "trainer_ail", "trainer_ele", "trainer_thr", "trainer_rud"
    };
    return channel_index >= 0 && channel_index < 4 ? names[channel_index] : "trainer_ch";
}

const char* PlaybackPcInputSourceName(int channel_index) {
    return channel_index == 0 || channel_index == 1
        ? "PC right-stick mouse"
        : "PC left-stick mouse/buttons";
}

bool ResolveIntegratedPlaybackMaskForProfile(PlaybackChannelMask* mask,
                                             const TrainerProfile& profile,
                                             const LoadedRecording& recording,
                                             const std::string& recording_path,
                                             bool verbose) {
    if (!mask) {
        return false;
    }

    for (int ch = 0; ch < 4; ++ch) {
        const size_t index = static_cast<size_t>(ch);
        if (!mask->enabled[index] || !mask->use_pc_input[index] ||
            PlaybackChannelUsesInputInjection(*mask, profile, ch)) {
            continue;
        }

        if (recording.has_hid_samples) {
            mask->use_pc_input[index] = false;
            mask->use_hid[index] = true;
            if (verbose) {
                std::fprintf(stderr,
                             "trainer profile playback: %s requested %s replay, "
                             "but the profile has no matching source; using %s from recorded GX12 HID (%s).\n",
                             PrimaryPlaybackChannelName(ch),
                             PlaybackPcInputSourceName(ch),
                             PrimaryPlaybackHidChannelName(ch),
                             recording_path.c_str());
            }
            continue;
        }

        if (verbose) {
            std::fprintf(stderr,
                         "trainer profile playback error: %s requested %s replay, "
                         "but the profile has no matching source and the recording has no GX12 HID samples: %s. "
                         "Use %s for recorded trainer output, or record with a matching PC input source.\n",
                         PrimaryPlaybackChannelName(ch),
                         PlaybackPcInputSourceName(ch),
                         recording_path.c_str(),
                         PrimaryPlaybackFinalChannelName(ch));
        }
        return false;
    }

    return true;
}

bool PlaybackMaskAllEnabledChannelsUseInputInjection(const PlaybackChannelMask& mask,
                                                     const TrainerProfile& profile) {
    bool any_enabled = false;
    for (int ch = 0; ch < kSbusChannels; ++ch) {
        if (!mask.enabled[static_cast<size_t>(ch)]) {
            continue;
        }
        any_enabled = true;
        if (!PlaybackChannelUsesInputInjection(mask, profile, ch)) {
            return false;
        }
    }
    return any_enabled;
}

void AddPlaybackSampleInput(const RecordingSample& sample,
                            const PlaybackChannelMask& mask,
                            const TrainerProfile& profile,
                            PlaybackInputInjection* injection) {
    if (!injection) {
        return;
    }

    if (profile.mouse_right_stick_enabled) {
        const bool roll = PlaybackChannelUsesInputInjection(mask, profile, 0);
        const bool pitch = PlaybackChannelUsesInputInjection(mask, profile, 1);
        if (profile.swap_axes) {
            if (roll) {
                injection->right_dy += sample.right_dy;
            }
            if (pitch) {
                injection->right_dx += sample.right_dx;
            }
        } else {
            if (roll) {
                injection->right_dx += sample.right_dx;
            }
            if (pitch) {
                injection->right_dy += sample.right_dy;
            }
        }
    }

    const bool throttle = PlaybackChannelUsesInputInjection(mask, profile, 2);
    const bool yaw = PlaybackChannelUsesInputInjection(mask, profile, 3);
    if (profile.mouse_left.enabled) {
        if (profile.mouse_left.swap_axes) {
            if (throttle) {
                injection->left_dx += sample.left_dx;
            }
            if (yaw) {
                injection->left_dy += sample.left_dy;
            }
        } else {
            if (throttle) {
                injection->left_dy += sample.left_dy;
            }
            if (yaw) {
                injection->left_dx += sample.left_dx;
            }
        }
    } else if (profile.right_mouse_left.enabled) {
        if (profile.right_mouse_left.swap_axes) {
            if (throttle) {
                injection->right_buttons = sample.right_buttons;
                injection->right_buttons_valid = true;
            }
            if (yaw) {
                injection->right_wheel_y += sample.right_wheel_y;
            }
        } else {
            if (throttle) {
                injection->right_wheel_y += sample.right_wheel_y;
            }
            if (yaw) {
                injection->right_buttons = sample.right_buttons;
                injection->right_buttons_valid = true;
            }
        }
    }
}

PlaybackInputInjection ConsumePlaybackInputInjection(const PlaybackBankSlot& slot,
                                                     const TrainerProfile& profile,
                                                     int64_t playback_elapsed_us,
                                                     int64_t playback_base_us,
                                                     size_t* sample_index,
                                                     bool* have_mapper_tick,
                                                     uint32_t* last_mapper_tick) {
    // PC-input playback must follow the mapper update stream, not frame
    // timestamps. Recorded frame timing can drift when the live mapper misses
    // scheduler slots; replaying by wall time would insert no-input return
    // ticks that never happened in the original mapper state.
    (void)playback_elapsed_us;
    (void)playback_base_us;
    PlaybackInputInjection injection;
    if (!sample_index || !have_mapper_tick || !last_mapper_tick) {
        return injection;
    }

    const std::vector<RecordingSample>& samples = slot.recording.samples;
    while (*sample_index < samples.size()) {
        const RecordingSample& sample = samples[*sample_index];
        if (!*have_mapper_tick || sample.mapper_tick != *last_mapper_tick) {
            AddPlaybackSampleInput(sample, slot.spec.mask, profile, &injection);
            *last_mapper_tick = sample.mapper_tick;
            *have_mapper_tick = true;
            ++injection.mapper_ticks;
            ++(*sample_index);
            while (*sample_index < samples.size() &&
                   samples[*sample_index].mapper_tick == *last_mapper_tick) {
                ++(*sample_index);
            }
            break;
        }
        ++(*sample_index);
    }
    return injection;
}

void ApplyPlaybackInputInjection(const PlaybackInputInjection& injection,
                                 int64_t* right_dx,
                                 int64_t* right_dy,
                                 int64_t* right_wheel_x,
                                 int64_t* right_wheel_y,
                                 uint32_t* right_buttons,
                                 int64_t* left_dx,
                                 int64_t* left_dy,
                                 int64_t* left_wheel_x,
                                 int64_t* left_wheel_y,
                                 uint32_t* left_buttons) {
    if (right_dx) *right_dx += injection.right_dx;
    if (right_dy) *right_dy += injection.right_dy;
    if (right_wheel_x) *right_wheel_x += injection.right_wheel_x;
    if (right_wheel_y) *right_wheel_y += injection.right_wheel_y;
    if (right_buttons && injection.right_buttons_valid) {
        *right_buttons |= injection.right_buttons;
    }
    if (left_dx) *left_dx += injection.left_dx;
    if (left_dy) *left_dy += injection.left_dy;
    if (left_wheel_x) *left_wheel_x += injection.left_wheel_x;
    if (left_wheel_y) *left_wheel_y += injection.left_wheel_y;
    if (left_buttons && injection.left_buttons_valid) {
        *left_buttons |= injection.left_buttons;
    }
}

void ClearPlaybackLiveInputForMask(const PlaybackBankSlotSpec& spec,
                                   const TrainerProfile& profile,
                                   int64_t* right_dx,
                                   int64_t* right_dy,
                                   int64_t* right_wheel_y,
                                   uint32_t* right_buttons,
                                   int64_t* left_dx,
                                   int64_t* left_dy) {
    const PlaybackChannelMask& mask = spec.mask;
    if (profile.mouse_right_stick_enabled) {
        const bool roll = mask.enabled[0];
        const bool pitch = mask.enabled[1];
        if (profile.swap_axes) {
            if (roll && right_dy) *right_dy = 0;
            if (pitch && right_dx) *right_dx = 0;
        } else {
            if (roll && right_dx) *right_dx = 0;
            if (pitch && right_dy) *right_dy = 0;
        }
    }

    const bool throttle = mask.enabled[2];
    const bool yaw = mask.enabled[3];
    if (profile.mouse_left.enabled) {
        if (profile.mouse_left.swap_axes) {
            if (throttle && left_dx) *left_dx = 0;
            if (yaw && left_dy) *left_dy = 0;
        } else {
            if (throttle && left_dy) *left_dy = 0;
            if (yaw && left_dx) *left_dx = 0;
        }
    } else if (profile.right_mouse_left.enabled) {
        if (profile.right_mouse_left.swap_axes) {
            if (throttle && right_buttons) *right_buttons = 0;
            if (yaw && right_wheel_y) *right_wheel_y = 0;
        } else {
            if (throttle && right_wheel_y) *right_wheel_y = 0;
            if (yaw && right_buttons) *right_buttons = 0;
        }
    }
}

std::array<int, kSbusChannels> BuildPlaybackPulses(const RecordingSample& sample,
                                                   const PlaybackChannelMask& mask,
                                                   TrainerResolutionMode mode) {
    std::array<int, kSbusChannels> pulses{};
    pulses[2] = TrainerLowValue(mode);
    for (int ch = 0; ch < kSbusChannels; ++ch) {
        if (!mask.enabled[static_cast<size_t>(ch)]) {
            continue;
        }

        int value = sample.final_channels[static_cast<size_t>(ch)];
        if (sample.hid_valid &&
            mask.use_hid[static_cast<size_t>(ch)] &&
            ch < kGx12ChannelCount) {
            value = HidCenteredToTrainerOutput(sample.hid_channels[static_cast<size_t>(ch)], mode);
        }
        pulses[static_cast<size_t>(ch)] = ClampTrainerOutput(value, mode);
    }
    return pulses;
}

bool ValidatePlaybackRecordingForCurrentBuild(const LoadedRecording& recording,
                                              const char* recording_path) {
    if (recording.metadata.resolution_mode == TrainerResolutionMode::Gx12_2x &&
        !kGx12Resolution2xBuild) {
        std::fprintf(stderr,
                     "--trainer-playback error: gx12_2x recording requires a 2x-capable gx12mouse build: %s\n",
                     recording_path ? recording_path : "(recording)");
        return false;
    }
    return true;
}

struct PlaybackRunControl {
    HANDLE launcher_stop_event = nullptr;
    const PlaybackTrigger* stop_trigger = nullptr;
    bool stop_trigger_was_down = false;
    bool stopped_by_trigger = false;
};

bool PlaybackRunShouldStop(PlaybackRunControl* control) {
    const HANDLE launcher_stop_event = control ? control->launcher_stop_event : nullptr;
    if (LauncherStopRequested(launcher_stop_event) || StopKeyDown()) {
        return true;
    }

    if (!control || !control->stop_trigger) {
        return false;
    }

    const bool trigger_down = PlaybackTriggerPressed(*control->stop_trigger);
    if (!trigger_down) {
        control->stop_trigger_was_down = false;
        return false;
    }
    if (control->stop_trigger_was_down) {
        return false;
    }

    control->stop_trigger_was_down = true;
    control->stopped_by_trigger = true;
    return true;
}

bool PlayLoadedRecordingOnce(HANDLE serial,
                             const LoadedRecording& recording,
                             const PlaybackChannelMask& mask,
                             PlaybackRunControl* control,
                             uint64_t* frames_sent) {
    using clock = std::chrono::steady_clock;
    if (control) {
        control->stopped_by_trigger = false;
    }

    bool stop_requested = false;
    const int64_t base_us = recording.samples.front().time_us;
    const auto playback_start = clock::now();
    for (const RecordingSample& sample : recording.samples) {
        if (PlaybackRunShouldStop(control)) {
            stop_requested = true;
            break;
        }

        const int64_t offset_us = std::max<int64_t>(0, sample.time_us - base_us);
        const auto target = playback_start + std::chrono::microseconds(offset_us);
        while (clock::now() < target) {
            if (PlaybackRunShouldStop(control)) {
                stop_requested = true;
                break;
            }
            const auto remaining =
                std::chrono::duration_cast<std::chrono::microseconds>(target - clock::now()).count();
            if (remaining > 2000) {
                std::this_thread::sleep_for(std::chrono::microseconds(std::min<int64_t>(remaining / 2, 2000)));
            } else {
                Sleep(0);
            }
        }
        if (stop_requested) {
            break;
        }

        const std::array<int, kSbusChannels> pulses =
            BuildPlaybackPulses(sample, mask, recording.metadata.resolution_mode);
        uint8_t frame[kSbusFrameSize];
        BuildSbusFrame(pulses,
                       PlaybackActiveFlags(mask, recording.metadata.resolution_mode),
                       recording.metadata.resolution_mode,
                       frame);
        if (!WriteTrainerFrame(serial, frame, sizeof(frame))) {
            std::fprintf(stderr,
                         "WriteFile failed after %llu playback frame(s): le=%lu\n",
                         static_cast<unsigned long long>(frames_sent ? *frames_sent : 0),
                         static_cast<unsigned long>(GetLastError()));
            stop_requested = true;
            break;
        }
        if (frames_sent) {
            ++(*frames_sent);
        }
    }
    return stop_requested;
}

int RunRecordingInfo(const char* path) {
    LoadedRecording recording;
    std::string error;
    if (!LoadRecordingFile(path, &recording, &error)) {
        std::fprintf(stderr, "--recording-info failed: %s\n", error.c_str());
        return 1;
    }

    const int64_t first_us = recording.samples.front().time_us;
    const int64_t last_us = recording.samples.back().time_us;
    const double duration_s = std::max<int64_t>(0, last_us - first_us) / 1000000.0;
    std::printf("\n--recording-info: %s\n", path);
    std::printf("  schema=%d app=%s profile=%s hash=%s\n",
                recording.metadata.schema,
                recording.metadata.app_version.c_str(),
                recording.metadata.profile_name.c_str(),
                recording.metadata.profile_hash.c_str());
    std::printf("  frame_rate=%d mapper_rate=%d resolution=%s duration=%.3fs samples=%zu hid_rows=%llu hid_samples=%s buffer=%s\n",
                recording.metadata.trainer_frame_rate_hz,
                recording.metadata.mapper_rate_hz,
                TrainerResolutionModeName(recording.metadata.resolution_mode),
                duration_s,
                recording.samples.size(),
                static_cast<unsigned long long>(recording.hid_rows),
                recording.has_hid_samples ? "yes" : "no",
                recording.metadata.recording_buffer.c_str());
    std::printf("  default playback channels: trainer_right uses recorded final ch1/ch2. Use --channels=ail,ele or ail,ele,thr,rud to rerun PC input tracks, or gimbals for radio HID ch1-ch4.\n");
    return 0;
}

int RunTrainerPlayback(const char* recording_path,
                       const std::string& port,
                       bool loop,
                       const PlaybackChannelMask& mask,
                       const PlaybackTrigger& trigger) {
    if (!mask.Any()) {
        std::fprintf(stderr, "--trainer-playback needs at least one playback channel.\n");
        return 2;
    }

    LoadedRecording recording;
    std::string error;
    if (!LoadRecordingFile(recording_path, &recording, &error)) {
        std::fprintf(stderr, "--trainer-playback failed: %s\n", error.c_str());
        return 1;
    }
    if (!ValidatePlaybackRecordingForCurrentBuild(recording, recording_path)) {
        return 2;
    }

    g_stop_virtual_key.store(VK_ESCAPE, std::memory_order_release);
    HANDLE launcher_stop_event = CreateEventW(nullptr, TRUE, FALSE, kLauncherStopEventName);
    HANDLE serial = OpenTrainerSerialForPlayback(port);
    if (serial == INVALID_HANDLE_VALUE) {
        if (launcher_stop_event) CloseHandle(launcher_stop_event);
        return 1;
    }

    (void)SendPlaybackNeutral(serial, recording.metadata.resolution_mode);
    std::printf("\n--trainer-playback: recording=%s samples=%zu resolution=%s channels=%s mode=%s trigger=%s\n",
                recording_path,
                recording.samples.size(),
                TrainerResolutionModeName(recording.metadata.resolution_mode),
                DescribePlaybackChannelMask(mask).c_str(),
                loop ? "loop" : "once",
                trigger.label.c_str());
    if (mask.UsesThrottleOrYaw()) {
        std::printf("WARNING: playback includes recorded throttle/yaw. Keep this sim/bench-only and keep the stop key reachable.\n");
        if (!recording.has_hid_samples) {
            std::printf("WARNING: recording has no HID samples; ch3/ch4 will use final trainer rows from the recording.\n");
        }
    }
    if (mask.UsesHidRightStick() && !recording.has_hid_samples) {
        std::printf("WARNING: radio right-gimbal playback was requested, but this recording has no HID samples; ch1/ch2 will fall back to final trainer rows.\n");
    }
    std::printf("Esc or launcher Stop sends neutral and exits.\n");

    if (WaitForPlaybackTrigger(trigger, launcher_stop_event) != 0) {
        (void)SendPlaybackNeutral(serial, recording.metadata.resolution_mode);
        CloseHandle(serial);
        if (launcher_stop_event) CloseHandle(launcher_stop_event);
        return 0;
    }

    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    uint64_t frames_sent = 0;
    bool stop_requested = false;
    PlaybackRunControl playback_control;
    playback_control.launcher_stop_event = launcher_stop_event;
    do {
        stop_requested = PlayLoadedRecordingOnce(serial, recording, mask, &playback_control, &frames_sent);
    } while (loop && !stop_requested);

    (void)SendPlaybackNeutral(serial, recording.metadata.resolution_mode);
    CloseHandle(serial);
    if (launcher_stop_event) CloseHandle(launcher_stop_event);
    std::printf("playback summary: frames=%llu stopped=%s\n",
                static_cast<unsigned long long>(frames_sent),
                stop_requested ? "yes" : "no");
    return 0;
}

int RunTrainerPlaybackBank(const std::string& port,
                           bool loop,
                           const std::vector<PlaybackBankSlotSpec>& specs) {
    if (specs.empty()) {
        std::fprintf(stderr, "--trainer-playback-bank needs at least one --bind KEY CHANNELS RECORDING slot.\n");
        return 2;
    }
    if (specs.size() > kMaxPlaybackBankSlots) {
        std::fprintf(stderr,
                     "--trainer-playback-bank supports at most %zu binding slots.\n",
                     kMaxPlaybackBankSlots);
        return 2;
    }

    std::vector<PlaybackBankSlot> slots;
    slots.reserve(specs.size());
    for (const PlaybackBankSlotSpec& spec : specs) {
        if (spec.trigger.Immediate()) {
            std::fprintf(stderr,
                         "--trainer-playback-bank bind for %s needs a hotkey trigger; immediate is only valid for single playback.\n",
                         spec.recording_path.c_str());
            return 2;
        }
        if (!spec.mask.Any()) {
            std::fprintf(stderr,
                         "--trainer-playback-bank bind for %s needs at least one channel.\n",
                         spec.recording_path.c_str());
            return 2;
        }

        PlaybackBankSlot slot;
        slot.spec = spec;
        std::string error;
        if (!LoadRecordingFile(spec.recording_path.c_str(), &slot.recording, &error)) {
            std::fprintf(stderr,
                         "--trainer-playback-bank failed loading %s: %s\n",
                         spec.recording_path.c_str(),
                         error.c_str());
            return 1;
        }
        if (!ValidatePlaybackRecordingForCurrentBuild(slot.recording, spec.recording_path.c_str())) {
            return 2;
        }
        slots.push_back(std::move(slot));
    }

    g_stop_virtual_key.store(VK_ESCAPE, std::memory_order_release);
    HANDLE launcher_stop_event = CreateEventW(nullptr, TRUE, FALSE, kLauncherStopEventName);
    HANDLE serial = OpenTrainerSerialForPlayback(port);
    if (serial == INVALID_HANDLE_VALUE) {
        if (launcher_stop_event) CloseHandle(launcher_stop_event);
        return 1;
    }

    (void)SendPlaybackNeutral(serial, slots.front().recording.metadata.resolution_mode);
    std::printf("\n--trainer-playback-bank: slots=%zu port=%s mode=%s\n",
                slots.size(),
                port.c_str(),
                loop ? "loop" : "once");
    for (size_t index = 0; index < slots.size(); ++index) {
        const PlaybackBankSlot& slot = slots[index];
        std::printf("  [%zu] key=%s channels=%s live_input=%s samples=%zu resolution=%s recording=%s\n",
                    index + 1,
                    slot.spec.trigger.label.c_str(),
                    DescribePlaybackChannelMask(slot.spec.mask).c_str(),
                    slot.spec.block_live_input ? "blocked" : "pass",
                    slot.recording.samples.size(),
                    TrainerResolutionModeName(slot.recording.metadata.resolution_mode),
                    slot.spec.recording_path.c_str());
        if (slot.spec.mask.UsesThrottleOrYaw()) {
            std::printf("      WARNING: bind includes recorded throttle/yaw; keep this sim/bench-only.\n");
            if (!slot.recording.has_hid_samples) {
                std::printf("      WARNING: recording has no HID samples; ch3/ch4 will use final trainer rows.\n");
            }
        }
        if (slot.spec.mask.UsesHidRightStick() && !slot.recording.has_hid_samples) {
            std::printf("      WARNING: radio right-gimbal playback requested, but recording has no HID samples; ch1/ch2 will fall back to final trainer rows.\n");
        }
    }
    std::printf("Esc or launcher Stop sends neutral and exits. Press a bind key to play that recording; press the active bind again to stop it.\n");
    std::fflush(stdout);

    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    std::vector<bool> previous_down(slots.size(), false);
    uint64_t total_frames = 0;
    uint64_t playbacks_started = 0;
    bool stop_requested = false;
    while (!stop_requested) {
        if (LauncherStopRequested(launcher_stop_event) || StopKeyDown()) {
            stop_requested = true;
            break;
        }

        for (size_t index = 0; index < slots.size(); ++index) {
            const bool down = PlaybackTriggerPressed(slots[index].spec.trigger);
            if (!down) {
                previous_down[index] = false;
                continue;
            }
            if (previous_down[index]) {
                continue;
            }

            previous_down[index] = true;
            const PlaybackBankSlot& slot = slots[index];
            ++playbacks_started;
            std::printf("playback bind %zu start: key=%s recording=%s channels=%s live_input=%s\n",
                        index + 1,
                        slot.spec.trigger.label.c_str(),
                        slot.spec.recording_path.c_str(),
                        DescribePlaybackChannelMask(slot.spec.mask).c_str(),
                        slot.spec.block_live_input ? "blocked" : "pass");
            std::fflush(stdout);

            PlaybackRunControl playback_control;
            playback_control.launcher_stop_event = launcher_stop_event;
            playback_control.stop_trigger = &slot.spec.trigger;
            playback_control.stop_trigger_was_down = true;
            do {
                const bool playback_stop_requested = PlayLoadedRecordingOnce(
                    serial,
                    slot.recording,
                    slot.spec.mask,
                    &playback_control,
                    &total_frames);
                if (playback_control.stopped_by_trigger) {
                    break;
                }
                if (playback_stop_requested) {
                    stop_requested = true;
                    break;
                }
            } while (loop && !stop_requested);

            (void)SendPlaybackNeutral(serial, slot.recording.metadata.resolution_mode);
            std::printf("playback bind %zu end: total_frames=%llu stopped=%s\n",
                        index + 1,
                        static_cast<unsigned long long>(total_frames),
                        stop_requested ? "yes" : (playback_control.stopped_by_trigger ? "bind" : "no"));
            std::fflush(stdout);
            break;
        }

        Sleep(5);
    }

    (void)SendPlaybackNeutral(serial, slots.front().recording.metadata.resolution_mode);
    CloseHandle(serial);
    if (launcher_stop_event) CloseHandle(launcher_stop_event);
    std::printf("playback bank summary: playbacks=%llu frames=%llu stopped=%s\n",
                static_cast<unsigned long long>(playbacks_started),
                static_cast<unsigned long long>(total_frames),
                stop_requested ? "yes" : "no");
    return 0;
}

struct RecordingPlaybackAuditResult {
    size_t samples = 0;
    uint32_t mapper_ticks = 0;
    int current_max_abs_error = 0;
    int legacy_timestamp_max_abs_error = 0;
    uint32_t legacy_timestamp_empty_ticks = 0;
};

struct AuditRightStickState {
    double roll_position = 0.0;
    double pitch_position = 0.0;
    double filtered_roll = 0.0;
    double filtered_pitch = 0.0;
    ElasticAxisState roll_elastic_state;
    ElasticAxisState pitch_elastic_state;
    RightStickSharedState shared_state;
    int roll_pulse = 0;
    int pitch_pulse = 0;
};

std::array<int, 2> StepAuditRightStick(const TrainerProfile& profile,
                                       int64_t dx,
                                       int64_t dy,
                                       int mapper_rate_hz,
                                       AuditRightStickState* state) {
    if (!state) {
        return {0, 0};
    }

    const double mapper_dt = 1.0 / static_cast<double>(mapper_rate_hz);
    const double gain_scale = TrainerRateGainScale(mapper_rate_hz);
    const double roll_source = profile.swap_axes
        ? static_cast<double>(-dy)
        : static_cast<double>(dx);
    const double pitch_source = profile.swap_axes
        ? static_cast<double>(dx)
        : static_cast<double>(-dy);
    const bool use_position_mapper = RightStickNeedsPositionMapper(profile);
    if (use_position_mapper) {
        const double combined_return_step = profile.constant_return_enabled
            ? profile.constant_return_rate / static_cast<double>(mapper_rate_hz)
            : 0.0;
        const double elastic_return_coefficient = profile.elastic_return_enabled
            ? profile.elastic_return_coefficient
            : 0.0;
        ShapeRightStickPositionPulses(roll_source,
                                      pitch_source,
                                      gain_scale,
                                      combined_return_step,
                                      elastic_return_coefficient,
                                      mapper_dt,
                                      combined_return_step > 0.0 ||
                                          elastic_return_coefficient > 0.0,
                                      &state->roll_position,
                                      &state->pitch_position,
                                      &state->roll_elastic_state,
                                      &state->pitch_elastic_state,
                                      &state->shared_state,
                                      profile,
                                      &state->roll_pulse,
                                      &state->pitch_pulse);
    } else {
        double filtered_roll_source = roll_source;
        double filtered_pitch_source = pitch_source;
        ApplyRightStickInputPreprocessors(&filtered_roll_source,
                                          &filtered_pitch_source,
                                          mapper_dt,
                                          profile,
                                          &state->shared_state);
        const double input_gain = UpdateAdaptiveInputGain(filtered_roll_source,
                                                          filtered_pitch_source,
                                                          mapper_dt,
                                                          profile,
                                                          &state->shared_state);
        state->roll_pulse = ShapeTrainerPulse(filtered_roll_source,
                                              profile.roll_gain * gain_scale * input_gain,
                                              profile.invert_roll,
                                              &state->filtered_roll,
                                              profile);
        state->pitch_pulse = ShapeTrainerPulse(filtered_pitch_source,
                                               profile.pitch_gain * gain_scale * input_gain,
                                               profile.invert_pitch,
                                               &state->filtered_pitch,
                                               profile);
    }
    return {state->roll_pulse, state->pitch_pulse};
}

PlaybackInputInjection ConsumeTimestampPlaybackInputInjectionForAudit(const PlaybackBankSlot& slot,
                                                                      const TrainerProfile& profile,
                                                                      int64_t playback_elapsed_us,
                                                                      int64_t playback_base_us,
                                                                      size_t* sample_index,
                                                                      bool* have_mapper_tick,
                                                                      uint32_t* last_mapper_tick) {
    PlaybackInputInjection injection;
    if (!sample_index || !have_mapper_tick || !last_mapper_tick) {
        return injection;
    }

    const std::vector<RecordingSample>& samples = slot.recording.samples;
    while (*sample_index < samples.size()) {
        const RecordingSample& sample = samples[*sample_index];
        const int64_t sample_offset_us =
            std::max<int64_t>(0, sample.time_us - playback_base_us);
        if (sample_offset_us > playback_elapsed_us) {
            break;
        }

        if (!*have_mapper_tick || sample.mapper_tick != *last_mapper_tick) {
            AddPlaybackSampleInput(sample, slot.spec.mask, profile, &injection);
            *last_mapper_tick = sample.mapper_tick;
            *have_mapper_tick = true;
            ++injection.mapper_ticks;
        }
        ++(*sample_index);
    }
    return injection;
}

bool RunSyntheticRecordingPlaybackAudit(RecordingPlaybackAuditResult* result,
                                        std::string* error) {
    const std::filesystem::path recording_path =
        std::filesystem::path("logs") / "recording-playback-audit.gx12rec.csv";
    const std::filesystem::path deviation_path =
        std::filesystem::path("logs") / "recording-playback-audit-deviation.csv";
    constexpr int kAuditMapperRateHz = 1000;
    constexpr int kAuditFrameRateHz = 8000;
    constexpr int kAuditFramesPerMapperTick = kAuditFrameRateHz / kAuditMapperRateHz;
    constexpr int kAuditMapperTicks = 360;
    constexpr int64_t kAuditFrameSpacingUs = 1000000 / kAuditFrameRateHz;
    constexpr int64_t kAuditRecordedMapperSpacingUs = 1100;

    TrainerProfile profile;
    profile.name = "recording-playback-audit";
    profile.source_file = "recording-playback-audit";
    profile.frame_rate_hz = kAuditFrameRateHz;
    profile.resolution_mode = TrainerResolutionMode::Gx12_2x;
    profile.mouse_right_stick_enabled = true;
    profile.roll_gain = 0.3;
    profile.pitch_gain = 0.3;
    profile.max_output = 512;
    profile.deadband = 0;
    profile.expo = 0.0;
    profile.smoothing = 0.0;
    profile.invert_roll = false;
    profile.invert_pitch = false;
    profile.swap_axes = false;
    profile.constant_return_enabled = false;
    profile.constant_return_rate = 0.0;
    profile.elastic_return_enabled = true;
    profile.elastic_return_mode = ElasticReturnMode::Linear;
    profile.elastic_return_coefficient = 12.0;
    profile.elastic_return_curve = 0.0;
    profile.output_curve = OutputCurveMode::Expo;
    profile.position_model = PositionModel::Integrator;
    profile.input_gain_mode = InputGainMode::Flat;
    profile.gate_shape = GateShape::Axis;
    profile.input_filter = InputFilterMode::Off;
    profile.despike_enabled = false;
    profile.despike_count_enabled = false;

    Gx12RecordingHidCapture fake_hid;
    RecordingCsvWriter writer;
    if (!writer.Open(profile, recording_path.string().c_str(), kAuditMapperRateHz, fake_hid)) {
        if (error) *error = "failed to open synthetic audit recording";
        return false;
    }

    std::vector<int64_t> input_dx;
    std::vector<int64_t> input_dy;
    std::vector<std::array<int, 2>> expected_outputs;
    input_dx.reserve(kAuditMapperTicks);
    input_dy.reserve(kAuditMapperTicks);
    expected_outputs.reserve(kAuditMapperTicks);

    AuditRightStickState expected_state;
    for (int tick = 0; tick < kAuditMapperTicks; ++tick) {
        const int64_t dx = tick < 130 ? 4 : (tick >= 210 && tick < 285 ? -3 : 0);
        const int64_t dy = (tick >= 45 && tick < 115) ? 2 : 0;
        const std::array<int, 2> expected =
            StepAuditRightStick(profile, dx, dy, kAuditMapperRateHz, &expected_state);
        input_dx.push_back(dx);
        input_dy.push_back(dy);
        expected_outputs.push_back(expected);

        std::array<int, kSbusChannels> final_channels{};
        final_channels[0] = expected[0];
        final_channels[1] = expected[1];
        final_channels[2] = TrainerLowValue(profile.resolution_mode);
        final_channels[3] = 0;
        for (int frame = 0; frame < kAuditFramesPerMapperTick; ++frame) {
            const int64_t time_us =
                (static_cast<int64_t>(tick) * kAuditRecordedMapperSpacingUs) +
                (static_cast<int64_t>(frame) * kAuditFrameSpacingUs);
            writer.WriteSample(time_us,
                               static_cast<uint32_t>(tick * kAuditFramesPerMapperTick + frame + 1),
                               static_cast<uint32_t>(tick + 1),
                               static_cast<uint64_t>(tick + 1),
                               static_cast<uint64_t>(tick + 1),
                               0,
                               dx,
                               dy,
                               0,
                               0,
                               0,
                               0,
                               0,
                               0,
                               0,
                               0,
                               final_channels,
                               fake_hid,
                               kSbusTrainerMaskMarker,
                               0);
        }
    }
    writer.Close();

    LoadedRecording loaded;
    std::string load_error;
    if (!LoadRecordingFile(recording_path.string().c_str(), &loaded, &load_error)) {
        if (error) *error = "failed to load synthetic audit recording: " + load_error;
        return false;
    }

    PlaybackChannelMask right_mask;
    ClearPlaybackChannelMask(&right_mask);
    if (!ParsePlaybackChannelMask("ail,ele", &right_mask)) {
        if (error) *error = "failed to parse synthetic audit channel mask";
        return false;
    }

    PlaybackBankSlot slot;
    slot.spec.mask = right_mask;
    slot.recording = loaded;
    slot.loaded = true;

    std::vector<std::array<int, 2>> current_outputs;
    std::vector<std::array<int, 2>> legacy_outputs;
    current_outputs.reserve(kAuditMapperTicks);
    legacy_outputs.reserve(kAuditMapperTicks);

    int current_max_abs_error = 0;
    size_t current_index = 0;
    bool current_have_tick = false;
    uint32_t current_last_tick = 0;
    AuditRightStickState current_state;
    for (int tick = 0; tick < kAuditMapperTicks; ++tick) {
        const PlaybackInputInjection injection =
            ConsumePlaybackInputInjection(slot,
                                          profile,
                                          static_cast<int64_t>(tick) * 1000,
                                          loaded.samples.front().time_us,
                                          &current_index,
                                          &current_have_tick,
                                          &current_last_tick);
        const std::array<int, 2> output =
            StepAuditRightStick(profile,
                                injection.right_dx,
                                injection.right_dy,
                                kAuditMapperRateHz,
                                &current_state);
        current_outputs.push_back(output);
        current_max_abs_error = std::max(current_max_abs_error,
                                         std::abs(output[0] - expected_outputs[tick][0]));
        current_max_abs_error = std::max(current_max_abs_error,
                                         std::abs(output[1] - expected_outputs[tick][1]));
    }

    int legacy_max_abs_error = 0;
    uint32_t legacy_empty_ticks = 0;
    size_t legacy_index = 0;
    bool legacy_have_tick = false;
    uint32_t legacy_last_tick = 0;
    AuditRightStickState legacy_state;
    for (int tick = 0; tick < kAuditMapperTicks; ++tick) {
        const PlaybackInputInjection injection =
            ConsumeTimestampPlaybackInputInjectionForAudit(slot,
                                                           profile,
                                                           static_cast<int64_t>(tick) * 1000,
                                                           loaded.samples.front().time_us,
                                                           &legacy_index,
                                                           &legacy_have_tick,
                                                           &legacy_last_tick);
        if (injection.mapper_ticks == 0 && legacy_index < loaded.samples.size()) {
            ++legacy_empty_ticks;
        }
        const std::array<int, 2> output =
            StepAuditRightStick(profile,
                                injection.right_dx,
                                injection.right_dy,
                                kAuditMapperRateHz,
                                &legacy_state);
        legacy_outputs.push_back(output);
        legacy_max_abs_error = std::max(legacy_max_abs_error,
                                        std::abs(output[0] - expected_outputs[tick][0]));
        legacy_max_abs_error = std::max(legacy_max_abs_error,
                                        std::abs(output[1] - expected_outputs[tick][1]));
    }

    std::ofstream deviation(deviation_path, std::ios::out | std::ios::trunc);
    if (!deviation) {
        if (error) *error = "failed to open synthetic audit deviation output";
        return false;
    }
    deviation << "tick,input_dx,input_dy,expected_roll,expected_pitch,"
                 "current_roll,current_pitch,legacy_timestamp_roll,legacy_timestamp_pitch,"
                 "current_abs_error,legacy_timestamp_abs_error\n";
    for (int tick = 0; tick < kAuditMapperTicks; ++tick) {
        const int current_error = std::max(
            std::abs(current_outputs[tick][0] - expected_outputs[tick][0]),
            std::abs(current_outputs[tick][1] - expected_outputs[tick][1]));
        const int legacy_error = std::max(
            std::abs(legacy_outputs[tick][0] - expected_outputs[tick][0]),
            std::abs(legacy_outputs[tick][1] - expected_outputs[tick][1]));
        deviation << tick << ','
                  << input_dx[tick] << ','
                  << input_dy[tick] << ','
                  << expected_outputs[tick][0] << ','
                  << expected_outputs[tick][1] << ','
                  << current_outputs[tick][0] << ','
                  << current_outputs[tick][1] << ','
                  << legacy_outputs[tick][0] << ','
                  << legacy_outputs[tick][1] << ','
                  << current_error << ','
                  << legacy_error << '\n';
    }

    if (result) {
        result->samples = loaded.samples.size();
        result->mapper_ticks = kAuditMapperTicks;
        result->current_max_abs_error = current_max_abs_error;
        result->legacy_timestamp_max_abs_error = legacy_max_abs_error;
        result->legacy_timestamp_empty_ticks = legacy_empty_ticks;
    }
    return true;
}

constexpr uint64_t kFnv1aOffset = 1469598103934665603ULL;
constexpr uint64_t kFnv1aPrime = 1099511628211ULL;

void HashAppendByte(uint64_t* hash, uint8_t value) {
    if (!hash) {
        return;
    }
    *hash ^= value;
    *hash *= kFnv1aPrime;
}

void HashAppendU64(uint64_t* hash, uint64_t value) {
    for (int shift = 0; shift < 64; shift += 8) {
        HashAppendByte(hash, static_cast<uint8_t>((value >> shift) & 0xFFU));
    }
}

void HashAppendI64(uint64_t* hash, int64_t value) {
    HashAppendU64(hash, static_cast<uint64_t>(value));
}

void HashAppendPulses(uint64_t* hash,
                      const std::array<int, kSbusChannels>& pulses,
                      uint8_t flags) {
    for (int value : pulses) {
        HashAppendI64(hash, static_cast<int64_t>(value));
    }
    HashAppendByte(hash, flags);
}

bool RightReplayFieldsMatchForMapperTick(const RecordingSample& a,
                                         const RecordingSample& b) {
    return a.right_dx == b.right_dx &&
           a.right_dy == b.right_dy &&
           a.final_channels[0] == b.final_channels[0] &&
           a.final_channels[1] == b.final_channels[1];
}

bool LeftReplayFieldsMatchForMapperTick(const RecordingSample& a,
                                        const RecordingSample& b) {
    return a.left_dx == b.left_dx &&
           a.left_dy == b.left_dy &&
           a.right_wheel_y == b.right_wheel_y &&
           a.right_buttons == b.right_buttons &&
           a.final_channels[2] == b.final_channels[2] &&
           a.final_channels[3] == b.final_channels[3];
}

bool HidReplayFieldsMatchForMapperTick(const RecordingSample& a,
                                       const RecordingSample& b) {
    return a.hid_valid == b.hid_valid &&
           (!a.hid_valid ||
            (a.hid_buttons == b.hid_buttons &&
             a.hid_channels == b.hid_channels));
}

struct RecordingDeterminismStaticSummary {
    size_t samples = 0;
    size_t unique_mapper_ticks = 0;
    uint64_t mapper_tick_gaps = 0;
    uint64_t right_duplicate_tick_mismatches = 0;
    uint64_t left_duplicate_tick_mismatches = 0;
    uint64_t hid_duplicate_tick_mismatches = 0;
    std::array<uint64_t, 33> rows_per_mapper_tick{};
    uint64_t rows_per_mapper_tick_overflow = 0;
    int64_t max_late_us = 0;
    uint64_t late_rows_gt_1000us = 0;
};

RecordingDeterminismStaticSummary SummarizeRecordingDeterminism(
    const LoadedRecording& recording) {
    RecordingDeterminismStaticSummary summary;
    summary.samples = recording.samples.size();
    if (recording.samples.empty()) {
        return summary;
    }

    size_t index = 0;
    bool have_previous_tick = false;
    uint32_t previous_tick = 0;
    while (index < recording.samples.size()) {
        const RecordingSample& first = recording.samples[index];
        const uint32_t tick = first.mapper_tick;
        if (have_previous_tick && tick > previous_tick + 1U) {
            summary.mapper_tick_gaps += static_cast<uint64_t>(tick - previous_tick - 1U);
        }
        have_previous_tick = true;
        previous_tick = tick;
        ++summary.unique_mapper_ticks;

        bool right_mismatch = false;
        bool left_mismatch = false;
        bool hid_mismatch = false;
        size_t rows_for_tick = 0;
        while (index < recording.samples.size() &&
               recording.samples[index].mapper_tick == tick) {
            const RecordingSample& sample = recording.samples[index];
            if (!RightReplayFieldsMatchForMapperTick(first, sample)) {
                right_mismatch = true;
            }
            if (!LeftReplayFieldsMatchForMapperTick(first, sample)) {
                left_mismatch = true;
            }
            if (!HidReplayFieldsMatchForMapperTick(first, sample)) {
                hid_mismatch = true;
            }
            summary.max_late_us = std::max<int64_t>(summary.max_late_us, sample.late_us);
            if (sample.late_us > 1000) {
                ++summary.late_rows_gt_1000us;
            }
            ++rows_for_tick;
            ++index;
        }

        if (right_mismatch) {
            ++summary.right_duplicate_tick_mismatches;
        }
        if (left_mismatch) {
            ++summary.left_duplicate_tick_mismatches;
        }
        if (hid_mismatch) {
            ++summary.hid_duplicate_tick_mismatches;
        }
        if (rows_for_tick < summary.rows_per_mapper_tick.size()) {
            ++summary.rows_per_mapper_tick[rows_for_tick];
        } else {
            ++summary.rows_per_mapper_tick_overflow;
        }
    }
    return summary;
}

std::string FormatRowsPerMapperTick(
    const RecordingDeterminismStaticSummary& summary) {
    std::ostringstream out;
    bool first = true;
    for (size_t rows = 0; rows < summary.rows_per_mapper_tick.size(); ++rows) {
        const uint64_t ticks = summary.rows_per_mapper_tick[rows];
        if (ticks == 0) {
            continue;
        }
        if (!first) {
            out << ',';
        }
        first = false;
        out << rows << ':' << ticks;
    }
    if (summary.rows_per_mapper_tick_overflow > 0) {
        if (!first) {
            out << ',';
        }
        out << "overflow:" << summary.rows_per_mapper_tick_overflow;
    }
    return out.str();
}

struct MapperReplayAuditSummary {
    uint64_t replay_hash = kFnv1aOffset;
    size_t input_ticks = 0;
    uint64_t right_compare_ticks = 0;
    uint64_t right_mismatches = 0;
    int right_max_abs_error = 0;
    uint64_t left_compare_ticks = 0;
    uint64_t left_mismatches = 0;
    int left_max_abs_error = 0;
    uint64_t hid_compare_ticks = 0;
    uint64_t hid_missing_ticks = 0;
    bool have_first_right_mismatch = false;
    uint32_t first_right_mismatch_tick = 0;
    int first_expected_roll = 0;
    int first_expected_pitch = 0;
    int first_replayed_roll = 0;
    int first_replayed_pitch = 0;
    bool have_first_left_mismatch = false;
    uint32_t first_left_mismatch_tick = 0;
    int first_expected_throttle = 0;
    int first_expected_yaw = 0;
    int first_replayed_throttle = 0;
    int first_replayed_yaw = 0;
};

MapperReplayAuditSummary AuditMapperReplayFromCleanStart(
    const TrainerProfile& profile,
    const LoadedRecording& recording,
    const PlaybackChannelMask& mask) {
    MapperReplayAuditSummary summary;
    PlaybackBankSlot slot;
    slot.spec.mask = mask;
    slot.recording = recording;
    slot.loaded = true;

    size_t input_sample_index = 0;
    bool have_mapper_tick = false;
    uint32_t last_mapper_tick = 0;
    AuditRightStickState right_state;
    if (recording.metadata.start_state.right_mapper_available) {
        right_state.roll_position = recording.metadata.start_state.right_roll_value;
        right_state.pitch_position = recording.metadata.start_state.right_pitch_value;
        right_state.filtered_roll = recording.metadata.start_state.right_roll_value;
        right_state.filtered_pitch = recording.metadata.start_state.right_pitch_value;
        right_state.roll_elastic_state.velocity =
            recording.metadata.start_state.right_roll_velocity;
        right_state.pitch_elastic_state.velocity =
            recording.metadata.start_state.right_pitch_velocity;
        right_state.roll_pulse = recording.metadata.start_state.right_roll_pulse;
        right_state.pitch_pulse = recording.metadata.start_state.right_pitch_pulse;
    }
    int last_roll = right_state.roll_pulse;
    int last_pitch = right_state.pitch_pulse;
    LeftStickPlaybackState left_state =
        MakeLeftStickPlaybackStateFromRecording(profile, recording.metadata.start_state);
    int last_throttle = left_state.throttle_pulse;
    int last_yaw = left_state.yaw_pulse;
    const int mapper_rate_hz = std::min(profile.frame_rate_hz, kTrainerMapperReferenceHz);
    const int64_t playback_base_us = recording.samples.front().time_us;
    const bool compare_roll = PlaybackChannelUsesInputInjection(mask, profile, 0);
    const bool compare_pitch = PlaybackChannelUsesInputInjection(mask, profile, 1);
    const bool compare_throttle = PlaybackChannelUsesInputInjection(mask, profile, 2);
    const bool compare_yaw = PlaybackChannelUsesInputInjection(mask, profile, 3);
    const bool compare_left = compare_throttle || compare_yaw;

    while (input_sample_index < recording.samples.size()) {
        const size_t source_index = input_sample_index;
        const PlaybackInputInjection injection =
            ConsumePlaybackInputInjection(slot,
                                          profile,
                                          0,
                                          playback_base_us,
                                          &input_sample_index,
                                          &have_mapper_tick,
                                          &last_mapper_tick);
        if (injection.mapper_ticks == 0 || source_index >= recording.samples.size()) {
            break;
        }

        const RecordingSample& source_sample = recording.samples[source_index];
        if (profile.mouse_right_stick_enabled) {
            const std::array<int, 2> right_output =
                StepAuditRightStick(profile,
                                    injection.right_dx,
                                    injection.right_dy,
                                    mapper_rate_hz,
                                    &right_state);
            last_roll = right_output[0];
            last_pitch = right_output[1];
        }
        if (profile.mouse_left.enabled || profile.right_mouse_left.enabled) {
            StepLeftStickPlaybackState(profile,
                                       injection,
                                       mapper_rate_hz,
                                       &left_state);
            last_throttle = left_state.throttle_pulse;
            last_yaw = left_state.yaw_pulse;
        }

        std::array<int, kSbusChannels> pulses{};
        pulses[0] = last_roll;
        pulses[1] = last_pitch;
        pulses[2] = last_throttle;
        pulses[3] = last_yaw;
        const std::array<int, kSbusChannels> playback_pulses =
            BuildPlaybackPulses(source_sample, mask, profile.resolution_mode);
        for (int ch = 0; ch < kSbusChannels; ++ch) {
            if (mask.enabled[static_cast<size_t>(ch)] &&
                !PlaybackChannelUsesInputInjection(mask, profile, ch)) {
                pulses[static_cast<size_t>(ch)] = playback_pulses[static_cast<size_t>(ch)];
            }
        }
        HashAppendPulses(&summary.replay_hash,
                         pulses,
                         PlaybackActiveFlags(mask, profile.resolution_mode));
        ++summary.input_ticks;

        if (compare_roll || compare_pitch) {
            ++summary.right_compare_ticks;
            int tick_error = 0;
            if (compare_roll) {
                tick_error = std::max(tick_error,
                                      std::abs(last_roll - source_sample.final_channels[0]));
            }
            if (compare_pitch) {
                tick_error = std::max(tick_error,
                                      std::abs(last_pitch - source_sample.final_channels[1]));
            }
            if (tick_error != 0) {
                ++summary.right_mismatches;
                summary.right_max_abs_error =
                    std::max(summary.right_max_abs_error, tick_error);
                if (!summary.have_first_right_mismatch) {
                    summary.have_first_right_mismatch = true;
                    summary.first_right_mismatch_tick = source_sample.mapper_tick;
                    summary.first_expected_roll = source_sample.final_channels[0];
                    summary.first_expected_pitch = source_sample.final_channels[1];
                    summary.first_replayed_roll = last_roll;
                    summary.first_replayed_pitch = last_pitch;
                }
            }
        }
        if (compare_left) {
            ++summary.left_compare_ticks;
            int tick_error = 0;
            if (compare_throttle) {
                tick_error = std::max(
                    tick_error,
                    std::abs(last_throttle - source_sample.final_channels[2]));
            }
            if (compare_yaw) {
                tick_error = std::max(
                    tick_error,
                    std::abs(last_yaw - source_sample.final_channels[3]));
            }
            if (tick_error != 0) {
                ++summary.left_mismatches;
                summary.left_max_abs_error =
                    std::max(summary.left_max_abs_error, tick_error);
                if (!summary.have_first_left_mismatch) {
                    summary.have_first_left_mismatch = true;
                    summary.first_left_mismatch_tick = source_sample.mapper_tick;
                    summary.first_expected_throttle = source_sample.final_channels[2];
                    summary.first_expected_yaw = source_sample.final_channels[3];
                    summary.first_replayed_throttle = last_throttle;
                    summary.first_replayed_yaw = last_yaw;
                }
            }
        }
        for (int ch = 0; ch < 4; ++ch) {
            if (!mask.enabled[static_cast<size_t>(ch)] ||
                !mask.use_hid[static_cast<size_t>(ch)]) {
                continue;
            }
            ++summary.hid_compare_ticks;
            if (!source_sample.hid_valid) {
                ++summary.hid_missing_ticks;
            }
        }
    }
    return summary;
}

struct TimedPlaybackAuditRun {
    uint64_t frame_hash = kFnv1aOffset;
    uint64_t frames = 0;
    uint64_t input_ticks = 0;
    uint64_t mapper_ticks = 0;
    int64_t max_late_us = 0;
    uint64_t late_frames_gt_1000us = 0;
    double elapsed_seconds = 0.0;
    int frames_per_mapper_tick = 0;
    bool normalized_overlay_clock = false;
    bool timed_out = false;
};

TimedPlaybackAuditRun RunTimedIntegratedPlaybackAuditOnce(
    const TrainerProfile& profile,
    const LoadedRecording& recording,
    const PlaybackChannelMask& mask) {
    TimedPlaybackAuditRun run;
    PlaybackBankSlot slot;
    slot.spec.mask = mask;
    slot.recording = recording;
    slot.mapper_tick_sample_indices =
        BuildPlaybackMapperTickSampleIndices(slot.recording);
    slot.loaded = true;

    const int frame_rate_hz = profile.frame_rate_hz;
    const int mapper_rate_hz = std::min(frame_rate_hz, kTrainerMapperReferenceHz);
    const int64_t playback_base_us = recording.samples.front().time_us;

    const bool playback_uses_right_input =
        PlaybackChannelUsesInputInjection(mask, profile, 0) ||
        PlaybackChannelUsesInputInjection(mask, profile, 1);
    const bool playback_uses_left_input =
        PlaybackChannelUsesInputInjection(mask, profile, 2) ||
        PlaybackChannelUsesInputInjection(mask, profile, 3);
    const bool uses_frame_clocked_input =
        playback_uses_right_input || playback_uses_left_input;
    const int frames_per_mapper_tick =
        (PlaybackMaskUsesOnlyRecordedOverlay(mask) || uses_frame_clocked_input)
            ? PlaybackFramesPerRecordedMapperTick(profile, recording)
            : 0;
    if (frames_per_mapper_tick > 0 &&
        !slot.mapper_tick_sample_indices.empty()) {
        run.normalized_overlay_clock = true;
        run.frames_per_mapper_tick = frames_per_mapper_tick;
        run.mapper_ticks = slot.mapper_tick_sample_indices.size();
        size_t input_sample_index = 0;
        bool have_mapper_tick = false;
        uint32_t last_mapper_tick = 0;
        RightStickPlaybackState right_state =
            MakeRightStickPlaybackStateFromRecording(recording.metadata.start_state);
        LeftStickPlaybackState left_state =
            MakeLeftStickPlaybackStateFromRecording(profile, recording.metadata.start_state);
        for (size_t tick_index = 0;
             tick_index < slot.mapper_tick_sample_indices.size();
             ++tick_index) {
            const RecordingSample& playback_sample =
                recording.samples[slot.mapper_tick_sample_indices[tick_index]];
            if (uses_frame_clocked_input) {
                while (input_sample_index < recording.samples.size() &&
                       (!have_mapper_tick ||
                        last_mapper_tick != playback_sample.mapper_tick)) {
                    const size_t before_index = input_sample_index;
                    const PlaybackInputInjection injection =
                        ConsumePlaybackInputInjection(slot,
                                                      profile,
                                                      0,
                                                      playback_base_us,
                                                      &input_sample_index,
                                                      &have_mapper_tick,
                                                      &last_mapper_tick);
                    if (injection.mapper_ticks == 0 || input_sample_index == before_index) {
                        break;
                    }
                    if (playback_uses_right_input && profile.mouse_right_stick_enabled) {
                        StepRightStickPlaybackState(profile,
                                                    injection.right_dx,
                                                    injection.right_dy,
                                                    mapper_rate_hz,
                                                    &right_state);
                    }
                    if (playback_uses_left_input &&
                        (profile.mouse_left.enabled || profile.right_mouse_left.enabled)) {
                        StepLeftStickPlaybackState(profile,
                                                   injection,
                                                   mapper_rate_hz,
                                                   &left_state);
                    }
                    run.input_ticks += injection.mapper_ticks;
                    if (last_mapper_tick == playback_sample.mapper_tick) {
                        break;
                    }
                }
            }
            std::array<int, kSbusChannels> pulses{};
            pulses[0] = right_state.roll_pulse;
            pulses[1] = right_state.pitch_pulse;
            pulses[2] = left_state.throttle_pulse;
            pulses[3] = left_state.yaw_pulse;
            const std::array<int, kSbusChannels> playback_pulses =
                BuildPlaybackPulses(playback_sample,
                                    mask,
                                    profile.resolution_mode);
            for (int ch = 0; ch < kSbusChannels; ++ch) {
                if (mask.enabled[static_cast<size_t>(ch)] &&
                    !PlaybackChannelUsesInputInjection(mask, profile, ch)) {
                    pulses[static_cast<size_t>(ch)] =
                        playback_pulses[static_cast<size_t>(ch)];
                }
            }
            for (int repeat = 0; repeat < frames_per_mapper_tick; ++repeat) {
                HashAppendPulses(&run.frame_hash,
                                 pulses,
                                 PlaybackActiveFlags(mask,
                                                     profile.resolution_mode));
                ++run.frames;
            }
        }
        run.elapsed_seconds = frame_rate_hz > 0
            ? static_cast<double>(run.frames) / static_cast<double>(frame_rate_hz)
            : 0.0;
        return run;
    }

    size_t input_sample_index = 0;
    bool have_mapper_tick = false;
    uint32_t last_mapper_tick = 0;
    RightStickPlaybackState right_state =
        MakeRightStickPlaybackStateFromRecording(recording.metadata.start_state);
    int last_roll = right_state.roll_pulse;
    int last_pitch = right_state.pitch_pulse;
    LeftStickPlaybackState left_state =
        MakeLeftStickPlaybackStateFromRecording(profile, recording.metadata.start_state);
    int last_throttle = left_state.throttle_pulse;
    int last_yaw = left_state.yaw_pulse;

    for (size_t playback_sample_index = 0;
         playback_sample_index < recording.samples.size();
         ++playback_sample_index) {
        const RecordingSample& playback_sample = recording.samples[playback_sample_index];
        while (input_sample_index < recording.samples.size() &&
               (!have_mapper_tick ||
                last_mapper_tick != playback_sample.mapper_tick)) {
            const size_t before_index = input_sample_index;
            const PlaybackInputInjection injection =
                ConsumePlaybackInputInjection(slot,
                                              profile,
                                              0,
                                              playback_base_us,
                                              &input_sample_index,
                                              &have_mapper_tick,
                                              &last_mapper_tick);
            if (injection.mapper_ticks == 0 || input_sample_index == before_index) {
                break;
            }
            if (playback_uses_right_input && profile.mouse_right_stick_enabled) {
                StepRightStickPlaybackState(profile,
                                            injection.right_dx,
                                            injection.right_dy,
                                            mapper_rate_hz,
                                            &right_state);
                last_roll = right_state.roll_pulse;
                last_pitch = right_state.pitch_pulse;
            }
            if (playback_uses_left_input &&
                (profile.mouse_left.enabled || profile.right_mouse_left.enabled)) {
                StepLeftStickPlaybackState(profile,
                                           injection,
                                           mapper_rate_hz,
                                           &left_state);
                last_throttle = left_state.throttle_pulse;
                last_yaw = left_state.yaw_pulse;
            }
            run.input_ticks += injection.mapper_ticks;
            if (last_mapper_tick == playback_sample.mapper_tick) {
                break;
            }
        }

        std::array<int, kSbusChannels> pulses{};
        pulses[0] = last_roll;
        pulses[1] = last_pitch;
        pulses[2] = last_throttle;
        pulses[3] = last_yaw;
        const std::array<int, kSbusChannels> playback_pulses =
            BuildPlaybackPulses(playback_sample,
                                mask,
                                profile.resolution_mode);
        for (int ch = 0; ch < kSbusChannels; ++ch) {
            if (mask.enabled[static_cast<size_t>(ch)] &&
                !PlaybackChannelUsesInputInjection(mask, profile, ch)) {
                pulses[static_cast<size_t>(ch)] =
                    playback_pulses[static_cast<size_t>(ch)];
            }
        }

        HashAppendPulses(&run.frame_hash,
                         pulses,
                         PlaybackActiveFlags(mask, profile.resolution_mode));
        ++run.frames;
    }

    run.elapsed_seconds = frame_rate_hz > 0
        ? static_cast<double>(run.frames) / static_cast<double>(frame_rate_hz)
        : 0.0;
    return run;
}

int RunRecordingDeterminismAudit(const char* recording_path,
                                 const char* profile_path,
                                 const PlaybackChannelMask& mask,
                                 int timed_runs) {
    LoadedRecording recording;
    std::string error;
    if (!LoadRecordingFile(recording_path, &recording, &error)) {
        std::fprintf(stderr, "--recording-determinism-audit failed loading recording: %s\n",
                     error.c_str());
        return 1;
    }

    TrainerProfile profile;
    if (!LoadTrainerProfile(profile_path, &profile)) {
        return 2;
    }
    if (recording.metadata.resolution_mode != profile.resolution_mode) {
        std::fprintf(stderr,
                     "--recording-determinism-audit resolution mismatch: recording=%s profile=%s\n",
                     TrainerResolutionModeName(recording.metadata.resolution_mode),
                     TrainerResolutionModeName(profile.resolution_mode));
        return 2;
    }
    if (recording.metadata.trainer_frame_rate_hz > 0 &&
        recording.metadata.trainer_frame_rate_hz != profile.frame_rate_hz) {
        std::printf("warning: recording frame_rate=%d differs from profile frame_rate=%d; "
                    "timed audit uses the profile frame rate.\n",
                    recording.metadata.trainer_frame_rate_hz,
                    profile.frame_rate_hz);
    }
    PlaybackChannelMask resolved_mask = mask;
    if (!ResolveIntegratedPlaybackMaskForProfile(&resolved_mask,
                                                 profile,
                                                 recording,
                                                 recording_path ? recording_path : "",
                                                 true)) {
        return 2;
    }

    const RecordingDeterminismStaticSummary static_summary =
        SummarizeRecordingDeterminism(recording);
    const MapperReplayAuditSummary mapper_summary =
        AuditMapperReplayFromCleanStart(profile, recording, resolved_mask);

    std::printf("\n--recording-determinism-audit: recording=%s\n", recording_path);
    std::printf("  profile=%s channels=%s timed_runs=%d\n",
                profile_path,
                DescribePlaybackChannelMask(resolved_mask).c_str(),
                timed_runs);
    std::printf("  recording: samples=%zu unique_mapper_ticks=%zu mapper_tick_gaps=%llu "
                "right_duplicate_tick_mismatches=%llu "
                "left_duplicate_tick_mismatches=%llu "
                "hid_duplicate_tick_variations=%llu\n",
                static_summary.samples,
                static_summary.unique_mapper_ticks,
                static_cast<unsigned long long>(static_summary.mapper_tick_gaps),
                static_cast<unsigned long long>(
                    static_summary.right_duplicate_tick_mismatches),
                static_cast<unsigned long long>(
                    static_summary.left_duplicate_tick_mismatches),
                static_cast<unsigned long long>(
                    static_summary.hid_duplicate_tick_mismatches));
    std::printf("  recording: rows_per_mapper_tick=%s max_late_us=%lld late_rows_gt_1000us=%llu\n",
                FormatRowsPerMapperTick(static_summary).c_str(),
                static_cast<long long>(static_summary.max_late_us),
                static_cast<unsigned long long>(static_summary.late_rows_gt_1000us));
    std::printf("  clean-start mapper replay: input_ticks=%zu hash=%s "
                "right_compare_ticks=%llu right_mismatches=%llu right_max_abs_error=%d "
                "left_compare_ticks=%llu left_mismatches=%llu left_max_abs_error=%d "
                "hid_compare_ticks=%llu hid_missing_ticks=%llu\n",
                mapper_summary.input_ticks,
                HexUint64(mapper_summary.replay_hash).c_str(),
                static_cast<unsigned long long>(mapper_summary.right_compare_ticks),
                static_cast<unsigned long long>(mapper_summary.right_mismatches),
                mapper_summary.right_max_abs_error,
                static_cast<unsigned long long>(mapper_summary.left_compare_ticks),
                static_cast<unsigned long long>(mapper_summary.left_mismatches),
                mapper_summary.left_max_abs_error,
                static_cast<unsigned long long>(mapper_summary.hid_compare_ticks),
                static_cast<unsigned long long>(mapper_summary.hid_missing_ticks));
    if (mapper_summary.have_first_right_mismatch) {
        std::printf("  first_right_mismatch: mapper_tick=%u recorded_roll=%d recorded_pitch=%d "
                    "replayed_roll=%d replayed_pitch=%d\n",
                    mapper_summary.first_right_mismatch_tick,
                    mapper_summary.first_expected_roll,
                    mapper_summary.first_expected_pitch,
                    mapper_summary.first_replayed_roll,
                    mapper_summary.first_replayed_pitch);
    }
    if (mapper_summary.have_first_left_mismatch) {
        std::printf("  first_left_mismatch: mapper_tick=%u recorded_throttle=%d recorded_yaw=%d "
                    "replayed_throttle=%d replayed_yaw=%d\n",
                    mapper_summary.first_left_mismatch_tick,
                    mapper_summary.first_expected_throttle,
                    mapper_summary.first_expected_yaw,
                    mapper_summary.first_replayed_throttle,
                    mapper_summary.first_replayed_yaw);
    }

    if (timed_runs > 0) {
        std::vector<TimedPlaybackAuditRun> runs;
        runs.reserve(static_cast<size_t>(timed_runs));
        bool repeatable = true;
        for (int run_index = 0; run_index < timed_runs; ++run_index) {
            const TimedPlaybackAuditRun run =
                RunTimedIntegratedPlaybackAuditOnce(profile, recording, resolved_mask);
            if (!runs.empty() &&
                (run.frame_hash != runs.front().frame_hash ||
                 run.frames != runs.front().frames ||
                 run.timed_out != runs.front().timed_out)) {
                repeatable = false;
            }
            const std::string clock_label = run.normalized_overlay_clock
                ? ("mapper-normalized/" + std::to_string(run.frames_per_mapper_tick))
                : "sample-rows";
            std::printf("  timed_run[%d]: frames=%llu input_ticks=%llu mapper_ticks=%llu hash=%s "
                        "elapsed=%.3fs clock=%s max_late_us=%lld "
                        "late_frames_gt_1000us=%llu timed_out=%s\n",
                        run_index + 1,
                        static_cast<unsigned long long>(run.frames),
                        static_cast<unsigned long long>(run.input_ticks),
                        static_cast<unsigned long long>(run.mapper_ticks),
                        HexUint64(run.frame_hash).c_str(),
                        run.elapsed_seconds,
                        clock_label.c_str(),
                        static_cast<long long>(run.max_late_us),
                        static_cast<unsigned long long>(run.late_frames_gt_1000us),
                        run.timed_out ? "yes" : "no");
            runs.push_back(run);
        }
        std::printf("  timed_repeatable=%s\n", repeatable ? "yes" : "no");
    }

    std::printf("  clean_start_replay_matches_recorded_right=%s\n",
                mapper_summary.right_mismatches == 0 ? "yes" : "no");
    std::printf("  clean_start_replay_matches_recorded_left=%s\n",
                mapper_summary.left_mismatches == 0 ? "yes" : "no");
    std::printf("  hid_overlay_has_samples=%s\n",
                mapper_summary.hid_missing_ticks == 0 ? "yes" : "no");
    return 0;
}

int RunRecordingSelfTest() {
    const std::filesystem::path test_path =
        std::filesystem::path("logs") / "recording-self-test.gx12rec.csv";
    TrainerProfile profile;
    profile.name = "recording-self-test";
    profile.source_file = "recording-self-test";
    profile.frame_rate_hz = 1000;
    profile.resolution_mode = TrainerResolutionMode::Gx12_2x;
    {
        TrainerProfile return_profile;
        return_profile.max_output = 512;
        double roll = 256.0;
        double pitch = 256.0;
        ApplyRightStickDiagonalNormalizedReturn(0.0,
                                                12.0,
                                                ElasticReturnMode::Linear,
                                                0.0,
                                                0.001,
                                                true,
                                                &roll,
                                                &pitch,
                                                return_profile);
        const double roll_step = 256.0 - roll;
        const double pitch_step = 256.0 - pitch;
        const double vector_step = std::sqrt((roll_step * roll_step) + (pitch_step * pitch_step));
        if (std::abs(vector_step - 3.072) > 0.001 ||
            std::abs(roll_step - pitch_step) > 0.001 ||
            roll_step >= 3.072) {
            std::fprintf(stderr, "--recording-self-test diagonal elastic return metric failed.\n");
            return 1;
        }
    }
    {
        TrainerProfile clutched_profile;
        clutched_profile.max_output = 512;
        clutched_profile.roll_gain = 0.0;
        clutched_profile.pitch_gain = 0.0;
        clutched_profile.elastic_return_activation = ElasticReturnActivation::WhileMoving;
        clutched_profile.elastic_return_mode = ElasticReturnMode::Linear;
        double roll = 256.0;
        double pitch = 0.0;
        ElasticAxisState roll_state;
        ElasticAxisState pitch_state;
        RightStickSharedState shared_state;
        int roll_pulse = 0;
        int pitch_pulse = 0;
        ShapeRightStickPositionPulses(0.0,
                                      0.0,
                                      1.0,
                                      0.0,
                                      12.0,
                                      0.001,
                                      true,
                                      &roll,
                                      &pitch,
                                      &roll_state,
                                      &pitch_state,
                                      &shared_state,
                                      clutched_profile,
                                      &roll_pulse,
                                      &pitch_pulse);
        if (std::abs(roll - 256.0) > 0.001) {
            std::fprintf(stderr, "--recording-self-test input-gated elastic return drifted on zero input.\n");
            return 1;
        }
        ShapeRightStickPositionPulses(1.0,
                                      0.0,
                                      1.0,
                                      0.0,
                                      12.0,
                                      0.001,
                                      true,
                                      &roll,
                                      &pitch,
                                      &roll_state,
                                      &pitch_state,
                                      &shared_state,
                                      clutched_profile,
                                      &roll_pulse,
                                      &pitch_pulse);
        if (std::abs(roll - (256.0 - 3.072)) > 0.001) {
            std::fprintf(stderr, "--recording-self-test input-gated elastic return did not apply while moving.\n");
            return 1;
        }
    }
    {
        TrainerProfile tapered_profile;
        tapered_profile.elastic_return_activation = ElasticReturnActivation::WhileMoving;
        tapered_profile.elastic_return_idle_coefficient = 3.0;
        tapered_profile.elastic_return_taper_ms = 100.0;
        RightStickSharedState tapered_state;
        const double moving_coeff =
            EffectiveElasticReturnCoefficient(tapered_profile, 12.0, true, 0.001, &tapered_state);
        const double mid_coeff =
            EffectiveElasticReturnCoefficient(tapered_profile, 12.0, false, 0.050, &tapered_state);
        const double idle_coeff =
            EffectiveElasticReturnCoefficient(tapered_profile, 12.0, false, 0.050, &tapered_state);
        if (std::abs(moving_coeff - 12.0) > 0.001 ||
            std::abs(mid_coeff - 7.5) > 0.001 ||
            std::abs(idle_coeff - 3.0) > 0.001) {
            std::fprintf(stderr, "--recording-self-test tapered elastic return coefficient failed.\n");
            return 1;
        }
    }

    Gx12RecordingHidCapture fake_hid;
    fake_hid.available = true;
    fake_hid.last_valid = true;
    fake_hid.reports = 1;
    fake_hid.last_report.channels[0] = 11;
    fake_hid.last_report.channels[1] = -22;
    fake_hid.last_report.channels[2] = 333;
    fake_hid.last_report.channels[3] = -444;

    RecordingCsvWriter writer;
    if (!writer.Open(profile, test_path.string().c_str(), 1000, fake_hid)) {
        return 1;
    }
    ElasticAxisState self_test_roll_elastic;
    ElasticAxisState self_test_pitch_elastic;
    self_test_roll_elastic.velocity = 1.25;
    self_test_pitch_elastic.velocity = -2.5;
    RecordingStartState self_test_start =
        MakeRecordingStartStateFromRightStick(7.0,
                                              -9.0,
                                              self_test_roll_elastic,
                                              self_test_pitch_elastic,
                                              14,
                                              -18);
    self_test_start.mouse_left_available = true;
    self_test_start.mouse_left_throttle_value = -321.0;
    self_test_start.mouse_left_yaw_value = 12.0;
    self_test_start.mouse_left_yaw_filtered = 10.0;
    self_test_start.mouse_left_yaw_position = 12.0;
    self_test_start.mouse_left_yaw_dummy_position = 0.0;
    self_test_start.mouse_left_yaw_velocity = 1.5;
    self_test_start.mouse_left_yaw_dummy_velocity = -0.25;
    self_test_start.mouse_left_throttle_pulse =
        QuantizeTrainerProfileOutput(-321.0, profile.resolution_mode);
    self_test_start.mouse_left_yaw_pulse =
        QuantizeTrainerProfileOutput(10.0, profile.resolution_mode);
    writer.WriteStartState(self_test_start);
    std::array<int, kSbusChannels> final_channels{};
    final_channels[0] = 20;
    final_channels[1] = -40;
    final_channels[2] = QuantizeTrainerProfileOutput(-313.0, profile.resolution_mode);
    final_channels[3] = QuantizeTrainerProfileOutput(19.0, profile.resolution_mode);
    writer.WriteSample(0, 1, 1, 10, 10, 0, 3, -4, 0, 0, 0, 7, -8, 0, 0, 0,
                       final_channels, fake_hid, kSbusTrainerMaskMarker, 0);
    fake_hid.last_report.channels[2] = 555;
    fake_hid.last_report.channels[3] = -666;
    fake_hid.reports = 2;
    final_channels[0] = 30;
    final_channels[1] = -60;
    final_channels[2] = QuantizeTrainerProfileOutput(-303.0, profile.resolution_mode);
    final_channels[3] = QuantizeTrainerProfileOutput(28.0, profile.resolution_mode);
    writer.WriteSample(1000, 2, 2, 11, 11, 0, 5, -6, 0, 120, 0, 9, -10, 0, 0, 0,
                       final_channels, fake_hid, kSbusTrainerMaskMarker, 0);
    writer.Close();

    const std::filesystem::path async_test_path =
        std::filesystem::path("logs") / "recording-async-commit-self-test.gx12rec.csv";
    RecordingCsvWriter async_writer;
    if (!async_writer.Open(profile, async_test_path.string().c_str(), 1000, fake_hid)) {
        std::fprintf(stderr, "--recording-self-test async writer open failed.\n");
        return 1;
    }
    async_writer.WriteStartState(self_test_start);
    async_writer.WriteSample(0, 1, 1, 12, 12, 0, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0,
                             final_channels, fake_hid, kSbusTrainerMaskMarker, 0);
    RecordingCsvWriter::CommitSnapshot async_snapshot =
        async_writer.CloseToSnapshot();
    RecordingCommitQueue async_commits;
    async_commits.Start();
    async_commits.Enqueue(std::move(async_snapshot));
    async_commits.Stop();
    const std::vector<RecordingCsvWriter::CommitResult> async_results =
        async_commits.DrainCompleted();
    if (async_results.size() != 1 ||
        !async_results.front().ok ||
        !async_results.front().attempted ||
        async_results.front().sample_rows != 1) {
        std::fprintf(stderr, "--recording-self-test async commit failed.\n");
        return 1;
    }
    LoadedRecording async_loaded;
    std::string async_error;
    if (!LoadRecordingFile(async_test_path.string().c_str(), &async_loaded, &async_error) ||
        async_loaded.samples.size() != 1) {
        std::fprintf(stderr,
                     "--recording-self-test async parse failed: %s\n",
                     async_error.c_str());
        return 1;
    }

    const std::filesystem::path profile_log_path =
        std::filesystem::path("logs") / "trainer-csv-self-test.csv";
    TrainerProfile profile_log_settings = profile;
    profile_log_settings.log_csv = true;
    profile_log_settings.log_path = profile_log_path.string();
    TrainerCsvLogWriter profile_log;
    if (!profile_log.Open(profile_log_settings)) {
        std::fprintf(stderr, "--recording-self-test trainer CSV log open failed.\n");
        return 1;
    }
    TrainerCsvLogWriter::Row profile_log_row;
    profile_log_row.frame = 42;
    profile_log_row.mouse_events = 99;
    profile_log_row.roll = 123;
    profile_log_row.pitch = -45;
    profile_log_row.throttle = TrainerLowValue(profile.resolution_mode);
    profile_log_row.yaw = 6;
    profile_log.Enqueue(profile_log_row);
    profile_log.Close();
    {
        std::ifstream profile_log_file(profile_log_path);
        std::ostringstream profile_log_text;
        profile_log_text << profile_log_file.rdbuf();
        const std::string text = profile_log_text.str();
        if (text.find("time_s,frame,mouse_events") == std::string::npos ||
            text.find("\n0,42,99,") == std::string::npos) {
            std::fprintf(stderr, "--recording-self-test trainer CSV log writer failed.\n");
            return 1;
        }
    }

    LoadedRecording loaded;
    std::string error;
    if (!LoadRecordingFile(test_path.string().c_str(), &loaded, &error)) {
        std::fprintf(stderr, "--recording-self-test parse failed: %s\n", error.c_str());
        return 1;
    }
    if (loaded.samples.size() != 2 || !loaded.has_hid_samples ||
        loaded.metadata.resolution_mode != TrainerResolutionMode::Gx12_2x ||
        loaded.metadata.recording_buffer != "memory" ||
        !loaded.metadata.start_state.right_mapper_available ||
        loaded.metadata.start_state.right_roll_value != 7.0 ||
        loaded.metadata.start_state.right_pitch_value != -9.0 ||
        loaded.metadata.start_state.right_roll_pulse != 14 ||
        loaded.metadata.start_state.right_pitch_pulse != -18 ||
        !loaded.metadata.start_state.mouse_left_available ||
        loaded.metadata.start_state.mouse_left_throttle_value != -321.0 ||
        loaded.metadata.start_state.mouse_left_yaw_value != 12.0) {
        std::fprintf(stderr, "--recording-self-test metadata/sample validation failed.\n");
        return 1;
    }
    if (loaded.samples[0].right_dx != 3 || loaded.samples[0].right_dy != -4 ||
        loaded.samples[1].right_dx != 5 || loaded.samples[1].right_dy != -6) {
        std::fprintf(stderr, "--recording-self-test raw mouse input parse failed.\n");
        return 1;
    }

    PlaybackChannelMask right_mask;
    if (!ParsePlaybackChannelMask("ail+ele", &right_mask)) {
        std::fprintf(stderr, "--recording-self-test channel parser failed for ail+ele.\n");
        return 1;
    }
    PlaybackBankSlot input_slot;
    input_slot.spec.mask = right_mask;
    input_slot.spec.block_live_input = true;
    input_slot.recording = loaded;
    profile.mouse_right_stick_enabled = true;
    int64_t live_right_dx = 99;
    int64_t live_right_dy = -88;
    int64_t live_right_wheel_y = 77;
    uint32_t live_right_buttons = 0x30;
    int64_t live_left_dx = 66;
    int64_t live_left_dy = -55;
    ClearPlaybackLiveInputForMask(input_slot.spec,
                                  profile,
                                  &live_right_dx,
                                  &live_right_dy,
                                  &live_right_wheel_y,
                                  &live_right_buttons,
                                  &live_left_dx,
                                  &live_left_dy);
    if (live_right_dx != 0 || live_right_dy != 0 ||
        live_right_wheel_y != 77 || live_right_buttons != 0x30 ||
        live_left_dx != 66 || live_left_dy != -55) {
        std::fprintf(stderr, "--recording-self-test live input block helper failed.\n");
        return 1;
    }
    size_t input_sample_index = 0;
    bool have_mapper_tick = false;
    uint32_t last_mapper_tick = 0;
    const auto first_injection =
        ConsumePlaybackInputInjection(input_slot,
                                      profile,
                                      0,
                                      loaded.samples.front().time_us,
                                      &input_sample_index,
                                      &have_mapper_tick,
                                      &last_mapper_tick);
    if (first_injection.right_dx != 3 || first_injection.right_dy != -4 ||
        first_injection.mapper_ticks != 1) {
        std::fprintf(stderr, "--recording-self-test input injection first tick failed.\n");
        return 1;
    }
    const auto second_injection =
        ConsumePlaybackInputInjection(input_slot,
                                      profile,
                                      1000,
                                      loaded.samples.front().time_us,
                                      &input_sample_index,
                                      &have_mapper_tick,
                                      &last_mapper_tick);
    if (second_injection.right_dx != 5 || second_injection.right_dy != -6 ||
        second_injection.mapper_ticks != 1) {
        std::fprintf(stderr, "--recording-self-test input injection second tick failed.\n");
        return 1;
    }

    PlaybackChannelMask mouse_left_input_mask;
    if (!ParsePlaybackChannelMask("mouse_left", &mouse_left_input_mask) ||
        DescribePlaybackChannelMask(mouse_left_input_mask) != "thr,rud") {
        std::fprintf(stderr, "--recording-self-test channel parser failed for mouse_left.\n");
        return 1;
    }
    PlaybackBankSlot mouse_left_input_slot;
    mouse_left_input_slot.spec.mask = mouse_left_input_mask;
    mouse_left_input_slot.spec.block_live_input = true;
    mouse_left_input_slot.recording = loaded;
    TrainerProfile mouse_left_profile = profile;
    mouse_left_profile.mouse_right_stick_enabled = false;
    mouse_left_profile.mouse_left.enabled = true;
    mouse_left_profile.mouse_left.throttle_rate = 1.0;
    mouse_left_profile.mouse_left.yaw_gain = 1.0;
    mouse_left_profile.mouse_left.yaw_shaping_enabled = false;
    mouse_left_profile.mouse_left.yaw_slew_rate = 0.0;
    mouse_left_profile.mouse_left.yaw_deadband = 0;
    size_t left_input_sample_index = 0;
    bool left_have_mapper_tick = false;
    uint32_t left_last_mapper_tick = 0;
    const auto left_first_injection =
        ConsumePlaybackInputInjection(mouse_left_input_slot,
                                      mouse_left_profile,
                                      0,
                                      loaded.samples.front().time_us,
                                      &left_input_sample_index,
                                      &left_have_mapper_tick,
                                      &left_last_mapper_tick);
    if (left_first_injection.left_dx != 7 ||
        left_first_injection.left_dy != -8 ||
        left_first_injection.mapper_ticks != 1) {
        std::fprintf(stderr, "--recording-self-test left input injection failed.\n");
        return 1;
    }
    LeftStickPlaybackState left_playback_state =
        MakeLeftStickPlaybackStateFromRecording(mouse_left_profile,
                                                loaded.metadata.start_state);
    StepLeftStickPlaybackState(mouse_left_profile,
                               left_first_injection,
                               1000,
                               &left_playback_state);
    const int expected_left_throttle = QuantizeTrainerProfileOutput(
        -313.0,
        mouse_left_profile.resolution_mode);
    const int expected_left_yaw = QuantizeTrainerProfileOutput(
        19.0,
        mouse_left_profile.resolution_mode);
    if (left_playback_state.throttle_pulse != expected_left_throttle ||
        left_playback_state.yaw_pulse != expected_left_yaw) {
        std::fprintf(stderr, "--recording-self-test left playback state failed.\n");
        return 1;
    }
    const auto right_pulses =
        BuildPlaybackPulses(loaded.samples[0], right_mask, loaded.metadata.resolution_mode);
    if (right_pulses[0] != 20 || right_pulses[1] != -40 ||
        right_pulses[2] != TrainerLowValue(TrainerResolutionMode::Gx12_2x)) {
        std::fprintf(stderr, "--recording-self-test right-channel pulse validation failed.\n");
        return 1;
    }
    if (!PlaybackChannelUsesInputInjection(right_mask, profile, 0) ||
        !PlaybackChannelUsesInputInjection(right_mask, profile, 1)) {
        std::fprintf(stderr, "--recording-self-test right input mask validation failed.\n");
        return 1;
    }

    PlaybackChannelMask trainer_right_mask;
    if (!ParsePlaybackChannelMask("trainer_right", &trainer_right_mask) ||
        DescribePlaybackChannelMask(trainer_right_mask) != "trainer_ail,trainer_ele" ||
        PlaybackChannelUsesInputInjection(trainer_right_mask, profile, 0) ||
        PlaybackChannelUsesInputInjection(trainer_right_mask, profile, 1)) {
        std::fprintf(stderr, "--recording-self-test channel parser failed for trainer_right.\n");
        return 1;
    }
    const PlaybackChannelMask default_mask = DefaultPlaybackChannelMask();
    if (DescribePlaybackChannelMask(default_mask) != "trainer_ail,trainer_ele" ||
        PlaybackChannelUsesInputInjection(default_mask, profile, 0) ||
        PlaybackChannelUsesInputInjection(default_mask, profile, 1)) {
        std::fprintf(stderr, "--recording-self-test default playback mask validation failed.\n");
        return 1;
    }
    const std::vector<size_t> mapper_tick_indices =
        BuildPlaybackMapperTickSampleIndices(loaded);
    TrainerProfile high_rate_profile = profile;
    high_rate_profile.frame_rate_hz = 2000;
    if (mapper_tick_indices.size() != 2 ||
        mapper_tick_indices[0] != 0 ||
        mapper_tick_indices[1] != 1 ||
        !PlaybackMaskUsesOnlyRecordedOverlay(default_mask) ||
        PlaybackMaskUsesOnlyRecordedOverlay(right_mask) ||
        PlaybackFramesPerRecordedMapperTick(high_rate_profile, loaded) != 2) {
        std::fprintf(stderr,
                     "--recording-self-test normalized overlay clock validation failed.\n");
        return 1;
    }

    PlaybackChannelMask radio_right_mask;
    if (!ParsePlaybackChannelMask("radio_right", &radio_right_mask)) {
        std::fprintf(stderr, "--recording-self-test channel parser failed for radio_right.\n");
        return 1;
    }
    const auto radio_right_pulses =
        BuildPlaybackPulses(loaded.samples[0], radio_right_mask, loaded.metadata.resolution_mode);
    if (radio_right_pulses[0] != 11 || radio_right_pulses[1] != -22 ||
        radio_right_pulses[2] != TrainerLowValue(TrainerResolutionMode::Gx12_2x) ||
        !radio_right_mask.UsesHidRightStick()) {
        std::fprintf(stderr, "--recording-self-test radio-right-gimbal pulse validation failed.\n");
        return 1;
    }

    PlaybackChannelMask left_mask;
    if (!ParsePlaybackChannelMask("thr+rud", &left_mask) ||
        DescribePlaybackChannelMask(left_mask) != "thr,rud") {
        std::fprintf(stderr, "--recording-self-test channel parser failed for thr+rud.\n");
        return 1;
    }
    if (!PlaybackChannelUsesInputInjection(left_mask, mouse_left_profile, 2) ||
        !PlaybackChannelUsesInputInjection(left_mask, mouse_left_profile, 3) ||
        PlaybackMaskUsesOnlyRecordedOverlay(left_mask)) {
        std::fprintf(stderr, "--recording-self-test left input mask validation failed.\n");
        return 1;
    }

    PlaybackChannelMask trainer_left_mask;
    if (!ParsePlaybackChannelMask("trainer_thr,trainer_rud", &trainer_left_mask) ||
        DescribePlaybackChannelMask(trainer_left_mask) != "trainer_thr,trainer_rud") {
        std::fprintf(stderr, "--recording-self-test channel parser failed for trainer left.\n");
        return 1;
    }
    const auto trainer_left_pulses =
        BuildPlaybackPulses(loaded.samples[1], trainer_left_mask, loaded.metadata.resolution_mode);
    if (trainer_left_pulses[2] != QuantizeTrainerProfileOutput(-303.0, profile.resolution_mode) ||
        trainer_left_pulses[3] != QuantizeTrainerProfileOutput(28.0, profile.resolution_mode) ||
        PlaybackChannelUsesInputInjection(trainer_left_mask, profile, 2) ||
        PlaybackChannelUsesInputInjection(trainer_left_mask, profile, 3)) {
        std::fprintf(stderr, "--recording-self-test trainer-left pulse validation failed.\n");
        return 1;
    }

    PlaybackChannelMask radio_left_mask;
    if (!ParsePlaybackChannelMask("radio_thr,radio_rud", &radio_left_mask) ||
        DescribePlaybackChannelMask(radio_left_mask) != "radio_thr,radio_rud") {
        std::fprintf(stderr, "--recording-self-test channel parser failed for radio left.\n");
        return 1;
    }
    const auto radio_left_pulses =
        BuildPlaybackPulses(loaded.samples[1], radio_left_mask, loaded.metadata.resolution_mode);
    if (radio_left_pulses[2] != 555 || radio_left_pulses[3] != -666 ||
        PlaybackChannelUsesInputInjection(radio_left_mask, profile, 2) ||
        PlaybackChannelUsesInputInjection(radio_left_mask, profile, 3)) {
        std::fprintf(stderr, "--recording-self-test radio-left pulse validation failed.\n");
        return 1;
    }

    TrainerProfile no_left_source_profile = profile;
    no_left_source_profile.mouse_left.enabled = false;
    no_left_source_profile.right_mouse_left.enabled = false;
    PlaybackChannelMask unavailable_left_input_mask;
    if (!ParsePlaybackChannelMask("thr,rud", &unavailable_left_input_mask) ||
        !ResolveIntegratedPlaybackMaskForProfile(&unavailable_left_input_mask,
                                                 no_left_source_profile,
                                                 loaded,
                                                 "self-test",
                                                 false) ||
        DescribePlaybackChannelMask(unavailable_left_input_mask) !=
            "radio_thr,radio_rud") {
        std::fprintf(stderr,
                     "--recording-self-test unavailable left input HID fallback failed.\n");
        return 1;
    }
    LoadedRecording no_hid_loaded = loaded;
    no_hid_loaded.has_hid_samples = false;
    PlaybackChannelMask unavailable_left_no_hid_mask;
    if (!ParsePlaybackChannelMask("thr,rud", &unavailable_left_no_hid_mask) ||
        ResolveIntegratedPlaybackMaskForProfile(&unavailable_left_no_hid_mask,
                                                no_left_source_profile,
                                                no_hid_loaded,
                                                "self-test",
                                                false)) {
        std::fprintf(stderr,
                     "--recording-self-test unavailable left input no-HID failure check failed.\n");
        return 1;
    }

    PlaybackChannelMask gimbals_mask;
    if (!ParsePlaybackChannelMask("gimbals", &gimbals_mask)) {
        std::fprintf(stderr, "--recording-self-test channel parser failed for gimbals.\n");
        return 1;
    }
    const auto gimbals_pulses =
        BuildPlaybackPulses(loaded.samples[1], gimbals_mask, loaded.metadata.resolution_mode);
    if (gimbals_pulses[0] != 11 || gimbals_pulses[1] != -22 ||
        gimbals_pulses[2] != 555 || gimbals_pulses[3] != -666 ||
        DescribePlaybackChannelMask(gimbals_mask) != "radio_ail,radio_ele,radio_thr,radio_rud") {
        std::fprintf(stderr, "--recording-self-test all-gimbal pulse validation failed.\n");
        return 1;
    }

    PlaybackTrigger trigger;
    if (!ParsePlaybackTrigger("Mouse4", &trigger) || trigger.virtual_key != VK_XBUTTON1) {
        std::fprintf(stderr, "--recording-self-test trigger parser failed.\n");
        return 1;
    }
    if (ParseTriggerVirtualKeyName("Mouse5") != VK_XBUTTON2) {
        std::fprintf(stderr, "--recording-self-test record-toggle mouse parser failed.\n");
        return 1;
    }

    const std::filesystem::path control_path =
        std::filesystem::path("logs") / "runtime-control-self-test.tsv";
    {
        std::ofstream control(control_path);
        control << "# gx12_runtime_control=1\n"
                << "recording_path\tlogs\\live.gx12rec.csv\n"
                << "record_duration\t30\n"
                << "record_toggle\tF4\n"
                << "record_overwrite\t1\n"
                << "playback_loop\t0\n"
                << "bind\tF5\tail,ele\tlogs\\live.gx12rec.csv\t1\n";
    }
    TrainerRuntimeControlSnapshot control_snapshot;
    std::string control_error;
    if (!LoadTrainerRuntimeControlFile(control_path.string(), &control_snapshot, &control_error) ||
        control_snapshot.recording.path != "logs\\live.gx12rec.csv" ||
        control_snapshot.recording.max_seconds != 30 ||
        control_snapshot.recording.toggle_key_vk != VK_F4 ||
        !control_snapshot.recording.overwrite_existing ||
        control_snapshot.playback_bank.loop ||
        control_snapshot.playback_bank.specs.size() != 1 ||
        control_snapshot.playback_bank.specs[0].trigger.virtual_key != VK_F5 ||
        !control_snapshot.playback_bank.specs[0].mask.UsesRightStick() ||
        !control_snapshot.playback_bank.specs[0].block_live_input) {
        std::fprintf(stderr,
                     "--recording-self-test runtime control parser failed: %s\n",
                     control_error.c_str());
        return 1;
    }

    RecordingPlaybackAuditResult audit_result;
    std::string audit_error;
    if (!RunSyntheticRecordingPlaybackAudit(&audit_result, &audit_error)) {
        std::fprintf(stderr,
                     "--recording-self-test synthetic playback audit failed: %s\n",
                     audit_error.c_str());
        return 1;
    }
    if (audit_result.current_max_abs_error != 0 ||
        audit_result.legacy_timestamp_max_abs_error <= 0 ||
        audit_result.legacy_timestamp_empty_ticks == 0) {
        std::fprintf(stderr,
                     "--recording-self-test synthetic playback audit deviation check failed: "
                     "current_max=%d legacy_timestamp_max=%d legacy_empty_ticks=%u\n",
                     audit_result.current_max_abs_error,
                     audit_result.legacy_timestamp_max_abs_error,
                     audit_result.legacy_timestamp_empty_ticks);
        return 1;
    }
    std::printf("--recording-self-test synthetic playback audit: samples=%zu mapper_ticks=%u "
                "current_max_dev=%d legacy_timestamp_max_dev=%d legacy_empty_ticks=%u\n",
                audit_result.samples,
                audit_result.mapper_ticks,
                audit_result.current_max_abs_error,
                audit_result.legacy_timestamp_max_abs_error,
                audit_result.legacy_timestamp_empty_ticks);

    std::printf("\n--recording-self-test PASS path=%s samples=%zu\n",
                test_path.string().c_str(),
                loaded.samples.size());
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
        csv << "elapsed_ms,elapsed_us,dt_us,report,buttons";
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
    auto last_print_time = start;
    std::array<unsigned char, 64> buffer{};
    std::array<Gx12HidChannelGranularity, kGx12ChannelCount> channels{};
    HidReportTiming timing;
    uint64_t reports = 0;
    uint64_t last_print_reports = 0;
    uint64_t timeouts = 0;
    uint64_t errors = 0;
    std::string last_error;

    while (clock::now() < end) {
        const int read = hid_read_timeout(device, buffer.data(), buffer.size(), 1);
        const auto now = clock::now();
        if (read > 0) {
            Gx12DecodedReport decoded{};
            if (DecodeGx12Report(buffer.data(), read, &decoded)) {
                ++reports;
                const double elapsed_ms =
                    std::chrono::duration<double, std::milli>(now - start).count();
                const auto elapsed_us =
                    std::chrono::duration_cast<std::chrono::microseconds>(now - start)
                        .count();
                const uint64_t dt_us = timing.Record(now);

                if (csv) {
                    csv << elapsed_ms << "," << elapsed_us << "," << dt_us << ","
                        << reports << "," << decoded.buttons;
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

        if (now >= next_print) {
            const double elapsed = std::chrono::duration<double>(now - start).count();
            const double window_seconds =
                std::chrono::duration<double>(now - last_print_time).count();
            const uint64_t window_reports = reports - last_print_reports;
            const double window_hz = window_seconds > 0.0
                                         ? static_cast<double>(window_reports) / window_seconds
                                         : 0.0;
            std::printf("[%.3fs] reports=%6llu rate=%7.1f Hz ch1=%4d ch2=%4d ch3=%4d ch4=%4d timeout=%llu err=%llu\n",
                        elapsed,
                        static_cast<unsigned long long>(reports),
                        window_hz,
                        channels[0].last_raw,
                        channels[1].last_raw,
                        channels[2].last_raw,
                        channels[3].last_raw,
                        static_cast<unsigned long long>(timeouts),
                        static_cast<unsigned long long>(errors));
            std::fflush(stdout);
            last_print_reports = reports;
            last_print_time = now;
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
    if (timing.intervals > 0) {
        std::printf("report_interval: avg=%.3f ms min=%.3f ms max=%.3f ms samples=%llu\n",
                    timing.AverageIntervalUs() / 1000.0,
                    static_cast<double>(timing.min_interval_us) / 1000.0,
                    static_cast<double>(timing.max_interval_us) / 1000.0,
                    static_cast<unsigned long long>(timing.intervals));
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

    if (std::strcmp(argv[1], "--trainer-record") == 0) {
        if (argc < 4) {
            std::fprintf(stderr, "--trainer-record requires a profile file and output recording, for example profiles\\whoop-linear.toml logs\\run.gx12rec.csv.\n");
            return 2;
        }

        TrainerProfile profile;
        if (!LoadTrainerProfile(argv[2], &profile)) {
            return 2;
        }
        bool live_reload = false;
        bool saw_duration = false;
        int recording_duration_seconds = 0;
        TrainerRecordingOptions recording_options;
        TrainerRuntimeControlOptions runtime_control_options;
        recording_options.path = argv[3];
        recording_options.start_immediately = true;
        auto set_record_toggle_key = [&](const std::string& text) {
            const int vk = ParseTriggerVirtualKeyName(text);
            if (vk <= 0) {
                return false;
            }
            recording_options.toggle_key_vk = vk;
            recording_options.toggle_key_label = TrimAscii(text);
            return true;
        };
        for (int i = 4; i < argc; ++i) {
            const std::string arg = argv[i];
            const std::string lowered = ToLowerAscii(arg);
            if (lowered == "live" || lowered == "--live") {
                live_reload = true;
                continue;
            }
            if (lowered == "--record-overwrite" ||
                lowered == "--recording-overwrite" ||
                lowered == "--overwrite-recording") {
                recording_options.overwrite_existing = true;
                continue;
            }
            if (lowered.rfind("--runtime-control=", 0) == 0 ||
                lowered.rfind("--control=", 0) == 0) {
                const size_t equals = arg.find('=');
                runtime_control_options.path = equals == std::string::npos ? "" : arg.substr(equals + 1);
                continue;
            }
            if (lowered == "--runtime-control" || lowered == "--control") {
                if (i + 1 >= argc) {
                    std::fprintf(stderr, "%s needs a runtime control file path.\n", arg.c_str());
                    return 2;
                }
                runtime_control_options.path = argv[++i];
                continue;
            }
            if (lowered.rfind("--record-toggle=", 0) == 0 ||
                lowered.rfind("--recording-toggle=", 0) == 0 ||
                lowered.rfind("--record-key=", 0) == 0 ||
                lowered.rfind("--recording-key=", 0) == 0) {
                const size_t equals = arg.find('=');
                const std::string value = equals == std::string::npos ? "" : arg.substr(equals + 1);
                if (!set_record_toggle_key(value)) {
                    std::fprintf(stderr, "invalid --trainer-record toggle key/button: %s\n", arg.c_str());
                    return 2;
                }
                continue;
            }
            if (lowered == "--record-toggle" ||
                lowered == "--recording-toggle" ||
                lowered == "--record-key" ||
                lowered == "--recording-key") {
                if (i + 1 >= argc) {
                    std::fprintf(stderr, "%s needs a key or mouse button name such as F4, Space, or Mouse4.\n", arg.c_str());
                    return 2;
                }
                if (!set_record_toggle_key(argv[++i])) {
                    std::fprintf(stderr, "invalid --trainer-record toggle key/button: %s\n", argv[i]);
                    return 2;
                }
                continue;
            }
            if (saw_duration) {
                std::fprintf(stderr, "duplicate --trainer-record duration override: %s\n", arg.c_str());
                return 2;
            }
            profile.seconds = ParsePositiveIntLimit(arg.c_str(),
                                                    profile.seconds,
                                                    kTrainerProfileMaxSeconds);
            if (profile.seconds <= 0) {
                std::fprintf(stderr, "invalid --trainer-record duration override: %s\n", arg.c_str());
                return 2;
            }
            recording_duration_seconds = profile.seconds;
            saw_duration = true;
            if (!ValidateTrainerProfile(profile)) {
                return 2;
            }
        }
        if (recording_options.ToggleMode()) {
            recording_options.start_immediately = false;
            recording_options.max_seconds = saw_duration ? recording_duration_seconds : 0;
            profile.seconds = kTrainerProfileIndefiniteSeconds;
        }
        return RunTrainerProfile(profile, false, live_reload, recording_options, TrainerPlaybackBankOptions{}, runtime_control_options);
    }

    if (std::strcmp(argv[1], "--trainer-playback") == 0) {
        if (argc < 3) {
            std::fprintf(stderr, "--trainer-playback requires a recording file.\n");
            return 2;
        }

        std::string port = "auto";
        bool port_explicit = false;
        bool loop = false;
        PlaybackChannelMask mask;
        bool channel_mask_specified = false;
        PlaybackTrigger trigger;
        for (int i = 3; i < argc; ++i) {
            std::string arg = argv[i];
            const std::string lowered = ToLowerAscii(arg);
            if (lowered == "once" || lowered == "--once") {
                loop = false;
                continue;
            }
            if (lowered == "loop" || lowered == "--loop") {
                loop = true;
                continue;
            }
            if (lowered.rfind("--port=", 0) == 0) {
                port = arg.substr(7);
                port_explicit = true;
                continue;
            }
            if (lowered == "--port") {
                if (i + 1 >= argc) {
                    std::fprintf(stderr, "--trainer-playback --port needs COM3 or auto.\n");
                    return 2;
                }
                port = argv[++i];
                port_explicit = true;
                continue;
            }
            if (lowered.rfind("--channels=", 0) == 0) {
                if (!channel_mask_specified) {
                    ClearPlaybackChannelMask(&mask);
                    channel_mask_specified = true;
                }
                if (!ParsePlaybackChannelMask(arg.substr(11), &mask)) {
                    std::fprintf(stderr, "invalid --trainer-playback channel mask: %s\n", arg.c_str());
                    return 2;
                }
                continue;
            }
            if (lowered.rfind("channels=", 0) == 0) {
                if (!channel_mask_specified) {
                    ClearPlaybackChannelMask(&mask);
                    channel_mask_specified = true;
                }
                if (!ParsePlaybackChannelMask(arg.substr(9), &mask)) {
                    std::fprintf(stderr, "invalid --trainer-playback channel mask: %s\n", arg.c_str());
                    return 2;
                }
                continue;
            }
            if (lowered == "--channels" || lowered == "--play") {
                if (i + 1 >= argc) {
                    std::fprintf(stderr, "--trainer-playback %s needs a channel mask.\n", arg.c_str());
                    return 2;
                }
                if (!channel_mask_specified) {
                    ClearPlaybackChannelMask(&mask);
                    channel_mask_specified = true;
                }
                if (!ParsePlaybackChannelMask(argv[++i], &mask)) {
                    std::fprintf(stderr, "invalid --trainer-playback channel mask: %s\n", argv[i]);
                    return 2;
                }
                continue;
            }
            if (lowered.rfind("--trigger=", 0) == 0) {
                if (!ParsePlaybackTrigger(arg.substr(10), &trigger)) {
                    std::fprintf(stderr, "invalid --trainer-playback trigger: %s\n", arg.c_str());
                    return 2;
                }
                continue;
            }
            if (lowered == "--trigger") {
                if (i + 1 >= argc) {
                    std::fprintf(stderr, "--trainer-playback --trigger needs a key or mouse button name.\n");
                    return 2;
                }
                if (!ParsePlaybackTrigger(argv[++i], &trigger)) {
                    std::fprintf(stderr, "invalid --trainer-playback trigger: %s\n", argv[i]);
                    return 2;
                }
                continue;
            }
            if (!port_explicit && LooksLikeComPortArgument(arg)) {
                port = arg;
                port_explicit = true;
                continue;
            }
            if (!channel_mask_specified) {
                ClearPlaybackChannelMask(&mask);
                channel_mask_specified = true;
            }
            if (!ParsePlaybackChannelMask(arg, &mask)) {
                std::fprintf(stderr,
                             "unrecognized --trainer-playback argument: %s\n",
                             arg.c_str());
                return 2;
            }
        }
        if (!channel_mask_specified) {
            mask = DefaultPlaybackChannelMask();
        }
        return RunTrainerPlayback(argv[2], port, loop, mask, trigger);
    }

    if (std::strcmp(argv[1], "--trainer-playback-bank") == 0) {
        std::string port = "auto";
        bool port_explicit = false;
        bool loop = false;
        std::vector<PlaybackBankSlotSpec> specs;
        for (int i = 2; i < argc; ++i) {
            std::string arg = argv[i];
            const std::string lowered = ToLowerAscii(arg);
            if (lowered == "once" || lowered == "--once") {
                loop = false;
                continue;
            }
            if (lowered == "loop" || lowered == "--loop") {
                loop = true;
                continue;
            }
            if (lowered.rfind("--port=", 0) == 0) {
                port = arg.substr(7);
                port_explicit = true;
                continue;
            }
            if (lowered == "--port") {
                if (i + 1 >= argc) {
                    std::fprintf(stderr, "--trainer-playback-bank --port needs COM3 or auto.\n");
                    return 2;
                }
                port = argv[++i];
                port_explicit = true;
                continue;
            }
            bool bind_blocks_live_input = false;
            if (ParsePlaybackBindOptionName(lowered, &bind_blocks_live_input)) {
                if (i + 3 >= argc) {
                    std::fprintf(stderr,
                                 "--trainer-playback-bank %s needs KEY CHANNELS RECORDING.\n",
                                 arg.c_str());
                    return 2;
                }
                if (specs.size() >= kMaxPlaybackBankSlots) {
                    std::fprintf(stderr,
                                 "--trainer-playback-bank supports at most %zu binding slots.\n",
                                 kMaxPlaybackBankSlots);
                    return 2;
                }

                PlaybackBankSlotSpec spec;
                spec.block_live_input = bind_blocks_live_input;
                const std::string trigger_text = argv[++i];
                const std::string channels_text = argv[++i];
                spec.recording_path = argv[++i];
                ClearPlaybackChannelMask(&spec.mask);
                if (!ParsePlaybackTrigger(trigger_text, &spec.trigger)) {
                    std::fprintf(stderr,
                                 "invalid --trainer-playback-bank bind trigger: %s\n",
                                 trigger_text.c_str());
                    return 2;
                }
                if (!ParsePlaybackChannelMask(channels_text, &spec.mask)) {
                    std::fprintf(stderr,
                                 "invalid --trainer-playback-bank bind channel mask: %s\n",
                                 channels_text.c_str());
                    return 2;
                }
                specs.push_back(std::move(spec));
                continue;
            }
            if (!port_explicit && LooksLikeComPortArgument(arg)) {
                port = arg;
                port_explicit = true;
                continue;
            }

            std::fprintf(stderr,
                         "unrecognized --trainer-playback-bank argument: %s\n",
                         arg.c_str());
            return 2;
        }
        return RunTrainerPlaybackBank(port, loop, specs);
    }

    if (std::strcmp(argv[1], "--recording-info") == 0) {
        if (argc < 3) {
            std::fprintf(stderr, "--recording-info requires a recording file.\n");
            return 2;
        }
        if (argc > 3) {
            std::fprintf(stderr, "too many arguments for --recording-info.\n");
            return 2;
        }
        return RunRecordingInfo(argv[2]);
    }

    if (std::strcmp(argv[1], "--recording-determinism-audit") == 0) {
        if (argc < 4) {
            std::fprintf(stderr,
                         "--recording-determinism-audit requires RECORDING PROFILE [CHANNELS] [--timed-runs=N].\n");
            return 2;
        }

        PlaybackChannelMask mask = DefaultPlaybackChannelMask();
        bool channel_mask_specified = false;
        int timed_runs = 0;
        for (int i = 4; i < argc; ++i) {
            const std::string arg = argv[i];
            const std::string lowered = ToLowerAscii(arg);
            if (lowered.rfind("--timed-runs=", 0) == 0) {
                timed_runs = ParsePositiveIntLimit(arg.substr(13).c_str(), 0, 20);
                if (timed_runs <= 0) {
                    std::fprintf(stderr,
                                 "invalid --recording-determinism-audit timed run count: %s\n",
                                 arg.c_str());
                    return 2;
                }
                continue;
            }
            if (lowered == "--timed-runs") {
                if (i + 1 >= argc) {
                    std::fprintf(stderr,
                                 "--recording-determinism-audit --timed-runs needs a positive run count.\n");
                    return 2;
                }
                timed_runs = ParsePositiveIntLimit(argv[++i], 0, 20);
                if (timed_runs <= 0) {
                    std::fprintf(stderr,
                                 "invalid --recording-determinism-audit timed run count: %s\n",
                                 argv[i]);
                    return 2;
                }
                continue;
            }
            if (lowered.rfind("--channels=", 0) == 0) {
                if (!channel_mask_specified) {
                    ClearPlaybackChannelMask(&mask);
                    channel_mask_specified = true;
                }
                if (!ParsePlaybackChannelMask(arg.substr(11), &mask)) {
                    std::fprintf(stderr,
                                 "invalid --recording-determinism-audit channel mask: %s\n",
                                 arg.c_str());
                    return 2;
                }
                continue;
            }
            if (lowered == "--channels") {
                if (i + 1 >= argc) {
                    std::fprintf(stderr,
                                 "--recording-determinism-audit --channels needs a channel mask.\n");
                    return 2;
                }
                if (!channel_mask_specified) {
                    ClearPlaybackChannelMask(&mask);
                    channel_mask_specified = true;
                }
                if (!ParsePlaybackChannelMask(argv[++i], &mask)) {
                    std::fprintf(stderr,
                                 "invalid --recording-determinism-audit channel mask: %s\n",
                                 argv[i]);
                    return 2;
                }
                continue;
            }

            if (!channel_mask_specified) {
                ClearPlaybackChannelMask(&mask);
                channel_mask_specified = true;
            }
            if (!ParsePlaybackChannelMask(arg, &mask)) {
                std::fprintf(stderr,
                             "unrecognized --recording-determinism-audit argument: %s\n",
                             arg.c_str());
                return 2;
            }
        }
        if (!mask.Any()) {
            std::fprintf(stderr, "--recording-determinism-audit needs at least one playback channel.\n");
            return 2;
        }
        return RunRecordingDeterminismAudit(argv[2], argv[3], mask, timed_runs);
    }

    if (std::strcmp(argv[1], "--recording-self-test") == 0) {
        if (argc != 2) {
            std::fprintf(stderr, "too many arguments for --recording-self-test.\n");
            return 2;
        }
        return RunRecordingSelfTest();
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
        bool saw_recording_duration = false;
        int recording_duration_seconds = 0;
        TrainerRecordingOptions recording_options;
        TrainerPlaybackBankOptions playback_bank_options;
        TrainerRuntimeControlOptions runtime_control_options;
        auto set_record_toggle_key = [&](const std::string& text) {
            const std::string lowered = ToLowerAscii(TrimAscii(text));
            if (lowered.empty() ||
                lowered == "off" ||
                lowered == "none" ||
                lowered == "immediate") {
                recording_options.toggle_key_vk = 0;
                recording_options.toggle_key_label.clear();
                return true;
            }
            const int vk = ParseTriggerVirtualKeyName(text);
            if (vk <= 0) {
                return false;
            }
            recording_options.toggle_key_vk = vk;
            recording_options.toggle_key_label = TrimAscii(text);
            return true;
        };
        auto set_recording_duration = [&](const std::string& text) {
            const int parsed = ParsePositiveIntLimit(text.c_str(),
                                                     0,
                                                     kTrainerProfileMaxSeconds);
            if (parsed <= 0) {
                return false;
            }
            recording_duration_seconds = parsed;
            saw_recording_duration = true;
            return true;
        };
        for (int i = 3; i < argc; ++i) {
            const std::string arg = argv[i];
            const std::string lowered = ToLowerAscii(arg);
            if (lowered == "live" || lowered == "--live") {
                live_reload = true;
                continue;
            }
            if (lowered == "--record-overwrite" ||
                lowered == "--recording-overwrite" ||
                lowered == "--overwrite-recording") {
                recording_options.overwrite_existing = true;
                continue;
            }
            if (lowered.rfind("--runtime-control=", 0) == 0 ||
                lowered.rfind("--control=", 0) == 0) {
                const size_t equals = arg.find('=');
                runtime_control_options.path = equals == std::string::npos ? "" : arg.substr(equals + 1);
                continue;
            }
            if (lowered == "--runtime-control" || lowered == "--control") {
                if (i + 1 >= argc) {
                    std::fprintf(stderr, "%s needs a runtime control file path.\n", arg.c_str());
                    return 2;
                }
                runtime_control_options.path = argv[++i];
                continue;
            }
            if (lowered.rfind("--recording=", 0) == 0 ||
                lowered.rfind("--record=", 0) == 0) {
                const size_t equals = arg.find('=');
                recording_options.path = equals == std::string::npos ? "" : arg.substr(equals + 1);
                continue;
            }
            if (lowered == "--recording" || lowered == "--record") {
                if (i + 1 >= argc) {
                    std::fprintf(stderr, "%s needs an output recording path.\n", arg.c_str());
                    return 2;
                }
                recording_options.path = argv[++i];
                continue;
            }
            if (lowered.rfind("--record-duration=", 0) == 0 ||
                lowered.rfind("--record-seconds=", 0) == 0 ||
                lowered.rfind("--recording-duration=", 0) == 0 ||
                lowered.rfind("--recording-seconds=", 0) == 0) {
                const size_t equals = arg.find('=');
                const std::string value = equals == std::string::npos ? "" : arg.substr(equals + 1);
                if (!set_recording_duration(value)) {
                    std::fprintf(stderr, "invalid %s value: %s\n", argv[1], arg.c_str());
                    return 2;
                }
                continue;
            }
            if (lowered == "--record-duration" ||
                lowered == "--record-seconds" ||
                lowered == "--recording-duration" ||
                lowered == "--recording-seconds") {
                if (i + 1 >= argc) {
                    std::fprintf(stderr, "%s needs seconds.\n", arg.c_str());
                    return 2;
                }
                if (!set_recording_duration(argv[++i])) {
                    std::fprintf(stderr, "invalid %s value: %s\n", arg.c_str(), argv[i]);
                    return 2;
                }
                continue;
            }
            if (lowered.rfind("--record-toggle=", 0) == 0 ||
                lowered.rfind("--recording-toggle=", 0) == 0 ||
                lowered.rfind("--record-key=", 0) == 0 ||
                lowered.rfind("--recording-key=", 0) == 0) {
                const size_t equals = arg.find('=');
                const std::string value = equals == std::string::npos ? "" : arg.substr(equals + 1);
                if (!set_record_toggle_key(value)) {
                    std::fprintf(stderr, "invalid %s recording toggle key/button: %s\n", argv[1], arg.c_str());
                    return 2;
                }
                continue;
            }
            if (lowered == "--record-toggle" ||
                lowered == "--recording-toggle" ||
                lowered == "--record-key" ||
                lowered == "--recording-key") {
                if (i + 1 >= argc) {
                    std::fprintf(stderr, "%s needs a key or mouse button name such as F4, Space, or Mouse4.\n", arg.c_str());
                    return 2;
                }
                if (!set_record_toggle_key(argv[++i])) {
                    std::fprintf(stderr, "invalid %s recording toggle key/button: %s\n", argv[1], argv[i]);
                    return 2;
                }
                continue;
            }
            if (lowered == "--playback-loop" ||
                lowered == "--bank-loop" ||
                lowered == "--bind-loop") {
                playback_bank_options.loop = true;
                continue;
            }
            if (lowered == "--playback-once" ||
                lowered == "--bank-once" ||
                lowered == "--bind-once") {
                playback_bank_options.loop = false;
                continue;
            }
            bool bind_blocks_live_input = false;
            if (ParsePlaybackBindOptionName(lowered, &bind_blocks_live_input)) {
                if (i + 3 >= argc) {
                    std::fprintf(stderr,
                                 "%s %s needs KEY CHANNELS RECORDING.\n",
                                 argv[1],
                                 arg.c_str());
                    return 2;
                }
                if (playback_bank_options.specs.size() >= kMaxPlaybackBankSlots) {
                    std::fprintf(stderr,
                                 "%s supports at most %zu playback binding slots.\n",
                                 argv[1],
                                 kMaxPlaybackBankSlots);
                    return 2;
                }

                PlaybackBankSlotSpec spec;
                spec.block_live_input = bind_blocks_live_input;
                const std::string trigger_text = argv[++i];
                const std::string channels_text = argv[++i];
                spec.recording_path = argv[++i];
                ClearPlaybackChannelMask(&spec.mask);
                if (!ParsePlaybackTrigger(trigger_text, &spec.trigger)) {
                    std::fprintf(stderr,
                                 "invalid %s playback bind trigger: %s\n",
                                 argv[1],
                                 trigger_text.c_str());
                    return 2;
                }
                if (!ParsePlaybackChannelMask(channels_text, &spec.mask)) {
                    std::fprintf(stderr,
                                 "invalid %s playback bind channel mask: %s\n",
                                 argv[1],
                                 channels_text.c_str());
                    return 2;
                }
                playback_bank_options.specs.push_back(std::move(spec));
                continue;
            }
            if (saw_duration) {
                std::fprintf(stderr, "duplicate %s duration override: %s\n", argv[1], arg.c_str());
                return 2;
            }
            profile.seconds = ParsePositiveIntLimit(arg.c_str(),
                                                    profile.seconds,
                                                    kTrainerProfileMaxSeconds);
            if (profile.seconds <= 0) {
                std::fprintf(stderr, "invalid %s duration override: %s\n", argv[1], arg.c_str());
                return 2;
            }
            saw_duration = true;
            if (!ValidateTrainerProfile(profile)) {
                return 2;
            }
        }
        if (recording_options.Enabled()) {
            recording_options.max_seconds = saw_recording_duration ? recording_duration_seconds : 0;
            recording_options.start_immediately = !recording_options.ToggleMode();
        }
        const bool guided = std::strcmp(argv[1], "--tune") == 0;
        return RunTrainerProfile(profile, guided, live_reload, recording_options, playback_bank_options, runtime_control_options);
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
