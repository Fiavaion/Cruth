// SPDX-License-Identifier: MIT
#include "cruth/mesh_io.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace cruth {

namespace {

std::string lower(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// Line/token scanner over an in-memory buffer; never allocates per token.
class Scanner {
public:
    explicit Scanner(std::string_view s) : s_(s) {}
    bool at_end() const { return pos_ >= s_.size(); }

    std::string_view line() {
        std::size_t end = s_.find('\n', pos_);
        if (end == std::string_view::npos) end = s_.size();
        std::string_view l = s_.substr(pos_, end - pos_);
        pos_ = std::min(end + 1, s_.size());
        if (!l.empty() && l.back() == '\r') l.remove_suffix(1);
        return l;
    }
    std::size_t position() const { return pos_; }

private:
    std::string_view s_;
    std::size_t pos_ = 0;
};

std::string_view next_token(std::string_view& line) {
    std::size_t i = 0;
    while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
    std::size_t j = i;
    while (j < line.size() && !std::isspace(static_cast<unsigned char>(line[j]))) ++j;
    std::string_view tok = line.substr(i, j - i);
    line.remove_prefix(j);
    return tok;
}

bool parse_float(std::string_view tok, float& out) {
    if (tok.empty()) return false;
    // std::from_chars for floats is not available on every Apple libc++ we target,
    // so use strtof on a bounded copy.
    char buf[64];
    if (tok.size() >= sizeof(buf)) return false;
    std::memcpy(buf, tok.data(), tok.size());
    buf[tok.size()] = '\0';
    char* end = nullptr;
    out = std::strtof(buf, &end);
    return end == buf + tok.size() && std::isfinite(out);
}

bool parse_long(std::string_view tok, long& out) {
    if (!tok.empty() && tok.front() == '+') tok.remove_prefix(1);
    auto r = std::from_chars(tok.data(), tok.data() + tok.size(), out);
    return r.ec == std::errc{} && r.ptr == tok.data() + tok.size();
}

Error parse_error(const std::string& what) { return Error{ErrorCode::Parse, what}; }

template <typename T>
T read_le(const char* p) {
    T v;
    std::memcpy(&v, p, sizeof(T));  // all supported targets are little-endian
    return v;
}

template <typename T>
void append_le(std::string& out, T v) {
    char buf[sizeof(T)];
    std::memcpy(buf, &v, sizeof(T));
    out.append(buf, sizeof(T));
}

}  // namespace

MeshFormat format_from_path(std::string_view path) {
    auto dot = path.rfind('.');
    if (dot == std::string_view::npos) return MeshFormat::Unknown;
    std::string ext = lower(path.substr(dot + 1));
    if (ext == "obj") return MeshFormat::Obj;
    if (ext == "stl") return MeshFormat::Stl;
    if (ext == "ply") return MeshFormat::Ply;
    return MeshFormat::Unknown;
}

// ---------------------------------------------------------------- OBJ

Result<Mesh> read_obj(std::string_view text, UVSet* uvs) {
    Mesh mesh;
    Scanner sc(text);
    std::vector<Index> corners;
    std::vector<Vec2> texcoords;
    std::vector<Index> corner_uv;
    bool every_corner_has_uv = true;
    std::size_t line_no = 0;
    auto resolve = [](long idx, std::size_t count) -> long {
        return idx > 0 ? idx - 1 : static_cast<long>(count) + idx;
    };
    while (!sc.at_end()) {
        std::string_view l = sc.line();
        ++line_no;
        std::string_view tag = next_token(l);
        if (tag == "v") {
            Vec3 p;
            for (int i = 0; i < 3; ++i) {
                if (!parse_float(next_token(l), p[i])) {
                    return parse_error("OBJ line " + std::to_string(line_no) + ": bad vertex");
                }
            }
            mesh.add_vertex(p);
        } else if (tag == "vt") {
            Vec2 t;
            if (!parse_float(next_token(l), t.x) || !parse_float(next_token(l), t.y)) {
                return parse_error("OBJ line " + std::to_string(line_no) + ": bad texture coordinate");
            }
            texcoords.push_back(t);
        } else if (tag == "f") {
            corners.clear();
            for (std::string_view tok = next_token(l); !tok.empty(); tok = next_token(l)) {
                // "v", "v/vt", "v//vn" or "v/vt/vn".
                const std::size_t slash = tok.find('/');
                std::string_view vtok = tok.substr(0, slash);
                long idx = 0;
                if (!parse_long(vtok, idx) || idx == 0) {
                    return parse_error("OBJ line " + std::to_string(line_no) + ": bad face index");
                }
                const long resolved = resolve(idx, mesh.vertex_count());
                if (resolved < 0 || resolved >= static_cast<long>(mesh.vertex_count())) {
                    return parse_error("OBJ line " + std::to_string(line_no) + ": face index out of range");
                }
                corners.push_back(static_cast<Index>(resolved));

                long t = 0;
                std::string_view rest = slash == std::string_view::npos ? std::string_view{} : tok.substr(slash + 1);
                std::string_view ttok = rest.substr(0, rest.find('/'));
                const long tr = !ttok.empty() && parse_long(ttok, t) && t != 0 ? resolve(t, texcoords.size()) : -1;
                if (tr < 0 || tr >= static_cast<long>(texcoords.size())) {
                    every_corner_has_uv = false;
                    corner_uv.push_back(0);
                } else {
                    corner_uv.push_back(static_cast<Index>(tr));
                }
            }
            if (corners.size() < 3) return parse_error("OBJ line " + std::to_string(line_no) + ": face has < 3 corners");
            mesh.add_face(corners);
        }
        // vn, g, o, s, usemtl, mtllib and comments are ignored.
    }
    if (uvs) {
        *uvs = UVSet{};
        if (every_corner_has_uv && !texcoords.empty() && mesh.face_count() > 0) {
            float max_u = 0;
            for (const auto& t : texcoords) max_u = std::max(max_u, t.x);
            uvs->uvs = std::move(texcoords);
            uvs->corner_uv = std::move(corner_uv);
            uvs->face_chart.assign(mesh.face_count(), 0);
            uvs->tiles = std::max(1, int(std::ceil(max_u)));
        }
    }
    return mesh;
}

std::string write_obj(const Mesh& mesh, const UVSet* uvs) {
    const bool with_uv = uvs && uvs->matches(mesh);
    std::ostringstream os;
    os << "# Cruth OBJ export\n";
    os.precision(9);
    for (const auto& p : mesh.positions) os << "v " << p.x << ' ' << p.y << ' ' << p.z << '\n';
    if (with_uv) {
        for (const auto& t : uvs->uvs) os << "vt " << t.x << ' ' << t.y << '\n';
    }
    for (std::size_t f = 0; f < mesh.face_count(); ++f) {
        os << 'f';
        auto fv = mesh.face(f);
        for (std::size_t i = 0; i < fv.size(); ++i) {
            os << ' ' << (fv[i] + 1);
            if (with_uv) os << '/' << (uvs->corner_uv[mesh.face_offsets()[f] + i] + 1);
        }
        os << '\n';
    }
    return os.str();
}

std::string write_baked_asset_obj(const Mesh& mesh, const UVSet& uvs) {
    std::ostringstream os;
    os << "# Cruth OBJ export (baked asset)\n";
    os.precision(9);
    for (const auto& p : mesh.positions) os << "v " << p.x << ' ' << p.y << ' ' << p.z << '\n';
    for (const auto& t : uvs.uvs) os << "vt " << t.x << ' ' << t.y << '\n';
    for (const auto& n : mesh.vertex_normals()) os << "vn " << n.x << ' ' << n.y << ' ' << n.z << '\n';
    for (std::size_t f = 0; f < mesh.face_count(); ++f) {
        const auto fv = mesh.face(f);
        const std::size_t base = mesh.face_offsets()[f];
        for (std::size_t i = 1; i + 1 < fv.size(); ++i) {
            os << 'f';
            for (const std::size_t c : {std::size_t(0), i, i + 1}) {
                os << ' ' << (fv[c] + 1) << '/' << (uvs.corner_uv[base + c] + 1) << '/' << (fv[c] + 1);
            }
            os << '\n';
        }
    }
    return os.str();
}

// ---------------------------------------------------------------- STL

static Result<Mesh> read_stl_ascii(std::string_view text) {
    Mesh mesh;
    Scanner sc(text);
    std::vector<Index> loop;
    while (!sc.at_end()) {
        std::string_view l = sc.line();
        std::string_view tag = next_token(l);
        if (tag == "vertex") {
            Vec3 p;
            for (int i = 0; i < 3; ++i) {
                if (!parse_float(next_token(l), p[i])) return parse_error("STL: bad vertex");
            }
            loop.push_back(mesh.add_vertex(p));
        } else if (tag == "endloop") {
            if (loop.size() < 3) return parse_error("STL: facet with < 3 vertices");
            mesh.add_face(loop);
            loop.clear();
        }
    }
    return mesh;
}

static Result<Mesh> read_stl_binary(std::string_view bytes) {
    if (bytes.size() < 84) return parse_error("STL: file too short");
    auto count = read_le<std::uint32_t>(bytes.data() + 80);
    if (bytes.size() < 84 + std::size_t(count) * 50) return parse_error("STL: truncated triangle data");
    Mesh mesh;
    mesh.reserve(std::size_t(count) * 3, count, std::size_t(count) * 3);
    const char* p = bytes.data() + 84;
    for (std::uint32_t t = 0; t < count; ++t, p += 50) {
        Index base = static_cast<Index>(mesh.vertex_count());
        for (int v = 0; v < 3; ++v) {
            const char* q = p + 12 + v * 12;
            mesh.add_vertex({read_le<float>(q), read_le<float>(q + 4), read_le<float>(q + 8)});
        }
        mesh.add_face({base, base + 1, base + 2});
    }
    return mesh;
}

Result<Mesh> read_stl(std::string_view bytes) {
    // Binary files may also start with "solid", so trust the size field first.
    bool binary = false;
    if (bytes.size() >= 84) {
        auto count = read_le<std::uint32_t>(bytes.data() + 80);
        binary = (84 + std::size_t(count) * 50 == bytes.size());
    }
    if (!binary && lower(bytes.substr(0, 5)) != "solid") binary = true;
    auto result = binary ? read_stl_binary(bytes) : read_stl_ascii(bytes);
    if (result) weld_vertices(result.value());
    return result;
}

std::string write_stl_binary(const Mesh& mesh) {
    std::string out;
    const std::size_t tris = mesh.triangle_count();
    out.reserve(84 + tris * 50);
    std::string header = "Cruth binary STL";
    header.resize(80, ' ');
    out += header;
    append_le<std::uint32_t>(out, static_cast<std::uint32_t>(tris));
    auto idx = mesh.triangulated_indices();
    for (std::size_t t = 0; t < idx.size(); t += 3) {
        Vec3 a = mesh.positions[idx[t]], b = mesh.positions[idx[t + 1]], c = mesh.positions[idx[t + 2]];
        Vec3 n = normalize(cross(b - a, c - a));
        for (Vec3 v : {n, a, b, c}) {
            append_le(out, v.x);
            append_le(out, v.y);
            append_le(out, v.z);
        }
        append_le<std::uint16_t>(out, 0);
    }
    return out;
}

// ---------------------------------------------------------------- PLY

namespace {

enum class PlyType { Int8, UInt8, Int16, UInt16, Int32, UInt32, Float32, Float64, Invalid };

PlyType ply_type(std::string_view t) {
    if (t == "char" || t == "int8") return PlyType::Int8;
    if (t == "uchar" || t == "uint8") return PlyType::UInt8;
    if (t == "short" || t == "int16") return PlyType::Int16;
    if (t == "ushort" || t == "uint16") return PlyType::UInt16;
    if (t == "int" || t == "int32") return PlyType::Int32;
    if (t == "uint" || t == "uint32") return PlyType::UInt32;
    if (t == "float" || t == "float32") return PlyType::Float32;
    if (t == "double" || t == "float64") return PlyType::Float64;
    return PlyType::Invalid;
}

std::size_t ply_size(PlyType t) {
    switch (t) {
        case PlyType::Int8: case PlyType::UInt8: return 1;
        case PlyType::Int16: case PlyType::UInt16: return 2;
        case PlyType::Int32: case PlyType::UInt32: case PlyType::Float32: return 4;
        case PlyType::Float64: return 8;
        default: return 0;
    }
}

double ply_read_binary(const char* p, PlyType t) {
    switch (t) {
        case PlyType::Int8: return read_le<std::int8_t>(p);
        case PlyType::UInt8: return read_le<std::uint8_t>(p);
        case PlyType::Int16: return read_le<std::int16_t>(p);
        case PlyType::UInt16: return read_le<std::uint16_t>(p);
        case PlyType::Int32: return read_le<std::int32_t>(p);
        case PlyType::UInt32: return read_le<std::uint32_t>(p);
        case PlyType::Float32: return read_le<float>(p);
        case PlyType::Float64: return read_le<double>(p);
        default: return 0;
    }
}

struct PlyProperty {
    std::string name;
    PlyType type = PlyType::Invalid;
    bool is_list = false;
    PlyType count_type = PlyType::Invalid;
};

struct PlyElement {
    std::string name;
    std::size_t count = 0;
    std::vector<PlyProperty> properties;
};

// Reads values one at a time from either the ASCII body or the binary body.
class PlyReader {
public:
    PlyReader(std::string_view body, bool binary) : body_(body), binary_(binary) {}

    bool read(PlyType t, double& out) {
        if (binary_) {
            std::size_t n = ply_size(t);
            if (pos_ + n > body_.size()) return false;
            out = ply_read_binary(body_.data() + pos_, t);
            pos_ += n;
            return true;
        }
        while (pos_ < body_.size() && std::isspace(static_cast<unsigned char>(body_[pos_]))) ++pos_;
        std::size_t start = pos_;
        while (pos_ < body_.size() && !std::isspace(static_cast<unsigned char>(body_[pos_]))) ++pos_;
        float f = 0;
        if (!parse_float(body_.substr(start, pos_ - start), f)) return false;
        out = f;
        return true;
    }

private:
    std::string_view body_;
    bool binary_;
    std::size_t pos_ = 0;
};

}  // namespace

Result<Mesh> read_ply(std::string_view bytes) {
    Scanner sc(bytes);
    if (sc.line() != "ply") return parse_error("PLY: missing magic");

    bool binary = false;
    std::vector<PlyElement> elements;
    for (;;) {
        if (sc.at_end()) return parse_error("PLY: header has no end_header");
        std::string_view l = sc.line();
        std::string_view tag = next_token(l);
        if (tag == "format") {
            std::string_view fmt = next_token(l);
            if (fmt == "ascii") binary = false;
            else if (fmt == "binary_little_endian") binary = true;
            else return Error{ErrorCode::UnsupportedFormat, "PLY: unsupported format " + std::string(fmt)};
        } else if (tag == "element") {
            PlyElement e;
            e.name = std::string(next_token(l));
            long count = 0;
            if (!parse_long(next_token(l), count) || count < 0) return parse_error("PLY: bad element count");
            e.count = static_cast<std::size_t>(count);
            elements.push_back(std::move(e));
        } else if (tag == "property") {
            if (elements.empty()) return parse_error("PLY: property before element");
            PlyProperty prop;
            std::string_view t = next_token(l);
            if (t == "list") {
                prop.is_list = true;
                prop.count_type = ply_type(next_token(l));
                prop.type = ply_type(next_token(l));
            } else {
                prop.type = ply_type(t);
            }
            prop.name = std::string(next_token(l));
            if (prop.type == PlyType::Invalid || (prop.is_list && prop.count_type == PlyType::Invalid)) {
                return parse_error("PLY: unknown property type");
            }
            elements.back().properties.push_back(std::move(prop));
        } else if (tag == "end_header") {
            break;
        }
    }

    Mesh mesh;
    PlyReader in(bytes.substr(sc.position()), binary);
    std::vector<Index> corners;
    for (const auto& e : elements) {
        const bool is_vertex = e.name == "vertex", is_face = e.name == "face";
        int axis_of[3] = {-1, -1, -1};
        if (is_vertex) {
            for (std::size_t i = 0; i < e.properties.size(); ++i) {
                const auto& n = e.properties[i].name;
                if (n == "x") axis_of[0] = int(i);
                if (n == "y") axis_of[1] = int(i);
                if (n == "z") axis_of[2] = int(i);
            }
            if (axis_of[0] < 0 || axis_of[1] < 0 || axis_of[2] < 0) return parse_error("PLY: vertex lacks x/y/z");
            mesh.positions.reserve(e.count);
        }
        for (std::size_t item = 0; item < e.count; ++item) {
            Vec3 p;
            for (std::size_t pi = 0; pi < e.properties.size(); ++pi) {
                const auto& prop = e.properties[pi];
                double value = 0;
                if (prop.is_list) {
                    if (!in.read(prop.count_type, value) || value < 0) return parse_error("PLY: truncated list");
                    auto n = static_cast<std::size_t>(value);
                    const bool indices = is_face && (prop.name == "vertex_indices" || prop.name == "vertex_index");
                    if (indices) corners.clear();
                    for (std::size_t k = 0; k < n; ++k) {
                        if (!in.read(prop.type, value)) return parse_error("PLY: truncated list");
                        if (indices) corners.push_back(static_cast<Index>(value));
                    }
                    if (indices) {
                        if (corners.size() < 3) return parse_error("PLY: face with < 3 corners");
                        mesh.add_face(corners);
                    }
                } else {
                    if (!in.read(prop.type, value)) return parse_error("PLY: truncated element data");
                    if (is_vertex) {
                        for (int a = 0; a < 3; ++a) {
                            if (axis_of[a] == int(pi)) p[a] = static_cast<float>(value);
                        }
                    }
                }
            }
            if (is_vertex) mesh.add_vertex(p);
        }
    }
    if (!mesh.is_valid()) return parse_error("PLY: face index out of range");
    return mesh;
}

std::string write_ply_binary(const Mesh& mesh) {
    std::ostringstream header;
    header << "ply\nformat binary_little_endian 1.0\ncomment Cruth export\n"
           << "element vertex " << mesh.vertex_count() << "\n"
           << "property float x\nproperty float y\nproperty float z\n"
           << "element face " << mesh.face_count() << "\n"
           << "property list uchar int vertex_indices\nend_header\n";
    std::string out = header.str();
    out.reserve(out.size() + mesh.vertex_count() * 12 + mesh.face_count() + mesh.corner_count() * 4);
    for (const auto& p : mesh.positions) {
        append_le(out, p.x);
        append_le(out, p.y);
        append_le(out, p.z);
    }
    for (std::size_t f = 0; f < mesh.face_count(); ++f) {
        auto fv = mesh.face(f);
        // The "uchar" count caps polygons at 255 corners; save_mesh() rejects larger ones.
        append_le<std::uint8_t>(out, static_cast<std::uint8_t>(fv.size()));
        for (Index v : fv) append_le<std::int32_t>(out, static_cast<std::int32_t>(v));
    }
    return out;
}

// ---------------------------------------------------------------- files

Result<Mesh> load_mesh(const std::string& path, UVSet* uvs) {
    if (uvs) *uvs = UVSet{};
    MeshFormat fmt = format_from_path(path);
    if (fmt == MeshFormat::Unknown) return Error{ErrorCode::UnsupportedFormat, "unsupported file type: " + path};
    std::ifstream f(path, std::ios::binary);
    if (!f) return Error{ErrorCode::Io, "cannot open " + path};
    std::string bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    switch (fmt) {
        case MeshFormat::Obj: return read_obj(bytes, uvs);
        case MeshFormat::Stl: return read_stl(bytes);
        case MeshFormat::Ply: return read_ply(bytes);
        default: return Error{ErrorCode::UnsupportedFormat, path};
    }
}

Status save_mesh(const Mesh& mesh, const std::string& path, const UVSet* uvs) {
    std::string bytes;
    switch (format_from_path(path)) {
        case MeshFormat::Obj: bytes = write_obj(mesh, uvs); break;
        case MeshFormat::Stl: bytes = write_stl_binary(mesh); break;
        case MeshFormat::Ply:
            for (std::size_t f = 0; f < mesh.face_count(); ++f) {
                if (mesh.face_size(f) > 255) return Error{ErrorCode::InvalidArgument, "PLY export: polygon with > 255 corners"};
            }
            bytes = write_ply_binary(mesh);
            break;
        default: return Error{ErrorCode::UnsupportedFormat, "unsupported file type: " + path};
    }
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return Error{ErrorCode::Io, "cannot write " + path};
    f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!f) return Error{ErrorCode::Io, "write failed: " + path};
    return Status::ok();
}

// ---------------------------------------------------------------- weld

std::size_t weld_vertices(Mesh& mesh, float tolerance) {
    const std::size_t n = mesh.vertex_count();
    std::vector<Index> remap(n);
    std::vector<Vec3> kept;
    kept.reserve(n);

    if (tolerance <= 0) {
        struct Key {
            std::uint32_t x, y, z;
            bool operator==(const Key&) const = default;
        };
        struct KeyHash {
            std::size_t operator()(const Key& k) const {
                return (std::size_t(k.x) * 73856093u) ^ (std::size_t(k.y) * 19349663u) ^ (std::size_t(k.z) * 83492791u);
            }
        };
        std::unordered_map<Key, Index, KeyHash> seen;
        seen.reserve(n);
        for (std::size_t i = 0; i < n; ++i) {
            Vec3 p = mesh.positions[i];
            for (int a = 0; a < 3; ++a) {
                if (p[a] == 0.0f) p[a] = 0.0f;  // fold -0 into +0
            }
            Key k;
            std::memcpy(&k.x, &p.x, 4);
            std::memcpy(&k.y, &p.y, 4);
            std::memcpy(&k.z, &p.z, 4);
            auto [it, inserted] = seen.emplace(k, static_cast<Index>(kept.size()));
            if (inserted) kept.push_back(mesh.positions[i]);
            remap[i] = it->second;
        }
    } else {
        // Grid hash with cell = tolerance; check the 27 neighbouring cells.
        auto cell = [&](float v) { return static_cast<std::int64_t>(std::floor(v / tolerance)); };
        auto key = [](std::int64_t x, std::int64_t y, std::int64_t z) {
            return (std::uint64_t(x) * 73856093u) ^ (std::uint64_t(y) * 19349663u) ^ (std::uint64_t(z) * 83492791u);
        };
        std::unordered_multimap<std::uint64_t, Index> grid;
        grid.reserve(n);
        const float tol2 = tolerance * tolerance;
        for (std::size_t i = 0; i < n; ++i) {
            Vec3 p = mesh.positions[i];
            std::int64_t cx = cell(p.x), cy = cell(p.y), cz = cell(p.z);
            Index found = kInvalidIndex;
            for (int dx = -1; dx <= 1 && found == kInvalidIndex; ++dx)
                for (int dy = -1; dy <= 1 && found == kInvalidIndex; ++dy)
                    for (int dz = -1; dz <= 1 && found == kInvalidIndex; ++dz) {
                        auto range = grid.equal_range(key(cx + dx, cy + dy, cz + dz));
                        for (auto it = range.first; it != range.second; ++it) {
                            if (length_squared(kept[it->second] - p) <= tol2) {
                                found = it->second;
                                break;
                            }
                        }
                    }
            if (found == kInvalidIndex) {
                found = static_cast<Index>(kept.size());
                kept.push_back(p);
                grid.emplace(key(cx, cy, cz), found);
            }
            remap[i] = found;
        }
    }

    const std::size_t merged = n - kept.size();
    if (merged == 0) return 0;

    Mesh out;
    out.reserve(kept.size(), mesh.face_count(), mesh.corner_count());
    out.positions = std::move(kept);
    std::vector<Index> corners;
    for (std::size_t f = 0; f < mesh.face_count(); ++f) {
        corners.clear();
        for (Index v : mesh.face(f)) corners.push_back(remap[v]);
        out.add_face(corners);
    }
    mesh = std::move(out);
    return merged;
}

}  // namespace cruth
