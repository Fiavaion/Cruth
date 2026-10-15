// SPDX-License-Identifier: MIT
#include "cruth/tangents.hpp"

extern "C" {
#include "mikktspace.h"
}

namespace cruth {

namespace {

struct Tri {
    Index vertex[3];
    Vec2 uv[3];
};

// Everything the MikkTSpace callbacks read, plus the output they fill.
struct MikkMesh {
    const Mesh* mesh;
    std::vector<Vec3> normals;
    std::vector<Tri> tris;
    CornerTangents* out;
};

const MikkMesh& user(const SMikkTSpaceContext* ctx) { return *static_cast<const MikkMesh*>(ctx->m_pUserData); }

}  // namespace

Result<CornerTangents> compute_tangents(const Mesh& mesh, const UVSet& uv) {
    if (!uv.matches(mesh)) return Error{ErrorCode::InvalidArgument, "compute_tangents: no matching UV set"};

    MikkMesh m;
    m.mesh = &mesh;
    m.normals = mesh.vertex_normals();
    for (std::size_t f = 0; f < mesh.face_count(); ++f) {
        const auto fv = mesh.face(f);
        for (std::size_t i = 1; i + 1 < fv.size(); ++i) {
            const std::size_t c[3] = {0, i, i + 1};
            Tri t;
            for (int k = 0; k < 3; ++k) {
                t.vertex[k] = fv[c[k]];
                t.uv[k] = uv.corner(mesh, f, c[k]);
            }
            m.tris.push_back(t);
        }
    }
    CornerTangents result;
    result.tangent.assign(m.tris.size() * 3, Vec3{1, 0, 0});
    result.sign.assign(m.tris.size() * 3, 1.0f);
    m.out = &result;

    SMikkTSpaceInterface iface{};
    iface.m_getNumFaces = [](const SMikkTSpaceContext* c) { return int(user(c).tris.size()); };
    iface.m_getNumVerticesOfFace = [](const SMikkTSpaceContext*, int) { return 3; };
    iface.m_getPosition = [](const SMikkTSpaceContext* c, float out[], int face, int vert) {
        const MikkMesh& u = user(c);
        const Vec3& p = u.mesh->positions[u.tris[std::size_t(face)].vertex[vert]];
        out[0] = p.x;
        out[1] = p.y;
        out[2] = p.z;
    };
    iface.m_getNormal = [](const SMikkTSpaceContext* c, float out[], int face, int vert) {
        const MikkMesh& u = user(c);
        const Vec3& n = u.normals[u.tris[std::size_t(face)].vertex[vert]];
        out[0] = n.x;
        out[1] = n.y;
        out[2] = n.z;
    };
    iface.m_getTexCoord = [](const SMikkTSpaceContext* c, float out[], int face, int vert) {
        const Vec2& t = user(c).tris[std::size_t(face)].uv[vert];
        out[0] = t.x;
        out[1] = t.y;
    };
    iface.m_setTSpaceBasic = [](const SMikkTSpaceContext* c, const float tangent[], float sign, int face, int vert) {
        const MikkMesh& u = user(c);
        const std::size_t i = std::size_t(face) * 3 + std::size_t(vert);
        u.out->tangent[i] = {tangent[0], tangent[1], tangent[2]};
        u.out->sign[i] = sign;
    };

    SMikkTSpaceContext ctx{&iface, &m};
    if (!genTangSpaceDefault(&ctx)) return Error{ErrorCode::Internal, "compute_tangents: MikkTSpace failed"};
    return result;
}

}  // namespace cruth
