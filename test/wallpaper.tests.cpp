#ifndef UNICODE
#define UNICODE
#endif
#define _WIN32_WINNT 0x0A00
#include "WallpaperTestSupport.h"
#include <algorithm>

static bool pass = true;
static void Check(bool condition, const char *name) {
    printf("%s %s\n", condition ? "PASS" : "FAIL", name);
    pass &= condition;
}
static std::vector<unsigned char> Edid() {
    std::vector<unsigned char> bytes(128);
    const unsigned char header[] = {0, 255, 255, 255, 255, 255, 255, 0};
    std::copy(std::begin(header), std::end(header), bytes.begin());
    unsigned manufacturer = (1 << 10) | (2 << 5) | 3;
    bytes[8] = manufacturer >> 8;
    bytes[9] = manufacturer;
    bytes[10] = 0x34;
    bytes[11] = 0x12;
    bytes[57] = 0xff;
    const char serial[] = "Serial ABC\n";
    std::copy(serial, serial + sizeof(serial) - 1, bytes.begin() + 59);
    bytes[75] = 0xfc;
    const char name[] = "Test Monitor\n";
    std::copy(name, name + sizeof(name) - 1, bytes.begin() + 77);
    unsigned sum = 0;
    for (int n = 0; n < 127; ++n)
        sum += bytes[n];
    bytes[127] = (256 - sum % 256) % 256;
    return bytes;
}
int wmain(int argc, wchar_t **argv) {
    if (argc < 2)
        return 2;
    std::wstring directory = argv[1], first = directory + L"\\wallpaper-a.bmp",
                 second = directory + L"\\wallpaper-b.bmp",
                 invalid = directory + L"\\wallpaper-invalid.bmp";
    Check(WallpaperTest::MakeImage(first) && WallpaperTest::MakeImage(second),
          "isolated test images created");
    auto bytes = Edid();
    Wallpaper::Monitor decoded;
    Check(Wallpaper::DecodeEdid(bytes, decoded) && decoded.manufacturer == L"ABC" &&
              decoded.product == L"1234" && decoded.serial == L"Serial ABC" &&
              decoded.name == L"Test Monitor",
          "EDID manufacturer, product, descriptor serial and model are decoded generically");
    bytes[20] ^= 1;
    Check(!Wallpaper::DecodeEdid(bytes, decoded), "corrupt EDID checksum is rejected");
    auto monitor = WallpaperTest::Fixture(first).monitors[0], moved = monitor;
    moved.devicePath = L"new-port";
    std::vector<Wallpaper::Monitor> one{monitor}, two{moved};
    Wallpaper::AssignIdentities(one);
    Wallpaper::AssignIdentities(two);
    Check(one[0].identity == two[0].identity && one[0].portableIdentity,
          "hardware serial identity survives source numbering, position and port changes");
    std::vector<Wallpaper::Monitor> duplicates{monitor, moved};
    Wallpaper::AssignIdentities(duplicates);
    Check(!duplicates[0].portableIdentity && duplicates[0].identity != duplicates[1].identity,
          "duplicate serials fall back to distinct device paths instead of guessing");
    monitor.serial.clear();
    one = {monitor};
    Wallpaper::AssignIdentities(one);
    Check(!one[0].portableIdentity && one[0].identity.find(L"device|") == 0,
          "missing serial uses a clearly scoped connection identity");
    WallpaperTest::Backend backend;
    WallpaperTest::snapshot = WallpaperTest::Fixture(first);
    auto a = WallpaperTest::snapshot.monitors[0], b = WallpaperTest::snapshot.monitors[1];
    std::wstring error;
    Check(Wallpaper::Apply({{a, second}}, DWPOS_FILL, false, error) &&
              WallpaperTest::writes.size() == 1 && WallpaperTest::writes[0].first == a.devicePath &&
              WallpaperTest::snapshot.monitors[1].image == first,
          "single-monitor apply addresses the exact device path and leaves the other monitor "
          "untouched");
    WallpaperTest::Reset();
    WallpaperTest::snapshot = WallpaperTest::Fixture(first);
    WallpaperTest::snapshot.monitors[0].connected = false;
    Check(!Wallpaper::Apply({{a, second}}, DWPOS_FILL, false, error) &&
              WallpaperTest::writes.empty(),
          "unplugged display is rejected before any wallpaper write");
    WallpaperTest::snapshot = WallpaperTest::Fixture(first);
    WallpaperTest::snapshot.monitors[0].identity += L"changed";
    Check(!Wallpaper::Apply({{a, second}}, DWPOS_FILL, false, error) &&
              WallpaperTest::writes.empty(),
          "replacement display on the same port cannot inherit the previous target");
    WallpaperTest::snapshot = WallpaperTest::Fixture(first);
    WallpaperTest::snapshot.monitors[1].rect = WallpaperTest::snapshot.monitors[0].rect;
    Check(!Wallpaper::Apply({{a, second}}, DWPOS_FILL, false, error) &&
              WallpaperTest::writes.empty(),
          "cloned displays cannot silently accept independent wallpapers");
    WallpaperTest::snapshot = WallpaperTest::Fixture(first);
    Check(!Wallpaper::Apply({{a, second}, {a, first}}, DWPOS_FILL, false, error) &&
              WallpaperTest::writes.empty(),
          "duplicate targets are rejected before writes");
    Check(!Wallpaper::Apply({{a, L"relative.bmp"}}, DWPOS_FILL, false, error) &&
              WallpaperTest::writes.empty(),
          "relative or missing images cannot be applied");
    FILE *file = _wfopen(invalid.c_str(), L"wb");
    if (file) {
        fputs("not an image", file);
        fclose(file);
    }
    Check(!Wallpaper::Apply({{a, invalid}}, DWPOS_FILL, false, error) &&
              WallpaperTest::writes.empty(),
          "invalid bitmap content is rejected by image decoding");
    WallpaperTest::Reset();
    WallpaperTest::rejectWrite = 2;
    Check(!Wallpaper::Apply({{a, second}, {b, second}}, DWPOS_FIT, true, error) &&
              WallpaperTest::snapshot.monitors[0].image == first &&
              WallpaperTest::snapshot.monitors[1].image == first &&
              WallpaperTest::snapshot.position == DWPOS_FILL &&
              WallpaperTest::positions.size() == 2,
          "failure on second display rolls back first display and global position");
    WallpaperTest::Reset();
    Check(Wallpaper::Apply({}, DWPOS_FILL, false, error) && WallpaperTest::writes.empty() &&
              WallpaperTest::positions.empty(),
          "opening and cancelling without edits performs no system writes");
    std::wstring defaults = directory + L"\\wallpapers.ini";
    FILE *config = _wfopen(defaults.c_str(), L"wb");
    if (config) {
        fputs("[ByResolution]\n640x480=wallpaper-a.bmp\n", config);
        fclose(config);
    }
    Check(Wallpaper::DefaultImage(directory, 640, 480) == first &&
              Wallpaper::DefaultImage(directory, 800, 600).empty(),
          "default image uses the exact monitor resolution and resolves paths against the portable "
          "root");
    DeleteFileW(defaults.c_str());
    DeleteFileW(first.c_str());
    DeleteFileW(second.c_str());
    DeleteFileW(invalid.c_str());
    return pass ? 0 : 1;
}
