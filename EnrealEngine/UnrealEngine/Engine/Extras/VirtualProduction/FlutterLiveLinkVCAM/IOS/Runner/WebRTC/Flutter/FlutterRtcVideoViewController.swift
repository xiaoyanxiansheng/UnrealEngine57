// Copyright Epic Games, Inc. All Rights Reserved.

import Flutter
import Foundation
import MetalKit
import simd
import WebRTC

protocol FlutterRtcVideoViewControllerListener: AnyObject {
  /**
   Called when the active video track changes.
   
   - Parameter track: The new video track.
   */
  func videoViewController(didChangeVideoTrack track: RTCVideoTrack?)
}

/// Vertex data used by the render pipeline.
struct Vertex {
  /// The position of the vertex in the scene.
  var position: SIMD2<Float>
  
  /// The texture coordinate for this vertex.
  var texCoord: SIMD2<Float>
}

/// Controller for a WebRTC video view which renders a video stream to a shared texture shared with Flutter and
/// communicates related messages.
class FlutterRtcVideoViewController: IdObject {
  /// The ID of the output texture bound to the Flutter engine.
  public private(set) var textureId: Int64 = -1
  
  /// The Metal device used to render WebRTC frame data.
  private let device: MTLDevice
  
  /// Cache containing Metal textures.
  private var textureCache: CVMetalTextureCache?
  
  /// Metal texture-backed buffer for the renderTexture.
  private var renderTextureBuffer: CVMetalTexture?
  
  /// Handle of the Metal texture registered for display in Flutter.
  private var renderTexture: MTLTexture?
  
  /// Texture containing the Y (luma) channel of the WebRTC video stream.
  private var yTexture: MTLTexture?
  
  /// Texture containing the U (chroma) channel of the WebRTC video stream.
  private var uTexture: MTLTexture?
  
  /// Texture containing the V (chroma) channel of the WebRTC video stream.
  private var vTexture: MTLTexture?
  
  /// Descriptor of yTexture.
  private var yDescriptor: MTLTextureDescriptor?
  
  /// Descriptor of uTexture/vTexture.
  private var uvDescriptor: MTLTextureDescriptor?
  
  /// Pipeline state containing our configuration for Metal rendering.
  var pipelineState: MTLRenderPipelineState
  
  /// Queue of commands to run on the GPU.
  var commandQueue: MTLCommandQueue
  
  /// Descriptor for the rendering pass that converts I420 to BGRA data.
  var convertPassDescriptor = MTLRenderPassDescriptor()
  
  /// The API used to send messages to Flutter.
  private let api: FlutterRtcVideoViewControllerApi
  
  /// Vertices for the quad used to render the converted video stream.
  private let vertices = [
    Vertex(position: [-1.0, -1.0], texCoord: [0.0, 1.0]),
    Vertex(position: [ 1.0, -1.0], texCoord: [1.0, 1.0]),
    Vertex(position: [-1.0,  1.0], texCoord: [0.0, 0.0]),
    Vertex(position: [ 1.0,  1.0], texCoord: [1.0, 0.0]),
  ]
  
  /// The current video track to be displayed.
  private var track: RTCVideoTrack?
  
  /// Whether this has reported its first frame since being created/cleared.
  private var bHasReportedFirstFrame: Bool = false
  
  /// The Core Video pixel buffer backing the Flutter texture for the video stream.
  private var pixelBuffer: Unmanaged<CVPixelBuffer>?
  
  /// The width of the last received frame.
  private var lastFrameWidth: Int?
  
  /// The height of the last received frame.
  private var lastFrameHeight: Int?
  
  init(api: FlutterRtcVideoViewControllerApi) {
    self.api = api
    
    guard let device = MTLCreateSystemDefaultDevice()
    else {
        fatalError("Failed to create default Metal device for WebRTC stream")
    }
    self.device = device
    
    guard CVMetalTextureCacheCreate(
      kCFAllocatorDefault,
      nil,
      device,
      nil,
      &textureCache
    ) == kCVReturnSuccess
    else {
      fatalError("Failed to create texture cache for WebRTC stream")
    }
    
    let shaderLib = device.makeDefaultLibrary()
    
    let pipelineDescriptor = MTLRenderPipelineDescriptor()
    pipelineDescriptor.label = "FlutterRtcPipeline"
    pipelineDescriptor.colorAttachments[0].pixelFormat = .bgra8Unorm
    pipelineDescriptor.vertexFunction = shaderLib?.makeFunction(name: "i420ToBgraVertex")
    pipelineDescriptor.fragmentFunction = shaderLib?.makeFunction(name: "i420ToBgraFragment")
    
    convertPassDescriptor.colorAttachments[0].loadAction = .clear
    convertPassDescriptor.colorAttachments[0].clearColor = MTLClearColor(red: 0, green: 0, blue: 0, alpha: 0)
    
    guard let pipelineState = try? device.makeRenderPipelineState(descriptor: pipelineDescriptor)
    else {
      fatalError("Failed to create render pipeline for WebRTC stream")
    }
    self.pipelineState = pipelineState
    
    guard let commandQueue = device.makeCommandQueue()
    else {
      fatalError("Failed to create command queue for WebRTC stream")
    }
    self.commandQueue = commandQueue
    
    super.init()
    
    textureId = api.textureRegistry.register(self)
  }
  
  override func dispose() {
    api.textureRegistry.unregisterTexture(textureId)
    track?.remove(self)
    pixelBuffer = nil
  }
  
  /**
   Set the Flutter track to be displayed.
   
   - Parameter newTrack: The new track to display.
   */
  func setTrack(newTrack: FlutterRtcMediaStreamTrack?) {
    var newVideoTrack: RTCVideoTrack?
    
    if (newTrack?.inner != nil) {
      newVideoTrack = newTrack!.inner as? RTCVideoTrack
      
      if (newVideoTrack == nil) {
        fatalError("Track \(newTrack!.id) is not a video track")
      }
    }
    
    track = newVideoTrack!
    track!.add(self)
    
    clear()
  }
  
  /// Clear the rendered video stream texture.
  func clear() {
    bHasReportedFirstFrame = false
    
    if (pixelBuffer == nil || renderTexture == nil || renderTexture!.bufferBytesPerRow <= 0) {
      return
    }
    
    let clearBytes = [Float](
      repeating: 0.0,
      count: renderTexture!.allocatedSize
    )

    renderTexture!.replace(
      region: MTLRegionMake2D(0, 0, renderTexture!.width, renderTexture!.height),
      mipmapLevel: 0,
      withBytes: clearBytes,
      bytesPerRow: renderTexture!.width * renderTexture!.bufferBytesPerRow
    )
  }
  
  /// Render the stored I420 data to the BGRA texture shared with Flutter.
  private func convertFrame() {
    guard let commandBuffer = commandQueue.makeCommandBuffer()
    else {
      fatalError("Failed to create command buffer for WebRTC stream")
    }
    commandBuffer.label = "FlutterRtcCommandBuffer"
    
    guard let encoder = commandBuffer.makeRenderCommandEncoder(descriptor: convertPassDescriptor)
    else {
      fatalError("Failed to create command encoder for WebRTC stream")
    }
    encoder.label = "FlutterRtcEncoder"
    
    encoder.pushDebugGroup("FlutterRtcRenderFrame")
    encoder.setRenderPipelineState(pipelineState)
    
    encoder.setFragmentTexture(yTexture, index: 0)
    encoder.setFragmentTexture(uTexture, index: 1)
    encoder.setFragmentTexture(vTexture, index: 2)
    
    // Draw the quad
    encoder.setVertexBytes(
      vertices,
      length: MemoryLayout<Vertex>.stride * vertices.count,
      index: 0
    )
    encoder.drawPrimitives(type: .triangleStrip, vertexStart: 0, vertexCount: 4, instanceCount: 1)
    
    encoder.popDebugGroup()
    encoder.endEncoding()
    
    commandBuffer.commit()
  }
}

//MARK: RTCVideoRenderer
extension FlutterRtcVideoViewController: RTCVideoRenderer {
  func setSize(_ size: CGSize) {
    let width = Int(size.width)
    let height = Int(size.height)
    
    if (width <= 0 || height <= 0) {
      return
    }
    
    // Create a surface for inter-process sharing
    guard let surface = IOSurfaceCreate([
      kIOSurfaceWidth: width,
      kIOSurfaceHeight: height,
      kIOSurfaceBytesPerElement: 4,
      kIOSurfacePixelFormat: kCVPixelFormatType_32BGRA
    ] as CFDictionary)
    else {
      fatalError("Failed to create surface for WebRTC stream")
    }
    
    // Create a new pixel buffer of the correct size. Any previous buffer will be released automatically
    guard CVPixelBufferCreateWithIOSurface(
      kCFAllocatorDefault,
      surface,
      [ kCVPixelBufferCGImageCompatibilityKey: true, kCVPixelBufferCGBitmapContextCompatibilityKey: true ] as CFDictionary,
      &pixelBuffer
    ) == 0
    else {
      fatalError("Failed to create pixel buffer for WebRTC stream")
    }
    
    guard CVMetalTextureCacheCreateTextureFromImage(
      kCFAllocatorDefault,
      textureCache!, pixelBuffer!.takeUnretainedValue(),
      nil, .bgra8Unorm,
      width, height, 0,
      &renderTextureBuffer
    ) == kCVReturnSuccess
    else {
      fatalError("Failed to create Metal texture buffer for WebRTC stream")
    }
    
    renderTexture = CVMetalTextureGetTexture(renderTextureBuffer!)
    convertPassDescriptor.colorAttachments[0].texture = renderTexture
    
    api.textureRegistry.textureFrameAvailable(textureId)
    
    api.callFlutter { flutter in
      flutter.onFrameSizeChanged(
        controllerId: self.id,
        width: Int64(size.width),
        height: Int64(size.height)
      ) { _ in }
    }
  }
  
  func renderFrame(_ frame: RTCVideoFrame?) {
    if (frame == nil || renderTexture == nil) {
      return
    }
    
    let width = Int(frame!.width)
    let height = Int(frame!.height)
    let chromaWidth = width / 2
    let chromaHeight = height / 2
    
    let hasSizeChanged = width != lastFrameWidth || height != lastFrameHeight
    let yuvBuffer: RTCI420BufferProtocol = frame!.buffer.toI420()
    
    // Create/update luma (Y) texture from frame data
    if (hasSizeChanged || yTexture == nil) {
      yDescriptor = MTLTextureDescriptor.texture2DDescriptor(
        pixelFormat: .r8Unorm,
        width: width,
        height: height,
        mipmapped: false
      )
      yDescriptor?.usage = .shaderRead
      
      yTexture = device.makeTexture(descriptor: yDescriptor!)
    }
    
    yTexture!.replace(
      region: MTLRegionMake2D(0, 0, width, height),
      mipmapLevel: 0,
      withBytes: yuvBuffer.dataY,
      bytesPerRow: Int(yuvBuffer.strideY)
    )
    
    // Create/update chroma (U/V) texture from frame data
    if (hasSizeChanged || uTexture == nil || vTexture == nil) {
      uvDescriptor = MTLTextureDescriptor.texture2DDescriptor(
        pixelFormat: .r8Unorm,
        width: width / 2,
        height: height / 2,
        mipmapped: false
      )
      uvDescriptor?.usage = .shaderRead
      
      uTexture = device.makeTexture(descriptor: uvDescriptor!)
      vTexture = device.makeTexture(descriptor: uvDescriptor!)
    }
    
    uTexture!.replace(
      region: MTLRegionMake2D(0, 0, chromaWidth, chromaHeight),
      mipmapLevel: 0,
      withBytes: yuvBuffer.dataU,
      bytesPerRow: Int(yuvBuffer.strideU)
    )
    
    vTexture!.replace(
      region: MTLRegionMake2D(0, 0, chromaWidth, chromaHeight),
      mipmapLevel: 0,
      withBytes: yuvBuffer.dataV,
      bytesPerRow: Int(yuvBuffer.strideV)
    )
    
    lastFrameWidth = width
    lastFrameHeight = height
    
    convertFrame()
    
    if (!bHasReportedFirstFrame) {
      api.callFlutter { flutter in
        flutter.onFirstFrameRendered(controllerId: self.id) { _ in }
      }
      
      bHasReportedFirstFrame = true
    }
  }
}

//MARK: FlutterTexture
extension FlutterRtcVideoViewController: FlutterTexture {
  func copyPixelBuffer() -> Unmanaged<CVPixelBuffer>? {
    guard let buffer = pixelBuffer?.takeUnretainedValue()
    else {
      return nil
    }
    
    // Retain since Flutter releases the texture when finished
    return Unmanaged.passRetained(buffer)
  }
}
