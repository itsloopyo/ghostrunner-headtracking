// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// The differential test for the conversion from HeadTracking.ini to the
// canonical config.
//
//   Oracle     the reader of the newest published build (the dev pre-release,
//              84353ca, core daa973c), compiled from its own sources
//              (oracle_api.h)
//   Import     the frozen reader in src/legacy_config/
//
// Comparison 1, oracle against import, is what a player sees change that the
// conversion did not cause: commits since the published build that change how
// the file is read. The import's files and every core source both readers
// compile hash-equal the published build's, so it is empty by construction,
// and the test holds it empty.
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
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "legacy_config/legacy_config.h"
#include "oracle_api.h"
#include "test_harness.h"

#include "cameraunlock/config/legacy_import.h"
#include "cameraunlock/config/testing/ini_mutations.h"
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
// game exe.

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
    std::string ini() const { return dir() + "\\HeadTracking.ini"; }

    void Write(const std::string& bytes) const {
        std::ofstream out(ini(), std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!out) throw std::runtime_error("cannot write " + ini());
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

// Every key the frozen reader takes a value from. The generator refuses the call
// when these and the descriptors name different keys.
std::vector<cfg::LegacyKey> CorpusReads() {
    return {
        {"Network", "Port"},
        {"Tracking", "LocalSmoothing"},
        {"Tracking", "RemoteSmoothing"},
        {"General", "WorldSpaceYaw"},
        {"Hotkeys", "YawMode"},
        {"Camera", "CollisionEnabled"},
        {"Camera", "CollisionMargin"},
        {"Camera", "CollisionChannel"},
        {"Camera", "CollisionReleaseSmoothing"},
        {"Camera", "AimTraceChannel"},
        {"Dev", "DevCommands"},
    };
}

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

}  // namespace

int main() {
    // Unbuffered, so the lines before an uncaught exception reach the log.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SourcesAreThePinnedOnes();
    FirstRunFileIsThePublishedBuilds();
    const std::vector<Input> inputs = Inputs();
    OracleAgainstImport(inputs);
    return gr_test::Report();
}
