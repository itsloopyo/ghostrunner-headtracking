// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Whether the head pose reaches the view, and the reason logged when it does
// not. The order the gate is asked in decides which reason a frame reports when
// more than one applies, and the heartbeat line is read against that reason, so
// the order is locked here.

#include "test_harness.h"
#include "tracking_gate.h"

#include <cstring>

using namespace gr_ht;

namespace {

game_state::Verdict Gate(bool inGameplay) {
    game_state::Verdict v;
    v.InGameplay = inGameplay;
    v.Why = inGameplay ? game_state::Blocker::None : game_state::Blocker::Paused;
    return v;
}

void TestOnlyAFullyOpenGateApplies() {
    CHECK(DecideTracking(Gate(true), true, true) == TrackingVerdict::Active);
    CHECK(PoseApplies(TrackingVerdict::Active));
    CHECK(!PoseApplies(TrackingVerdict::Disabled));
    CHECK(!PoseApplies(TrackingVerdict::NotGameplay));
    CHECK(!PoseApplies(TrackingVerdict::NoTracker));
}

// The master toggle is asked first, then gameplay, then the tracker.
void TestTheOrderTheGateIsAskedIn() {
    CHECK_MSG(DecideTracking(Gate(false), false, false) == TrackingVerdict::Disabled,
              "toggled off outranks every other reason");
    CHECK(DecideTracking(Gate(true), false, true) == TrackingVerdict::Disabled);
    CHECK_MSG(DecideTracking(Gate(false), true, false) == TrackingVerdict::NotGameplay,
              "a menu outranks a missing tracker");
    CHECK(DecideTracking(Gate(false), true, true) == TrackingVerdict::NotGameplay);
    CHECK(DecideTracking(Gate(true), true, false) == TrackingVerdict::NoTracker);
}

void TestEveryVerdictHasItsLogReason() {
    CHECK(std::strcmp(Reason(TrackingVerdict::Active), "gameplay") == 0);
    CHECK(std::strcmp(Reason(TrackingVerdict::Disabled), "tracking toggled off") == 0);
    CHECK(std::strcmp(Reason(TrackingVerdict::NotGameplay), "menu or loading") == 0);
    CHECK(std::strcmp(Reason(TrackingVerdict::NoTracker), "no tracker data") == 0);
}

}  // namespace

int main() {
    TestOnlyAFullyOpenGateApplies();
    TestTheOrderTheGateIsAskedIn();
    TestEveryVerdictHasItsLogReason();
    return gr_test::Report();
}
