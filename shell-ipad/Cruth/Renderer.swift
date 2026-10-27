import MetalKit
import simd

/// Draws the session's mesh in the chosen shading mode, with an optional
/// wireframe of its polygon edges on top. Vertex data is re-copied from the
/// core only when its geometry revision changes, into one of three buffers so
/// the CPU never writes a buffer the GPU is still reading.
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
    private let shadingPipelines: [ShadingMode: MTLRenderPipelineState]
    private let wirePipeline: MTLRenderPipelineState
    private let depthState: MTLDepthStencilState
    /// X-ray surfaces and the wire over them: no occlusion, no depth writes.
    private let unoccludedState: MTLDepthStencilState
    /// The wire over a solid mode: tested against the surface, never written.
    private let wireDepthState: MTLDepthStencilState
    private let inFlight = DispatchSemaphore(value: Renderer.framesInFlight)

    private var vertexBuffers: [MTLBuffer?] = Array(repeating: nil, count: Renderer.framesInFlight)
    private var vertexRevisions: [UInt64] = Array(repeating: .max, count: Renderer.framesInFlight)
    private var indexBuffer: MTLBuffer?
    private var indexCount = 0
    /// Index buffers follow (document generation, topology revision): a new
    /// document starts its revisions again from zero.
    private struct TopologyKey: Equatable {
        var generation: Int
        var revision: UInt64
    }
    private var topologyKey: TopologyKey?
    private var edgeBuffer: MTLBuffer?
    private var edgeIndexCount = 0
    private var edgeKey: TopologyKey?
    private var framedGeneration = -1
    private var frameIndex = 0

    init?(view: MTKView, session: SculptSession) {
        guard let device = view.device,
              let queue = device.makeCommandQueue(),
              let library = device.makeDefaultLibrary() else { return nil }

        func makePipeline(_ label: String, vertex: String, fragment: String,
                          blended: Bool) -> MTLRenderPipelineState? {
            let descriptor = MTLRenderPipelineDescriptor()
            descriptor.label = label
            descriptor.vertexFunction = library.makeFunction(name: vertex)
            descriptor.fragmentFunction = library.makeFunction(name: fragment)
            descriptor.colorAttachments[0].pixelFormat = view.colorPixelFormat
            descriptor.depthAttachmentPixelFormat = view.depthStencilPixelFormat
            if blended {
                let color = descriptor.colorAttachments[0]!
                color.isBlendingEnabled = true
                color.sourceRGBBlendFactor = .sourceAlpha
                color.destinationRGBBlendFactor = .oneMinusSourceAlpha
                color.sourceAlphaBlendFactor = .one
                color.destinationAlphaBlendFactor = .oneMinusSourceAlpha
            }
            return try? device.makeRenderPipelineState(descriptor: descriptor)
        }
        func makeDepthState(_ compare: MTLCompareFunction, write: Bool) -> MTLDepthStencilState? {
            let depth = MTLDepthStencilDescriptor()
            depth.depthCompareFunction = compare
            depth.isDepthWriteEnabled = write
            return device.makeDepthStencilState(descriptor: depth)
        }

        var shadingPipelines: [ShadingMode: MTLRenderPipelineState] = [:]
        for mode in ShadingMode.allCases {
            guard let pipeline = makePipeline(mode.title, vertex: "sculpt_vertex", fragment: mode.fragmentFunction,
                                              blended: mode == .xray) else { return nil }
            shadingPipelines[mode] = pipeline
        }
        guard let wirePipeline = makePipeline("Wireframe", vertex: "wire_vertex", fragment: "wire_fragment",
                                              blended: true),
              let depthState = makeDepthState(.less, write: true),
              let unoccludedState = makeDepthState(.always, write: false),
              let wireDepthState = makeDepthState(.lessEqual, write: false) else { return nil }

        self.device = device
        self.queue = queue
        self.shadingPipelines = shadingPipelines
        self.wirePipeline = wirePipeline
        self.depthState = depthState
        self.unoccludedState = unoccludedState
        self.wireDepthState = wireDepthState
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
        let key = TopologyKey(generation: session.documentGeneration,
                              revision: cruth_document_topology_revision(document))
        let shading = session.shading
        let wireframe = session.wireframe
        syncTopology(document, key: key)
        if wireframe { syncEdges(document, key: key) }
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
        let drawWire = wireframe && edgeIndexCount > 0 && edgeBuffer != nil
        encoder.setRenderPipelineState(shadingPipelines[shading]!)
        encoder.setDepthStencilState(shading == .xray ? unoccludedState : depthState)
        // Pushes the surface back a little so the wire, which lies exactly on
        // it, wins the depth test instead of z-fighting.
        if drawWire && shading != .xray { encoder.setDepthBias(2, slopeScale: 2, clamp: 0) }
        encoder.setFrontFacing(.counterClockwise)
        encoder.setCullMode(.none)
        encoder.setVertexBuffer(vertexBuffer, offset: 0, index: 0)
        encoder.setVertexBytes(&uniforms, length: MemoryLayout<Uniforms>.stride, index: 1)
        encoder.drawIndexedPrimitives(type: .triangle, indexCount: indexCount, indexType: .uint32,
                                      indexBuffer: indexBuffer, indexBufferOffset: 0)
        if drawWire, let edgeBuffer {
            // Dark lines read on clay and on the mask tint; in X-ray every edge
            // shows, so they are light and faint enough to stack.
            var color: SIMD4<Float> = shading == .xray ? SIMD4(0.9, 0.95, 1.0, 0.18) : SIMD4(0.04, 0.05, 0.07, 0.55)
            encoder.setDepthBias(0, slopeScale: 0, clamp: 0)
            encoder.setRenderPipelineState(wirePipeline)
            encoder.setDepthStencilState(shading == .xray ? unoccludedState : wireDepthState)
            encoder.setFragmentBytes(&color, length: MemoryLayout<SIMD4<Float>>.stride, index: 0)
            encoder.drawIndexedPrimitives(type: .line, indexCount: edgeIndexCount, indexType: .uint32,
                                          indexBuffer: edgeBuffer, indexBufferOffset: 0)
        }
        encoder.endEncoding()
        if session.restoreAwaitingFrame {
            // A restore counts once the GPU has drawn the restored mesh (see AutosaveStore.restoreMarker).
            let session = self.session
            commands.addCompletedHandler { _ in DispatchQueue.main.async { session.restoredFrameDrawn() } }
        }
        commands.present(drawable)
        commands.commit()
    }

    private func syncTopology(_ document: OpaquePointer, key: TopologyKey) {
        guard key != topologyKey else { return }
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
        topologyKey = key
        // Vertex count may have changed too: force every slot to re-copy.
        vertexRevisions = Array(repeating: .max, count: Renderer.framesInFlight)
    }

    /// Polygon edges for the wireframe, fetched only while it is shown. A failed
    /// upload leaves the key unset, so the next frame tries again.
    private func syncEdges(_ document: OpaquePointer, key: TopologyKey) {
        guard key != edgeKey else { return }
        edgeIndexCount = 0
        let count = cruth_document_edge_count(document) * 2
        guard count > 0,
              let buffer = device.makeBuffer(length: count * MemoryLayout<UInt32>.stride, options: .storageModeShared)
        else { return }
        let pointer = buffer.contents().bindMemory(to: UInt32.self, capacity: count)
        guard cruth_document_copy_edges(document, pointer, count) == CRUTH_OK else { return }
        edgeBuffer = buffer
        edgeIndexCount = count
        edgeKey = key
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

private extension ShadingMode {
    /// One pipeline per mode, so no fragment branches on the mode.
    var fragmentFunction: String {
        switch self {
        case .lit: return "sculpt_fragment"
        case .flat: return "flat_fragment"
        case .xray: return "xray_fragment"
        case .normals: return "normals_fragment"
        }
    }
}
