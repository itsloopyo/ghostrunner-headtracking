// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "build_profile.h"

// Every Steam build of Ghostrunner this mod knows about. Append-only: a patch
// gets a new kSteamProfile_<date> at the top of kKnownProfiles in
// build_registry.cpp, and the profiles below stay for players who have not
// updated.

namespace gr_ht::builds
{
    // Ghostrunner-Win64-Shipping.exe, Unreal Engine 4.26.
    extern const BuildProfile kSteamProfile_20220624 = {
        "steam-win64-20220624",
        { 0x62B55E49u, 0x0571E000u, 0x0548CD02u },
        {
            // APlayerController::GetPlayerViewPoint. The .pdata entry covering
            // the body is a chained continuation; the primary entry, and the
            // real prologue, is here. Found from the two wide format strings
            // "APlayerController::GetPlayerViewPoint: out_Location/out_Rotation,
            // ViewTarget=%s" it passes to the ensure path. Dispatched only
            // through the vtable (+0x710 of APlayerController's), so the binary
            // holds no direct call to it.
            0x02fca770ULL,

            // Slot 1 is ULocalPlayer::GetViewPoint, the render path: its call
            // site stores PlayerCameraManager->GetFOVAngle() (vtable +0x6c8) at
            // OutViewInfo+0x18 and then passes OutViewInfo+0x00 / +0x0c as the
            // out-params, which is the only caller whose rotation sits 0x0c
            // after its location. The rest are captured in game with inject
            // mode 0.
            {{
                0x02e5b77fULL,  // 1: ULocalPlayer::GetViewPoint - render path
                0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL,
                0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL,
            }},
            inject::kFirstCaller,

            // FMinimalViewInfo: Location 0x00, Rotation 0x0c and FOV 0x18 are
            // the three the render caller's own code above writes; AspectRatio
            // follows DesiredFOV, OrthoWidth and the two ortho clip planes at
            // 0x2c.
            { 0x18, 0x0c, 0x2c },

            {
                // UObject::ProcessEvent compares InternalIndex against
                // NumElements at 0x0505e54c; the chunk table sits 0x14 below it,
                // indexed (idx >> 16) * 8 then idx * 0x18.
                0x0505e538ULL,
                0x14,
                0x18,
                0x10000,
                // The pool the FNamePool constructor (0x016b4b90) is handed.
                0x05045f80ULL,
                0x10,
                0x10,
                0x18,
                0x20,
            },

            // Opens by comparing this->InternalIndex (+0x0c) with the object
            // array's element count before resolving the object item, then
            // tests Function->FunctionFlags (+0xb0) for FUNC_Native.
            0x018940f0ULL,

            // UE 4.26 FField / FProperty layout, unchanged from 4.27: Owner at
            // +0x10 is a 16-byte FFieldVariant, so Next sits at +0x20 and
            // Offset_Internal at +0x4c. UStruct carries FStructBaseChain after
            // UField, which is what puts SuperStruct at +0x40 rather than +0x30.
            {
                0x08,   // kFField_ClassPrivate
                0x20,   // kFField_Next
                0x28,   // kFField_NamePrivate
                0x38,   // kFProperty_ArrayDim
                0x3c,   // kFProperty_ElementSize
                0x4c,   // kFProperty_Offset
                0x00,   // kFFieldClass_Name
                0x40,   // kUStruct_SuperStruct
                0x50,   // kUStruct_ChildProperties
                0x58,   // kUStruct_PropertiesSize
                0x78,   // kFBoolProperty_FieldSize
                0x78,   // kFStructProperty_Struct
            },
        },
    };
}
