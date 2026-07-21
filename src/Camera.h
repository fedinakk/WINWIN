#pragma once
// Camera model: the single source of truth for the canvas <-> screen mapping.
//
//   screen = (virtual - offset) * scale
//   virtual = screen / scale + offset
//
// "offset" is the virtual-space point shown at screen (0,0); "scale" is zoom.
// Every subsystem (canvas layer, real windows, previews, inertia, snapping)
// converts through these two helpers - no per-module math.

#include "Common.h"

struct Camera {
    Vec2 offset{};      // virtual coords of screen origin
    double scale = 1.0;

    Vec2 VirtualToScreen(Vec2 v) const {
        return { (v.x - offset.x) * scale, (v.y - offset.y) * scale };
    }
    Vec2 ScreenToVirtual(Vec2 s) const {
        return { s.x / scale + offset.x, s.y / scale + offset.y };
    }
    RECT VirtualToScreen(const VRect& r) const {
        Vec2 tl = VirtualToScreen({r.x, r.y});
        Vec2 br = VirtualToScreen({r.Right(), r.Bottom()});
        return RECT{ (LONG)std::lround(tl.x), (LONG)std::lround(tl.y),
                     (LONG)std::lround(br.x), (LONG)std::lround(br.y) };
    }
    VRect ScreenToVirtual(const RECT& r) const {
        Vec2 tl = ScreenToVirtual({(double)r.left, (double)r.top});
        Vec2 br = ScreenToVirtual({(double)r.right, (double)r.bottom});
        return VRect{ tl.x, tl.y, br.x - tl.x, br.y - tl.y };
    }

    // Change scale keeping the virtual point under screenAnchor fixed on screen.
    void ZoomAroundScreenPoint(double newScale, Vec2 screenAnchor) {
        Vec2 v = ScreenToVirtual(screenAnchor);
        scale = newScale;
        offset = { v.x - screenAnchor.x / scale, v.y - screenAnchor.y / scale };
    }

    bool AlmostEquals(const Camera& o) const {
        return std::abs(offset.x - o.offset.x) < 1e-4 &&
               std::abs(offset.y - o.offset.y) < 1e-4 &&
               std::abs(scale - o.scale) < 1e-6;
    }
};

// Smooth camera animation (fly to target) + pan inertia.
// Fly: cubic ease-out over a fixed duration, scale interpolated in log space.
// Inertia: velocity in VIRTUAL px/s (consistent regardless of zoom), with
// exponential friction decay.
class CameraAnimator {
public:
    void FlyTo(const Camera& from, const Camera& to, double now, double durationSec) {
        m_from = from;
        m_to = to;
        m_start = now;
        m_dur = std::max(0.001, durationSec);
        m_flying = true;
        m_inertiaVel = {};
    }
    void CancelFly() { m_flying = false; }
    bool Flying() const { return m_flying; }

    void StartInertia(Vec2 virtualVelocity) {
        m_inertiaVel = virtualVelocity;
        m_flying = false;
    }
    void StopInertia() { m_inertiaVel = {}; }
    bool Coasting() const { return m_inertiaVel.Len() > 1.0; }

    // Advances the camera; returns true if the camera changed.
    bool Tick(Camera& cam, double now, double dt, double friction) {
        if (m_flying) {
            double t = Clamp((now - m_start) / m_dur, 0.0, 1.0);
            double e = 1.0 - std::pow(1.0 - t, 3.0); // ease-out cubic
            double logS = std::log(m_from.scale) + (std::log(m_to.scale) - std::log(m_from.scale)) * e;
            cam.scale = std::exp(logS);
            cam.offset.x = m_from.offset.x + (m_to.offset.x - m_from.offset.x) * e;
            cam.offset.y = m_from.offset.y + (m_to.offset.y - m_from.offset.y) * e;
            if (t >= 1.0) { m_flying = false; cam = m_to; }
            return true;
        }
        if (Coasting()) {
            cam.offset.x += m_inertiaVel.x * dt;
            cam.offset.y += m_inertiaVel.y * dt;
            double decay = std::exp(-friction * dt);
            m_inertiaVel = m_inertiaVel * decay;
            if (!Coasting()) m_inertiaVel = {};
            return true;
        }
        return false;
    }

private:
    Camera m_from{}, m_to{};
    double m_start = 0.0, m_dur = 0.3;
    bool m_flying = false;
    Vec2 m_inertiaVel{};
};
