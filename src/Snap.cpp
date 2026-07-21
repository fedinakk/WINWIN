#include "Snap.h"

namespace {

bool Ignored(HWND h, const std::vector<HWND>& ignore) {
    return std::find(ignore.begin(), ignore.end(), h) != ignore.end();
}

bool Participates(const ManagedWindow& mw) {
    return !mw.minimized && !mw.excluded && IsWindow(mw.hwnd);
}

// Overlap of [a1,a2) and [b1,b2), with slack so nearby-but-not-overlapping
// windows can still snap corner-to-corner.
bool RangesNear(double a1, double a2, double b1, double b2, double slack) {
    return a1 < b2 + slack && b1 < a2 + slack;
}

struct AxisSnap {
    bool found = false;
    double adjust = 0.0; // add to raw coordinate to become flush
};

// Candidate edges of 'o' for the X axis of a moving rect [x, x+w).
void CollectX(AxisSnap& best, double x, double w, const VRect& o, double dist, double gap) {
    const double cands[] = {
        o.x - gap - w - x,   // moving right edge -> o.left (side by side)
        o.Right() + gap - x, // moving left edge  -> o.right
        o.x - x,             // left edges aligned
        o.Right() - (x + w), // right edges aligned
    };
    for (double d : cands) {
        if (std::abs(d) <= dist && (!best.found || std::abs(d) < std::abs(best.adjust))) {
            best.found = true;
            best.adjust = d;
        }
    }
}

void CollectY(AxisSnap& best, double y, double h, const VRect& o, double dist, double gap) {
    const double cands[] = {
        o.y - gap - h - y,
        o.Bottom() + gap - y,
        o.y - y,
        o.Bottom() - (y + h),
    };
    for (double d : cands) {
        if (std::abs(d) <= dist && (!best.found || std::abs(d) < std::abs(best.adjust))) {
            best.found = true;
            best.adjust = d;
        }
    }
}

} // namespace

Vec2 SnapMove(const VRect& movingBounds, Vec2 raw,
              const std::vector<ManagedWindow>& all,
              const std::vector<HWND>& ignore,
              double captureDist, double releaseDist, double gap,
              SnapState& state) {
    const double w = movingBounds.w, h = movingBounds.h;

    // Hysteresis: while a snap is active, keep searching with the (larger)
    // release distance so tiny mouse motion doesn't flip the snap on and off.
    AxisSnap bx, by;
    const double dx = state.activeX ? releaseDist : captureDist;
    const double dy = state.activeY ? releaseDist : captureDist;

    for (auto& mw : all) {
        if (!Participates(mw) || Ignored(mw.hwnd, ignore)) continue;
        const VRect& o = mw.virt;
        if (RangesNear(raw.y, raw.y + h, o.y, o.Bottom(), dy))
            CollectX(bx, raw.x, w, o, dx, gap);
        if (RangesNear(raw.x, raw.x + w, o.x, o.Right(), dx))
            CollectY(by, raw.y, h, o, dy, gap);
    }

    state.activeX = bx.found;
    state.adjustX = bx.found ? bx.adjust : 0.0;
    state.activeY = by.found;
    state.adjustY = by.found ? by.adjust : 0.0;

    return { raw.x + state.adjustX, raw.y + state.adjustY };
}

Vec2 SnapResize(const VRect& r, Vec2 rawBR,
                const std::vector<ManagedWindow>& all,
                const std::vector<HWND>& ignore,
                double captureDist, double releaseDist, double gap,
                SnapState& state) {
    AxisSnap bx, by;
    const double dx = state.activeX ? releaseDist : captureDist;
    const double dy = state.activeY ? releaseDist : captureDist;

    for (auto& mw : all) {
        if (!Participates(mw) || Ignored(mw.hwnd, ignore)) continue;
        const VRect& o = mw.virt;
        if (RangesNear(r.y, rawBR.y, o.y, o.Bottom(), dy)) {
            const double cands[] = { o.x - gap - rawBR.x, o.Right() - rawBR.x };
            for (double d : cands)
                if (std::abs(d) <= dx && (!bx.found || std::abs(d) < std::abs(bx.adjust))) {
                    bx.found = true; bx.adjust = d;
                }
        }
        if (RangesNear(r.x, rawBR.x, o.x, o.Right(), dx)) {
            const double cands[] = { o.y - gap - rawBR.y, o.Bottom() - rawBR.y };
            for (double d : cands)
                if (std::abs(d) <= dy && (!by.found || std::abs(d) < std::abs(by.adjust))) {
                    by.found = true; by.adjust = d;
                }
        }
    }

    state.activeX = bx.found;
    state.adjustX = bx.found ? bx.adjust : 0.0;
    state.activeY = by.found;
    state.adjustY = by.found ? by.adjust : 0.0;

    return { rawBR.x + state.adjustX, rawBR.y + state.adjustY };
}

std::vector<HWND> ClusterOf(HWND seed, const std::vector<ManagedWindow>& all,
                            double tolerance, double gap) {
    std::vector<HWND> cluster;
    const ManagedWindow* seedMw = nullptr;
    for (auto& mw : all)
        if (mw.hwnd == seed) { seedMw = &mw; break; }
    if (!seedMw) return cluster;

    auto touching = [&](const VRect& a, const VRect& b) {
        const double t = tolerance + gap;
        bool xTouch = std::abs(a.Right() - b.x) <= t || std::abs(b.Right() - a.x) <= t;
        bool yTouch = std::abs(a.Bottom() - b.y) <= t || std::abs(b.Bottom() - a.y) <= t;
        bool xOver = a.x < b.Right() + tolerance && b.x < a.Right() + tolerance;
        bool yOver = a.y < b.Bottom() + tolerance && b.y < a.Bottom() + tolerance;
        return (xTouch && yOver) || (yTouch && xOver);
    };

    cluster.push_back(seed);
    std::vector<const ManagedWindow*> frontier{ seedMw };
    while (!frontier.empty()) {
        const ManagedWindow* cur = frontier.back();
        frontier.pop_back();
        for (auto& mw : all) {
            if (!Participates(mw)) continue;
            if (std::find(cluster.begin(), cluster.end(), mw.hwnd) != cluster.end()) continue;
            if (touching(cur->virt, mw.virt)) {
                cluster.push_back(mw.hwnd);
                frontier.push_back(&mw);
            }
        }
    }
    return cluster;
}
