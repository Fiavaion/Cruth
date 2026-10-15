#include <metal_stdlib>
using namespace metal;

// Vertex layout written by cruth_document_copy_vertices: position xyz, normal xyz, mask.
constant uint kFloatsPerVertex = 7;

struct Uniforms {
    float4x4 viewProjection;
    float4x4 view;
};

struct VertexOut {
    float4 position [[position]];
    float3 viewNormal;
    float mask;
};

vertex VertexOut sculpt_vertex(uint vid [[vertex_id]],
                               const device float* vertices [[buffer(0)]],
                               constant Uniforms& u [[buffer(1)]]) {
    const device float* v = vertices + vid * kFloatsPerVertex;
    VertexOut out;
    out.position = u.viewProjection * float4(v[0], v[1], v[2], 1.0);
    out.viewNormal = (u.view * float4(v[3], v[4], v[5], 0.0)).xyz;
    out.mask = v[6];
    return out;
}

// Studio clay look: key + fill + rim lights in view space, so the lighting
// follows the camera like a matcap. PBR and IBL arrive with PRD P-04.
fragment float4 sculpt_fragment(VertexOut in [[stage_in]], bool frontFacing [[front_facing]]) {
    float3 n = normalize(in.viewNormal);
    if (!frontFacing) n = -n;

    const float3 base = float3(0.74, 0.60, 0.52);
    const float3 key = normalize(float3(0.45, 0.65, 0.6));
    const float3 fill = normalize(float3(-0.7, -0.1, 0.5));

    float diffuse = max(dot(n, key), 0.0);
    float fillLight = max(dot(n, fill), 0.0) * 0.25;
    float rim = pow(1.0 - saturate(n.z), 3.0) * 0.3;
    float3 h = normalize(key + float3(0, 0, 1));
    float specular = pow(max(dot(n, h), 0.0), 40.0) * 0.18;

    float3 color = base * (0.16 + 0.78 * diffuse + fillLight) + rim + specular;
    color = mix(color, color * float3(0.25, 0.28, 0.4), saturate(in.mask) * 0.85);
    return float4(color, 1.0);
}
