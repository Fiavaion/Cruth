import MetalKit
import simd

/// Draws the session's mesh. Vertex data is re-copied from the core only when
/// its geometry revision changes, into one of three buffers so the CPU never
/// writes a buffer the GPU is still reading.
final class Renderer: NSObject, MTKViewDelegate {
    private struct Uniforms {
        var viewProjection: float4x4
        var view: float4x4
    }

    private static let floatsPerVertex = 7
    private static let framesInFlight = 3

    let session: SculptSession
    var camera = OrbitCamera()

    private let device: MTLDevice
    private let queue: MTLCommandQueue
    private let pipeline: MTLRenderPipelineState
    private let depthState: MTLDepthStencilState
    private let inFlight = DispatchSemaphore(value: Renderer.framesInFlight)

    private var vertexBuffers: [MTLBuffer?] = Array(repeating: nil, count: Renderer.framesInFlight)
    private var vertexRevisions: [UInt64] = Array(repeating: .max, count: Renderer.framesInFlight)
    private var indexBuffer: MTLBuffer?
    private var indexCount = 0
    private var topologyRevision: UInt64 = .max
    private var framedGeneration = -1
    private var frameIndex = 0

    init?(view: MTKView, session: SculptSession) {
        guard let device = view.device,
              let queue = device.makeCommandQueue(),
              let library = device.makeDefaultLibrary() else { return nil }

        let descriptor = MTLRenderPipelineDescriptor()
        descriptor.label = "Sculpt"
        descriptor.vertexFunction = library.makeFunction(name: "sculpt_vertex")
        descriptor.fragmentFunction = library.makeFunction(name: "sculpt_fragment")
        descriptor.colorAttachments[0].pixelFormat = view.colorPixelFormat
        descriptor.depthAttachmentPixelFormat = view.depthStencilPixelFormat
        guard let pipeline = try? device.makeRenderPipelineState(descriptor: descriptor) else { return nil }

        let depth = MTLDepthStencilDescriptor()
        depth.depthCompareFunction = .less
        depth.isDepthWriteEnabled = true
        guard let depthState = device.makeDepthStencilState(descriptor: depth) else { return nil }

        self.device = device
        self.queue = queue
        self.pipeline = pipeline
        self.depthState = depthState
        self.session = session
        super.init()
        let size = view.drawableSize
        camera.aspect = Float(size.width / max(size.height, 1))
    }

    func mtkView(_ view: MTKView, drawableSizeWillChange size: CGSize) {
        camera.aspect = Float(size.width / max(size.height, 1))
    }

    func draw(in view: MTKView) {
        // A background command may be rewriting the mesh; keep the last frame.
        guard let document = session.document, !session.isBusy else { return }
        if framedGeneration != session.documentGeneration {
            framedGeneration = session.documentGeneration
            let b = session.bounds()
            camera.frame(center: b.center, radius: b.radius)
            view.setNeedsLayout()  // the overlay was laid out for the old camera
        }

        inFlight.wait()
        let slot = frameIndex % Renderer.framesInFlight
        frameIndex += 1
        syncTopology(document)
        syncVertices(document, slot: slot)

        guard indexCount > 0,
              let vertexBuffer = vertexBuffers[slot],
              let indexBuffer,
              let pass = view.currentRenderPassDescriptor,
              let drawable = view.currentDrawable,
              let commands = queue.makeCommandBuffer(),
              let encoder = commands.makeRenderCommandEncoder(descriptor: pass) else {
            inFlight.signal()
            return
        }
        let semaphore = inFlight
        commands.addCompletedHandler { _ in semaphore.signal() }

        var uniforms = Uniforms(viewProjection: camera.viewProjection, view: camera.viewMatrix)
        encoder.setRenderPipelineState(pipeline)
        encoder.setDepthStencilState(depthState)
        encoder.setFrontFacing(.counterClockwise)
        encoder.setCullMode(.none)
        encoder.setVertexBuffer(vertexBuffer, offset: 0, index: 0)
        encoder.setVertexBytes(&uniforms, length: MemoryLayout<Uniforms>.stride, index: 1)
        encoder.drawIndexedPrimitives(type: .triangle, indexCount: indexCount, indexType: .uint32,
                                      indexBuffer: indexBuffer, indexBufferOffset: 0)
        encoder.endEncoding()
        commands.present(drawable)
        commands.commit()
    }

    private func syncTopology(_ document: OpaquePointer) {
        let revision = cruth_document_topology_revision(document)
        guard revision != topologyRevision else { return }
        let count = cruth_document_triangle_count(document) * 3
        guard count > 0,
              let buffer = device.makeBuffer(length: count * MemoryLayout<UInt32>.stride, options: .storageModeShared)
        else {
            indexCount = 0
            return
        }
        let pointer = buffer.contents().bindMemory(to: UInt32.self, capacity: count)
        guard cruth_document_copy_triangles(document, pointer, count) == CRUTH_OK else {
            indexCount = 0
            return
        }
        indexBuffer = buffer
        indexCount = count
        topologyRevision = revision
        // Vertex count may have changed too: force every slot to re-copy.
        vertexRevisions = Array(repeating: .max, count: Renderer.framesInFlight)
    }

    private func syncVertices(_ document: OpaquePointer, slot: Int) {
        let revision = cruth_document_geometry_revision(document)
        guard revision != vertexRevisions[slot] else { return }
        let floats = cruth_document_vertex_count(document) * Renderer.floatsPerVertex
        let bytes = floats * MemoryLayout<Float>.stride
        if (vertexBuffers[slot]?.length ?? 0) < bytes {
            vertexBuffers[slot] = device.makeBuffer(length: max(bytes, 16), options: .storageModeShared)
        }
        guard let buffer = vertexBuffers[slot] else { return }
        let pointer = buffer.contents().bindMemory(to: Float.self, capacity: floats)
        if cruth_document_copy_vertices(document, pointer, floats) == CRUTH_OK {
            vertexRevisions[slot] = revision
        }
    }
}
