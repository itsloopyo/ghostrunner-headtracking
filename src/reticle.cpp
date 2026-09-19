// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "reticle.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <string>

#include <windows.h>

#include "logging.h"
#include "ue4_types.h"
#include "ue_call.h"
#include "ue_reflect.h"
#include "ue_vm.h"

#include "cameraunlock/unreal/ue_runtime.h"

namespace gr_ht::reticle {

namespace {

namespace ue = ::cameraunlock::unreal;

// UPanelCrosshair is the native class behind BP_PanelCrosshair, and BrdCrosshair
// is its BindWidget border wrapping the crosshair image. The whole panel is
// moved, not the border: its other two children, BrdSlowmotion (the
// sensory-boost gauge) and BrdDash (the dash charges), are laid out around the
// crosshair and are left sitting in an empty screen centre if it moves without
// them. The border is what the readback measures, because its centre is the
// aim mark's.
constexpr const char* kCrosshairPanelClass = "PanelCrosshair";
constexpr const char* kCrosshairWidget = "BrdCrosshair";
constexpr std::size_t kInternalIndexOffset = 0x0c;

// Ghostrunner builds one crosshair panel per HUD, and a session can hold more
// than one HUD - the campaign's and the Hel DLC's. Which is on screen is the
// game's business; every live one is moved, because only the painted one shows
// and picking between them would be a guess that fails silently in whichever
// mode was not tested.
constexpr std::size_t kMaxPanels = 4;

ue_vm::ResolveRetry g_resolveRetry;
bool g_resolved = false;
std::uintptr_t g_panelClass = 0;      // the PanelCrosshair UClass
std::uintptr_t g_layoutLib = 0;
ue_call::Function g_setTranslation;   // Widget::SetRenderTranslation
ue_call::Function g_viewportScale;    // WidgetLayoutLibrary::GetViewportScale
ue_call::Function g_viewportSize;     // WidgetLayoutLibrary::GetViewportSize

// What a readback found. Unpainted is its own answer rather than a
// misplacement: a HUD that is not the active one sits collapsed for a whole
// session, and Slate leaves an unpainted widget's cached geometry at whatever
// it last was, so calling that "not where it was put" cries wolf every time.
enum class Paint { Unknown, Unpainted, Placed, Misplaced };

struct Panel {
    std::uintptr_t Widget = 0;      // the PanelCrosshair, which is what moves
    std::int32_t Index = -1;
    std::uintptr_t Crosshair = 0;   // its BrdCrosshair, which is what is read back
    // What this mod last wrote to the widget. NaN until the first write, so that
    // write always goes out: a widget re-bound after another panel died keeps
    // whatever translation it was last given, and assuming 0 there would skip
    // the move that puts it back in the centre.
    float LastX = std::numeric_limits<float>::quiet_NaN();
    float LastY = std::numeric_limits<float>::quiet_NaN();
    Paint LastPaint = Paint::Unknown;
    // What the sample before last said. A readback compares this frame's
    // requested position against the geometry Slate cached when it last
    // PAINTED, which is a frame behind, so a head that is moving disagrees by
    // however far the mark travelled in that frame - every time. Reporting only
    // a verdict that has survived two samples a second apart drops that
    // transient and keeps a widget that has genuinely stopped following.
    Paint PendingPaint = Paint::Unknown;
    bool Drawn = false;
};
Panel g_panels[kMaxPanels];
std::size_t g_panelCount = 0;

ue_vm::ResolveRetry g_findRetry;
// Whether the panels have been moved at least once, so the first move writes
// the viewport and DPI it was made against and the rest stay silent.
bool g_bound = false;

bool Resolve() {
    if (g_resolved) return true;
    if (!g_resolveRetry.Due() || !ue_vm::Ready()) return false;
    g_panelClass = ue::FindLiveObject("Class", kCrosshairPanelClass, nullptr);
    g_layoutLib = ue_call::DefaultObject("WidgetLayoutLibrary");
    const std::size_t ptr = sizeof(std::uintptr_t);
    const std::size_t v2 = sizeof(ue4::FVector2D);
    if (!g_panelClass || !g_layoutLib ||
        !g_setTranslation.Resolve("Widget", "SetRenderTranslation", {{"Translation", v2}}) ||
        !g_viewportScale.Resolve("WidgetLayoutLibrary", "GetViewportScale",
                                 {{"WorldContextObject", ptr}, {"ReturnValue", sizeof(float)}}) ||
        !g_viewportSize.Resolve("WidgetLayoutLibrary", "GetViewportSize",
                                {{"WorldContextObject", ptr}, {"ReturnValue", v2}}))
        return false;
    g_resolved = true;
    return true;
}

// ---- readback: where Slate last drew the crosshair ----------------------
// The move is a request; the paint is the fact. Once a second the bound
// widget's render translation and its last painted geometry are read back and
// compared with the position that was asked for, so "the crosshair is not where
// the mod put it" is a line in the log rather than a guess.
//
// Only a CHANGE in that comparison is written. Logging every sample buries the
// build match, the link and the gate transitions a report is read for.
constexpr std::uint64_t kReadbackMs = 1000;
// Whole-pixel rounding on the way out and the DPI divide on the way back leave
// well under a pixel of round-trip error; anything past this is the widget not
// following.
constexpr float kDrawTolerancePx = 2.0f;
ue_vm::ResolveRetry g_readbackRetry;
bool g_readbackResolved = false;
std::uintptr_t g_slateLib = 0;
ue_call::Function g_cachedGeometry;   // Widget::GetCachedGeometry
ue_call::Function g_localToViewport;  // SlateBlueprintLibrary::LocalToViewport
ue_call::Function g_localSize;        // SlateBlueprintLibrary::GetLocalSize
ue_call::Function g_isVisible;        // Widget::IsVisible
std::size_t g_geometrySize = 0;
std::size_t g_renderTransformOffset = 0;
std::uint64_t g_lastReadbackMs = 0;

bool ResolveReadback() {
    if (g_readbackResolved) return true;
    if (!g_readbackRetry.Due()) return false;
    const std::uintptr_t geometry = ue::FindLiveObject("ScriptStruct", "Geometry", nullptr);
    const std::uintptr_t widgetClass = ue::FindLiveObject("Class", "Widget", nullptr);
    g_slateLib = ue_call::DefaultObject("SlateBlueprintLibrary");
    ue_reflect::FieldInfo transform;
    if (!geometry || !widgetClass || !g_slateLib ||
        !ue_reflect::FindPropertyInChain(widgetClass, "RenderTransform", transform))
        return false;
    g_geometrySize = ue_reflect::StructSize(geometry);
    // A zero size would satisfy every FieldFits below, and the readback would
    // then hand the engine a geometry of no bytes and report "has not been
    // drawn" for the rest of the session with nothing saying why.
    if (g_geometrySize == 0 || g_geometrySize > ue_call::Function::kMaxFrame) return false;
    const std::size_t v2 = sizeof(ue4::FVector2D);
    if (!g_cachedGeometry.Resolve("Widget", "GetCachedGeometry", {{"ReturnValue", g_geometrySize}}) ||
        !g_localToViewport.Resolve("SlateBlueprintLibrary", "LocalToViewport",
                                   {{"WorldContextObject", sizeof(std::uintptr_t)}, {"Geometry", g_geometrySize},
                                    {"LocalCoordinate", v2}, {"PixelPosition", v2}, {"ViewportPosition", v2}}) ||
        !g_localSize.Resolve("SlateBlueprintLibrary", "GetLocalSize",
                             {{"Geometry", g_geometrySize}, {"ReturnValue", v2}}) ||
        !g_isVisible.Resolve("Widget", "IsVisible", {{"ReturnValue", 1}}))
        return false;
    g_renderTransformOffset = transform.Offset;
    g_readbackResolved = true;
    return true;
}

struct Drawn {
    bool Valid = false;
    bool Visible = false;
    ue4::FVector2D Translation{0.0f, 0.0f};
    ue4::FVector2D Centre{0.0f, 0.0f};   // viewport pixels
    ue4::FVector2D Size{0.0f, 0.0f};     // local units
};

// `panel` is the widget that was moved, `widget` the one whose painted centre is
// measured.
Drawn ReadDrawn(std::uintptr_t controller, std::uintptr_t panel, std::uintptr_t widget) {
    Drawn d;
    if (!widget || !ResolveReadback()) return d;
    ue::SafeReadFloat(panel + g_renderTransformOffset, d.Translation.X);
    ue::SafeReadFloat(panel + g_renderTransformOffset + 4, d.Translation.Y);
    ue_call::Frame vis(g_isVisible);
    if (vis.Call(widget)) d.Visible = (vis.Get<std::uint8_t>(0) & 1u) != 0;
    ue_call::Frame geo(g_cachedGeometry);
    if (!geo.Call(widget)) return d;
    ue_call::Frame size(g_localSize);
    std::memcpy(size.Data() + g_localSize.Offset(0), geo.At(0), g_geometrySize);
    if (!size.Call(g_slateLib)) return d;
    d.Size = size.Get<ue4::FVector2D>(1);
    ue_call::Frame toViewport(g_localToViewport);
    toViewport.Set(0, controller);
    std::memcpy(toViewport.Data() + g_localToViewport.Offset(1), geo.At(0), g_geometrySize);
    toViewport.Set(2, ue4::FVector2D{d.Size.X * 0.5f, d.Size.Y * 0.5f});
    if (!toViewport.Call(g_slateLib)) return d;
    d.Centre = toViewport.Get<ue4::FVector2D>(3);
    d.Valid = true;
    return d;
}

// A widget the engine has destroyed leaves its object slot to be reused by
// something else, so the index has to still hold the same object.
bool StillAlive(std::uintptr_t obj, std::int32_t index) {
    const std::uintptr_t item = ue_call::ObjectItem(index);
    std::uintptr_t live = 0;
    return obj != 0 && item != 0 && ue::SafeReadPtr(item, live) && live == obj;
}

bool PanelsAlive() {
    if (g_panelCount == 0) return false;
    for (std::size_t i = 0; i < g_panelCount; ++i)
        if (!StillAlive(g_panels[i].Widget, g_panels[i].Index)) return false;
    return true;
}

// Every live PanelCrosshair, with its BrdCrosshair. A level load builds new HUD
// widgets and destroys the old ones, so the set is rebuilt whenever one of them
// dies.
bool Bind() {
    if (PanelsAlive()) return true;
    for (Panel& panel : g_panels) panel = Panel{};
    g_panelCount = 0;
    if (!g_findRetry.Due()) return false;

    ue_reflect::FieldInfo field;
    bool haveField = false;
    ue::ForEachUObject([&](std::uintptr_t obj) {
        if (g_panelCount >= kMaxPanels) return true;
        // By class pointer rather than name: this runs for every object in the
        // table, and a name test resolves a string for every class up each one's
        // chain.
        if (!ue_call::IsInstanceOf(obj, g_panelClass)) return false;
        // The class default object carries the same properties and is never
        // drawn; moving it would look like success and change nothing.
        if (ue_call::IsDefaultObject(obj)) return false;
        const std::uintptr_t cls = ue_call::ClassOf(obj);
        if (!haveField) {
            if (!cls || !ue_reflect::FindPropertyInChain(cls, kCrosshairWidget, field) ||
                field.Size != sizeof(std::uintptr_t))
                return false;
            haveField = true;
        }
        std::uintptr_t crosshair = 0;
        std::uint32_t index = 0;
        if (!ue::SafeReadPtr(obj + field.Offset, crosshair) || !crosshair ||
            !ue::SafeReadU32(obj + kInternalIndexOffset, index))
            return false;
        g_panels[g_panelCount].Widget = obj;
        g_panels[g_panelCount].Index = static_cast<std::int32_t>(index);
        g_panels[g_panelCount].Crosshair = crosshair;
        ++g_panelCount;
        return false;
    });
    if (g_panelCount == 0) return false;
    for (std::size_t i = 0; i < g_panelCount; ++i)
        Log::Line("reticle: bound %s (0x%llx), %s at 0x%llx",
                  ue::ClassName(g_panels[i].Widget).c_str(),
                  static_cast<unsigned long long>(g_panels[i].Widget), kCrosshairWidget,
                  static_cast<unsigned long long>(g_panels[i].Crosshair));
    return true;
}

bool Move(float x, float y) {
    for (std::size_t i = 0; i < g_panelCount; ++i) {
        Panel& panel = g_panels[i];
        if (x == panel.LastX && y == panel.LastY) continue;
        ue_call::Frame frame(g_setTranslation);
        frame.Set(0, ue4::FVector2D{x, y});
        if (!frame.Call(panel.Widget)) {
            g_panelCount = 0;
            return false;
        }
        panel.LastX = x;
        panel.LastY = y;
    }
    return true;
}

// The viewport size and DPI scale, or false when they do not read.
bool Viewport(std::uintptr_t controller, ue4::FVector2D& size, float& dpi) {
    ue_call::Frame sizeFrame(g_viewportSize);
    sizeFrame.Set(0, controller);
    ue_call::Frame scale(g_viewportScale);
    scale.Set(0, controller);
    if (!sizeFrame.Call(g_layoutLib) || !scale.Call(g_layoutLib)) return false;
    size = sizeFrame.Get<ue4::FVector2D>(1);
    dpi = scale.Get<float>(1);
    return size.X > 0.0f && size.Y > 0.0f && dpi > 0.0f;
}

// What the last readback said about one panel, against the centre the move
// asked for. Only a CHANGE is written: see kReadbackMs.
void LogPanelPaint(Panel& panel, const Drawn& d, float askedX, float askedY, float expectedX,
                   float expectedY, const ue4::FVector2D& viewport, float dpi) {
    const Paint paint =
        !d.Valid || d.Size.X <= 0.0f || d.Size.Y <= 0.0f ? Paint::Unpainted
        : std::fabs(d.Centre.X - expectedX) <= kDrawTolerancePx &&
          std::fabs(d.Centre.Y - expectedY) <= kDrawTolerancePx ? Paint::Placed
                                                                : Paint::Misplaced;
    if (paint != panel.PendingPaint) {
        panel.PendingPaint = paint;
        return;
    }
    if (paint == panel.LastPaint && d.Visible == panel.Drawn) return;
    panel.LastPaint = paint;
    panel.Drawn = d.Visible;
    Log::Line("reticle: %s in %s %s - asked translation (%.1f,%.1f) -> expected centre (%.0f,%.0f) px | "
              "translation (%.1f,%.1f) visible=%d drawn centre (%.0f,%.0f) px size (%.1f,%.1f) valid=%d | "
              "viewport %.0fx%.0f dpi %.3f",
              kCrosshairWidget, ue::ClassName(panel.Widget).c_str(),
              paint == Paint::Unpainted ? "has not been drawn"
                  : paint == Paint::Placed ? "is drawn where it was put"
                                           : "is NOT drawn where it was put",
              askedX, askedY, expectedX, expectedY,
              d.Translation.X, d.Translation.Y, d.Visible ? 1 : 0, d.Centre.X, d.Centre.Y, d.Size.X,
              d.Size.Y, d.Valid ? 1 : 0, viewport.X, viewport.Y, dpi);
}

// Once a second, where Slate actually drew each panel against where it was put.
void ReadBackPanels(std::uintptr_t controller, float askedX, float askedY, float pixelX,
                    float pixelY, const ue4::FVector2D& viewport, float dpi) {
    const std::uint64_t now = GetTickCount64();
    if (now - g_lastReadbackMs < kReadbackMs) return;
    g_lastReadbackMs = now;

    const float expectedX = viewport.X * 0.5f + pixelX;
    const float expectedY = viewport.Y * 0.5f + pixelY;
    for (std::size_t i = 0; i < g_panelCount; ++i)
        LogPanelPaint(g_panels[i], ReadDrawn(controller, g_panels[i].Widget, g_panels[i].Crosshair),
                      askedX, askedY, expectedX, expectedY, viewport, dpi);
}

}  // namespace

void Publish(std::uintptr_t controller, std::uintptr_t pawn, bool valid, float ndcX, float ndcY) {
    if (!pawn || !Resolve() || !Bind()) return;
    if (!valid) {
        Move(0.0f, 0.0f);
        return;
    }

    ue4::FVector2D viewport{};
    float dpi = 1.0f;
    if (!Viewport(controller, viewport, dpi)) {
        Move(0.0f, 0.0f);
        return;
    }
    // Render translation is in the widget's layout units, which the viewport
    // scales by the DPI curve; screen y runs down.
    const float pixelX = std::round(ndcX * viewport.X * 0.5f);
    const float pixelY = std::round(-ndcY * viewport.Y * 0.5f);
    const float tx = pixelX / dpi;
    const float ty = pixelY / dpi;
    if (Move(tx, ty) && !g_bound) {
        g_bound = true;
        Log::Line("reticle: first move (viewport %.0fx%.0f, DPI scale %.3f)", viewport.X, viewport.Y, dpi);
    }

    ReadBackPanels(controller, tx, ty, pixelX, pixelY, viewport, dpi);
}

}  // namespace gr_ht::reticle
