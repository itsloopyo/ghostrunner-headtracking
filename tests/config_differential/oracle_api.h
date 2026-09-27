// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <string>

// The oracle: the HeadTracking.ini reader of the newest published build, the dev
// pre-release (84353ca) against cameraunlock-core daa973c, compiled only into
// this test. oracle/src/ holds that build's config.h, config.cpp and logging.h
// byte for byte as the tag has them (provenance in differential_tests.cpp).
// Every core source they compile is hash-equal at today's pin, so no core file
// is copied. oracle_api.cpp compiles them with their namespaces renamed, so
// they link beside today's code.
namespace gr_oracle {

// gr_ht::Config as the dev build declared it, field for field.
struct PublishedConfig {
    int udp_port;
    float local_smoothing;
    float remote_smoothing;
    int yaw_mode_key;
    bool world_space_yaw;
    bool collision_enabled;
    float collision_margin;
    int collision_channel;
    int aim_trace_channel;
    float collision_release_smoothing;
    bool dev_commands;
};

// The published config::Load on a default Config, as its bootstrap called it.
PublishedConfig Load(const std::string& exe_dir);

// The published config::WriteDefaultIfMissing: that build's first-run file.
void WriteDefaultIfMissing(const std::string& exe_dir);

}  // namespace gr_oracle
