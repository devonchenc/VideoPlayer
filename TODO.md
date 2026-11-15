# 1. Question

## 1.1 Playback commands such as Play, Pause, Resume, and Stop are handled by PlayController and do not need to be routed through the rendering command queue?

## 1.2 When play is paused, should D3D12 keep rendering?


# 2. D3D12 + FFmpeg Player – Enabler Workstreams
The development of the D3D12 + FFmpeg Player should be divided into four Enabler Workstreams. Each workstream focuses on a specific set of foundational components required to establish the player architecture.

The objective is to complete these foundational components first, allowing subsequent playback features (Play, Pause, Stop, Seek, Fast Forward, etc.) to be developed independently and in parallel.
Workstream 1: Generic Core Infrastructure
No dependency on D3D12 or FFmpeg
Establish the generic infrastructure required by the player. These components should be independent of specific graphics APIs and multimedia frameworks, and should ideally be reusable across other projects.
Jira Enabler	Purpose / Responsibilities
1.1 Thread Lifecycle Management	Establish a generic thread lifecycle management mechanism, covering thread creation, startup, shutdown, joining, and resource cleanup. Ensure threads can terminate safely without resource leaks or race conditions.
1.2 Stop Token and Cancellation Mechanism	Implement a unified cancellation mechanism using C++20 std::stop_token and std::stop_source. Allow multiple worker threads to receive stop requests and terminate execution safely.
1.3 Thread-safe BlockingQueue	Implement a thread-safe BlockingQueue for communication between worker threads. Support blocking waits, timed waits, cancellation notifications, and appropriate queue capacity and cleanup strategies.
1.4 Common Error Handling	Establish a consistent error-handling mechanism, including error codes, exception handling, and error propagation across different modules.
1.5 Logging Infrastructure	Establish a unified logging infrastructure supporting different log levels, thread identification, and error tracing to simplify debugging and troubleshooting in a multithreaded environment.

Expected Deliverables:
- Reusable thread management components.
- Thread-safe inter-thread communication mechanisms.
- Unified cancellation and error-handling mechanisms.
- Common logging interfaces.

Workstream 2: Player Core Infrastructure
Depends on Generic Core Infrastructure
Establish the overall player framework, including Player Instance management, global resource management, window management, and the interfaces between player components.

This workstream focuses on the player architecture and component ownership rather than implementing actual video decoding or GPU rendering.
Jira Enabler	Purpose / Responsibilities
2.1 Implement CPlayerManager	Responsible for managing all Player Instances. Provide interfaces to create, destroy, retrieve, and manage individual instances. Maintain the lifecycle of all instances and ensure multiple player instances can operate independently without interfering with each other.
2.2 Implement CPlayerGlobalContext	Manage global resources shared across multiple Player Instances, such as IDXGIFactory6, ID3D12Device, and other shared graphics resources. Centralize resource initialization, access, and cleanup to avoid unnecessary duplication and resource lifecycle conflicts.
2.3 Implement CPlayerWindow	Manage the child window associated with each Player Instance. Responsibilities include window creation, destruction, resizing, window event handling, and forwarding window-related commands to the rendering thread.
2.4 Define Player Instance Architecture and Interfaces	Define the internal structure, responsibilities, interfaces, and resource ownership of each Player Instance. Establish how components such as CPlayerWindow, CD3D12Renderer, and CPlayController interact to minimize coupling.
2.5 Implement Player Instance Lifecycle Management	Implement the initialization, startup, shutdown, and destruction lifecycle of an individual Player Instance. Coordinate the lifecycle of its Window, Renderer, and Controller components, ensuring resources are properly released even when initialization fails or shutdown is requested.

Expected Deliverables:
- CPlayerManager – centralized management of all Player Instances.
- CPlayerGlobalContext – centralized management of shared global resources.
- CPlayerWindow – player window management.
- Clearly defined Player Instance architecture and interfaces.
- A well-defined Player Instance lifecycle management mechanism.
Important Design Considerations:
- CPlayerManager should manage Player Instance lifecycles and the collection of instances. It should not directly handle D3D12 rendering or FFmpeg decoding.
- CPlayerGlobalContext should manage shared resources, but should not own resources that belong exclusively to individual Player Instances, such as Swap Chains, Command Allocators, or Decoder contexts.
- Shared resources must remain valid until all Player Instances using them have been destroyed.

Workstream 3: D3D12 Rendering Infrastructure
Depends on Player Core Interfaces
Establish the GPU rendering infrastructure required by the player, providing an independent rendering capability based on Direct3D 12.

This workstream primarily focuses on implementing CD3D12Renderer.
Jira Enabler	Purpose / Responsibilities
3.1 Implement D3D12 Device Initialization	Implement D3D12 initialization, including adapter selection, device creation, and feature-level validation. Integrate with CPlayerGlobalContext to support shared device management.
3.2 Implement D3D12 Renderer Initialization	Implement initialization and shutdown of CD3D12Renderer. Create the required rendering resources, including Command Queue, Swap Chain, Command Allocators, and Command Lists. Clearly define which resources are shared through the Global Context and which are owned by individual Renderer instances.
3.3 Implement D3D12 Rendering Pipeline	Establish the GPU rendering pipeline, including Root Signature, Pipeline State Objects (PSO), Vertex Shaders, Pixel Shaders, and the associated input and output resource configurations.
3.4 Implement GPU Resource Management	Implement GPU resource management, including Textures, Descriptor Heaps, Upload Buffers, and Resource Barriers. Ensure correct resource allocation, state transitions, and cleanup according to D3D12 requirements.
3.5 Implement D3D12 Frame Synchronization	Implement CPU/GPU synchronization mechanisms, including Fences, Frame Contexts, Command Allocator reuse, and GPU completion tracking to prevent premature resource reuse.
3.6 Implement Video Frame Rendering	Implement video frame uploading and rendering interfaces. Transfer CPU video frame data to GPU Textures and use Shaders to render the video image.
3.7 Implement Renderer Resize Handling	Implement Swap Chain resizing, Back Buffer recreation, Viewport updates, and Scissor Rect updates to ensure rendering resources are correctly updated when the player window changes size.

Expected Deliverables:
- An independently initialized and managed CD3D12Renderer.
- A complete D3D12 rendering pipeline.
- GPU resource lifecycle management.
- CPU/GPU synchronization mechanisms.
- A rendering module capable of receiving and displaying video frames.

Workstream 4: FFmpeg Media Infrastructure
Depends on Generic Core Infrastructure
Establish the media input, demuxing, decoding, and video frame conversion capabilities required by the player.

This workstream focuses on the underlying FFmpeg components that will eventually be integrated into CPlayController. It does not implement playback control features such as Play, Pause, or Seek.
Jira Enabler	Purpose / Responsibilities
4.1 Implement FFmpeg Initialization and Cleanup	Establish FFmpeg resource initialization and cleanup mechanisms. Manage resources such as AVFormatContext, AVCodecContext, AVPacket, and AVFrame, ensuring safe cleanup even when errors occur.
4.2 Implement Media Demuxing	Implement media file and RTSP stream opening, stream information retrieval, and packet demuxing. Distribute packets to the appropriate processing queues based on their stream types.
4.3 Implement Video Decoder	Implement the video decoding module, including decoder initialization, codec configuration, packet submission, and decoded frame retrieval. Provide decoded video frames for subsequent playback processing.
4.4 Implement Video Frame Conversion	Implement pixel format conversion and necessary image processing. Support conversion of different FFmpeg output formats into formats supported by the Renderer, such as NV12 and YUV420P.
4.5 Implement Packet and Frame Queue	Establish media data queues for transferring Packets and Frames between worker threads. Support queue capacity management, blocking waits, timeouts, frame dropping strategies, and queue cleanup during shutdown.
4.6 Implement Media Stream Information Management	Centralize media stream information management, including video resolution, frame rate, pixel format, time base, duration, and codec parameters. Provide the necessary information for subsequent playback control and rendering.

Expected Deliverables:
- Support for opening local media files and RTSP streams.
- Independent media demuxing and video decoding capabilities.
- Standardized decoded video frame output.
- Packet and Frame data transfer mechanisms.
- Centralized media stream information management.