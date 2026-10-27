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
    float3 viewPosition;
    float3 viewNormal;
    float mask;
};

vertex VertexOut sculpt_vertex(uint vid [[vertex_id]],
                               const device float* vertices [[buffer(0)]],
                               constant Uniforms& u [[buffer(1)]]) {
    const device float* v = vertices + vid * kFloatsPerVertex;
    VertexOut out;
    out.position = u.viewProjection * float4(v[0], v[1], v[2], 1.0);
    out.viewPosition = (u.view * float4(v[0], v[1], v[2], 1.0)).xyz;
    out.viewNormal = (u.view * float4(v[3], v[4], v[5], 0.0)).xyz;
    out.mask = v[6];
    return out;
}

// Masked vertices darken toward a cool blue in every shading mode.
static float3 apply_mask(float3 color, float mask) {
    return mix(color, color * float3(0.25, 0.28, 0.4), saturate(mask) * 0.85);
}

// Studio clay look: key + fill + rim lights in view space, so the lighting
// follows the camera like a matcap. PBR and IBL arrive with PRD P-04.
static float3 clay(float3 n) {
    const float3 base = float3(0.74, 0.60, 0.52);
    const float3 key = normalize(float3(0.45, 0.65, 0.6));
    const float3 fill = normalize(float3(-0.7, -0.1, 0.5));

    float diffuse = max(dot(n, key), 0.0);
    float fillLight = max(dot(n, fill), 0.0) * 0.25;
    float rim = pow(1.0 - saturate(n.z), 3.0) * 0.3;
    float3 h = normalize(key + float3(0, 0, 1));
    float specular = pow(max(dot(n, h), 0.0), 40.0) * 0.18;
    return base * (0.16 + 0.78 * diffuse + fillLight) + rim + specular;
}

static float3 smooth_normal(VertexOut in, bool frontFacing) {
    float3 n = normalize(in.viewNormal);
    return frontFacing ? n : -n;
}

// Lit: smooth vertex normals.
fragment float4 sculpt_fragment(VertexOut in [[stage_in]], bool frontFacing [[front_facing]]) {
    return float4(apply_mask(clay(smooth_normal(in, frontFacing)), in.mask), 1.0);
}

// Flat: one normal per triangle from screen-space derivatives of the view
// position, turned toward the camera (both sides are drawn).
fragment float4 flat_fragment(VertexOut in [[stage_in]]) {
    float3 n = normalize(cross(dfdx(in.viewPosition), dfdy(in.viewPosition)));
    if (dot(n, in.viewPosition) > 0.0) n = -n;
    return float4(apply_mask(clay(n), in.mask), 1.0);
}

// Normals: the view-space normal as colour. The drawable is sRGB, so the value
// is linearised first to land on screen as the familiar normal-map colours.
fragment float4 normals_fragment(VertexOut in [[stage_in]], bool frontFacing [[front_facing]]) {
    float3 c = smooth_normal(in, frontFacing) * 0.5 + 0.5;
    float3 linear = select(pow((c + 0.055) / 1.055, 2.4), c / 12.92, c <= 0.04045);
    return float4(apply_mask(linear, in.mask), 1.0);
}

// X-ray: blended without depth writes. Faces seen head-on are nearly clear and
// grazing ones (silhouettes, folds) nearly opaque, so on a dense mesh the
// layers build up outlines instead of washing out to white.
fragment float4 xray_fragment(VertexOut in [[stage_in]], bool frontFacing [[front_facing]]) {
    float3 n = smooth_normal(in, frontFacing);
    float fresnel = pow(1.0 - saturate(abs(n.z)), 2.0);
    float3 color = mix(float3(0.42, 0.52, 0.62), float3(0.85, 0.9, 1.0), fresnel);
    float mask = saturate(in.mask);
    color = mix(color, float3(0.25, 0.32, 0.85), mask * 0.85);
    float alpha = mix(0.04, 0.5, fresnel) + mask * 0.15;
    return float4(color, alpha);
}

struct WireOut {
    float4 position [[position]];
};

vertex WireOut wire_vertex(uint vid [[vertex_id]],
                           const device float* vertices [[buffer(0)]],
                           constant Uniforms& u [[buffer(1)]]) {
    const device float* v = vertices + vid * kFloatsPerVertex;
    return {u.viewProjection * float4(v[0], v[1], v[2], 1.0)};
}

fragment float4 wire_fragment(WireOut in [[stage_in]], constant float4& color [[buffer(0)]]) {
    return color;
}
