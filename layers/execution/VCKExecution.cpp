#include "../../VCK.h"
// VCKExecution.h is pulled in by VCK.h via the aggregate includes.

#include <algorithm>
#include <fstream>
#include <iterator>

namespace VCK {

//  EXECUTION & ORCHESTRATION LAYER  -  implementations
// =============================================================================


// -----------------------------------------------------------------------------
// [14] TimelineSemaphore
// -----------------------------------------------------------------------------
bool TimelineSemaphore::Initialize(VulkanDevice& device, uint64_t initialValue)
{
    m_Device = &device;

    VkSemaphoreTypeCreateInfo typeInfo{};
    typeInfo.sType         = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
    typeInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    typeInfo.initialValue  = initialValue;

    VkSemaphoreCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    ci.pNext = &typeInfo;

    const VkResult r = vkCreateSemaphore(device.GetDevice(), &ci, nullptr, &m_Sem);
    if (r != VK_SUCCESS)
    {
        VCKLog::Error("TimelineSemaphore", "vkCreateSemaphore failed (VkResult=" +
              std::to_string(static_cast<int>(r)) +
              "). The device was likely not created with timelineSemaphore=VK_TRUE; "
              "fall back to VulkanSync.");
        m_Sem = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

void TimelineSemaphore::Shutdown()
{
    if (m_Sem != VK_NULL_HANDLE && m_Device != nullptr)
    {
        vkDestroySemaphore(m_Device->GetDevice(), m_Sem, nullptr);
    }
    m_Sem    = VK_NULL_HANDLE;
    m_Device = nullptr;
}

uint64_t TimelineSemaphore::LastSignaledValue() const
{
    if (!IsValid()) return 0;
    uint64_t v = 0;
    const VkResult r = vkGetSemaphoreCounterValue(m_Device->GetDevice(), m_Sem, &v);
    (void)r;
    return v;
}

bool TimelineSemaphore::Wait(uint64_t value, uint64_t timeoutNs) const
{
    if (!IsValid()) return true;
    VkSemaphoreWaitInfo wi{};
    wi.sType          = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
    wi.semaphoreCount = 1;
    wi.pSemaphores    = &m_Sem;
    wi.pValues        = &value;
    return vkWaitSemaphores(m_Device->GetDevice(), &wi, timeoutNs) == VK_SUCCESS;
}

bool TimelineSemaphore::Signal(uint64_t value)
{
    if (!IsValid()) return false;
    VkSemaphoreSignalInfo si{};
    si.sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO;
    si.semaphore = m_Sem;
    si.value     = value;
    return vkSignalSemaphore(m_Device->GetDevice(), &si) == VK_SUCCESS;
}


// -----------------------------------------------------------------------------
// [16] QueueSet
// -----------------------------------------------------------------------------
bool QueueSet::Initialize(VulkanDevice& device)
{
    const QueueFamilyIndices& qfi = device.GetQueueFamilyIndices();

    m_Graphics       = device.GetGraphicsQueue();
    m_GraphicsFamily = qfi.GraphicsFamily.value_or(0);

    // v0.3: wire real dedicated queues when VulkanDevice picked them up.
    // GetComputeQueue / GetTransferQueue fall back to the graphics queue
    // if no dedicated family exists, so the resulting VkQueue handle is
    // always non-null.  The Family() accessors, however, must reflect the
    // actual family the queue belongs to - code paths that care about
    // family indices (VK_SHARING_MODE_CONCURRENT, ownership transfers,
    // queue-family image barriers) use them directly.
    m_Compute        = device.GetComputeQueue();
    m_ComputeFamily  = qfi.ComputeFamily.value_or(m_GraphicsFamily);
    m_Transfer       = device.GetTransferQueue();
    m_TransferFamily = qfi.TransferFamily.value_or(m_GraphicsFamily);

    return m_Graphics != VK_NULL_HANDLE;
}


// -----------------------------------------------------------------------------
// [17] GpuSubmissionBatcher
// -----------------------------------------------------------------------------
bool GpuSubmissionBatcher::Initialize(VulkanDevice& device, QueueSet& queues)
{
    m_Device = &device;
    m_Queues = &queues;
    return true;
}

void GpuSubmissionBatcher::Shutdown()
{
    DiscardAll();
    m_Device = nullptr;
    m_Queues = nullptr;
}

void GpuSubmissionBatcher::QueueGraphics(VkCommandBuffer cmd, const SubmitInfo& info)
{
    m_Graphics.push_back({cmd, info});
}

void GpuSubmissionBatcher::QueueCompute(VkCommandBuffer cmd, const SubmitInfo& info)
{
    m_Compute.push_back({cmd, info});
}

void GpuSubmissionBatcher::QueueTransfer(VkCommandBuffer cmd, const SubmitInfo& info)
{
    m_Transfer.push_back({cmd, info});
}

void GpuSubmissionBatcher::FlushAll(VkFence graphicsFence)
{
    FlushAll(graphicsFence, VK_NULL_HANDLE, 0);
}

void GpuSubmissionBatcher::FlushAll(VkFence     graphicsFence,
                                    VkSemaphore graphicsTimeline,
                                    uint64_t    graphicsTimelineValue)
{
    if (m_Queues == nullptr) return;

    // Transfer → Compute → Graphics.  Graphics is submitted last with the
    // in-flight fence (and optional timeline signal) so the CPU can track
    // retirement of the whole frame by either mechanism.
    FlushQueue(m_Queues->Transfer(), m_Transfer, VK_NULL_HANDLE);
    FlushQueue(m_Queues->Compute(),  m_Compute,  VK_NULL_HANDLE);
    if (graphicsTimeline != VK_NULL_HANDLE)
    {
        FlushQueueWithTimeline(m_Queues->Graphics(), m_Graphics,
                               graphicsFence, graphicsTimeline, graphicsTimelineValue);
    }
    else
    {
        FlushQueue(m_Queues->Graphics(), m_Graphics, graphicsFence);
    }
}

void GpuSubmissionBatcher::DiscardAll()
{
    m_Graphics.clear();
    m_Compute.clear();
    m_Transfer.clear();
}

void GpuSubmissionBatcher::FlushQueue(VkQueue q, std::vector<Entry>& bucket, VkFence fence)
{
    if (bucket.empty())
    {
        if (fence != VK_NULL_HANDLE)
        {
            // Still need to signal the fence so the CPU doesn't deadlock on it
            // next cycle.  Empty submit with no cmds just signals the fence.
            VkSubmitInfo emptyInfo{};
            emptyInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            VK_CHECK(vkQueueSubmit(q, 1, &emptyInfo, fence));
        }
        return;
    }

    std::vector<VkSubmitInfo> submits;
    submits.reserve(bucket.size());
    for (const Entry& e : bucket)
    {
        VkSubmitInfo si{};
        si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers    = &e.cmd;

        if (e.info.waitSem != VK_NULL_HANDLE)
        {
            si.waitSemaphoreCount = 1;
            si.pWaitSemaphores    = &e.info.waitSem;
            si.pWaitDstStageMask  = &e.info.waitStage;
        }
        if (e.info.signalSem != VK_NULL_HANDLE)
        {
            si.signalSemaphoreCount = 1;
            si.pSignalSemaphores    = &e.info.signalSem;
        }
        submits.push_back(si);
    }

    VK_CHECK(vkQueueSubmit(q,
                           static_cast<uint32_t>(submits.size()),
                           submits.data(),
                           fence));
    bucket.clear();
}

void GpuSubmissionBatcher::FlushQueueWithTimeline(VkQueue              q,
                                                  std::vector<Entry>&  bucket,
                                                  VkFence              fence,
                                                  VkSemaphore          timelineSem,
                                                  uint64_t             timelineValue)
{
    // Empty bucket + timeline signal: one synthetic submit that just
    // signals the timeline value (and the fence, if present).  This
    // keeps both retirement mechanisms in lock-step with CPU frame N.
    if (bucket.empty())
    {
        VkTimelineSemaphoreSubmitInfo ts{};
        ts.sType                     = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
        ts.signalSemaphoreValueCount = 1;
        ts.pSignalSemaphoreValues    = &timelineValue;

        VkSubmitInfo si{};
        si.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.pNext                = &ts;
        si.signalSemaphoreCount = 1;
        si.pSignalSemaphores    = &timelineSem;

        VK_CHECK(vkQueueSubmit(q, 1, &si, fence));
        return;
    }

    // Non-empty bucket: append the timeline semaphore as an *extra* signal
    // on the last submit.  We rebuild signal arrays per entry to include
    // the existing per-entry signalSem (if any) alongside the timeline
    // signal on the trailing submit.  Earlier submits are unchanged.
    std::vector<VkSubmitInfo>                  submits;
    std::vector<VkTimelineSemaphoreSubmitInfo> tlInfos;
    std::vector<std::array<VkSemaphore,    2>> signals;
    std::vector<std::array<uint64_t,       2>> signalVals;

    submits.reserve(bucket.size());
    tlInfos.reserve(1);
    signals.reserve(bucket.size());
    signalVals.reserve(bucket.size());

    const size_t last = bucket.size() - 1;
    for (size_t i = 0; i < bucket.size(); ++i)
    {
        const Entry& e = bucket[i];
        VkSubmitInfo si{};
        si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers    = &e.cmd;

        if (e.info.waitSem != VK_NULL_HANDLE)
        {
            si.waitSemaphoreCount = 1;
            si.pWaitSemaphores    = &e.info.waitSem;
            si.pWaitDstStageMask  = &e.info.waitStage;
        }

        if (i == last)
        {
            // Trailing submit: combine the per-entry signal (if any) with
            // the timeline signal.  Values are meaningful only for the
            // timeline slot (binary semaphores ignore the paired value).
            signals.push_back({});
            signalVals.push_back({});
            auto&    sArr  = signals.back();
            auto&    vArr  = signalVals.back();
            uint32_t count = 0;

            if (e.info.signalSem != VK_NULL_HANDLE)
            {
                sArr[count] = e.info.signalSem;
                vArr[count] = 0;
                ++count;
            }
            sArr[count] = timelineSem;
            vArr[count] = timelineValue;
            ++count;

            tlInfos.push_back({});
            VkTimelineSemaphoreSubmitInfo& ts = tlInfos.back();
            ts.sType                          = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
            ts.signalSemaphoreValueCount      = count;
            ts.pSignalSemaphoreValues         = vArr.data();

            si.pNext                = &ts;
            si.signalSemaphoreCount = count;
            si.pSignalSemaphores    = sArr.data();
        }
        else if (e.info.signalSem != VK_NULL_HANDLE)
        {
            si.signalSemaphoreCount = 1;
            si.pSignalSemaphores    = &e.info.signalSem;
        }
        submits.push_back(si);
    }

    VK_CHECK(vkQueueSubmit(q,
                           static_cast<uint32_t>(submits.size()),
                           submits.data(),
                           fence));
    bucket.clear();
}


// -----------------------------------------------------------------------------
// [18] BackpressureGovernor
// -----------------------------------------------------------------------------
void BackpressureGovernor::Initialize(FramePolicy policy, uint32_t maxLag, uint32_t framesInFlight)
{
    m_Policy = policy;

    // Clamp maxLag to the actual number of frames the sync layer can track.
    // The slot-fence mechanism already caps real CPU/GPU lag at that depth,
    // so accepting a larger value would give the user a number Lag() can
    // never actually reach.  Log when clamping so the user can correct cfg.
    if (framesInFlight == 0) framesInFlight = 1;
    if (framesInFlight > MAX_FRAMES_IN_FLIGHT) framesInFlight = MAX_FRAMES_IN_FLIGHT;

    uint32_t clamped = maxLag == 0 ? 1u : maxLag;
    if (clamped > framesInFlight)
    {
        VCKLog::Info("BackpressureGovernor", std::string("asyncMaxLag=") +
              std::to_string(maxLag) +
              " exceeds framesInFlight=" +
              std::to_string(framesInFlight) +
              " - clamped.  Deeper pipelining requires timeline semaphores.");
        clamped = framesInFlight;
    }
    m_MaxLag = clamped;

    m_CpuFrame.store(0);
    m_GpuFrame.store(0);
}

void BackpressureGovernor::Shutdown()
{
    // Nothing to do - no threads, no handles.  All state is plain atomics.
}

void BackpressureGovernor::NoteCpuFrameStart(uint64_t absoluteFrame)
{
    m_CpuFrame.store(absoluteFrame, std::memory_order_release);
}

void BackpressureGovernor::NoteGpuFrameRetired(uint64_t absoluteFrame)
{
    uint64_t prev = m_GpuFrame.load(std::memory_order_acquire);
    // Monotonic update.
    while (absoluteFrame > prev &&
           !m_GpuFrame.compare_exchange_weak(prev, absoluteFrame,
                                             std::memory_order_acq_rel))
    {
        /* retry */
    }
}

// Non-blocking overrun check.
//
// Returns true when the CPU is more than `maxLag` frames ahead of the last
// retired GPU frame.  FrameScheduler is responsible for any actual waiting
// (it has the fences / device that can unstick the lag); the governor itself
// never blocks.  This used to wait on a condition variable, but every caller
// of NoteGpuFrameRetired runs on the render thread too - so the CV would
// self-deadlock the moment the CPU overran.
bool BackpressureGovernor::IsOverrun() const
{
    if (m_Policy != FramePolicy::AsyncMax) return false;
    const uint64_t cpu = m_CpuFrame.load(std::memory_order_acquire);
    const uint64_t gpu = m_GpuFrame.load(std::memory_order_acquire);
    return cpu > gpu && (cpu - gpu) > static_cast<uint64_t>(m_MaxLag);
}

// -----------------------------------------------------------------------------
// [19] JobGraph
// -----------------------------------------------------------------------------
void JobGraph::Initialize(uint32_t workerCount)
{
    if (!m_Workers.empty()) return;  // already initialised

    uint32_t n = workerCount;
    if (n == 0)
    {
        n = std::thread::hardware_concurrency();
        if (n == 0) n = 2;
    }
    if (n > 32) n = 32;

    m_Exit.store(false);
    m_Workers.reserve(n);
    for (uint32_t i = 0; i < n; ++i)
    {
        m_Workers.emplace_back([this] { WorkerLoop(); });
    }
}

void JobGraph::Shutdown()
{
    if (m_Workers.empty()) return;

    {
        std::lock_guard<std::mutex> lk(m_Mu);
        m_Exit.store(true);
    }
    m_CvWork.notify_all();
    for (std::thread& t : m_Workers)
    {
        if (t.joinable()) t.join();
    }
    m_Workers.clear();
    m_Jobs.clear();
    m_Ready.clear();
    m_Outstanding.store(0);
    m_Executing = false;
}

JobGraph::JobId JobGraph::Add(const char* name, Fn fn, std::initializer_list<JobId> deps)
{
    auto job = std::make_unique<Job>();
    job->id   = static_cast<JobId>(m_Jobs.size());
    job->name = name != nullptr ? name : "job";
    job->fn   = std::move(fn);

    // Count only *valid* deps.  A stale JobId from a previous frame (after
    // Reset()) or a bogus id would otherwise inflate pendingDeps and leave
    // the job unreachable - Execute() would deadlock on the threaded path
    // (m_Outstanding never reaches 0) or silently drop the job + all its
    // dependents on the inline fallback.
    uint32_t validDeps = 0;
    for (JobId dep : deps)
    {
        if (dep < m_Jobs.size())
        {
            m_Jobs[dep]->dependents.push_back(job->id);
            ++validDeps;
        }
        else
        {
            VCKLog::Info("JobGraph", std::string("Add('") +
                  (name != nullptr ? name : "job") +
                  "'): ignoring invalid dep " + std::to_string(dep));
        }
    }
    job->pendingDeps.store(validDeps);

    const JobId id = job->id;
    m_Jobs.push_back(std::move(job));
    return id;
}

void JobGraph::Execute()
{
    if (m_Jobs.empty()) return;
    if (m_Workers.empty())
    {
        // Graceful fallback: run inline on caller thread.
        // Use a simple ready queue; this is not parallel but is correct.
        std::vector<JobId> ready;
        ready.reserve(m_Jobs.size());
        for (auto& j : m_Jobs)
        {
            if (j->pendingDeps.load() == 0) ready.push_back(j->id);
        }
        while (!ready.empty())
        {
            const JobId id = ready.back();
            ready.pop_back();
            Job& j = *m_Jobs[id];
            if (j.fn) j.fn();
            j.done.store(true);
            for (JobId dep : j.dependents)
            {
                if (m_Jobs[dep]->pendingDeps.fetch_sub(1) == 1)
                {
                    ready.push_back(dep);
                }
            }
        }
        return;
    }

    m_Outstanding.store(static_cast<uint32_t>(m_Jobs.size()));
    {
        std::lock_guard<std::mutex> lk(m_Mu);
        m_Executing = true;
        m_Ready.clear();
        for (auto& j : m_Jobs)
        {
            if (j->pendingDeps.load() == 0) m_Ready.push_back(j->id);
        }
    }
    m_CvWork.notify_all();

    std::unique_lock<std::mutex> lk(m_Mu);
    m_CvDone.wait(lk, [this] {
        return m_Outstanding.load() == 0 || m_Exit.load();
    });
    m_Executing = false;
}

void JobGraph::Reset()
{
    std::lock_guard<std::mutex> lk(m_Mu);
    m_Jobs.clear();
    m_Ready.clear();
    m_Outstanding.store(0);
}

void JobGraph::WorkerLoop()
{
    for (;;)
    {
        JobId id = 0;
        {
            std::unique_lock<std::mutex> lk(m_Mu);
            m_CvWork.wait(lk, [this] { return !m_Ready.empty() || m_Exit.load(); });
            if (m_Exit.load() && m_Ready.empty()) return;

            id = m_Ready.back();
            m_Ready.pop_back();
        }

        Job& j = *m_Jobs[id];
        if (j.fn)
        {
            // User functions are expected to be noexcept; if they throw, we
            // let it propagate rather than silently swallow (matches VCK's
            // fail-loud policy).  In release builds g++ -fno-exceptions makes
            // this a no-op wrapping cost.
            j.fn();
        }
        FinishJob(id);
    }
}

void JobGraph::EnqueueReady(JobId id)
{
    {
        std::lock_guard<std::mutex> lk(m_Mu);
        m_Ready.push_back(id);
    }
    m_CvWork.notify_one();
}

void JobGraph::FinishJob(JobId id)
{
    Job& j = *m_Jobs[id];
    j.done.store(true);
    for (JobId dep : j.dependents)
    {
        if (m_Jobs[dep]->pendingDeps.fetch_sub(1) == 1)
        {
            EnqueueReady(dep);
        }
    }
    if (m_Outstanding.fetch_sub(1) == 1)
    {
        std::lock_guard<std::mutex> lk(m_Mu);
        m_CvDone.notify_all();
    }
}


// -----------------------------------------------------------------------------
// [20] DebugTimeline
// -----------------------------------------------------------------------------
void DebugTimeline::Initialize(bool enabled)
{
    m_Enabled = enabled;
    m_Origin  = std::chrono::steady_clock::now();
    m_Spans.clear();
    m_OpenCpu.clear();
}

void DebugTimeline::Shutdown()
{
    m_Enabled = false;
    m_Spans.clear();
    m_OpenCpu.clear();
}

uint64_t DebugTimeline::NowUs() const
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - m_Origin).count());
}

void DebugTimeline::BeginCpuSpan(const char* name, uint64_t frame)
{
    if (!m_Enabled) return;
    std::lock_guard<std::mutex> lk(m_Mu);
    Span s;
    s.name    = name ? name : "cpu";
    s.track   = "CPU";
    s.frame   = frame;
    s.startUs = NowUs();
    s.endUs   = 0;
    m_OpenCpu.push_back(std::move(s));
}

void DebugTimeline::EndCpuSpan(const char* name, uint64_t frame)
{
    if (!m_Enabled) return;
    std::lock_guard<std::mutex> lk(m_Mu);
    // Pop most recent open span with matching name+frame.
    for (auto it = m_OpenCpu.rbegin(); it != m_OpenCpu.rend(); ++it)
    {
        if (it->frame == frame && it->name == (name ? name : "cpu"))
        {
            it->endUs = NowUs();
            m_Spans.push_back(*it);
            m_OpenCpu.erase(std::next(it).base());
            return;
        }
    }
}

void DebugTimeline::RecordGpuSpan(const char* name, uint64_t frame,
                                  uint64_t startUs, uint64_t endUs)
{
    if (!m_Enabled) return;
    std::lock_guard<std::mutex> lk(m_Mu);
    Span s;
    s.name    = name ? name : "gpu";
    s.track   = "GPU";
    s.frame   = frame;
    s.startUs = startUs;
    s.endUs   = endUs;
    m_Spans.push_back(std::move(s));
}

void DebugTimeline::NoteStall(const char* reason, uint64_t frame, uint64_t durationUs)
{
    if (!m_Enabled) return;
    std::lock_guard<std::mutex> lk(m_Mu);
    Span s;
    // NoteStall is called *after* the stall has completed, so NowUs() is
    // the end of the stall, not its beginning.  Anchor the span backwards
    // by durationUs so Dump() lines it up with the surrounding CPU spans.
    const uint64_t now = NowUs();
    s.name    = reason ? reason : "stall";
    s.track   = "STALL";
    s.frame   = frame;
    s.startUs = now >= durationUs ? now - durationUs : 0;
    s.endUs   = now;
    m_Spans.push_back(std::move(s));
}

void DebugTimeline::Dump()
{
    if (!m_Enabled) return;
    std::vector<Span> snapshot;
    {
        std::lock_guard<std::mutex> lk(m_Mu);
        snapshot.swap(m_Spans);
    }
    std::sort(snapshot.begin(), snapshot.end(),
              [](const Span& a, const Span& b) { return a.startUs < b.startUs; });

    VCKLog::Info("DebugTimeline", std::to_string(snapshot.size()) + " spans:");
    for (const Span& s : snapshot)
    {
        const uint64_t dur = s.endUs > s.startUs ? s.endUs - s.startUs : 0;
        VCKLog::Info("DebugTimeline", std::string("  f=") + std::to_string(s.frame) +
              " [" + s.track + "] " + s.name +
              " @" + std::to_string(s.startUs) + "us  dur=" +
              std::to_string(dur) + "us");
    }
}

void DebugTimeline::ResetBuffer()
{
    std::lock_guard<std::mutex> lk(m_Mu);
    m_Spans.clear();
    m_OpenCpu.clear();
}

bool DebugTimeline::DumpChromeTracing(const char* path)
{
    if (!m_Enabled) return true;   // R19: nothing to dump when disabled.
    if (path == nullptr || *path == '\0')
    {
        VCKLog::Error("DebugTimeline", "DumpChromeTracing called with empty path");
        return false;
    }

    std::vector<Span> snapshot;
    {
        std::lock_guard<std::mutex> lk(m_Mu);
        snapshot = m_Spans;
    }

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
    {
        VCKLog::Error("DebugTimeline",
            std::string("DumpChromeTracing: cannot open '") + path + "'");
        return false;
    }

    // Chrome Trace Event format (array form).  Each span is one "X"
    // (complete) event with ts+dur in microseconds.  STALL spans with
    // endUs==0 are emitted as instant "i" events.  pid=1 is arbitrary;
    // tid is mapped from track name so CPU / GPU / STALL get separate rows.
    auto tidForTrack = [](const char* track) -> int
    {
        if (!track) return 0;
        // Trivial stable mapping.
        if (track[0] == 'C') return 1;   // CPU
        if (track[0] == 'G') return 2;   // GPU
        if (track[0] == 'S') return 3;   // STALL
        return 0;
    };

    out << "[\n";
    for (std::size_t i = 0; i < snapshot.size(); ++i)
    {
        const Span& s = snapshot[i];
        const int   tid = tidForTrack(s.track);
        const bool  isStall = (s.endUs == 0);
        // Guard the subtraction - matches Dump() (rule 14: never emit garbage).
        // Caller-supplied GPU spans from RecordGpuSpan may arrive with endUs <
        // startUs if timestamp-period math rounds weirdly; uint64_t underflow
        // would write nonsense into the JSON and confuse chrome://tracing.
        const uint64_t dur = isStall ? 0
                           : (s.endUs > s.startUs ? s.endUs - s.startUs : 0);

        if (i != 0) out << ",\n";
        out << "  {"
            << "\"name\":\"";
        // Escape double-quote and backslash in span name; everything else
        // is passed through.  Span names are author-controlled so this is
        // sufficient for the trace viewer to parse.
        for (char c : s.name)
        {
            if      (c == '"')  out << "\\\"";
            else if (c == '\\') out << "\\\\";
            else if (c == '\n') out << "\\n";
            else                out << c;
        }
        out << "\",\"cat\":\"" << (s.track ? s.track : "?")
            << "\",\"ph\":\"" << (isStall ? "i" : "X") << "\""
            << ",\"ts\":"   << s.startUs
            << ",\"dur\":"  << dur
            << ",\"pid\":1"
            << ",\"tid\":"  << tid
            << ",\"args\":{\"frame\":" << s.frame << "}"
            << "}";
    }
    out << "\n]\n";
    return true;
}


// -----------------------------------------------------------------------------
// [22] FrameScheduler
// -----------------------------------------------------------------------------
bool FrameScheduler::Initialize(VulkanDevice&  device,
                                VulkanCommand& command,
                                VulkanSync&    sync,
                                Config         cfg)
{
    m_Device  = &device;
    m_Command = &command;
    m_Sync    = &sync;
    m_Cfg     = cfg;

    if (!m_Queues.Initialize(device))
    {
        VCKLog::Error("FrameScheduler", "QueueSet::Initialize failed.");
        return false;
    }
    if (!m_Submissions.Initialize(device, m_Queues))
    {
        VCKLog::Error("FrameScheduler", "GpuSubmissionBatcher::Initialize failed.");
        return false;
    }
    // Runtime framesInFlight comes from VulkanSync (already clamped in its
    // Initialize).  Every loop over slots below uses this count, not the
    // compile-time bound.
    m_FramesInFlight = sync.GetFramesInFlight();
    if (m_FramesInFlight == 0) m_FramesInFlight = 1;
    if (m_FramesInFlight > MAX_FRAMES_IN_FLIGHT) m_FramesInFlight = MAX_FRAMES_IN_FLIGHT;

    m_Governor.Initialize(cfg.policy, cfg.asyncMaxLag, m_FramesInFlight);
    m_Timeline.Initialize(cfg.enableTimeline);

    // v0.3: only create the per-scheduler timeline semaphore when both
    // the user opts in (cfg.enableTimeline) AND the device exposes the
    // feature (VulkanDevice::HasTimelineSemaphores()).  The binary-fence
    // path remains fully functional; the timeline is layered on top.
    m_NextTimelineValue = 0;
    m_SlotTimelineValue.fill(0);
    bool timelineActive = false;
    if (cfg.enableTimeline && device.HasTimelineSemaphores())
    {
        if (m_FrameTimeline.Initialize(device, 0))
        {
            timelineActive = true;
        }
        else
        {
            VCKLog::Warn("FrameScheduler",
                  "Timeline requested but TimelineSemaphore::Initialize failed; "
                  "falling back to binary fences.");
        }
    }

    for (uint32_t i = 0; i < m_FramesInFlight; ++i)
    {
        m_Jobs[i].Initialize(cfg.jobWorkers);

        Frame& f          = m_Frames[i];
        f.m_Slot          = i;
        f.m_Absolute      = 0;
        f.m_Policy        = cfg.policy;
        f.m_Submissions   = &m_Submissions;
        f.m_Jobs          = &m_Jobs[i];

        m_SlotAbsolute[i] = 0;
    }

    m_Absolute = 0;
    m_InFrame  = false;

    VCKLog::Info("FrameScheduler", std::string("policy=") + FramePolicyName(cfg.policy) +
          " maxLag="    + std::to_string(cfg.asyncMaxLag) +
          " workers="   + std::to_string(m_Jobs[0].WorkerCount()) +
          " dbgSpans="  + (cfg.enableTimeline ? "on" : "off") +
          " frameSem="  + (timelineActive     ? "timeline" : "fence"));

    return true;
}

void FrameScheduler::Shutdown()
{
    for (uint32_t i = 0; i < m_FramesInFlight; ++i)
    {
        m_Jobs[i].Shutdown();
    }
    m_FrameTimeline.Shutdown();
    m_Timeline.Shutdown();
    m_Governor.Shutdown();
    m_Submissions.Shutdown();
    m_Queues.Shutdown();

    m_SlotTimelineValue.fill(0);
    m_NextTimelineValue = 0;

    m_Device  = nullptr;
    m_Command = nullptr;
    m_Sync    = nullptr;
    m_InFrame = false;
}

DependencyToken FrameScheduler::SlotToken(uint32_t slot)
{
    DependencyToken t;
    if (!TimelineActive() || slot >= m_FramesInFlight) return t;
    t.sem   = &m_FrameTimeline;
    t.value = m_SlotTimelineValue[slot];
    return t;
}

void FrameScheduler::DrainInFlight()
{
    if (m_Device == nullptr) return;

    // Timeline path: wait on the largest value we've ever scheduled.
    // Covers every slot in one call (timeline values are monotonic and
    // EndFrame assigns them in submission order, so waiting on the max
    // implies waiting on all slots' most recent submits).
    if (TimelineActive())
    {
        uint64_t maxVal = 0;
        for (uint32_t i = 0; i < m_FramesInFlight; ++i)
        {
            if (m_SlotTimelineValue[i] > maxVal) maxVal = m_SlotTimelineValue[i];
        }
        if (maxVal != 0)
        {
            m_FrameTimeline.Wait(maxVal);
            m_Governor.NoteGpuFrameRetired(m_Absolute);
        }
        return;
    }

    // Fence path: wait on every slot that has actually submitted work.
    // Resetting afterwards is NOT required - BeginFrame's
    // WaitInFlightFence will reset the slot's fence on its next pass.
    if (m_Sync == nullptr) return;
    for (uint32_t i = 0; i < m_FramesInFlight; ++i)
    {
        if (m_SlotAbsolute[i] == 0) continue;
        VkFence f = m_Sync->GetInFlightFence(i);
        if (f == VK_NULL_HANDLE) continue;
        VK_CHECK(vkWaitForFences(m_Device->GetDevice(), 1, &f, VK_TRUE, UINT64_MAX));
        m_Governor.NoteGpuFrameRetired(m_SlotAbsolute[i]);
    }
}

uint32_t FrameScheduler::CurrentSlot() const
{
    return m_Sync != nullptr ? m_Sync->GetCurrentFrameIndex() : 0u;
}

void FrameScheduler::WaitInFlightFence(uint32_t slot)
{
    if (m_Sync == nullptr || m_Device == nullptr) return;

    const auto t0 = std::chrono::steady_clock::now();

    // v0.3: prefer the timeline value when active.  The binary fence is
    // still reset (VulkanSync's next submit expects an unsignalled
    // fence), but the wait itself is done on the timeline so we get the
    // cheaper host wait and the slot can carry its retirement value
    // across BeginFrame→EndFrame→BeginFrame without needing the fence
    // round-trip.  When the timeline is inactive the fence does both.
    if (TimelineActive())
    {
        const uint64_t v = m_SlotTimelineValue[slot];
        if (v != 0)
        {
            m_FrameTimeline.Wait(v);
        }

        VkFence fence = m_Sync->GetInFlightFence(slot);
        if (fence != VK_NULL_HANDLE)
        {
            // vkResetFences is only legal after the fence has signalled.
            // The corresponding vkQueueSubmit happens in EndFrame, which
            // means on the first BeginFrame pass for this slot the fence
            // has never been submitted - reset would fail validation.
            // Only reset after the slot has actually run a frame.
            if (m_SlotAbsolute[slot] != 0)
            {
                VK_CHECK(vkWaitForFences(m_Device->GetDevice(), 1, &fence, VK_TRUE, UINT64_MAX));
                VK_CHECK(vkResetFences (m_Device->GetDevice(), 1, &fence));
            }
        }
    }
    else
    {
        VkFence fence = m_Sync->GetInFlightFence(slot);
        if (fence == VK_NULL_HANDLE) return;

        VK_CHECK(vkWaitForFences(m_Device->GetDevice(), 1, &fence, VK_TRUE, UINT64_MAX));
        VK_CHECK(vkResetFences(m_Device->GetDevice(), 1, &fence));
    }

    const auto t1 = std::chrono::steady_clock::now();

    // Anything the CPU spent blocking is a stall.
    const uint64_t waitedUs = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count());

    // When a slot's fence/timeline clears, the GPU has finished the frame
    // previously assigned to that slot.  That is the most precise
    // retirement signal we can produce.
    if (m_SlotAbsolute[slot] != 0)
    {
        m_Governor.NoteGpuFrameRetired(m_SlotAbsolute[slot]);
    }
    if (waitedUs > 100 && m_Timeline.Enabled())
    {
        m_Timeline.NoteStall(TimelineActive() ? "timeline-wait" : "fence-wait",
                             m_Absolute, waitedUs);
    }
}

void FrameScheduler::RetireCompletedFrames()
{
    // Non-blocking probe: for each slot whose GPU work has retired, mark
    // its absolute frame as retired.  When the timeline is active we read
    // the single counter once (cheaper than N fence-status queries) and
    // compare each slot's target value against it; otherwise we fall
    // back to vkGetFenceStatus per slot.
    if (m_Device == nullptr || m_Sync == nullptr) return;

    if (TimelineActive())
    {
        const uint64_t retired = m_FrameTimeline.LastSignaledValue();
        for (uint32_t i = 0; i < m_FramesInFlight; ++i)
        {
            if (m_SlotAbsolute[i] == 0) continue;
            if (m_SlotTimelineValue[i] != 0 && retired >= m_SlotTimelineValue[i])
            {
                m_Governor.NoteGpuFrameRetired(m_SlotAbsolute[i]);
            }
        }
        return;
    }

    for (uint32_t i = 0; i < m_FramesInFlight; ++i)
    {
        if (m_SlotAbsolute[i] == 0) continue;
        VkFence f = m_Sync->GetInFlightFence(i);
        if (f == VK_NULL_HANDLE) continue;
        if (vkGetFenceStatus(m_Device->GetDevice(), f) == VK_SUCCESS)
        {
            m_Governor.NoteGpuFrameRetired(m_SlotAbsolute[i]);
        }
    }
}

Frame& FrameScheduler::BeginFrame()
{
    ++m_Absolute;
    m_Governor.NoteCpuFrameStart(m_Absolute);

    // Opportunistic retirement probe - cheap, non-blocking fence status query.
    RetireCompletedFrames();

    const uint32_t slot = CurrentSlot();

    // Wait for this slot's fence before reusing its command buffer.
    //
    // All three policies share this single wait:
    //   Lockstep   : fence is still-signalled from its prior EndFrame's wait
    //                (that wait intentionally does NOT reset); the wait here
    //                is therefore a no-op and resets in preparation for the
    //                next submit.
    //   Pipelined  : standard case - block if the GPU hasn't finished this
    //                slot's previous frame yet.
    //   AsyncMax   : same as Pipelined.  The slot-fence mechanism already
    //                caps CPU-GPU lag at MAX_FRAMES_IN_FLIGHT; configuring
    //                asyncMaxLag higher than that is fine (IsOverrun never
    //                fires), configuring it lower is accepted but only
    //                observable via Governor().Lag() - the scheduler does
    //                NOT force extra waits beyond the slot fence (a naive
    //                extra wait risks blocking on an un-submitted fence).
    //                If you need tighter than MAX_FRAMES_IN_FLIGHT, use
    //                FramePolicy::Lockstep instead.
    WaitInFlightFence(slot);

    // Reset this slot's job graph.
    m_Jobs[slot].Reset();

    // Reset the slot's command buffer and open it for recording.
    if (m_Command != nullptr)
    {
        m_Command->BeginRecording(slot);
    }

    Frame& f = m_Frames[slot];
    f.m_Slot            = slot;
    f.m_Absolute        = m_Absolute;
    f.m_Policy          = m_Cfg.policy;
    f.m_Fence           = m_Sync ? m_Sync->GetInFlightFence(slot)            : VK_NULL_HANDLE;
    f.m_ImageAvailable  = m_Sync ? m_Sync->GetImageAvailableSemaphore(slot)  : VK_NULL_HANDLE;
    f.m_RenderFinished  = m_Sync ? m_Sync->GetRenderFinishedSemaphore(slot)  : VK_NULL_HANDLE;
    f.m_PrimaryCmd      = m_Command ? m_Command->GetCommandBuffer(slot)      : VK_NULL_HANDLE;
    f.m_Submissions     = &m_Submissions;
    f.m_Jobs            = &m_Jobs[slot];

    m_SlotAbsolute[slot] = m_Absolute;
    m_InFrame = true;

    if (m_Timeline.Enabled())
    {
        m_Timeline.BeginCpuSpan("frame", m_Absolute);
    }
    return f;
}

void FrameScheduler::DispatchJobs()
{
    const uint32_t slot = CurrentSlot();
    if (m_Timeline.Enabled()) m_Timeline.BeginCpuSpan("jobs", m_Absolute);
    m_Jobs[slot].Execute();
    if (m_Timeline.Enabled()) m_Timeline.EndCpuSpan("jobs", m_Absolute);
}

void FrameScheduler::EndFrame()
{
    if (!m_InFrame) return;

    const uint32_t slot  = CurrentSlot();
    VkFence        fence = m_Sync ? m_Sync->GetInFlightFence(slot) : VK_NULL_HANDLE;

    // Close the primary command buffer (caller was expected to fill it).
    if (m_Command != nullptr)
    {
        m_Command->EndRecording(slot);
    }

    // Flush all batched submits.  Graphics queue gets the fence, and - if
    // the per-scheduler timeline is active - signals the slot's timeline
    // value in the same submit so downstream consumers (SlotToken,
    // RetireCompletedFrames, Lockstep end-wait) can wait on either.
    if (TimelineActive())
    {
        const uint64_t v = ++m_NextTimelineValue;
        m_SlotTimelineValue[slot] = v;
        m_Submissions.FlushAll(fence, m_FrameTimeline.Handle(), v);
    }
    else
    {
        m_Submissions.FlushAll(fence);
    }

    // Advance VulkanSync's internal frame index.
    if (m_Sync != nullptr)
    {
        m_Sync->AdvanceFrame();
    }

    if (m_Timeline.Enabled())
    {
        m_Timeline.EndCpuSpan("frame", m_Absolute);
    }

    // Lockstep: block until this frame's GPU work retires.  We wait but do
    // NOT reset - the fence stays signalled until the next BeginFrame reaches
    // this slot, which does the reset inside WaitInFlightFence.  Resetting
    // here would leave the fence in a state where the next same-slot
    // BeginFrame hangs on a never-submitted fence.
    if (m_Cfg.policy == FramePolicy::Lockstep && m_Device != nullptr)
    {
        if (TimelineActive())
        {
            m_FrameTimeline.Wait(m_SlotTimelineValue[slot]);
            m_Governor.NoteGpuFrameRetired(m_Absolute);
        }
        else if (fence != VK_NULL_HANDLE)
        {
            VK_CHECK(vkWaitForFences(m_Device->GetDevice(), 1, &fence, VK_TRUE, UINT64_MAX));
            m_Governor.NoteGpuFrameRetired(m_Absolute);
        }
    }

    m_InFrame = false;
}
bool HandleLiveResize(Window&               window,
                      VulkanDevice&         device,
                      VulkanSwapchain&      swapchain,
                      VulkanFramebufferSet& framebuffers,
                      VulkanPipeline&       pipeline,
                      DebugTimeline&        timeline,
                      uint64_t              frame)
{
    if (!window.WasResized() || window.IsMinimized()) return false;

    if (timeline.Enabled()) timeline.BeginCpuSpan("HandleLiveResize", frame);
    const bool ok = HandleLiveResize(window, device, swapchain, framebuffers, pipeline);
    if (timeline.Enabled()) timeline.EndCpuSpan("HandleLiveResize", frame);
    return ok;
}

bool HandleLiveResize(Window&               window,
                      VulkanDevice&         device,
                      VulkanSwapchain&      swapchain,
                      VulkanFramebufferSet& framebuffers,
                      VulkanPipeline&       pipeline,
                      VulkanDepthBuffer&    depth,
                      DebugTimeline&        timeline,
                      uint64_t              frame)
{
    if (!window.WasResized() || window.IsMinimized()) return false;

    if (timeline.Enabled()) timeline.BeginCpuSpan("HandleLiveResize", frame);
    const bool ok = HandleLiveResize(window, device, swapchain, framebuffers, pipeline, depth);
    if (timeline.Enabled()) timeline.EndCpuSpan("HandleLiveResize", frame);
    return ok;
}

} // namespace VCK
