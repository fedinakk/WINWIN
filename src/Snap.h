#pragma once
// Edge snapping and window clusters, all in virtual (canvas) coordinates.
//
// Snapping happens while Alt-dragging: when a moving rect's edge comes within
// the capture distance of another window's edge, the rect is pulled flush.
// Hysteresis (release distance > capture distance) prevents jitter right at
// the boundary. Clusters are connected components of windows whose edges
// touch; they move/resize as a unit.

#include "Common.h"
#include "WindowTracker.h"

struct SnapState {
    // Active snap per axis: the correction applied to the raw drag position.
    bool activeX = false, activeY = false;
    double adjustX = 0.0, adjustY = 0.0;
};

// Snaps 'raw' (the unsnapped drag position of the moving bounds) against the
// edges of other windows. 'ignore' lists windows that move together with the
// dragged one (self or whole cluster). Updates 'state' with hysteresis.
// Returns the snapped position (top-left) for the moving bounds.
Vec2 SnapMove(const VRect& movingBounds, Vec2 raw,
              const std::vector<ManagedWindow>& all,
              const std::vector<HWND>& ignore,
              double captureDist, double releaseDist, double gap,
              SnapState& state);

// Snaps the bottom-right corner while resizing (left/top edges fixed).
Vec2 SnapResize(const VRect& fixedTopLeftRect, Vec2 rawBottomRight,
                const std::vector<ManagedWindow>& all,
                const std::vector<HWND>& ignore,
                double captureDist, double releaseDist, double gap,
                SnapState& state);

// Connected component of windows whose edges touch (within tolerance).
std::vector<HWND> ClusterOf(HWND seed, const std::vector<ManagedWindow>& all,
                            double tolerance, double gap);
