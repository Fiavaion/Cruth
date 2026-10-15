// Cruth core — editable 1D curves for brush falloff and pressure mapping (PRD B-02).
// SPDX-License-Identifier: MIT
#pragma once

#include <vector>

namespace cruth {

// Piecewise-linear curve through control points with x in [0, 1], sorted by x.
// Evaluation clamps x to [0, 1] and holds the end values outside the points.
class Curve {
public:
    struct Point {
        float x, y;
    };

    Curve() = default;
    explicit Curve(std::vector<Point> points);

    float evaluate(float x) const;
    const std::vector<Point>& points() const { return points_; }

    static Curve identity();          // y = x
    static Curve constant(float y);   // y = c
    static Curve linear_falloff();    // 1 at centre, 0 at rim
    static Curve smooth_falloff();    // smoothstep, the default brush falloff
    static Curve sphere_falloff();    // sqrt(1 - x^2): flat, round-shouldered
    static Curve sharp_falloff();     // (1 - x)^2: pointed

private:
    std::vector<Point> points_{{0, 1}, {1, 1}};
};

}  // namespace cruth
