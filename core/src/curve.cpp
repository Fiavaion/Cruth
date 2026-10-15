// SPDX-License-Identifier: MIT
#include "cruth/curve.hpp"

#include <algorithm>
#include <cmath>
#include <functional>

namespace cruth {

Curve::Curve(std::vector<Point> points) : points_(std::move(points)) {
    std::sort(points_.begin(), points_.end(), [](Point a, Point b) { return a.x < b.x; });
    if (points_.empty()) points_ = {{0, 1}, {1, 1}};
}

float Curve::evaluate(float x) const {
    x = std::clamp(x, 0.0f, 1.0f);
    if (x <= points_.front().x) return points_.front().y;
    if (x >= points_.back().x) return points_.back().y;
    auto hi = std::upper_bound(points_.begin(), points_.end(), x, [](float v, Point p) { return v < p.x; });
    auto lo = hi - 1;
    float span = hi->x - lo->x;
    float t = span > 0 ? (x - lo->x) / span : 0.0f;
    return lo->y + (hi->y - lo->y) * t;
}

static Curve sampled(const std::function<float(float)>& f, int samples = 33) {
    std::vector<Curve::Point> pts;
    pts.reserve(samples);
    for (int i = 0; i < samples; ++i) {
        float x = float(i) / float(samples - 1);
        pts.push_back({x, f(x)});
    }
    return Curve(std::move(pts));
}

Curve Curve::identity() { return Curve({{0, 0}, {1, 1}}); }
Curve Curve::constant(float y) { return Curve({{0, y}, {1, y}}); }
Curve Curve::linear_falloff() { return Curve({{0, 1}, {1, 0}}); }

Curve Curve::smooth_falloff() {
    return sampled([](float x) {
        float t = 1 - x;
        return t * t * (3 - 2 * t);
    });
}

Curve Curve::sphere_falloff() {
    return sampled([](float x) { return std::sqrt(std::max(0.0f, 1 - x * x)); });
}

Curve Curve::sharp_falloff() {
    return sampled([](float x) { return (1 - x) * (1 - x); });
}

}  // namespace cruth
