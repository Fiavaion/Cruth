// Cruth core — small vector maths used throughout the geometry kernel.
// SPDX-License-Identifier: MIT
#pragma once

#include <algorithm>
#include <cmath>
#include <limits>

namespace cruth {

struct Vec3 {
    float x = 0, y = 0, z = 0;

    constexpr Vec3() = default;
    constexpr Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

    constexpr float operator[](int i) const { return i == 0 ? x : (i == 1 ? y : z); }
    float& operator[](int i) { return i == 0 ? x : (i == 1 ? y : z); }

    constexpr Vec3 operator+(Vec3 o) const { return {x + o.x, y + o.y, z + o.z}; }
    constexpr Vec3 operator-(Vec3 o) const { return {x - o.x, y - o.y, z - o.z}; }
    constexpr Vec3 operator-() const { return {-x, -y, -z}; }
    constexpr Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    constexpr Vec3 operator/(float s) const { return {x / s, y / s, z / s}; }
    Vec3& operator+=(Vec3 o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3& operator-=(Vec3 o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    Vec3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
    Vec3& operator/=(float s) { x /= s; y /= s; z /= s; return *this; }
    constexpr bool operator==(const Vec3&) const = default;
};

constexpr Vec3 operator*(float s, Vec3 v) { return v * s; }

constexpr float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
constexpr Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
constexpr float length_squared(Vec3 v) { return dot(v, v); }
inline float length(Vec3 v) { return std::sqrt(length_squared(v)); }
inline float distance(Vec3 a, Vec3 b) { return length(a - b); }
constexpr Vec3 lerp(Vec3 a, Vec3 b, float t) { return a + (b - a) * t; }
inline Vec3 min(Vec3 a, Vec3 b) { return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)}; }
inline Vec3 max(Vec3 a, Vec3 b) { return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)}; }

// Returns the zero vector when v is degenerate, so callers never see NaNs.
inline Vec3 normalize(Vec3 v) {
    float len = length(v);
    return len > 1e-20f ? v / len : Vec3{};
}

struct Vec2 {
    float x = 0, y = 0;

    constexpr Vec2() = default;
    constexpr Vec2(float x_, float y_) : x(x_), y(y_) {}

    constexpr Vec2 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
    constexpr Vec2 operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
    constexpr Vec2 operator*(float s) const { return {x * s, y * s}; }
    constexpr Vec2 operator/(float s) const { return {x / s, y / s}; }
    Vec2& operator+=(Vec2 o) { x += o.x; y += o.y; return *this; }
    Vec2& operator-=(Vec2 o) { x -= o.x; y -= o.y; return *this; }
    constexpr bool operator==(const Vec2&) const = default;
};

constexpr float dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
constexpr float cross(Vec2 a, Vec2 b) { return a.x * b.y - a.y * b.x; }

struct Bounds {
    Vec3 lo{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max()};
    Vec3 hi{std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(),
            std::numeric_limits<float>::lowest()};

    bool empty() const { return lo.x > hi.x; }
    void extend(Vec3 p) { lo = min(lo, p); hi = max(hi, p); }
    Vec3 size() const { return empty() ? Vec3{} : hi - lo; }
    Vec3 center() const { return empty() ? Vec3{} : (lo + hi) * 0.5f; }
};

// Rodrigues rotation of v around a unit axis.
inline Vec3 rotate(Vec3 v, Vec3 axis, float angle) {
    float c = std::cos(angle), s = std::sin(angle);
    return v * c + cross(axis, v) * s + axis * (dot(axis, v) * (1 - c));
}

}  // namespace cruth
