import CoreGraphics
import simd

/// Turntable camera around a target point (PRD N-01). Right-handed, Y up,
/// Metal clip space (depth 0...1).
struct OrbitCamera: Equatable {
    var target = SIMD3<Float>(0, 0, 0)
    var distance: Float = 3.2
    var yaw: Float = 0
    var pitch: Float = 0.15
    var fovY: Float = 40 * .pi / 180
    var aspect: Float = 1

    var eye: SIMD3<Float> {
        let cp = cos(pitch)
        return target + distance * SIMD3(cp * sin(yaw), sin(pitch), cp * cos(yaw))
    }

    var forward: SIMD3<Float> { simd_normalize(target - eye) }
    var right: SIMD3<Float> { simd_normalize(simd_cross(forward, SIMD3(0, 1, 0))) }
    var up: SIMD3<Float> { simd_cross(right, forward) }

    var viewMatrix: float4x4 { .lookAt(eye: eye, target: target, up: SIMD3(0, 1, 0)) }
    var projectionMatrix: float4x4 {
        .perspective(fovY: fovY, aspect: aspect, near: max(0.001, distance * 0.01), far: distance * 100)
    }
    var viewProjection: float4x4 { projectionMatrix * viewMatrix }

    mutating func orbit(dx: Float, dy: Float) {
        yaw -= dx * 0.008
        pitch = min(max(pitch + dy * 0.008, -1.55), 1.55)
    }

    /// Moves the target so the surface under the fingers follows them.
    mutating func pan(dx: Float, dy: Float, viewHeight: Float) {
        let worldPerPoint = 2 * distance * tan(fovY / 2) / max(viewHeight, 1)
        target += (-right * dx + up * dy) * worldPerPoint
    }

    mutating func zoom(by scale: Float) {
        distance = min(max(distance / max(scale, 0.01), 0.02), 200)
    }

    mutating func frame(center: SIMD3<Float>, radius: Float) {
        target = center
        distance = max(radius, 0.01) / sin(fovY / 2) * 1.15
    }

    /// World-space ray through a point in view coordinates.
    func ray(through point: CGPoint, viewSize: CGSize) -> (origin: SIMD3<Float>, direction: SIMD3<Float>) {
        let x = Float(2 * point.x / max(viewSize.width, 1) - 1)
        let y = Float(1 - 2 * point.y / max(viewSize.height, 1))
        let inverse = viewProjection.inverse
        let near = inverse * SIMD4<Float>(x, y, 0, 1)
        let far = inverse * SIMD4<Float>(x, y, 1, 1)
        let n = SIMD3(near.x, near.y, near.z) / near.w
        let f = SIMD3(far.x, far.y, far.z) / far.w
        return (n, simd_normalize(f - n))
    }

    /// View coordinates of a world point, or nil when it is behind the camera.
    func project(_ p: SIMD3<Float>, viewSize: CGSize) -> CGPoint? {
        let clip = viewProjection * SIMD4<Float>(p.x, p.y, p.z, 1)
        guard clip.w > 0 else { return nil }
        let ndc = SIMD2(clip.x, clip.y) / clip.w
        return CGPoint(x: CGFloat(ndc.x + 1) / 2 * viewSize.width,
                       y: CGFloat(1 - ndc.y) / 2 * viewSize.height)
    }
}

extension float4x4 {
    static func perspective(fovY: Float, aspect: Float, near: Float, far: Float) -> float4x4 {
        let ys = 1 / tan(fovY * 0.5)
        let xs = ys / max(aspect, 0.0001)
        let zs = far / (near - far)
        return float4x4(columns: (
            SIMD4(xs, 0, 0, 0),
            SIMD4(0, ys, 0, 0),
            SIMD4(0, 0, zs, -1),
            SIMD4(0, 0, zs * near, 0)
        ))
    }

    static func lookAt(eye: SIMD3<Float>, target: SIMD3<Float>, up: SIMD3<Float>) -> float4x4 {
        let z = simd_normalize(eye - target)
        let x = simd_normalize(simd_cross(up, z))
        let y = simd_cross(z, x)
        return float4x4(columns: (
            SIMD4(x.x, y.x, z.x, 0),
            SIMD4(x.y, y.y, z.y, 0),
            SIMD4(x.z, y.z, z.z, 0),
            SIMD4(-simd_dot(x, eye), -simd_dot(y, eye), -simd_dot(z, eye), 1)
        ))
    }
}
