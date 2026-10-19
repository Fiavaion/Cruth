// SPDX-License-Identifier: MIT
#include "cruth/sculpt.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <unordered_map>

namespace cruth {

namespace {

// Uniform hash grid over vertex positions that can be updated as vertices move.
class SpatialGrid {
public:
    void build(const std::vector<Vec3>& positions, float cell) {
        cell_ = std::max(cell, 1e-6f);
        cells_.clear();
        key_of_.resize(positions.size());
        slot_of_.resize(positions.size());
        for (Index v = 0; v < positions.size(); ++v) insert(v, key(positions[v]));
    }

    float cell_size() const { return cell_; }

    void move(Index v, Vec3 p) {
        std::uint64_t k = key(p);
        if (k == key_of_[v]) return;
        remove(v);
        insert(v, k);
    }

    template <typename Fn>
    void query(Vec3 c, float r, const std::vector<Vec3>& positions, Fn&& fn) const {
        const float r2 = r * r;
        const int x0 = coord(c.x - r), x1 = coord(c.x + r);
        const int y0 = coord(c.y - r), y1 = coord(c.y + r);
        const int z0 = coord(c.z - r), z1 = coord(c.z + r);
        for (int x = x0; x <= x1; ++x)
            for (int y = y0; y <= y1; ++y)
                for (int z = z0; z <= z1; ++z) {
                    auto it = cells_.find(pack(x, y, z));
                    if (it == cells_.end()) continue;
                    for (Index v : it->second) {
                        float d2 = length_squared(positions[v] - c);
                        if (d2 <= r2) fn(v, std::sqrt(d2));
                    }
                }
    }

private:
    int coord(float v) const { return static_cast<int>(std::floor(v / cell_)); }
    static std::uint64_t pack(int x, int y, int z) {
        auto u = [](int i) { return std::uint64_t(std::uint32_t(i + (1 << 20)) & 0x1FFFFFu); };
        return (u(x) << 42) | (u(y) << 21) | u(z);
    }
    std::uint64_t key(Vec3 p) const { return pack(coord(p.x), coord(p.y), coord(p.z)); }

    void insert(Index v, std::uint64_t k) {
        auto& bucket = cells_[k];
        key_of_[v] = k;
        slot_of_[v] = static_cast<Index>(bucket.size());
        bucket.push_back(v);
    }
    void remove(Index v) {
        auto& bucket = cells_[key_of_[v]];
        Index slot = slot_of_[v];
        Index last = bucket.back();
        bucket[slot] = last;
        slot_of_[last] = slot;
        bucket.pop_back();
    }

    float cell_ = 1.0f;
    std::unordered_map<std::uint64_t, std::vector<Index>> cells_;
    std::vector<std::uint64_t> key_of_;
    std::vector<Index> slot_of_;
};

// Mirror (per-axis sign) followed by a rotation about a principal axis.
// Linear, so it maps points and directions alike.
struct SymmetryTransform {
    Vec3 sign{1, 1, 1};
    Vec3 axis{0, 1, 0};
    float angle = 0;

    Vec3 apply(Vec3 v) const {
        v = {v.x * sign.x, v.y * sign.y, v.z * sign.z};
        return angle == 0 ? v : rotate(v, axis, angle);
    }
};

std::vector<SymmetryTransform> symmetry_transforms(const Symmetry& s) {
    std::vector<SymmetryTransform> out;
    const int radial = std::max(1, s.radial_count);
    Vec3 axis{s.radial_axis == 0 ? 1.f : 0.f, s.radial_axis == 1 ? 1.f : 0.f, s.radial_axis == 2 ? 1.f : 0.f};
    for (int m = 0; m < 8; ++m) {
        if (((m & 1) && !s.x) || ((m & 2) && !s.y) || ((m & 4) && !s.z)) continue;
        Vec3 sign{(m & 1) ? -1.f : 1.f, (m & 2) ? -1.f : 1.f, (m & 4) ? -1.f : 1.f};
        for (int k = 0; k < radial; ++k) {
            out.push_back({sign, axis, 2.0f * std::numbers::pi_v<float> * float(k) / float(radial)});
        }
    }
    return out;
}

}  // namespace

struct Sculptor::Impl {
    explicit Impl(Mesh& m) : mesh(m) {}

    Mesh& mesh;
    VertexAdjacency adjacency;
    std::vector<Index> vf_offsets, vf_faces;  // vertex -> faces
    std::vector<Vec3> normals;
    std::vector<float> mask;
    SpatialGrid grid;
    std::uint64_t revision = 0;

    // Moved-vertex journal; journal_base is the sequence number of journal[0].
    std::vector<Index> journal;
    std::uint64_t journal_base = 0;

    // Stroke state.
    bool active = false;
    BrushSettings brush;
    std::vector<SymmetryTransform> transforms;
    bool has_last = false;
    Vec3 last_dab;
    float last_pressure = 1;
    Vec3 last_sample;

    std::vector<std::uint32_t> pos_stamp, mask_stamp, scratch_stamp;
    std::uint32_t stroke_id = 0, scratch_id = 0;
    SculptDelta delta;

    struct GrabSet {
        SymmetryTransform xf;
        std::vector<Index> vertices;
        std::vector<float> weights;
    };
    std::vector<GrabSet> grabs;

    // Scratch buffers reused between dabs.
    std::vector<Index> hit_v;
    std::vector<float> hit_w;
    std::vector<Vec3> new_pos;
    std::vector<Vec3> smooth_tmp;
    std::vector<Index> changed;

    void rebuild() {
        const std::size_t n = mesh.vertex_count();
        adjacency = VertexAdjacency::build(mesh);
        vf_offsets.assign(n + 1, 0);
        for (Index v : mesh.face_indices()) ++vf_offsets[v + 1];
        for (std::size_t i = 1; i <= n; ++i) vf_offsets[i] += vf_offsets[i - 1];
        vf_faces.resize(mesh.corner_count());
        std::vector<Index> fill(vf_offsets.begin(), vf_offsets.end() - 1);
        for (Index f = 0; f < mesh.face_count(); ++f) {
            for (Index v : mesh.face(f)) vf_faces[fill[v]++] = f;
        }
        normals = mesh.vertex_normals();
        mask.resize(n, 0.0f);
        pos_stamp.assign(n, 0);
        mask_stamp.assign(n, 0);
        scratch_stamp.assign(n, 0);
        stroke_id = scratch_id = 0;
        grid.build(mesh.positions, default_cell());
        ++revision;
        // Every vertex may have moved outside the journal: invalidate all marks.
        journal_base += journal.size() + 1;
        journal.clear();
    }

    // Bounded so long runs without a consumer cannot grow it without limit;
    // a consumer that falls behind an overflow resyncs in full.
    std::size_t journal_capacity() const { return std::max<std::size_t>(std::size_t(1) << 16, mesh.vertex_count()); }

    void journal_moved(const std::vector<Index>& moved) {
        if (journal.size() + moved.size() > journal_capacity()) {
            journal_base += journal.size();
            journal.clear();
            if (moved.size() > journal_capacity()) {
                journal_base += moved.size();
                return;
            }
        }
        journal.insert(journal.end(), moved.begin(), moved.end());
    }

    float default_cell() const {
        Vec3 s = mesh.bounds().size();
        float extent = std::max({s.x, s.y, s.z, 1e-3f});
        return extent / 64.0f;
    }

    std::uint32_t next_scratch() {
        if (++scratch_id == 0) {
            std::fill(scratch_stamp.begin(), scratch_stamp.end(), 0);
            scratch_id = 1;
        }
        return scratch_id;
    }

    void record_position(Index v) {
        if (pos_stamp[v] == stroke_id) return;
        pos_stamp[v] = stroke_id;
        delta.vertices.push_back(v);
        delta.before.push_back(mesh.positions[v]);
    }
    void record_mask(Index v) {
        if (mask_stamp[v] == stroke_id) return;
        mask_stamp[v] = stroke_id;
        delta.mask_vertices.push_back(v);
        delta.mask_before.push_back(mask[v]);
    }

    // Recompute normals around the vertices in `changed`.
    void update_normals(const std::vector<Index>& moved) {
        const std::uint32_t id = next_scratch();
        std::vector<Index>& ring = hit_v;  // reuse: the dab is finished with it
        ring.clear();
        for (Index v : moved) {
            for (Index i = vf_offsets[v]; i < vf_offsets[v + 1]; ++i) {
                for (Index u : mesh.face(vf_faces[i])) {
                    if (scratch_stamp[u] != id) {
                        scratch_stamp[u] = id;
                        ring.push_back(u);
                    }
                }
            }
        }
        for (Index u : ring) {
            Vec3 n;
            for (Index i = vf_offsets[u]; i < vf_offsets[u + 1]; ++i) {
                Index f = vf_faces[i];
                n += mesh.face_normal(f) * mesh.face_area(f);
            }
            normals[u] = normalize(n);
        }
    }

    void set_position(Index v, Vec3 p) {
        record_position(v);
        mesh.positions[v] = p;
        grid.move(v, p);
        changed.push_back(v);
    }

    float pressure_factor(float pressure) const { return std::clamp(brush.pressure_curve.evaluate(pressure), 0.0f, 1.0f); }
    float radius_at(float pressure) const {
        return brush.pressure_affects_radius ? brush.radius * std::max(0.05f, pressure_factor(pressure)) : brush.radius;
    }
    float strength_at(float pressure) const {
        return brush.pressure_affects_strength ? brush.strength * pressure_factor(pressure) : brush.strength;
    }

    void gather(Vec3 c, float r, bool use_mask) {
        hit_v.clear();
        hit_w.clear();
        grid.query(c, r, mesh.positions, [&](Index v, float d) {
            float w = brush.falloff.evaluate(d / r);
            if (use_mask) w *= 1.0f - mask[v];
            if (w > 0) {
                hit_v.push_back(v);
                hit_w.push_back(w);
            }
        });
    }

    void dab(Vec3 center, float pressure) {
        const float r = radius_at(pressure);
        const float s = strength_at(pressure);
        if (r <= 0 || s <= 0) return;
        if (r > grid.cell_size() * 8 || r < grid.cell_size() * 0.25f) grid.build(mesh.positions, r * 0.5f);
        changed.clear();
        for (const auto& xf : transforms) dab_one(xf.apply(center), r, s);
        if (!changed.empty()) {
            journal_moved(changed);
            update_normals(changed);
            ++revision;
        }
    }

    void dab_one(Vec3 c, float r, float s) {
        const bool is_mask = brush.type == BrushType::Mask;
        gather(c, r, brush.respect_mask && !is_mask);
        if (hit_v.empty()) return;

        const float sign = brush.invert ? -1.0f : 1.0f;
        if (is_mask) {
            for (std::size_t i = 0; i < hit_v.size(); ++i) {
                Index v = hit_v[i];
                record_mask(v);
                mask[v] = std::clamp(mask[v] + sign * hit_w[i] * s, 0.0f, 1.0f);
            }
            ++revision;
            return;
        }

        if (brush.type == BrushType::Smooth) {
            smooth(s);
            return;
        }

        // Brush frame: weighted average normal and centre of the affected area.
        Vec3 area_n, area_c;
        float wsum = 0;
        for (std::size_t i = 0; i < hit_v.size(); ++i) {
            area_n += normals[hit_v[i]] * hit_w[i];
            area_c += mesh.positions[hit_v[i]] * hit_w[i];
            wsum += hit_w[i];
        }
        area_n = normalize(area_n);
        area_c /= wsum;
        if (length_squared(area_n) == 0) return;

        // Displacement per dab scales with spacing so stroke strength does not
        // depend on how densely the dabs are placed.
        const float step = r * std::clamp(brush.spacing, 0.01f, 1.0f) * 0.5f;

        new_pos.resize(hit_v.size());
        for (std::size_t i = 0; i < hit_v.size(); ++i) {
            const Index v = hit_v[i];
            const Vec3 p = mesh.positions[v];
            const float w = hit_w[i] * s;
            Vec3 q = p;
            switch (brush.type) {
                case BrushType::Standard:
                    q = p + area_n * (sign * w * step);
                    break;
                case BrushType::Inflate:
                    q = p + normals[v] * (sign * w * step);
                    break;
                case BrushType::Clay: {
                    // Plane slightly above the surface; only points below it rise.
                    Vec3 plane = area_c + area_n * (sign * r * 0.15f);
                    float d = dot(p - plane, area_n) * sign;
                    if (d < 0) q = p - area_n * (d * sign * std::min(1.0f, w));
                    break;
                }
                case BrushType::Flatten: {
                    float d = dot(p - area_c, area_n);
                    q = p - area_n * (d * std::min(1.0f, w));
                    break;
                }
                case BrushType::Smooth:  // handled by smooth()
                case BrushType::Pinch:
                case BrushType::Crease: {
                    Vec3 to_c = c - p;
                    to_c -= area_n * dot(to_c, area_n);
                    q = p + to_c * (sign * w * 0.25f);
                    if (brush.type == BrushType::Crease) q -= area_n * (sign * w * step);
                    break;
                }
                case BrushType::Grab:
                case BrushType::Mask:
                    break;
            }
            new_pos[i] = q;
        }
        // Write after computing so Smooth reads consistent neighbour positions.
        for (std::size_t i = 0; i < hit_v.size(); ++i) {
            if (!(new_pos[i] == mesh.positions[hit_v[i]])) set_position(hit_v[i], new_pos[i]);
        }
    }

    // Taubin lambda|mu smoothing: a shrinking Laplacian step followed by an
    // inflating one, which removes noise without the volume loss of plain
    // Laplacian smoothing. Neighbours outside the brush keep their positions.
    void smooth(float s) {
        constexpr float kLambda = 0.5f, kMu = -0.53f;
        smooth_tmp.resize(mesh.vertex_count());
        const std::uint32_t id = next_scratch();
        for (Index v : hit_v) scratch_stamp[v] = id;
        auto at = [&](Index u, bool second) {
            return second && scratch_stamp[u] == id ? smooth_tmp[u] : mesh.positions[u];
        };
        auto step = [&](bool second, float factor) {
            new_pos.resize(hit_v.size());
            for (std::size_t i = 0; i < hit_v.size(); ++i) {
                const Index v = hit_v[i];
                const Vec3 p = at(v, second);
                auto nb = adjacency.of(v);
                if (nb.empty()) {
                    new_pos[i] = p;
                    continue;
                }
                Vec3 avg;
                for (Index u : nb) avg += at(u, second);
                avg /= static_cast<float>(nb.size());
                new_pos[i] = p + (avg - p) * (factor * std::min(1.0f, hit_w[i] * s));
            }
            for (std::size_t i = 0; i < hit_v.size(); ++i) smooth_tmp[hit_v[i]] = new_pos[i];
        };
        step(false, kLambda);
        step(true, kMu);
        for (std::size_t i = 0; i < hit_v.size(); ++i) {
            if (!(new_pos[i] == mesh.positions[hit_v[i]])) set_position(hit_v[i], new_pos[i]);
        }
    }

    void begin_grab(Vec3 c, float pressure) {
        const float r = radius_at(pressure);
        grabs.clear();
        for (const auto& xf : transforms) {
            gather(xf.apply(c), r, brush.respect_mask);
            grabs.push_back({xf, hit_v, hit_w});
        }
    }

    void drag_grab(Vec3 movement) {
        changed.clear();
        for (const auto& g : grabs) {
            Vec3 d = g.xf.apply(movement);
            for (std::size_t i = 0; i < g.vertices.size(); ++i) {
                Index v = g.vertices[i];
                set_position(v, mesh.positions[v] + d * (g.weights[i] * brush.strength * 2.0f));
            }
        }
        if (!changed.empty()) {
            journal_moved(changed);
            update_normals(changed);
            ++revision;
        }
    }
};

Sculptor::Sculptor(Mesh& mesh) : impl_(std::make_unique<Impl>(mesh)) { impl_->rebuild(); }
Sculptor::~Sculptor() = default;

void Sculptor::rebuild() { impl_->rebuild(); }

void Sculptor::begin_stroke(const BrushSettings& settings) {
    Impl& s = *impl_;
    s.active = true;
    s.brush = settings;
    s.transforms = symmetry_transforms(settings.symmetry);
    s.has_last = false;
    s.delta = {};
    s.grabs.clear();
    if (++s.stroke_id == 0) {
        std::fill(s.pos_stamp.begin(), s.pos_stamp.end(), 0);
        std::fill(s.mask_stamp.begin(), s.mask_stamp.end(), 0);
        s.stroke_id = 1;
    }
}

void Sculptor::add_sample(const StrokeSample& sample) {
    Impl& s = *impl_;
    if (!s.active) return;

    if (s.brush.type == BrushType::Grab) {
        if (!s.has_last) {
            s.begin_grab(sample.position, sample.pressure);
        } else {
            s.drag_grab(sample.position - s.last_sample);
        }
        s.has_last = true;
        s.last_sample = sample.position;
        return;
    }

    if (!s.has_last) {
        s.dab(sample.position, sample.pressure);
        s.has_last = true;
        s.last_dab = sample.position;
        s.last_pressure = sample.pressure;
        return;
    }

    // Place dabs every spacing * radius along the segment from the last dab.
    const float step = std::max(s.radius_at(sample.pressure) * std::clamp(s.brush.spacing, 0.01f, 1.0f), 1e-6f);
    const float d = distance(s.last_dab, sample.position);
    if (d < step) return;
    const int n = static_cast<int>(d / step);
    const Vec3 from = s.last_dab;
    const float from_p = s.last_pressure;
    for (int i = 1; i <= n; ++i) {
        float t = float(i) * step / d;
        s.dab(lerp(from, sample.position, t), from_p + (sample.pressure - from_p) * t);
    }
    float t = float(n) * step / d;
    s.last_dab = lerp(from, sample.position, t);
    s.last_pressure = from_p + (sample.pressure - from_p) * t;
}

SculptDelta Sculptor::end_stroke() {
    Impl& s = *impl_;
    s.active = false;
    s.grabs.clear();
    SculptDelta out = std::move(s.delta);
    s.delta = {};
    out.after.reserve(out.vertices.size());
    for (Index v : out.vertices) out.after.push_back(s.mesh.positions[v]);
    out.mask_after.reserve(out.mask_vertices.size());
    for (Index v : out.mask_vertices) out.mask_after.push_back(s.mask[v]);
    return out;
}

bool Sculptor::in_stroke() const { return impl_->active; }

void Sculptor::apply(const SculptDelta& delta, bool forward) {
    Impl& s = *impl_;
    const auto& pos = forward ? delta.after : delta.before;
    const auto& msk = forward ? delta.mask_after : delta.mask_before;
    for (std::size_t i = 0; i < delta.vertices.size(); ++i) {
        Index v = delta.vertices[i];
        s.mesh.positions[v] = pos[i];
        s.grid.move(v, pos[i]);
    }
    for (std::size_t i = 0; i < delta.mask_vertices.size(); ++i) s.mask[delta.mask_vertices[i]] = msk[i];
    s.journal_moved(delta.vertices);
    s.update_normals(delta.vertices);
    ++s.revision;
}

const std::vector<Vec3>& Sculptor::normals() const { return impl_->normals; }
std::vector<float>& Sculptor::mask() { return impl_->mask; }
const std::vector<float>& Sculptor::mask() const { return impl_->mask; }

void Sculptor::clear_mask() {
    std::fill(impl_->mask.begin(), impl_->mask.end(), 0.0f);
    ++impl_->revision;
}

void Sculptor::invert_mask() {
    for (auto& m : impl_->mask) m = 1.0f - m;
    ++impl_->revision;
}

std::uint64_t Sculptor::revision() const { return impl_->revision; }

std::uint64_t Sculptor::journal_end() const { return impl_->journal_base + impl_->journal.size(); }

std::optional<std::span<const Index>> Sculptor::moved_since(std::uint64_t mark) const {
    const Impl& s = *impl_;
    if (mark < s.journal_base || mark > journal_end()) return std::nullopt;
    const auto skip = static_cast<std::size_t>(mark - s.journal_base);
    return std::span<const Index>(s.journal).subspan(skip);
}

}  // namespace cruth
