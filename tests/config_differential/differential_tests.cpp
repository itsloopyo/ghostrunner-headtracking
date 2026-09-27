// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// The differential test for the conversion from HeadTracking.ini to
// CameraUnlock.ini.
//
// Three readings of every input, and what may differ between them:
//
//   Oracle     the reader of the newest published build (the dev pre-release,
//              84353ca, core daa973c), compiled from its own sources
//              (oracle_api.h)
//   Import     the frozen reader in src/legacy_config/
//   Migration  the config owner's Load in a folder holding only the input as
//              HeadTracking.ini, which imports it through config::Import into a
//              new CameraUnlock.ini, then the canonical reader and table on it
//
// Comparison 1, oracle against import, is what a player sees change that the
// conversion did not cause: commits since the published build that change how
// the file is read. The import's files and every core source both readers
// compile hash-equal the published build's, so it is empty by construction,
// and the test holds it empty.
//
// Comparison 2, import against migration, is the proof for the conversion. It
// allows no difference: the file carried no pose shaping and no reticle setting,
// the reader lets through no value that is not finite, the yaw key it keeps is
// inside 0x01-0xFE and never a Ctrl, Shift or Alt key alone (so N1 and N3 never
// apply), every key list is the one the build bound, and no default moved, so
// the no-file input may not differ either.
//
// Each input migrates three times: over a Defaults.ini the owner creates with the
// built-in values, from a read-only HeadTracking.ini, and over a Defaults.ini
// that differs from the built-in value on every global row. The first two give
// the settings the import read. Over the third, a setting the player never
// changed from the old build's value follows Defaults.ini (owner rule of
// 2026-09-26, LegacyFollowsDefaultsIni) and only a changed one stays the
// import's (OverDefaults). Over the built-in values every row the player never
// changed is written `default` and every changed one a value.
//
// Inputs: the published build's first-run file (the dev build shipped no config
// and seeded none, so every player's file started as that one), no file, an
// empty file, and core's corpus of mutations of the first-run file.

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "config.h"
#include "legacy_config/legacy_config.h"
#include "oracle_api.h"
#include "test_harness.h"

#include "cameraunlock/config/canonical_ini.h"
#include "cameraunlock/config/config_owner.h"
#include "cameraunlock/config/legacy_import.h"
#include "cameraunlock/config/testing/ini_mutations.h"
#include "cameraunlock/input/key_bindings.h"
#include "cameraunlock/tracking/tracking_mode.h"

namespace {

using cameraunlock::TrackingMode;
namespace cfg = cameraunlock::config;
namespace fs = std::filesystem;
namespace testing = cameraunlock::config::testing;

// ---- Provenance ------------------------------------------------------------
//
// Every source the oracle and the import compile, pinned by the SHA-256 of its
// bytes. The oracle's files are the published build's, taken with
// `git show 84353ca:src/<file>`. The core files both readers compile are
// hash-equal to daa973c's, so the readers differ only where the mod's own reader
// changed. The frozen import is pinned at the commit that froze it, so nothing
// edits it afterwards.

struct Pinned {
    const char* path;
    const char* sha256;
};

constexpr Pinned kPinned[] = {
    // The oracle: 84353ca:src/...
    {"tests/config_differential/oracle/src/config.cpp", "7a9a09a08835a7df5136e29dbb0d57a4cf699693e410b5e1e126ed426093b694"},
    {"tests/config_differential/oracle/src/config.h", "2f2a5a7c3cc14c04dec7e669bd7786918e7bc16692b2a34ae91fd92d07400e9f"},
    {"tests/config_differential/oracle/src/logging.h", "3fa3da4d8d462ebfb7bd8b9f930e54df0bfebbc91ba3dd1acebbdb7d31c32fc3"},
    // Both readers: core at the pin, hash-equal to daa973c:cpp/...
    {"cameraunlock-core/cpp/include/cameraunlock/config/ini_reader.h", "a7ffb44210ff59672fa97e8e5feaa2cb3e81938fcc0334a384c68bc371b3857a"},
    {"cameraunlock-core/cpp/src/config/ini_reader.cpp", "e01515c2656aaf533bae4350dc45b702c3e3d4043935743dcc9bd5ea581fbe1c"},
    {"cameraunlock-core/cpp/include/cameraunlock/config/value_guards.h", "6d3e3512bdfc9ac2a54bee750c5425191bdb4c24f3ae2ea3b9925612e01c78e5"},
    {"cameraunlock-core/cpp/src/config/value_guards.cpp", "a833ff7f2721f7974ce039a3597349a0974e6695ce3e75e598ce5bf4985a14b1"},
    {"cameraunlock-core/cpp/include/cameraunlock/math/finite_utils.h", "c59772d698d54ade3374ee1221b74f5563a86d76f0eb34a989ef7efab389c0ad"},
    {"cameraunlock-core/cpp/include/cameraunlock/protocol/port_utils.h", "91bff564d5e279b66527ec5553afcf4d591812dab0e78f71a48ef412e4db7a44"},
    {"cameraunlock-core/cpp/include/cameraunlock/logging/file_log.h", "43bdd2ef8554c78e5f440333463750c13b95110fe672b0b6e273244df9e7d169"},
    {"cameraunlock-core/cpp/src/logging/file_log.cpp", "73c53c2baa06bbfebe8211f62678aa2b60cb95f604743d3686951ba56b87ea47"},
    // The import: src/logging.h is the dev build's, the legacy folder is frozen.
    {"src/logging.h", "3fa3da4d8d462ebfb7bd8b9f930e54df0bfebbc91ba3dd1acebbdb7d31c32fc3"},
    {"src/legacy_config/legacy_config.h", "ebe70a6220b33af89e475c7512088bfbdc3c2bdc5abaf005a4d11ad19743cfe4"},
    {"src/legacy_config/legacy_config.cpp", "5a1f0693d0ec62a8e7e98b415e4c339fc0bc3b9a316dea3c05ce263c7b2eb5da"},
};

std::string ReadFileBytes(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + path);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::string Sha256Hex(const std::string& bytes) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0))) {
        throw std::runtime_error("BCryptOpenAlgorithmProvider(SHA256) failed");
    }
    BCRYPT_HASH_HANDLE hash = nullptr;
    unsigned char digest[32] = {};
    const bool ok =
        BCRYPT_SUCCESS(BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0)) &&
        BCRYPT_SUCCESS(BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data())),
                                      static_cast<ULONG>(bytes.size()), 0)) &&
        BCRYPT_SUCCESS(BCryptFinishHash(hash, digest, sizeof(digest), 0));
    if (hash) BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(alg, 0);
    if (!ok) throw std::runtime_error("SHA-256 failed");
    static const char kHex[] = "0123456789abcdef";
    std::string out;
    for (unsigned char b : digest) {
        out += kHex[b >> 4];
        out += kHex[b & 15];
    }
    return out;
}

void SourcesAreThePinnedOnes() {
    for (const Pinned& p : kPinned) {
        const std::string actual = Sha256Hex(ReadFileBytes(std::string(GR_SOURCE_DIR) + "/" + p.path));
        if (actual != p.sha256) std::printf("  %s is %s\n", p.path, actual.c_str());
        CHECK_MSG(actual == p.sha256, p.path);
    }
}

// ---- Scratch folders ---------------------------------------------------------
//
// One folder per input: GetPrivateProfileString, which both readers sit on, is
// free to cache the file it last read. `dir` stands for the folder holding the
// game exe; Defaults.ini sits in `global` beside it.

class Scratch {
public:
    Scratch() {
        static unsigned s_next = 0;
        wchar_t temp[MAX_PATH + 1] = {};
        if (GetTempPathW(MAX_PATH + 1, temp) == 0) throw std::runtime_error("GetTempPathW failed");
        root_ = fs::path(temp) /
                ("gr_ht_diff_" + std::to_string(GetCurrentProcessId()) + "_" + std::to_string(s_next++));
        Remove();
        fs::create_directories(root_ / "game");
    }
    Scratch(const Scratch&) = delete;
    Scratch& operator=(const Scratch&) = delete;
    // A scanner can still hold a file the test just wrote, and a destructor must
    // not throw, so a folder left behind is reported and the run carries on.
    ~Scratch() {
        try {
            Remove();
        } catch (const fs::filesystem_error& e) {
            std::printf("  scratch folder left behind: %s\n", e.what());
        }
    }

    std::string dir() const { return (root_ / "game").string(); }
    std::wstring wdir() const { return (root_ / "game").wstring(); }
    std::string ini() const { return dir() + "\\HeadTracking.ini"; }
    std::wstring wini() const { return wdir() + L"\\HeadTracking.ini"; }
    fs::path canonical() const { return root_ / "game" / "CameraUnlock.ini"; }
    fs::path defaults() const { return root_ / "global" / "Defaults.ini"; }

    // Every file in the game folder, by name, with its bytes.
    std::vector<std::pair<std::string, std::string>> Listing() const {
        std::vector<std::pair<std::string, std::string>> files;
        for (const auto& entry : fs::directory_iterator(root_ / "game")) {
            files.push_back({entry.path().filename().string(), ReadFileBytes(entry.path().string())});
        }
        std::sort(files.begin(), files.end());
        return files;
    }

    std::set<std::string> Names() const {
        std::set<std::string> names;
        for (const auto& entry : fs::directory_iterator(root_ / "game")) names.insert(entry.path().filename().string());
        return names;
    }

    void Write(const std::string& bytes) const {
        std::ofstream out(ini(), std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!out) throw std::runtime_error("cannot write " + ini());
    }

    void WriteDefaults(const std::string& bytes) const {
        fs::create_directories(defaults().parent_path());
        std::ofstream out(defaults(), std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!out) throw std::runtime_error("cannot write " + defaults().string());
    }

    cfg::ConfigOwnerOptions<gr_ht::Config> Options() const {
        return gr_ht::config::OwnerOptions(wdir(), cfg::DefaultsFile::At(defaults().wstring()));
    }

private:
    // The read-only inputs lose the attribute first, so remove_all can delete them.
    void Remove() const {
        if (!fs::exists(root_)) return;
        for (const auto& entry : fs::recursive_directory_iterator(root_)) {
            SetFileAttributesW(entry.path().c_str(), FILE_ATTRIBUTE_NORMAL);
        }
        fs::remove_all(root_);
    }

    fs::path root_;
};

// ---- What a reading does -------------------------------------------------------

std::uint32_t Bits(float f) {
    std::uint32_t u;
    std::memcpy(&u, &f, sizeof u);
    return u;
}

enum Action { kToggle, kCycleMode, kYawMode };

// One registered binding: the action, the virtual-key code, and the modifiers
// it needs (0, or Ctrl+Shift as cameraunlock::input::KeyModifiers spells it).
using Hotkey = std::tuple<int, int, unsigned>;
constexpr unsigned kPlain = 0;
constexpr unsigned kCtrlShift = 3;

constexpr int kVkEnd = 0x23;
constexpr int kVkPageUp = 0x21;

// Everything a reading decides that the running mod acts on: the settings, the
// state at startup, and the bindings the poller registers.
struct Observed {
    int udp_port = 0;
    bool start_enabled = false;
    bool start_world_yaw = false;
    int start_mode = 0;
    float local_smoothing = 0;
    float remote_smoothing = 0;
    bool collision_enabled = false;
    float collision_margin = 0;
    int collision_channel = 0;
    int aim_trace_channel = 0;
    float collision_release_smoothing = 0;
    bool dev_commands = false;
    std::vector<Hotkey> hotkeys;
};

std::vector<std::string> Differences(const Observed& a, const Observed& b) {
    std::vector<std::string> out;
    if (a.udp_port != b.udp_port) out.push_back("UDP port");
    if (a.start_enabled != b.start_enabled) out.push_back("tracking on at startup");
    if (a.start_world_yaw != b.start_world_yaw) out.push_back("yaw mode at startup");
    if (a.start_mode != b.start_mode) out.push_back("tracking mode at startup");
    if (Bits(a.local_smoothing) != Bits(b.local_smoothing)) out.push_back("local smoothing");
    if (Bits(a.remote_smoothing) != Bits(b.remote_smoothing)) out.push_back("remote smoothing");
    if (a.collision_enabled != b.collision_enabled) out.push_back("collision enabled");
    if (Bits(a.collision_margin) != Bits(b.collision_margin)) out.push_back("collision margin");
    if (a.collision_channel != b.collision_channel) out.push_back("collision channel");
    if (a.aim_trace_channel != b.aim_trace_channel) out.push_back("aim trace channel");
    if (Bits(a.collision_release_smoothing) != Bits(b.collision_release_smoothing)) {
        out.push_back("collision release smoothing");
    }
    if (a.dev_commands != b.dev_commands) out.push_back("dev commands");
    if (a.hotkeys != b.hotkeys) out.push_back("hotkeys");
    return out;
}

// Hand copied, not compiled from the published sources: the oracle library
// exports only the reader. Copied from 84353ca:src/mod_hotkeys.cpp:90-97
// (Register): End and Page Up NavGuarded; the configured yaw key NavGuarded
// unless it is End or Page Up, which addNav refuses; the Y, G and H chords
// ChordGuarded.
std::vector<Hotkey> LegacyHotkeys(int yaw_mode_key) {
    std::vector<Hotkey> keys = {
        {kToggle, kVkEnd, kPlain},
        {kCycleMode, kVkPageUp, kPlain},
        {kToggle, 0x59, kCtrlShift},
        {kCycleMode, 0x47, kCtrlShift},
        {kYawMode, 0x48, kCtrlShift},
    };
    if (yaw_mode_key != kVkEnd && yaw_mode_key != kVkPageUp) keys.push_back({kYawMode, yaw_mode_key, kPlain});
    std::sort(keys.begin(), keys.end());
    return keys;
}

// Hand copied from 84353ca:src/view_hook.cpp:74 (g_trackingEnabled starts true),
// 84353ca:src/view_hook.cpp:616 (the yaw mode from the config) and core
// daa973c's head_tracking_session.h:471 (the session starts in rotation and
// position, which nothing in the mod changes before the first key press).
constexpr bool kLegacyStartEnabled = true;
constexpr TrackingMode kLegacyStartMode = TrackingMode::RotationAndPosition;

Observed ReadOracle(const std::string& dir) {
    const gr_oracle::PublishedConfig c = gr_oracle::Load(dir);
    Observed o;
    o.udp_port = c.udp_port;
    o.start_enabled = kLegacyStartEnabled;
    o.start_world_yaw = c.world_space_yaw;
    o.start_mode = static_cast<int>(kLegacyStartMode);
    o.local_smoothing = c.local_smoothing;
    o.remote_smoothing = c.remote_smoothing;
    o.collision_enabled = c.collision_enabled;
    o.collision_margin = c.collision_margin;
    o.collision_channel = c.collision_channel;
    o.aim_trace_channel = c.aim_trace_channel;
    o.collision_release_smoothing = c.collision_release_smoothing;
    o.dev_commands = c.dev_commands;
    o.hotkeys = LegacyHotkeys(c.yaw_mode_key);
    return o;
}

Observed ObserveLegacy(const gr_ht::legacy::Config& c) {
    Observed o;
    o.udp_port = c.udp_port;
    o.start_enabled = kLegacyStartEnabled;
    o.start_world_yaw = c.world_space_yaw;
    o.start_mode = static_cast<int>(kLegacyStartMode);
    o.local_smoothing = c.local_smoothing;
    o.remote_smoothing = c.remote_smoothing;
    o.collision_enabled = c.collision_enabled;
    o.collision_margin = c.collision_margin;
    o.collision_channel = c.collision_channel;
    o.aim_trace_channel = c.aim_trace_channel;
    o.collision_release_smoothing = c.collision_release_smoothing;
    o.dev_commands = c.dev_commands;
    o.hotkeys = LegacyHotkeys(c.yaw_mode_key);
    return o;
}

// ---- Inputs --------------------------------------------------------------------

std::string DataPath(const char* name) {
    return std::string(GR_SOURCE_DIR) + "/tests/config_differential/data/" + name;
}

// The published build's first-run file, extracted once from the oracle's
// WriteDefaultIfMissing and committed.
std::string FirstRunFile() { return ReadFileBytes(DataPath("dev-first-run.ini")); }

// Every key the frozen reader reads, and how the corpus varies each one. The
// out-of-range values sit either side of the range each key is refused outside.
std::vector<testing::MutationKey> CorpusKeys() {
    return {
        {"Network", "Port", "5771", {"80", "70000"}},
        {"Tracking", "LocalSmoothing", "0.3", {"-0.5", "1.5"}},
        {"Tracking", "RemoteSmoothing", "0.6", {"-0.5", "1.5"}},
        {"General", "WorldSpaceYaw", "false", {}},
        {"Hotkeys", "YawMode", "0x2E", {"0x1FF"}, true},
        {"Camera", "CollisionEnabled", "false", {}},
        {"Camera", "CollisionMargin", "20.0", {"4.0", "41.0"}},
        {"Camera", "CollisionChannel", "2", {"-1", "32"}},
        {"Camera", "CollisionReleaseSmoothing", "0.5", {"-0.5", "1.5"}},
        {"Camera", "AimTraceChannel", "3", {"-1", "32"}},
        {"Dev", "DevCommands", "true", {}},
    };
}

// The generator refuses the call when these and the descriptors name different
// keys, so the corpus covers every key the import reads.
std::vector<cfg::LegacyKey> CorpusReads() { return gr_ht::config::Import().keys; }

struct Input {
    std::string name;
    bool present;
    std::string bytes;
};

std::vector<Input> Inputs() {
    std::vector<Input> inputs = {
        {"dev first-run file", true, FirstRunFile()},
        {"no file", false, {}},
        {"empty file", true, {}},
        {"yaw key on Ctrl", true, "[Hotkeys]\r\nYawMode=0x11\r\n"},
        {"yaw key on Right Alt", true, "[Hotkeys]\r\nYawMode=0xA5\r\n"},
        {"yaw key on End", true, "[Hotkeys]\r\nYawMode=0x23\r\n"},
        {"yaw key on H", true, "[Hotkeys]\r\nYawMode=0x48\r\n"},
    };
    for (testing::IniMutation& m : testing::GenerateIniMutations(FirstRunFile(), CorpusReads(), CorpusKeys())) {
        inputs.push_back({"corpus: " + m.name, true, std::move(m.bytes)});
    }
    return inputs;
}

// ---- Checks --------------------------------------------------------------------

// The first-run file committed as test data is what the published build writes.
void FirstRunFileIsThePublishedBuilds() {
    Scratch s;
    gr_oracle::WriteDefaultIfMissing(s.dir());
    CHECK_MSG(ReadFileBytes(s.ini()) == FirstRunFile(),
              "dev-first-run.ini is what the published build writes at first run");
}

// Comparison 1. Nothing may differ, floats bit for bit.
void OracleAgainstImport(const std::vector<Input>& inputs) {
    int compared = 0;
    for (const Input& input : inputs) {
        Scratch s;
        if (input.present) s.Write(input.bytes);

        const Observed published = ReadOracle(s.dir());
        gr_ht::legacy::Config imported;
        gr_ht::legacy::Load(s.dir(), imported);
        const Observed import = ObserveLegacy(imported);

        const std::vector<std::string> diff = Differences(published, import);
        for (const std::string& d : diff) std::printf("  comparison 1, %s: %s\n", input.name.c_str(), d.c_str());
        CHECK_MSG(diff.empty(), "comparison 1: oracle and import agree");
        ++compared;
    }
    std::printf("comparison 1: %d inputs\n", compared);
}

Observed ObserveCanonical(const gr_ht::Config& c) {
    Observed o;
    o.udp_port = c.udp_port;
    // headtracking_mod.cpp LoadSettings and tracking.cpp Start.
    o.start_enabled = c.enable_on_startup;
    o.start_world_yaw = c.world_space_yaw;
    o.start_mode = static_cast<int>(cameraunlock::DecodeTrackingMode(c.rotation_enabled, c.position_enabled).value());
    o.local_smoothing = c.local_smoothing;
    o.remote_smoothing = c.remote_smoothing;
    o.collision_enabled = c.collision_enabled;
    o.collision_margin = c.collision_margin;
    o.collision_channel = c.collision_channel;
    o.aim_trace_channel = c.aim_trace_channel;
    o.collision_release_smoothing = c.collision_release_smoothing;
    o.dev_commands = c.dev_commands;
    // mod_hotkeys.cpp Register: each list through ParseKeyBindings and
    // RegisterKeyBindings.
    const std::pair<Action, const std::string*> lists[] = {
        {kToggle, &c.toggle_key}, {kCycleMode, &c.cycle_tracking_mode_key}, {kYawMode, &c.yaw_mode_key}};
    for (const auto& [action, list] : lists) {
        const cameraunlock::input::KeyBindingsParseResult parsed = cameraunlock::input::ParseKeyBindings(*list);
        CHECK_MSG(parsed.ok(), "a migrated key list parses");
        for (const cameraunlock::input::KeyBinding& b : parsed.bindings) {
            o.hotkeys.push_back({action, b.vk, static_cast<unsigned>(b.modifiers)});
        }
    }
    std::sort(o.hotkeys.begin(), o.hotkeys.end());
    return o;
}

std::vector<std::string> CanonicalDiagnostics(const std::string& bytes, gr_ht::Config& out) {
    std::vector<std::string> found;
    const cfg::CanonicalIni doc = cfg::ParseCanonicalIni(bytes);
    for (const cfg::CanonicalDiagnostic& d : doc.diagnostics) found.push_back("reader: " + cfg::DescribeCanonicalDiagnostic(d));
    const cfg::ConfigTable<gr_ht::Config> table = gr_ht::config::Table();
    out = table.defaults();
    for (const cfg::CanonicalDiagnostic& d : cfg::ApplyCanonical(doc, table, out).diagnostics) {
        found.push_back("table: " + cfg::DescribeCanonicalDiagnostic(d));
    }
    return found;
}

bool AsciiCrlf(const std::string& bytes) {
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(bytes[i]);
        if (c > 0x7E) return false;
        if (c == '\r' && (i + 1 == bytes.size() || bytes[i + 1] != '\n')) return false;
        if (c == '\n' && (i == 0 || bytes[i - 1] != '\r')) return false;
        if (c < 0x20 && c != '\r' && c != '\n') return false;
    }
    return !bytes.empty() && bytes.back() == '\n';
}

// A file's bytes, last write time and attributes, which no load may change.
struct FileState {
    std::string bytes;
    unsigned long long written = 0;
    DWORD attributes = 0;
    bool operator==(const FileState& other) const {
        return bytes == other.bytes && written == other.written && attributes == other.attributes;
    }
};

std::optional<FileState> StateOf(const fs::path& path) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) {
        if (GetLastError() == ERROR_FILE_NOT_FOUND) return std::nullopt;
        throw std::runtime_error("cannot read the attributes of " + path.string());
    }
    FileState state;
    state.bytes = ReadFileBytes(path.string());
    state.written = (static_cast<unsigned long long>(data.ftLastWriteTime.dwHighDateTime) << 32) |
                    data.ftLastWriteTime.dwLowDateTime;
    state.attributes = data.dwFileAttributes;
    return state;
}

bool LogSays(const std::vector<std::string>& log, const std::string& text) {
    for (const std::string& line : log) {
        if (line.find(text) != std::string::npos) return true;
    }
    return false;
}

// A Defaults.ini holding a value other than the built-in one on every global row
// the table binds, so a migration that wrote `default` where the imported value
// is not what `default` gives would read back differently over it.
const char* const kSkewedDefaults =
    "[CameraUnlock]\r\nConfigFormat=1\r\n\r\n"
    "[Network]\r\nUdpPort=5252\r\n\r\n"
    "[General]\r\nEnableOnStartup=false\r\nWorldSpaceYaw=false\r\nRotationEnabled=false\r\n\r\n"
    "[Smoothing]\r\nLocalSmoothing=0.5\r\nRemoteSmoothing=0.5\r\n\r\n"
    "[Position]\r\nPositionEnabled=true\r\nCollisionEnabled=false\r\nCollisionReleaseSmoothing=0.25\r\n\r\n"
    "[Hotkeys]\r\nToggleKey=F8\r\nCycleTrackingModeKey=F9\r\nYawModeKey=F10\r\n";

// What the session runs on under kSkewedDefaults with no legacy setting at all.
Observed SkewedDefaults() {
    Observed o;
    o.udp_port = 5252;
    o.start_enabled = false;
    o.start_world_yaw = false;
    o.start_mode = static_cast<int>(TrackingMode::PositionOnly);
    o.local_smoothing = 0.5f;
    o.remote_smoothing = 0.5f;
    o.collision_enabled = false;
    o.collision_release_smoothing = 0.25f;
    // F8, F9 and F10.
    o.hotkeys = {{kToggle, 0x77, kPlain}, {kCycleMode, 0x78, kPlain}, {kYawMode, 0x79, kPlain}};
    return o;
}

// Each global row the table takes from Defaults.ini, and whether the player
// changed it from the old build's value. The start state, the toggle key and
// the mode key had no setting in the legacy file, so no player changed them.
struct GlobalRow {
    const char* key;
    bool changed;
};

std::vector<GlobalRow> GlobalRows(const gr_ht::legacy::Config& read) {
    const gr_ht::legacy::Config shipped;
    return {
        {"UdpPort", read.udp_port != shipped.udp_port},
        {"EnableOnStartup", false},
        {"WorldSpaceYaw", read.world_space_yaw != shipped.world_space_yaw},
        {"RotationEnabled", false},
        {"PositionEnabled", false},
        {"LocalSmoothing", Bits(read.local_smoothing) != Bits(shipped.local_smoothing)},
        {"RemoteSmoothing", Bits(read.remote_smoothing) != Bits(shipped.remote_smoothing)},
        {"CollisionEnabled", read.collision_enabled != shipped.collision_enabled},
        {"CollisionReleaseSmoothing",
         Bits(read.collision_release_smoothing) != Bits(shipped.collision_release_smoothing)},
        {"ToggleKey", false},
        {"CycleTrackingModeKey", false},
        {"YawModeKey", read.yaw_mode_key != shipped.yaw_mode_key},
    };
}

bool Changed(const std::vector<GlobalRow>& rows, const char* key) {
    for (const GlobalRow& row : rows) {
        if (std::strcmp(row.key, key) == 0) return row.changed;
    }
    throw std::logic_error(std::string("no global row ") + key);
}

// `into` with the bindings of `action` replaced by the ones `from` has.
void TakeBindings(Observed& into, const Observed& from, Action action) {
    into.hotkeys.erase(std::remove_if(into.hotkeys.begin(), into.hotkeys.end(),
                                      [action](const Hotkey& h) { return std::get<0>(h) == action; }),
                       into.hotkeys.end());
    for (const Hotkey& h : from.hotkeys) {
        if (std::get<0>(h) == action) into.hotkeys.push_back(h);
    }
    std::sort(into.hotkeys.begin(), into.hotkeys.end());
}

// The settings a migration over `defaults` runs on: the import's where the player
// changed the setting, Defaults.ini's where not.
Observed OverDefaults(const Observed& imported, const std::vector<GlobalRow>& rows, const Observed& defaults) {
    Observed o = imported;
    if (!Changed(rows, "UdpPort")) o.udp_port = defaults.udp_port;
    if (!Changed(rows, "EnableOnStartup")) o.start_enabled = defaults.start_enabled;
    if (!Changed(rows, "WorldSpaceYaw")) o.start_world_yaw = defaults.start_world_yaw;
    if (!Changed(rows, "RotationEnabled")) o.start_mode = defaults.start_mode;
    if (!Changed(rows, "LocalSmoothing")) o.local_smoothing = defaults.local_smoothing;
    if (!Changed(rows, "RemoteSmoothing")) o.remote_smoothing = defaults.remote_smoothing;
    if (!Changed(rows, "CollisionEnabled")) o.collision_enabled = defaults.collision_enabled;
    if (!Changed(rows, "CollisionReleaseSmoothing")) {
        o.collision_release_smoothing = defaults.collision_release_smoothing;
    }
    if (!Changed(rows, "ToggleKey")) TakeBindings(o, defaults, kToggle);
    if (!Changed(rows, "CycleTrackingModeKey")) TakeBindings(o, defaults, kCycleMode);
    if (!Changed(rows, "YawModeKey")) TakeBindings(o, defaults, kYawMode);
    return o;
}

// The folder beside this executable the migrated files are written to, for
// lint-migrated.mjs, which CTest runs after this test.
fs::path MigratedFolder() {
    std::vector<wchar_t> exe(MAX_PATH);
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size()));
        if (length == 0) throw std::runtime_error("cannot find this executable's path");
        if (length < exe.size()) return fs::path(std::wstring(exe.data(), length)).parent_path() / "migrated";
        exe.resize(exe.size() * 2);
    }
}

// Runs the owner's Load in `s`, whose game folder holds the input as
// HeadTracking.ini or nothing, checks what a load must do beyond comparison 2,
// and returns the settings the session runs on. A file it creates by migrating
// goes into `migrated_files`.
std::optional<gr_ht::Config> Migrate(const Input& input, const Scratch& s, const std::string& label,
                                     std::set<std::string>& migrated_files) {
    const char* name = label.c_str();
    const fs::path legacy = fs::path(s.wini());
    const std::optional<FileState> legacy_before = StateOf(legacy);
    const std::set<std::string> both{"CameraUnlock.ini", "HeadTracking.ini"};

    const cfg::ConfigLoadResult<gr_ht::Config> loaded = cfg::ConfigOwner<gr_ht::Config>(s.Options()).Load();
    const cfg::ConfigLoadStatus want = input.present ? cfg::ConfigLoadStatus::Migrated : cfg::ConfigLoadStatus::Created;
    if (loaded.status != want) {
        std::printf("  %s: %s, %s\n", name, cfg::ConfigLoadStatusName(loaded.status), loaded.reason.c_str());
    }
    CHECK_MSG(loaded.status == want, "every legacy input imports, and no file is created");
    CHECK_MSG(StateOf(legacy) == legacy_before, "a load leaves HeadTracking.ini's bytes, write time and attributes");
    if (loaded.status != want) return std::nullopt;
    CHECK_MSG(s.Names() == (input.present ? both : std::set<std::string>{"CameraUnlock.ini"}),
              "the game folder holds HeadTracking.ini and CameraUnlock.ini and nothing else");

    const std::string migrated = ReadFileBytes(s.canonical().string());
    CHECK_MSG(cfg::HasCanonicalStamp(migrated), "CameraUnlock.ini carries the stamp");
    CHECK_MSG(AsciiCrlf(migrated), "CameraUnlock.ini is ASCII with CRLF line ends");
    gr_ht::Config reread;
    const std::vector<std::string> diagnostics = CanonicalDiagnostics(migrated, reread);
    for (const std::string& d : diagnostics) std::printf("  %s: CameraUnlock.ini, %s\n", name, d.c_str());
    CHECK_MSG(diagnostics.empty(), "CameraUnlock.ini reads with no diagnostic");
    if (input.present) migrated_files.insert(migrated);

    // The next start reads CameraUnlock.ini, imports nothing and writes nothing.
    const std::optional<FileState> created = StateOf(s.canonical());
    const cfg::ConfigLoadResult<gr_ht::Config> again = cfg::ConfigOwner<gr_ht::Config>(s.Options()).Load();
    CHECK_MSG(again.status == cfg::ConfigLoadStatus::Canonical, "the next start reads CameraUnlock.ini");
    CHECK_MSG(Differences(ObserveCanonical(again.config), ObserveCanonical(loaded.config)).empty(),
              "the next start runs on the same settings");
    CHECK_MSG(StateOf(s.canonical()) == created && StateOf(legacy) == legacy_before,
              "the next start changes neither file");
    CHECK_MSG(!input.present || LogSays(again.log, "is left as it was and is not read"),
              "the next start logs that HeadTracking.ini is not read");
    return loaded.config;
}

// Comparison 2, and what the migration must do with every input besides.
void ImportAgainstMigration(const std::vector<Input>& inputs) {
    const std::string committed = ReadFileBytes(std::string(GR_SOURCE_DIR) + "/CameraUnlock.ini");
    const cfg::ConfigTable<gr_ht::Config> table = gr_ht::config::Table();
    std::set<std::string> migrated_files;
    int compared = 0;
    for (const Input& input : inputs) {
        const char* name = input.name.c_str();

        // The import, run as the owner runs it but on a read-only copy: it reads
        // what the frozen reader reads, drops nothing, and writes nothing.
        cfg::ImportResult imported;
        gr_ht::legacy::Config read;
        {
            Scratch ro;
            if (input.present) {
                ro.Write(input.bytes);
                SetFileAttributesA(ro.ini().c_str(), FILE_ATTRIBUTE_READONLY);
            }
            const auto before = ro.Listing();
            gr_ht::Config unused = table.defaults();
            imported = gr_ht::config::Import().run({ro.wini(), ro.ini(), false}, unused);
            CHECK_MSG(ro.Listing() == before, "the import leaves a read-only folder as it was");
            gr_ht::legacy::Load(ro.dir(), read);
        }
        CHECK_MSG(imported.status == (input.present ? cfg::ImportStatus::Imported : cfg::ImportStatus::Absent),
                  "the import reads every input, as the published build did");
        CHECK_MSG(imported.dropped.empty() && imported.pose_shaping.empty(),
                  "comparison 2: the import drops nothing and reads no pose shaping");
        const Observed want = ObserveLegacy(read);
        const std::vector<GlobalRow> rows = GlobalRows(read);

        // Over a Defaults.ini the owner creates with the built-in values.
        Scratch s;
        if (input.present) s.Write(input.bytes);
        const std::optional<gr_ht::Config> migrated = Migrate(input, s, input.name, migrated_files);
        if (migrated) {
            const std::vector<std::string> diff = Differences(want, ObserveCanonical(*migrated));
            for (const std::string& d : diff) std::printf("  comparison 2, %s: %s\n", name, d.c_str());
            CHECK_MSG(diff.empty(), "comparison 2: the migration runs as the import read");

            // Over the built-in values the table's own defaults stand for Defaults.ini.
            const std::string bytes = ReadFileBytes(s.canonical().string());
            gr_ht::Config reread;
            CanonicalDiagnostics(bytes, reread);
            CHECK_MSG(Differences(ObserveCanonical(reread), ObserveCanonical(*migrated)).empty(),
                      "CameraUnlock.ini reads back as the settings the session runs on");

            // A row the player never changed is written `default`, and one the
            // player changed holds a value, since the old build's values are
            // the built-in ones.
            for (const GlobalRow& row : rows) {
                const bool holds_default =
                    bytes.find("\r\n" + std::string(row.key) + "=default\r\n") != std::string::npos;
                if (holds_default == row.changed) {
                    std::printf("  %s: %s %s\n", name, row.key, row.changed ? "changed, written default" : "untouched, written as a value");
                }
                CHECK_MSG(holds_default != row.changed,
                          "an untouched setting migrates as default, a changed one as a value");
            }

            // Fresh equals upgrade: the published build's first-run file, and no
            // file at all, both end as the committed file.
            if (input.name == "dev first-run file" || input.name == "no file") {
                CHECK_MSG(ReadFileBytes(s.canonical().string()) == committed,
                          "the first-run file and no file both give the committed file");
            }
        }

        // From a read-only HeadTracking.ini, which keeps its attribute.
        if (input.present) {
            Scratch ro;
            ro.Write(input.bytes);
            SetFileAttributesA(ro.ini().c_str(), FILE_ATTRIBUTE_READONLY);
            const std::optional<gr_ht::Config> c = Migrate(input, ro, input.name + " (read-only)", migrated_files);
            CHECK_MSG(c && Differences(want, ObserveCanonical(*c)).empty(),
                      "a read-only HeadTracking.ini imports as a writable one does");
            CHECK_MSG((GetFileAttributesA(ro.ini().c_str()) & FILE_ATTRIBUTE_READONLY) != 0,
                      "HeadTracking.ini keeps its read-only attribute");
        }

        // Over a Defaults.ini that differs everywhere. With no legacy file the
        // settings are Defaults.ini's own and the owner creates rather than
        // migrates, so only an input with a file is held here.
        if (input.present) {
            Scratch skewed;
            skewed.Write(input.bytes);
            skewed.WriteDefaults(kSkewedDefaults);
            const std::optional<gr_ht::Config> c =
                Migrate(input, skewed, input.name + " (skewed Defaults.ini)", migrated_files);
            const std::vector<std::string> diff =
                c ? Differences(OverDefaults(want, rows, SkewedDefaults()), ObserveCanonical(*c))
                  : std::vector<std::string>{"the load"};
            for (const std::string& d : diff) std::printf("  comparison 2, %s (skewed Defaults.ini): %s\n", name, d.c_str());
            CHECK_MSG(diff.empty(),
                      "over a Defaults.ini that differs everywhere, untouched settings follow it and changed ones "
                      "stay the import's");
        }
        ++compared;
    }
    std::printf("comparison 2: %d inputs\n", compared);

    // Core's canonical config lint runs over these next (lint-migrated.mjs).
    const fs::path lint = MigratedFolder();
    fs::remove_all(lint);
    fs::create_directories(lint);
    std::size_t n = 0;
    for (const std::string& file : migrated_files) {
        std::ofstream out(lint / (std::to_string(n++) + ".ini"), std::ios::binary | std::ios::trunc);
        out.write(file.data(), static_cast<std::streamsize>(file.size()));
        if (!out) throw std::runtime_error("cannot write a migrated file under " + lint.string());
    }
    std::printf("%zu distinct migrated files written to %s\n", migrated_files.size(), lint.string().c_str());
}

}  // namespace

int main() {
    // Unbuffered, so the lines before an uncaught exception reach the log.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SourcesAreThePinnedOnes();
    FirstRunFileIsThePublishedBuilds();
    const std::vector<Input> inputs = Inputs();
    OracleAgainstImport(inputs);
    ImportAgainstMigration(inputs);
    return gr_test::Report();
}
