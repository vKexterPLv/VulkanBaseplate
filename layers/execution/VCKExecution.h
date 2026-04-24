// =============================================================================
//  VCKExecution.h  -  Execution & orchestration layer
//
//  Classes [13]-[22]: FramePolicy, TimelineSemaphore, DependencyToken,
//  QueueSet, GpuSubmissionBatcher, BackpressureGovernor, JobGraph,
//  DebugTimeline, Frame, FrameScheduler.  Plus the DebugTimeline-aware
//  HandleLiveResize overloads (rule 12 - explicit, timeline-observable
//  recreation events).
//
//  See VCK.h for the full API reference - this file is the structural home
//  of execution classes, not the documentation source of truth.
// =============================================================================
#pragma once
// Note: VCK.h is already included before this file.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace VCK {

//  EXECUTION & ORCHESTRATION LAYER
//
//  VCK Core gives you the GPU surface.  This layer gives you a frame loop.
//
//  It is entirely opt-in:  if you want to keep driving VulkanSync +
//  VulkanCommand by hand, everything below this point can be ignored - it
//  does not modify any core class, it just composes on top of them.
//
//  PRINCIPLE
//    CPU and GPU are asynchronous producers and consumers.  A frame is a
//    container of work items (CPU jobs + GPU submissions + transient
//    resource scope + dependency tokens), not a render function.  The
//    scheduler decides how far ahead the CPU may run and when the GPU
//    work is actually handed off.
// =============================================================================


// -----------------------------------------------------------------------------
// [13] FramePolicy
//
//   Lockstep    - CPU waits for GPU every frame.  Deterministic.  Slow.
//   Pipelined   - CPU N+1, GPU N.  Standard Vulkan double-buffering.  Default.
//   AsyncMax    - CPU may run up to `maxLag` frames ahead.  Requires
//                 BackpressureGovernor to stay bounded.
// -----------------------------------------------------------------------------
enum class FramePolicy : uint8_t
{
    Lockstep,
    Pipelined,
    AsyncMax,
};

inline const char* FramePolicyName(FramePolicy p)
{
    switch (p)
    {
        case FramePolicy::Lockstep:  return "Lockstep";
        case FramePolicy::Pipelined: return "Pipelined";
        case FramePolicy::AsyncMax:  return "AsyncMax";
    }
    return "?";
}


// -----------------------------------------------------------------------------
// [14] TimelineSemaphore  (opt-in)
//
//  Thin wrapper over VK_KHR_timeline_semaphore.  A single semaphore carries
//  a monotonically increasing 64-bit counter that any producer can signal
//  and any consumer can wait for - no fences, no binary semaphores.
//
//  Requires device creation with VkPhysicalDeviceTimelineSemaphoreFeatures
//  { timelineSemaphore = VK_TRUE }.  VCK's current VulkanDevice does NOT
//  enable that feature, so Initialize() will return false on most setups -
//  callers should be prepared to fall back to VulkanSync's binary fences.
//  Adding the feature bit to the device is a one-line core change planned
//  for a follow-up PR.
// -----------------------------------------------------------------------------
class TimelineSemaphore
{
public:
    TimelineSemaphore()  = default;
    ~TimelineSemaphore() = default;

    TimelineSemaphore(const TimelineSemaphore&)            = delete;
    TimelineSemaphore& operator=(const TimelineSemaphore&) = delete;

    bool Initialize(VulkanDevice& device, uint64_t initialValue = 0);
    void Shutdown();

    VkSemaphore Handle() const { return m_Sem; }
    bool        IsValid() const { return m_Sem != VK_NULL_HANDLE; }

    // Non-blocking read of the current counter.
    uint64_t LastSignaledValue() const;

    // CPU waits for counter >= value (blocks up to timeoutNs).  Returns true on success.
    bool Wait(uint64_t value, uint64_t timeoutNs = UINT64_MAX) const;

    // Host-side signal (rarely used in GPU pipelines - prefer GPU-side vkQueueSubmit signal).
    bool Signal(uint64_t value);

private:
    VulkanDevice* m_Device = nullptr;
    VkSemaphore   m_Sem    = VK_NULL_HANDLE;
};


// -----------------------------------------------------------------------------
// [15] DependencyToken
//
//  Ordering handle.  Produced by whoever submits GPU work; consumed by
//  whoever needs to wait for it.  Internally: a (TimelineSemaphore*, value)
//  pair.  Invalid tokens are a no-op on wait (decouples hot paths from
//  whether any producer actually ran).
// -----------------------------------------------------------------------------
struct DependencyToken
{
    TimelineSemaphore* sem   = nullptr;
    uint64_t           value = 0;

    bool IsValid() const { return sem != nullptr; }

    // Convenience host wait.  Returns true on success or when invalid.
    bool WaitHost(uint64_t timeoutNs = UINT64_MAX) const
    {
        return !IsValid() || sem->Wait(value, timeoutNs);
    }
};


// -----------------------------------------------------------------------------
// [16] QueueSet
//
//  Holds VkQueue handles for the three logical queue types VCK cares about.
//  Falls back to the graphics queue for any type the current device did not
//  expose a dedicated queue for.  (VCK's current VulkanDevice only creates a
//  graphics queue - so in practice all three slots point at the same queue.
//  The abstraction exists so call sites can be written against multi-queue
//  intent today and pick up real parallelism when VulkanDevice grows
//  dedicated compute / transfer queue support.)
// -----------------------------------------------------------------------------
class QueueSet
{
public:
    bool Initialize(VulkanDevice& device);
    void Shutdown() {}

    VkQueue Graphics() const { return m_Graphics; }
    VkQueue Compute()  const { return m_Compute;  }
    VkQueue Transfer() const { return m_Transfer; }

    uint32_t GraphicsFamily() const { return m_GraphicsFamily; }
    uint32_t ComputeFamily()  const { return m_ComputeFamily;  }
    uint32_t TransferFamily() const { return m_TransferFamily; }

    bool HasDedicatedCompute()  const { return m_Compute  != m_Graphics; }
    bool HasDedicatedTransfer() const { return m_Transfer != m_Graphics; }

private:
    VkQueue  m_Graphics       = VK_NULL_HANDLE;
    VkQueue  m_Compute        = VK_NULL_HANDLE;
    VkQueue  m_Transfer       = VK_NULL_HANDLE;
    uint32_t m_GraphicsFamily = 0;
    uint32_t m_ComputeFamily  = 0;
    uint32_t m_TransferFamily = 0;
};


// -----------------------------------------------------------------------------
// [17] GpuSubmissionBatcher
//
//  Collects per-frame vkQueueSubmit work into one batch per queue and flushes
//  it once at end-of-frame.  Keeps per-draw submission overhead off the hot
//  path.  No reordering is performed here - batches are submitted in the
//  order calls to QueueX() were made.
//
//  Each Queue() call can attach optional (wait, signal) semaphores.  The
//  flush for the Graphics queue accepts the in-flight VkFence that the
//  FrameScheduler will wait on at the start of the next cycle.
// -----------------------------------------------------------------------------
class GpuSubmissionBatcher
{
public:
    struct SubmitInfo
    {
        VkSemaphore          waitSem   = VK_NULL_HANDLE;
        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSemaphore          signalSem = VK_NULL_HANDLE;

        // Explicit default ctor - MinGW g++ (9.x/10.x) rejects `info = {}`
        // when the nested struct has NSDMI unless the default ctor is
        // explicitly user-declared.  Cheap fix, zero runtime cost.
        SubmitInfo() = default;
    };

    bool Initialize(VulkanDevice& device, QueueSet& queues);
    void Shutdown();

    // Overloads instead of a default-argument - avoids the same MinGW
    // corner case on `SubmitInfo = {}`.
    void QueueGraphics(VkCommandBuffer cmd, const SubmitInfo& info);
    void QueueCompute (VkCommandBuffer cmd, const SubmitInfo& info);
    void QueueTransfer(VkCommandBuffer cmd, const SubmitInfo& info);

    void QueueGraphics(VkCommandBuffer cmd) { QueueGraphics(cmd, SubmitInfo()); }
    void QueueCompute (VkCommandBuffer cmd) { QueueCompute (cmd, SubmitInfo()); }
    void QueueTransfer(VkCommandBuffer cmd) { QueueTransfer(cmd, SubmitInfo()); }

    // Flushes all three queue buckets.  graphicsFence (if not null) is passed
    // to vkQueueSubmit on the graphics queue so the CPU can wait on it.
    void FlushAll(VkFence graphicsFence = VK_NULL_HANDLE);

    // v0.3: flush with an additional timeline signal on the graphics queue.
    // When graphicsTimeline != VK_NULL_HANDLE, the graphics submit chains
    // VkTimelineSemaphoreSubmitInfo into pNext so the GPU signals
    // `graphicsTimelineValue` when the frame's graphics work retires.
    // FrameScheduler uses this to consolidate per-slot frame completion
    // under a single monotonically-increasing counter (one timeline
    // semaphore per scheduler, one value per frame) - cheaper than N
    // binary fences and the value can be composed with DependencyToken
    // for cross-queue/async waits.
    //
    // graphicsFence is still honoured; callers typically pass both during
    // the timeline transition window so non-timeline consumers (fence
    // status probes in RetireCompletedFrames, Lockstep end-wait) keep
    // working.  When the timeline semaphore is valid, the FrameScheduler
    // prefers the timeline for its own waits.
    void FlushAll(VkFence             graphicsFence,
                  VkSemaphore         graphicsTimeline,
                  uint64_t            graphicsTimelineValue);

    // Clear without submitting - used when a swapchain recreate aborts a frame.
    void DiscardAll();

    uint32_t PendingGraphics() const { return static_cast<uint32_t>(m_Graphics.size()); }
    uint32_t PendingCompute()  const { return static_cast<uint32_t>(m_Compute.size());  }
    uint32_t PendingTransfer() const { return static_cast<uint32_t>(m_Transfer.size()); }

private:
    struct Entry { VkCommandBuffer cmd; SubmitInfo info; };

    void FlushQueue(VkQueue q, std::vector<Entry>& bucket, VkFence fence);
    void FlushQueueWithTimeline(VkQueue q, std::vector<Entry>& bucket,
                                VkFence fence,
                                VkSemaphore timelineSem,
                                uint64_t    timelineValue);

    VulkanDevice* m_Device = nullptr;
    QueueSet*     m_Queues = nullptr;

    std::vector<Entry> m_Graphics;
    std::vector<Entry> m_Compute;
    std::vector<Entry> m_Transfer;
};


// -----------------------------------------------------------------------------
// [18] BackpressureGovernor
//
//  Tracks the gap between the CPU frame counter (produced) and the GPU
//  frame counter (retired).  AsyncMaxPolicy uses this to block CPU frame N+k
//  from starting while k > maxLag.  Pipelined / Lockstep do not stall here
//  (Pipelined relies on VulkanSync's per-slot fence; Lockstep waits inline).
// -----------------------------------------------------------------------------
class BackpressureGovernor
{
public:
    //  framesInFlight is taken from VulkanSync's runtime count (capped at
    //  MAX_FRAMES_IN_FLIGHT).  maxLag is clamped to framesInFlight with a
    //  warning if the user asks for more.
    void Initialize(FramePolicy policy, uint32_t maxLag, uint32_t framesInFlight = MAX_FRAMES_IN_FLIGHT);
    void Shutdown();

    void NoteCpuFrameStart(uint64_t absoluteFrame);
    void NoteGpuFrameRetired(uint64_t absoluteFrame);

    // Non-blocking overrun check (AsyncMax only).  FrameScheduler uses this
    // to decide whether to force an extra fence wait.  The governor itself
    // never blocks - earlier versions waited on a condition variable, but
    // NoteGpuFrameRetired is always called on the same render thread, which
    // self-deadlocked the CV path the moment the CPU overran.
    bool     IsOverrun() const;

    FramePolicy Policy() const { return m_Policy; }
    uint32_t    MaxLag() const { return m_MaxLag; }
    uint64_t    CpuFrame() const { return m_CpuFrame.load(); }
    uint64_t    GpuFrame() const { return m_GpuFrame.load(); }
    uint32_t    Lag()      const
    {
        const uint64_t c = m_CpuFrame.load();
        const uint64_t g = m_GpuFrame.load();
        return c > g ? static_cast<uint32_t>(c - g) : 0u;
    }

private:
    FramePolicy m_Policy = FramePolicy::Pipelined;
    uint32_t    m_MaxLag = 2;

    std::atomic<uint64_t> m_CpuFrame{0};
    std::atomic<uint64_t> m_GpuFrame{0};

    std::mutex              m_Mu;
    std::condition_variable m_Cv;
};


// -----------------------------------------------------------------------------
// [19] JobGraph
//
//  Minimal CPU job / task graph.  Designed to be frame-scoped: Reset()
//  between frames; Add() during the frame; Execute() runs everything on the
//  internal worker pool respecting declared deps.
//
//  This is NOT a production-grade scheduler - no fibers, no work stealing,
//  no priority.  It's a correct baseline:  std::thread workers + mutex +
//  condition_variable + atomic per-job pending-deps counter.  Good enough
//  to demonstrate the frame / jobs / GPU-submit pipeline; replace with
//  something fancier later without changing the surface.
// -----------------------------------------------------------------------------
class JobGraph
{
public:
    using JobId = uint32_t;
    using Fn    = std::function<void()>;

    JobGraph()  = default;
    ~JobGraph() { Shutdown(); }

    JobGraph(const JobGraph&)            = delete;
    JobGraph& operator=(const JobGraph&) = delete;

    // workerCount = 0 → std::thread::hardware_concurrency(), clamped to [1,32].
    void Initialize(uint32_t workerCount = 0);
    void Shutdown();

    // Register a new job.  deps are job IDs returned from earlier Add() calls.
    // Safe to call only between Reset() and Execute().
    JobId Add(const char* name, Fn fn, std::initializer_list<JobId> deps = {});

    // Kick the graph and block until every job has run.
    void  Execute();

    // Drop all pending jobs - typically called from FrameScheduler::BeginFrame.
    void  Reset();

    uint32_t JobCount()    const { return static_cast<uint32_t>(m_Jobs.size()); }
    uint32_t WorkerCount() const { return static_cast<uint32_t>(m_Workers.size()); }

private:
    struct Job
    {
        JobId                    id = 0;
        const char*              name = "";
        Fn                       fn;
        std::vector<JobId>       dependents;
        std::atomic<uint32_t>    pendingDeps{0};
        std::atomic<bool>        done{false};
    };

    void WorkerLoop();
    void EnqueueReady(JobId id);
    void FinishJob(JobId id);

    std::vector<std::unique_ptr<Job>>  m_Jobs;
    std::vector<JobId>                 m_Ready;
    std::mutex                         m_Mu;
    std::condition_variable            m_CvWork;
    std::condition_variable            m_CvDone;

    std::vector<std::thread>           m_Workers;
    std::atomic<uint32_t>              m_Outstanding{0};
    std::atomic<bool>                  m_Exit{false};
    bool                               m_Executing = false;
};


// -----------------------------------------------------------------------------
// [20] DebugTimeline
//
//  Plain-text span recorder.  Call BeginCpuSpan / EndCpuSpan around chunks of
//  CPU work; NoteStall() when you discover the CPU was forced to wait; call
//  RecordGpuSpan() after reading GPU timestamps back.  Dump() prints a
//  chronological view to LogVk().  Enable per FrameScheduler config - when
//  disabled, every method is a cheap no-op.
// -----------------------------------------------------------------------------
class DebugTimeline
{
public:
    struct Span
    {
        std::string name;
        const char* track;      // "CPU" | "GPU" | "STALL"
        uint64_t    frame;
        uint64_t    startUs;
        uint64_t    endUs;      // 0 for point events (STALL)
    };

    void Initialize(bool enabled);
    void Shutdown();

    bool Enabled() const { return m_Enabled; }

    void BeginCpuSpan(const char* name, uint64_t frame);
    void EndCpuSpan  (const char* name, uint64_t frame);
    void RecordGpuSpan(const char* name, uint64_t frame,
                       uint64_t startUs, uint64_t endUs);
    void NoteStall(const char* reason, uint64_t frame, uint64_t durationUs);

    // Print accumulated spans to LogVk() and forget them.
    void Dump();
    // Keep buffer, drop spans (call between frames to avoid unbounded growth).
    void ResetBuffer();

    // Write accumulated spans to a JSON file in Chrome Trace Event format.
    // Load the file in chrome://tracing or https://ui.perfetto.dev for a
    // flame-graph / timeline viewer.  Returns false and logs via
    // VCKLog::Error (rule 14) if the file cannot be opened.  Does NOT
    // clear the buffer (call ResetBuffer() yourself if you want to wipe).
    // When Enabled() is false, returns true without writing anything.
    bool DumpChromeTracing(const char* path);

private:
    uint64_t NowUs() const;

    bool                                 m_Enabled = false;
    std::chrono::steady_clock::time_point m_Origin;
    std::mutex                           m_Mu;
    std::vector<Span>                    m_Spans;

    // Open CPU spans, keyed by name for simple push/pop semantics.
    std::vector<Span>                    m_OpenCpu;
};


// -----------------------------------------------------------------------------
// [21] Frame
//
//  Lightweight handle exposed to user code during BeginFrame .. EndFrame.
//  Not constructed by the user - owned by the FrameScheduler.  Provides
//  typed access to the slot's per-frame primitives plus the per-frame
//  batcher and job graph.
// -----------------------------------------------------------------------------
class FrameScheduler;   // fwd - friendship below

class Frame
{
public:
    uint32_t    Slot()      const { return m_Slot;     }
    uint64_t    Absolute()  const { return m_Absolute; }
    FramePolicy Policy()    const { return m_Policy;   }

    VkFence         Fence()           const { return m_Fence;           }
    VkSemaphore     ImageAvailable()  const { return m_ImageAvailable;  }
    VkSemaphore     RenderFinished()  const { return m_RenderFinished;  }
    VkCommandBuffer PrimaryCmd()      const { return m_PrimaryCmd;      }

    GpuSubmissionBatcher& Submissions() { return *m_Submissions; }
    JobGraph&             Jobs()        { return *m_Jobs;        }

    //  Convenience: queue PrimaryCmd onto the graphics bucket of the per-frame
    //  GpuSubmissionBatcher.  Equivalent to:
    //      f.Submissions().QueueGraphics(f.PrimaryCmd(), info);
    //  The no-arg overload wires the frame's own ImageAvailable / RenderFinished
    //  semaphores - the common case for a simple render-then-present loop.
    void QueueGraphics()
    {
        GpuSubmissionBatcher::SubmitInfo info;
        info.waitSem   = m_ImageAvailable;
        info.waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        info.signalSem = m_RenderFinished;
        m_Submissions->QueueGraphics(m_PrimaryCmd, info);
    }

    void QueueGraphics(const GpuSubmissionBatcher::SubmitInfo& info)
    {
        m_Submissions->QueueGraphics(m_PrimaryCmd, info);
    }

private:
    friend class FrameScheduler;

    uint32_t    m_Slot     = 0;
    uint64_t    m_Absolute = 0;
    FramePolicy m_Policy   = FramePolicy::Pipelined;

    VkFence         m_Fence           = VK_NULL_HANDLE;
    VkSemaphore     m_ImageAvailable  = VK_NULL_HANDLE;
    VkSemaphore     m_RenderFinished  = VK_NULL_HANDLE;
    VkCommandBuffer m_PrimaryCmd      = VK_NULL_HANDLE;

    GpuSubmissionBatcher* m_Submissions = nullptr;
    JobGraph*             m_Jobs        = nullptr;
};


// -----------------------------------------------------------------------------
// [22] FrameScheduler
//
//  Top-level execution orchestrator.
//
//    Initialize(device, command, sync, cfg)
//        Wires everything up.  Requires the core objects already created.
//
//    Frame& f = BeginFrame();
//        1. Advances absolute frame counter.
//        2. Applies FramePolicy - may block on the slot's in-flight fence
//           (Lockstep / Pipelined) or defer blocking to BackpressureGovernor
//           (AsyncMax).
//        3. Resets the slot's JobGraph + batcher.
//        4. Resets command buffer via VulkanCommand::BeginRecording().
//
//    f.Jobs().Add(...);   // register CPU work with deps
//    DispatchJobs();      // runs the graph, blocks until all CPU jobs done
//
//    // Record vkCmd* into f.PrimaryCmd().  Push batched submits via
//    // f.Submissions().QueueGraphics(cmd, ...).
//
//    EndFrame();
//        1. Flushes GpuSubmissionBatcher.
//        2. Hands the frame's fence to the graphics queue so GPU retirement
//           can be tracked.
//        3. Advances VulkanSync's frame index.
//
//  The scheduler NEVER calls vkAcquireNextImageKHR or vkQueuePresentKHR.
//  Those remain the caller's responsibility; the scheduler just owns timing.
//
//  framesInFlight is inherited from VulkanSync (set via the core VCK::Config
//  at sync.Initialize time).  FrameScheduler reads sync.GetFramesInFlight()
//  and loops over that many per-frame slots, then forwards the count into
//  BackpressureGovernor so asyncMaxLag is clamped correctly.
//  FrameScheduler::Config is a SEPARATE, narrower struct - it governs the
//  scheduler's own knobs (policy, asyncMaxLag, timeline, jobWorkers), not
//  framesInFlight.
// -----------------------------------------------------------------------------
class FrameScheduler
{
public:
    struct Config
    {
        FramePolicy policy         = FramePolicy::Pipelined;
        uint32_t    asyncMaxLag    = 2;    // only relevant for AsyncMax
        bool        enableTimeline = false;
        uint32_t    jobWorkers     = 0;    // 0 → hardware_concurrency clamped

        // See SubmitInfo above - explicit default ctor works around a
        // MinGW g++ bug with NSDMI + `cfg = {}` default arguments.
        Config() = default;
    };

    FrameScheduler()  = default;
    ~FrameScheduler() = default;

    FrameScheduler(const FrameScheduler&)            = delete;
    FrameScheduler& operator=(const FrameScheduler&) = delete;

    bool Initialize(VulkanDevice&  device,
                    VulkanCommand& command,
                    VulkanSync&    sync,
                    Config         cfg);

    // No-config overload - defaults to `Config()`.
    bool Initialize(VulkanDevice&  device,
                    VulkanCommand& command,
                    VulkanSync&    sync)
    {
        return Initialize(device, command, sync, Config());
    }
    void Shutdown();

    // Frame lifecycle - must be called in order, once per frame.
    Frame& BeginFrame();
    void   DispatchJobs();
    void   EndFrame();

    // v0.3: wait for every slot's most-recent submit to retire without
    // touching the graphics queue globally.  Replaces vkDeviceWaitIdle on
    // the swapchain-recreate / resize path: only the scheduler's own
    // in-flight work is waited on, so concurrent work on dedicated
    // compute / transfer queues keeps making progress.  Uses the timeline
    // semaphore when active, the per-slot binary fences otherwise.  No-op
    // outside of a frame range (i.e., before the first EndFrame).
    //
    // Safe to call between frames (not while InFrame()==true).  Does not
    // advance m_Absolute or CurrentSlot().
    void   DrainInFlight();

    // Accessors.
    uint64_t              AbsoluteFrame()      const { return m_Absolute; }
    uint32_t              CurrentSlot()        const;
    FramePolicy           Policy()             const { return m_Cfg.policy; }
    uint64_t              LastRetiredFrame()   const { return m_Governor.GpuFrame(); }
    bool                  InFrame()            const { return m_InFrame; }

    QueueSet&             Queues()       { return m_Queues;      }
    GpuSubmissionBatcher& Submissions()  { return m_Submissions; }
    BackpressureGovernor& Governor()     { return m_Governor;    }
    DebugTimeline&        Timeline()     { return m_Timeline;    }

    // v0.3: per-frame timeline semaphore.  Valid (IsValid() == true) when
    // cfg.enableTimeline was set AND the device exposes
    // VK_KHR_timeline_semaphore (VulkanDevice::HasTimelineSemaphores()).
    // When valid, EndFrame signals a monotonically-increasing value on
    // this semaphore as part of the graphics submit; BeginFrame and
    // Lockstep's end-wait prefer the timeline value over the binary
    // fence.  The fence path stays active as a fallback / compatibility
    // mechanism so callers that peek at vkGetFenceStatus (or the
    // scheduler's own RetireCompletedFrames) keep working unchanged.
    //
    // Intended primarily for authoring DependencyToken instances that
    // compose with async compute / transfer submits (see example [13]).
    TimelineSemaphore&       FrameTimeline()        { return m_FrameTimeline; }
    const TimelineSemaphore& FrameTimeline() const  { return m_FrameTimeline; }

    // Token that resolves when the given frame slot's graphics work
    // retires.  IsValid() mirrors FrameTimeline().IsValid().  Value is
    // the per-slot timeline value last scheduled on that slot.  Safe to
    // call between BeginFrame / EndFrame; reading between frames returns
    // the most recent token for the slot (i.e., the token for the last
    // EndFrame that ran on it).
    DependencyToken          SlotToken(uint32_t slot);

    const Config&         Cfg()          const { return m_Cfg; }

private:
    void WaitInFlightFence(uint32_t slot);
    void RetireCompletedFrames();
    bool TimelineActive() const { return m_FrameTimeline.IsValid(); }

    VulkanDevice*  m_Device  = nullptr;
    VulkanCommand* m_Command = nullptr;
    VulkanSync*    m_Sync    = nullptr;

    Config m_Cfg;

    QueueSet             m_Queues;
    GpuSubmissionBatcher m_Submissions;
    BackpressureGovernor m_Governor;
    DebugTimeline        m_Timeline;

    std::array<Frame,    MAX_FRAMES_IN_FLIGHT> m_Frames{};
    std::array<JobGraph, MAX_FRAMES_IN_FLIGHT> m_Jobs{};
    std::array<uint64_t, MAX_FRAMES_IN_FLIGHT> m_SlotAbsolute{};

    // v0.3: timeline-semaphore frame retirement.  One semaphore per
    // scheduler.  m_SlotTimelineValue[i] is the value that will be
    // signalled on the timeline when slot i's graphics work finishes.
    // m_NextTimelineValue is the monotonic counter advanced in EndFrame.
    // All three are only populated when TimelineActive() is true.
    TimelineSemaphore                          m_FrameTimeline;
    std::array<uint64_t, MAX_FRAMES_IN_FLIGHT> m_SlotTimelineValue{};
    uint64_t                                   m_NextTimelineValue = 0;

    // Runtime frames-in-flight captured from VulkanSync (clamped to
    // MAX_FRAMES_IN_FLIGHT).  Loops over m_Frames / m_Jobs / m_SlotAbsolute
    // use this, not the compile-time bound.
    uint32_t m_FramesInFlight = 2;

    uint64_t m_Absolute = 0;
    bool     m_InFrame  = false;
};
// DebugTimeline-observable variants (rule 12 - explicit recreation events).
// Pass the scheduler's Timeline() so the resize shows up as a CPU span in
// DebugTimeline::Dump().  `frame` is typically scheduler.AbsoluteFrame() at
// the call site.  When timeline.Enabled() == false all span calls are no-ops.
bool HandleLiveResize(Window&               window,
                      VulkanDevice&         device,
                      VulkanSwapchain&      swapchain,
                      VulkanFramebufferSet& framebuffers,
                      VulkanPipeline&       pipeline,
                      DebugTimeline&        timeline,
                      uint64_t              frame);

bool HandleLiveResize(Window&               window,
                      VulkanDevice&         device,
                      VulkanSwapchain&      swapchain,
                      VulkanFramebufferSet& framebuffers,
                      VulkanPipeline&       pipeline,
                      VulkanDepthBuffer&    depth,
                      DebugTimeline&        timeline,
                      uint64_t              frame);

// v0.3: scheduler-aware live-resize.  Instead of vkDeviceWaitIdle, waits
// only on the scheduler's per-slot in-flight work (timeline or fence),
// leaving independent compute / transfer work on dedicated queues alone.
// Uses scheduler.Timeline() for the DebugTimeline span and reads the
// frame counter from scheduler.AbsoluteFrame().
//
// Drop-in replacement for the legacy vkDeviceWaitIdle-based overloads
// when a FrameScheduler drives the frame loop (which is the recommended
// v0.3 path - direct VulkanSync users can stay on the legacy overloads).
bool HandleLiveResize(Window&               window,
                      VulkanSwapchain&      swapchain,
                      VulkanFramebufferSet& framebuffers,
                      VulkanPipeline&       pipeline,
                      FrameScheduler&       scheduler);

bool HandleLiveResize(Window&               window,
                      VulkanSwapchain&      swapchain,
                      VulkanFramebufferSet& framebuffers,
                      VulkanPipeline&       pipeline,
                      VulkanDepthBuffer&    depth,
                      FrameScheduler&       scheduler);

} // namespace VCK
