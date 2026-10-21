#pragma once

#include <QtQml/QJSValue>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace scenebackend
{

// Camera-parallax parameters, mirror of WPShaderValueUpdater::m_parallax +
// m_mousePos.  The shader MVP path shifts a node's model matrix by
// `(nodePos - camPos + mouseVec) * parallaxDepth * amount` for main-camera
// draws, and we mirror the same offset here so clicks land on the layer's
// rendered position.
//
// Layer eligibility is already baked into the proxy's _state.parallaxDepth:
// WPSceneParser zeros it for layers whose final main-camera draw is a
// compose node that doesn't inherit parallax (see ParseImageObj's
// `!hasEffect` branch).  That means you can always pass the global config
// through `para.enable = true` — the shift degenerates to zero per-layer
// when the depth is zero, so layers not visibly shifted are hit-tested
// at their authored origin.
struct CursorParallax {
    bool  enable { false };
    float amount { 0.5f };
    float mouseInfluence { 0.1f };
    float camX { 0.0f }; // camera position X in scene units (ortho center)
    float camY { 0.0f };
    float mouseNx { 0.5f }; // mouse X widget-normalized 0..1
    float mouseNy { 0.5f }; // mouse Y widget-normalized 0..1 (top-down)
    float orthoW { 1920.0f };
    float orthoH { 1080.0f };
};

// Where the cursor falls on a layer: inside its rectangle or not, and the
// point in the layer's own coordinates (its centre at 0,0, before the layer's
// scale and rotation, Y up), which the cursor events carry as
// event.localPosition.  hasLocal is false when the proxy has no transform to
// invert (no _state, or a zero scale).
struct LayerCursorHit {
    bool  inside { false };
    bool  hasLocal { false };
    float localX { 0.0f };
    float localY { 0.0f };
};

// Hit-test against a layer proxy built by _makeLayerProxy (SceneBackend
// embeds its state under `_state` with origin/scale/size subobjects).
// The layer's rectangle is centred on its origin, size x scale, turned by
// its rotation.  Not inside when size is zero or the proxy lacks the
// expected shape — text layers that don't route through WPImageObject used
// to fall into that case; WPSceneParser now feeds text dimensions through
// nameToObjState so the hitbox matches the rendered text.
//
// The transform comes from `_state.worldFrame` when the proxy has one
// ({ox, oy, a, b, c, d}: world = [a c; b d] * local + (ox, oy), the live
// world matrix or the one baked at load), so a rotated or mirrored layer is
// hit where it is drawn.  Without it, the origin and |scale| below give the
// axis-aligned rectangle this test always used.
//
// When `para.enable` is true and the proxy carries a non-zero `parallaxDepth`,
// the hit-test centre is shifted by the same amount the shader MVP shifts the
// rendered quad, so a mouse-driven parallax offset doesn't take cursor clicks
// out from under the layer they're supposed to hit.
inline LayerCursorHit cursorHitOnLayer(const QJSValue& thisLayerProxy, float sceneX, float sceneY,
                                       const CursorParallax& para = {}) {
    LayerCursorHit hit;
    if (! thisLayerProxy.isObject()) return hit;
    QJSValue state = thisLayerProxy.property("_state");
    if (! state.isObject()) return hit;

    // WE `solid` field: when explicitly false the layer opts out of cursor
    // hit-testing.  Wallpaper 2866203962's playerplay alpha script toggles
    // `element.solid = false` when media-control buttons fade out so the
    // invisible quads don't swallow clicks meant for the layers below.
    // Undefined means "on" by default — scripts only set it explicitly.
    // The local point is still worked out: a drag that started on the layer
    // keeps getting it.
    QJSValue solid    = state.property("solid");
    bool     optedOut = solid.isBool() && solid.toBool() == false;

    QJSValue size = state.property("size");
    float    sw   = (float)size.property("x").toNumber();
    float    sh   = (float)size.property("y").toNumber();

    float    ox, oy, a, b, c, d;
    QJSValue frame = state.property("worldFrame");
    if (frame.isObject()) {
        ox = (float)frame.property("ox").toNumber();
        oy = (float)frame.property("oy").toNumber();
        a  = (float)frame.property("a").toNumber();
        b  = (float)frame.property("b").toNumber();
        c  = (float)frame.property("c").toNumber();
        d  = (float)frame.property("d").toNumber();
    } else {
        // Prefer parse-time-baked worldOrigin / worldScale when present.  Those
        // compose the full authored parent chain (image AND group ancestors),
        // so the world-space cursor compares against the world-space layer rect.
        // The local `origin`/`scale` properties are still authoritative for
        // unparented layers (top-level scene objects, dynamic-pool slots, and
        // layers whose script has rewritten `.origin = ...` at runtime), so
        // fall back to them when worldOrigin is missing.
        QJSValue origin, scale;
        QJSValue wo = state.property("worldOrigin");
        QJSValue ws = state.property("worldScale");
        if (wo.isObject() && ws.isObject()) {
            origin = wo;
            scale  = ws;
        } else {
            origin = state.property("origin");
            scale  = state.property("scale");
        }
        ox = (float)origin.property("x").toNumber();
        oy = (float)origin.property("y").toNumber();
        a  = std::abs((float)scale.property("x").toNumber());
        b  = 0.0f;
        c  = 0.0f;
        d  = std::abs((float)scale.property("y").toNumber());
    }

    // Apply parallax shift so the hit-test matches the rendered visual.
    // Mirrors WPShaderValueUpdater.cpp's MVP branch.
    if (para.enable) {
        QJSValue pd = state.property("parallaxDepth");
        float    dx = (float)pd.property("x").toNumber();
        float    dy = (float)pd.property("y").toNumber();
        if (dx != 0.0f || dy != 0.0f) {
            float mouseVx = (0.5f - para.mouseNx) * para.orthoW * para.mouseInfluence;
            float mouseVy = (para.mouseNy - 0.5f) * para.orthoH * para.mouseInfluence;
            float paraX   = (ox - para.camX + mouseVx) * dx * para.amount;
            float paraY   = (oy - para.camY + mouseVy) * dy * para.amount;
            ox += paraX;
            oy += paraY;
        }
    }

    float det = a * d - b * c;
    if (! std::isfinite(det) || det == 0.0f || ! std::isfinite(ox) || ! std::isfinite(oy))
        return hit;
    float px     = sceneX - ox;
    float py     = sceneY - oy;
    hit.localX   = (d * px - c * py) / det;
    hit.localY   = (a * py - b * px) / det;
    hit.hasLocal = true;
    hit.inside   = ! optedOut && sw > 0 && sh > 0 && std::abs(hit.localX) < sw / 2.0f &&
                 std::abs(hit.localY) < sh / 2.0f;
    return hit;
}

inline bool hitTestLayerProxy(const QJSValue& thisLayerProxy, float sceneX, float sceneY,
                              const CursorParallax& para = {}) {
    return cursorHitOnLayer(thisLayerProxy, sceneX, sceneY, para).inside;
}

// One layer under the cursor at a press, for pressPropagation().
struct PressHit {
    int     index { -1 };     // the caller's own index
    int32_t drawOrder { -1 }; // the object's place in scene.json's objects (later is on top)
    bool    stops { false };  // its `disablepropagation` is set
};

// Wallpaper Engine gives a press to every layer under the cursor, from the
// top down, and a layer whose `disablepropagation` is set keeps it from the
// layers beneath.  Returns the hits that get the press, topmost first, ending
// with the first one that stops it.  A layer of unknown order (-1) sorts below
// the others; equal orders keep the caller's order.
inline std::vector<PressHit> pressPropagation(std::vector<PressHit> hits) {
    std::stable_sort(hits.begin(), hits.end(), [](const PressHit& x, const PressHit& y) {
        return x.drawOrder > y.drawOrder;
    });
    std::vector<PressHit> reached;
    for (const auto& h : hits) {
        reached.push_back(h);
        if (h.stops) break;
    }
    return reached;
}

} // namespace scenebackend
