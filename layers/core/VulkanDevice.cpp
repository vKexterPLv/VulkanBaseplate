#include "VulkanDevice.h"
#include "VulkanContext.h"
#include "VulkanHelpers.h"

#include <set>
#include <algorithm>

namespace VCK {

    // ─────────────────────────────────────────────────────────────────────────────
    //  Public API
    // ─────────────────────────────────────────────────────────────────────────────

    // Preferred overload - forwards to the raw-handle form by pulling
    // instance + surface out of the context.  Keeps user call sites free of
    // `.GetInstance()` / `.GetSurface()` plumbing.
    bool VulkanDevice::Initialize(VulkanContext& context)
    {
        return Initialize(context.GetInstance(), context.GetSurface());
    }

    bool VulkanDevice::Initialize(VulkanContext& context, const Config& cfg)
    {
        return Initialize(context.GetInstance(), context.GetSurface(), cfg);
    }

    bool VulkanDevice::Initialize(VkInstance instance, VkSurfaceKHR surface, const Config& cfg)
    {
        m_CfgDevice    = cfg.device;
        m_CfgRendering = cfg.rendering;
        m_CfgSwapchain = cfg.swapchain;
        return Initialize(instance, surface);
    }

    bool VulkanDevice::Initialize(VkInstance instance, VkSurfaceKHR surface)
    {
        // Each sub-step owns the single subsystem-tagged Error on failure
        // (R14: exactly one Error per failure).  The Notice lines below are
        // public-API-level breadcrumbs so a reader scanning for "Device"
        // can see exactly which step Initialize gave up on without us
        // double-emitting Error.  Failure paths roll back already-built
        // sub-state so the caller does not have to know to call Shutdown()
        // after a failed Initialize().

        // Caller-level Error pattern (matches the pre-A1 design):
        //   - Sub-functions emit Warn-level diagnostic context (deviceCount,
        //     missing-extension name, etc.) before returning false.
        //   - Initialize() owns the single subsystem-tagged Error per
        //     failure path (R14: exactly one Error per failure).
        // This keeps the rich context AND keeps the caller-level marker
        // a user can grep for at the public-API boundary.
        VCKLog::Info("Device", "Selecting physical device...");
        if (!PickPhysicalDevice(instance, surface))
        {
            VCKLog::Error("Device", "Initialize aborted: PickPhysicalDevice failed");
            return false;
        }

        VCKLog::Info("Device", "Creating logical device...");
        if (!CreateLogicalDevice())
        {
            VCKLog::Error("Device", "Initialize aborted: CreateLogicalDevice failed");
            m_PhysicalDevice = VK_NULL_HANDLE;     // roll back PickPhysicalDevice
            return false;
        }

        VCKLog::Info("Device", "Creating VMA allocator...");
        if (!CreateAllocator(instance))
        {
            VCKLog::Error("Device", "Initialize aborted: CreateAllocator failed");
            // Roll back the logical device built two steps up so we do not
            // leak it.  Inlined rather than calling Shutdown() so a failed
            // Initialize never logs a misleading "shut down" line.
            vkDestroyDevice(m_LogicalDevice, nullptr);
            m_LogicalDevice  = VK_NULL_HANDLE;
            m_GraphicsQueue  = VK_NULL_HANDLE;
            m_PresentQueue   = VK_NULL_HANDLE;
            m_ComputeQueue   = VK_NULL_HANDLE;
            m_TransferQueue  = VK_NULL_HANDLE;
            m_PhysicalDevice = VK_NULL_HANDLE;
            return false;
        }

        VCKLog::Notice("Device", "Initialized OK");
        return true;
    }

    void VulkanDevice::Shutdown()
    {
        if (m_Allocator != VK_NULL_HANDLE)
        {
            vmaDestroyAllocator(m_Allocator);
            m_Allocator = VK_NULL_HANDLE;
            VCKLog::Info("Device", "VMA allocator destroyed");
        }

        if (m_LogicalDevice != VK_NULL_HANDLE)
        {
            vkDestroyDevice(m_LogicalDevice, nullptr);
            m_LogicalDevice = VK_NULL_HANDLE;
            m_GraphicsQueue = VK_NULL_HANDLE;
            m_PresentQueue  = VK_NULL_HANDLE;
            m_ComputeQueue  = VK_NULL_HANDLE;
            m_TransferQueue = VK_NULL_HANDLE;
            VCKLog::Info("Device", "Logical device destroyed");
        }

        m_PhysicalDevice = VK_NULL_HANDLE;
    }

    // ─────────────────────────────────────────────────────────────────────────────
    //  Physical device selection
    // ─────────────────────────────────────────────────────────────────────────────

    bool VulkanDevice::PickPhysicalDevice(VkInstance instance, VkSurfaceKHR surface)
    {
        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);

        if (deviceCount == 0)
        {
            // Diagnostic context only - the caller (Initialize) owns the
            // single Error per R14.
            VCKLog::Warn("Device", "No Vulkan-capable GPUs found (vkEnumeratePhysicalDevices returned 0)");
            return false;
        }

        std::vector<VkPhysicalDevice> devices(deviceCount);
        vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

        VCKLog::Info("Device", "Found " + std::to_string(deviceCount) + " GPU(s):");

        VkPhysicalDevice bestDevice = VK_NULL_HANDLE;
        int              bestScore = -1;

        for (const VkPhysicalDevice& device : devices)
        {
            VkPhysicalDeviceProperties properties{};
            vkGetPhysicalDeviceProperties(device, &properties);

            VkPhysicalDeviceMemoryProperties memProperties{};
            vkGetPhysicalDeviceMemoryProperties(device, &memProperties);

            uint64_t vramBytes = 0;
            for (uint32_t i = 0; i < memProperties.memoryHeapCount; ++i)
            {
                if (memProperties.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
                    vramBytes += memProperties.memoryHeaps[i].size;
            }

            const char* typeString = "Unknown";
            switch (properties.deviceType)
            {
            case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   typeString = "Discrete GPU";   break;
            case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: typeString = "Integrated GPU"; break;
            case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:    typeString = "Virtual GPU";    break;
            case VK_PHYSICAL_DEVICE_TYPE_CPU:            typeString = "CPU";            break;
            default: break;
            }

            VCKLog::Info("Device",
                std::string("  [") + properties.deviceName + "] " +
                typeString + " | VRAM: " + std::to_string(vramBytes / (1024ull * 1024ull)) + " MB");

            int score = ScorePhysicalDevice(device, surface);
            if (score > bestScore)
            {
                bestScore = score;
                bestDevice = device;
            }
        }

        if (bestDevice == VK_NULL_HANDLE || bestScore < 0)
        {
            VCKLog::Warn("Device",
                "PickPhysicalDevice: no suitable GPU found (best score < 0 across "
                + std::to_string(deviceCount) + " device(s) - check required extensions, queue families, and surface format support)");
            return false;
        }

        m_PhysicalDevice = bestDevice;
        m_QueueFamilyIndices = FindQueueFamilies(bestDevice, surface);

        VkPhysicalDeviceProperties chosen{};
        vkGetPhysicalDeviceProperties(bestDevice, &chosen);
        VCKLog::Notice("Device", std::string("Selected: ") + chosen.deviceName);

        return true;
    }

    int VulkanDevice::ScorePhysicalDevice(VkPhysicalDevice device, VkSurfaceKHR surface) const
    {
        if (!IsPhysicalDeviceSuitable(device, surface))
            return -1;

        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(device, &properties);

        int score = 0;
        if (m_CfgDevice.preferDiscreteGpu)
        {
            if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
                score += 10000;
            else if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
                score += 1000;
        }
        else
        {
            // User explicitly opted out of discrete-GPU preference.
            if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
                score += 10000;
            else if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
                score += 1000;
        }

        score += static_cast<int>(properties.limits.maxImageDimension2D);
        return score;
    }

    bool VulkanDevice::IsPhysicalDeviceSuitable(VkPhysicalDevice device, VkSurfaceKHR surface) const
    {
        if (!CheckDeviceExtensionSupport(device))
            return false;

        QueueFamilyIndices indices = FindQueueFamilies(device, surface);
        if (!indices.IsComplete())
            return false;

        uint32_t formatCount = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);

        uint32_t presentModeCount = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);

        return formatCount > 0 && presentModeCount > 0;
    }

    bool VulkanDevice::CheckDeviceExtensionSupport(VkPhysicalDevice device) const
    {
        uint32_t extensionCount = 0;
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

        std::vector<VkExtensionProperties> available(extensionCount);
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, available.data());

        std::vector<const char*> required(std::begin(k_RequiredDeviceExtensions), std::end(k_RequiredDeviceExtensions));
        for (const char* extra : m_CfgDevice.extraDeviceExtensions)
            required.push_back(extra);

        for (const char* needed : required)
        {
            bool found = false;
            for (const VkExtensionProperties& ext : available)
            {
                if (strcmp(ext.extensionName, needed) == 0) { found = true; break; }
            }
            if (!found)
            {
                VCKLog::Warn("Device", std::string("Missing required extension: ") + needed);
                return false;
            }
        }
        return true;
    }

    QueueFamilyIndices VulkanDevice::FindQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface) const
    {
        QueueFamilyIndices indices;

        uint32_t familyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &familyCount, nullptr);

        std::vector<VkQueueFamilyProperties> families(familyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &familyCount, families.data());

        // Pass 1: graphics + present.
        for (uint32_t i = 0; i < static_cast<uint32_t>(families.size()); ++i)
        {
            if (!indices.GraphicsFamily.has_value() &&
                (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT))
            {
                indices.GraphicsFamily = i;
            }

            if (!indices.PresentFamily.has_value())
            {
                VkBool32 presentSupport = VK_FALSE;
                vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);
                if (presentSupport) indices.PresentFamily = i;
            }
        }

        // Pass 2: dedicated compute (compute-capable, graphics-less).
        // Gated by cfg.device.enableDedicatedComputeQueue so callers can
        // opt out and force QueueSet::Compute() to alias graphics.
        if (m_CfgDevice.enableDedicatedComputeQueue)
        {
            for (uint32_t i = 0; i < static_cast<uint32_t>(families.size()); ++i)
            {
                const VkQueueFlags f = families[i].queueFlags;
                const bool hasCompute  = (f & VK_QUEUE_COMPUTE_BIT)  != 0;
                const bool hasGraphics = (f & VK_QUEUE_GRAPHICS_BIT) != 0;
                if (hasCompute && !hasGraphics)
                {
                    indices.ComputeFamily = i;
                    break;
                }
            }
        }

        // Pass 3: dedicated transfer (transfer-capable, graphics-less and
        // compute-less).  Per the spec GRAPHICS/COMPUTE queues implicitly
        // support transfer operations, so "dedicated transfer" means a
        // family whose only exposed capability is TRANSFER (often a DMA
        // engine).  Intel iGPUs usually don't expose one; AMD/NVIDIA do.
        if (m_CfgDevice.enableDedicatedTransferQueue)
        {
            for (uint32_t i = 0; i < static_cast<uint32_t>(families.size()); ++i)
            {
                const VkQueueFlags f = families[i].queueFlags;
                const bool hasTransfer = (f & VK_QUEUE_TRANSFER_BIT) != 0;
                const bool hasGraphics = (f & VK_QUEUE_GRAPHICS_BIT) != 0;
                const bool hasCompute  = (f & VK_QUEUE_COMPUTE_BIT)  != 0;
                if (hasTransfer && !hasGraphics && !hasCompute)
                {
                    indices.TransferFamily = i;
                    break;
                }
            }
        }

        return indices;
    }

    // ─────────────────────────────────────────────────────────────────────────────
    //  Logical device + queues
    // ─────────────────────────────────────────────────────────────────────────────

    bool VulkanDevice::CreateLogicalDevice()
    {
        // Unique queue families:  graphics + present + (optional) dedicated
        // compute + (optional) dedicated transfer.  std::set de-duplicates
        // the common case where present aliases graphics.
        std::set<uint32_t> uniqueFamilies = {
            m_QueueFamilyIndices.GraphicsFamily.value(),
            m_QueueFamilyIndices.PresentFamily.value()
        };
        if (m_QueueFamilyIndices.ComputeFamily.has_value())
            uniqueFamilies.insert(m_QueueFamilyIndices.ComputeFamily.value());
        if (m_QueueFamilyIndices.TransferFamily.has_value())
            uniqueFamilies.insert(m_QueueFamilyIndices.TransferFamily.value());

        const float queuePriority = 1.0f;
        std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
        queueCreateInfos.reserve(uniqueFamilies.size());

        for (uint32_t family : uniqueFamilies)
        {
            VkDeviceQueueCreateInfo queueInfo{};
            queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueInfo.queueFamilyIndex = family;
            queueInfo.queueCount = 1;
            queueInfo.pQueuePriorities = &queuePriority;
            queueCreateInfos.push_back(queueInfo);
        }

        VkPhysicalDeviceFeatures deviceFeatures{};

        // v0.3: timeline semaphore feature.  Queried first via
        // vkGetPhysicalDeviceFeatures2 (core Vulkan 1.1+); if the GPU
        // does not support it we simply skip enabling it and
        // HasTimelineSemaphores() reports false.  The execution layer
        // (TimelineSemaphore, FrameScheduler) gates on that flag.
        bool timelineRequested  = m_CfgDevice.enableTimelineSemaphores;
        bool timelineSupported  = false;
        if (timelineRequested)
        {
            VkPhysicalDeviceTimelineSemaphoreFeatures probeTs{};
            probeTs.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES;

            VkPhysicalDeviceFeatures2 probeF2{};
            probeF2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
            probeF2.pNext = &probeTs;

            vkGetPhysicalDeviceFeatures2(m_PhysicalDevice, &probeF2);
            timelineSupported = probeTs.timelineSemaphore == VK_TRUE;
            // (Tri-state Notice fires post-vkCreateDevice in the R23 block
            //  below; no per-feature Notice here to avoid a duplicate.)
        }

        // Theme S: VK_KHR_synchronization2 feature probe.  Same shape as
        // the timeline-semaphore path above - probe via Features2, chain
        // into pNext at vkCreateDevice time when supported, and gate the
        // hot-path codepath (VMM staging acquire-barrier, VulkanImage
        // layout transitions, FrameScheduler submit) on the resulting
        // m_Sync2Enabled flag.  When the cfg knob is off or the device
        // does not advertise the feature, hot-path code falls back to
        // vkCmdPipelineBarrier / VkSubmitInfo with no API surface change.
        bool sync2Requested  = m_CfgDevice.preferSync2;
        bool sync2Supported  = false;
        if (sync2Requested)
        {
            VkPhysicalDeviceSynchronization2Features probeSync2{};
            probeSync2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES;

            VkPhysicalDeviceFeatures2 probeF2{};
            probeF2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
            probeF2.pNext = &probeSync2;

            vkGetPhysicalDeviceFeatures2(m_PhysicalDevice, &probeF2);
            sync2Supported = probeSync2.synchronization2 == VK_TRUE;
        }

        // Theme E (E1): VK_KHR_dynamic_rendering / 1.3 core feature probe.
        // Same shape as the timeline-semaphore path above - probe via Features2,
        // chain into pNext at vkCreateDevice when supported, and gate the
        // VulkanPipeline codepath on the resulting m_DynamicRenderingEnabled
        // flag.  When the cfg knob picks Classic OR the device does not
        // advertise the feature, VulkanPipeline keeps emitting the
        // VkRenderPass + VkFramebuffer codepath with no API surface change.
        bool dynamicRenderingRequested = (m_CfgRendering.mode == RenderingMode::Dynamic);
        bool dynamicRenderingSupported = false;
        if (dynamicRenderingRequested)
        {
            VkPhysicalDeviceDynamicRenderingFeatures probeDr{};
            probeDr.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES;

            VkPhysicalDeviceFeatures2 probeF2{};
            probeF2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
            probeF2.pNext = &probeDr;

            vkGetPhysicalDeviceFeatures2(m_PhysicalDevice, &probeF2);
            dynamicRenderingSupported = probeDr.dynamicRendering == VK_TRUE;
        }


        // Build the merged extension list: required + user-supplied extras.
        std::vector<const char*> enabledExts(std::begin(k_RequiredDeviceExtensions), std::end(k_RequiredDeviceExtensions));
        for (const char* extra : m_CfgDevice.extraDeviceExtensions)
            enabledExts.push_back(extra);

        // ── Build the available-extension set once (Vulkan device-level) ─────
        // Used by the silent-bundle and cfg-gated probes below.  Rule 23: each
        // attempt logs Notice("Device", ...) so the user can grep "ext " and
        // see exactly what VCK requested + what the driver gave us back.
        std::set<std::string> availSet;
        {
            uint32_t cnt = 0;
            vkEnumerateDeviceExtensionProperties(m_PhysicalDevice, nullptr, &cnt, nullptr);
            std::vector<VkExtensionProperties> props(cnt);
            vkEnumerateDeviceExtensionProperties(m_PhysicalDevice, nullptr, &cnt, props.data());
            for (const auto& p : props) availSet.insert(p.extensionName);
        }
        auto extAvail = [&](const char* name) {
            return availSet.count(name) > 0;
        };

        // ── Silent bundle (R24: no cfg knob, internal plumbing only) ─────────
        // Each of these is enabled on-demand when the device advertises it.
        // VCK does not yet *use* the matching feature in any codepath - the
        // bundle is the prep for v0.4 (sync2 in FrameScheduler, BDA in VMM,
        // memory_budget polling in DebugTimeline, present_wait/id pacing in
        // FrameScheduler).  Today the user just sees the Notice line and can
        // start writing code that assumes the symbols are reachable.
        auto tryBundle = [&](const char* name) -> bool {
            if (!extAvail(name)) {
                VCKLog::Notice("Device", std::string("ext unavailable (bundle): ") + name);
                return false;
            }
            enabledExts.push_back(name);
            VCKLog::Notice("Device", std::string("ext enabled (bundle): ") + name);
            return true;
        };

        // Theme S: VK_KHR_synchronization2 used to live in the silent bundle.
        // It is now gated on cfg.device.preferSync2 so the user can pin to the
        // legacy 1.0 barrier path with one cfg line.  The actual extension
        // request happens in the cfg-gated block below alongside the feature
        // chain (so the Notice line is paired with the knob name, not
        // "bundle").
#ifdef VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME
        (void)tryBundle(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);
#endif
#ifdef VK_EXT_MEMORY_BUDGET_EXTENSION_NAME
        (void)tryBundle(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
#endif
#ifdef VK_EXT_DEVICE_FAULT_EXTENSION_NAME
        (void)tryBundle(VK_EXT_DEVICE_FAULT_EXTENSION_NAME);
#endif
#ifdef VK_KHR_PRESENT_WAIT_EXTENSION_NAME
        (void)tryBundle(VK_KHR_PRESENT_WAIT_EXTENSION_NAME);
#endif
#ifdef VK_KHR_PRESENT_ID_EXTENSION_NAME
        (void)tryBundle(VK_KHR_PRESENT_ID_EXTENSION_NAME);
#endif

        // ── cfg-gated extensions (R24: user-visible behaviour) ───────────────
        // The extension is requested only when the corresponding cfg knob is
        // set, with a per-knob Notice line so the user always sees both the
        // request and the driver's verdict.
        auto tryGated = [&](const char* name, const char* knob) -> bool {
            if (!extAvail(name)) {
                VCKLog::Notice("Device", std::string("ext unavailable (") + knob + "): " + name);
                return false;
            }
            enabledExts.push_back(name);
            VCKLog::Notice("Device", std::string("ext enabled (") + knob + "): " + name);
            return true;
        };

        // Theme E (E1): cfg-gated VK_KHR_dynamic_rendering request.  Mirrors
        // the Theme S sync2 pattern - only push the extension when the device
        // probe (above) said the feature is available; otherwise the gated
        // Notice would be misleading (the extension would be enabled but the
        // feature struct would not be chained, so HasDynamicRendering() would
        // still report false).  The tri-state R23 Notice (active / unavailable
        // / cfg=Classic) fires after vkCreateDevice succeeds.
        if (dynamicRenderingRequested && dynamicRenderingSupported)
        {
#ifdef VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME
            (void)tryGated(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME, "cfg.rendering.mode=Dynamic");
#endif
        }

        // Theme S: cfg-gated VK_KHR_synchronization2 request.  We only push
        // the extension when the device probe (above) said the feature is
        // available; otherwise the gated Notice would be misleading (the
        // extension would be enabled but the feature struct would not be
        // chained, so HasSynchronization2() would still report false).
        if (sync2Requested && sync2Supported)
        {
#ifdef VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME
            (void)tryGated(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME, "cfg.device.preferSync2");
#endif
        }

        if (m_CfgDevice.enableBindless)
        {
#ifdef VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME
            (void)tryGated(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME, "cfg.device.enableBindless");
#endif
            // R23 fallback path: bindless descriptor helpers (DescriptorPool::
            // AddBindlessSet, VulkanPipeline::EnableBindless, ...) ship in
            // v0.4.  Today the extension is enabled but the public API surface
            // is unchanged, so the user can probe support without having a
            // codepath to call into yet.
            VCKLog::Notice("Device",
                "cfg.device.enableBindless acknowledged - bindless descriptor helpers ship in v0.4; extension enabled, no public API surface yet (R23)");
        }

        // R24 cfg knob (continued from VulkanSwapchain): cfg.swapchain.presentMode
        // = FifoLatestReady requires the VK_EXT_present_mode_fifo_latest_ready
        // device extension to be enabled at vkCreateDevice time, otherwise
        // vkGetPhysicalDeviceSurfacePresentModesKHR will never advertise
        // VK_PRESENT_MODE_FIFO_LATEST_READY_EXT and the swapchain will silently
        // fall back to FIFO regardless of cfg.  Wire it here so the swapchain
        // case is genuinely reachable.
        if (m_CfgSwapchain.presentMode == PresentMode::FifoLatestReady)
        {
#ifdef VK_EXT_PRESENT_MODE_FIFO_LATEST_READY_EXTENSION_NAME
            (void)tryGated(VK_EXT_PRESENT_MODE_FIFO_LATEST_READY_EXTENSION_NAME,
                           "cfg.swapchain.presentMode=FifoLatestReady");
#endif
        }

        VkDeviceCreateInfo deviceInfo{};
        deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        deviceInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
        deviceInfo.pQueueCreateInfos = queueCreateInfos.data();
        deviceInfo.enabledExtensionCount = static_cast<uint32_t>(enabledExts.size());
        deviceInfo.ppEnabledExtensionNames = enabledExts.data();

        // Feature chain (Features2 + per-feature pNext extensions).  Whenever
        // any feature struct must be enabled at vkCreateDevice we route
        // everything through VkPhysicalDeviceFeatures2 and leave
        // pEnabledFeatures = nullptr (Vulkan spec requirement when features2
        // is in pNext).  When no extra feature is requested we keep the
        // legacy pEnabledFeatures path untouched for binary stability with
        // pre-Theme-S setups.  Currently chained features:
        //   - VkPhysicalDeviceTimelineSemaphoreFeatures (v0.3)
        //   - VkPhysicalDeviceSynchronization2Features  (Theme S - S1)
        //   - VkPhysicalDeviceDynamicRenderingFeatures  (Theme E - E1)
        VkPhysicalDeviceTimelineSemaphoreFeatures tsFeatures{};
        VkPhysicalDeviceSynchronization2Features  sync2Features{};
        VkPhysicalDeviceDynamicRenderingFeatures  drFeatures{};
        VkPhysicalDeviceFeatures2                 features2{};
        const bool useFeatures2 =
            (timelineRequested         && timelineSupported)         ||
            (sync2Requested            && sync2Supported)            ||
            (dynamicRenderingRequested && dynamicRenderingSupported);
        if (useFeatures2)
        {
            features2.sType    = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
            features2.features = deviceFeatures;
            void** chainTail   = &features2.pNext;

            if (timelineRequested && timelineSupported)
            {
                tsFeatures.sType             = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES;
                tsFeatures.timelineSemaphore = VK_TRUE;
                *chainTail = &tsFeatures;
                chainTail  = &tsFeatures.pNext;
            }
            if (sync2Requested && sync2Supported)
            {
                sync2Features.sType            = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES;
                sync2Features.synchronization2 = VK_TRUE;
                *chainTail = &sync2Features;
                chainTail  = &sync2Features.pNext;
            }
            if (dynamicRenderingRequested && dynamicRenderingSupported)
            {
                drFeatures.sType            = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES;
                drFeatures.dynamicRendering = VK_TRUE;
                *chainTail = &drFeatures;
                chainTail  = &drFeatures.pNext;
            }

            deviceInfo.pNext            = &features2;
            deviceInfo.pEnabledFeatures = nullptr;  // must be null with features2
        }
        else
        {
            deviceInfo.pEnabledFeatures = &deviceFeatures;
        }

        if (!VK_OK(vkCreateDevice(m_PhysicalDevice, &deviceInfo, nullptr, &m_LogicalDevice)))
        {
            VCKLog::Warn("Device", "vkCreateDevice failed");
            return false;
        }

        m_TimelineSemaphoresEnabled = timelineRequested         && timelineSupported;
        m_Sync2Enabled              = sync2Requested            && sync2Supported;
        m_DynamicRenderingEnabled   = dynamicRenderingRequested && dynamicRenderingSupported;

        vkGetDeviceQueue(m_LogicalDevice, m_QueueFamilyIndices.GraphicsFamily.value(), 0, &m_GraphicsQueue);
        vkGetDeviceQueue(m_LogicalDevice, m_QueueFamilyIndices.PresentFamily.value(),  0, &m_PresentQueue);
        if (m_QueueFamilyIndices.ComputeFamily.has_value())
            vkGetDeviceQueue(m_LogicalDevice, m_QueueFamilyIndices.ComputeFamily.value(),  0, &m_ComputeQueue);
        if (m_QueueFamilyIndices.TransferFamily.has_value())
            vkGetDeviceQueue(m_LogicalDevice, m_QueueFamilyIndices.TransferFamily.value(), 0, &m_TransferQueue);

        // Build a structured log line summarising what actually got picked,
        // so the user can tell at a glance whether they got dedicated queues
        // or not (rule 6 - no hidden behaviour).
        std::string queueSummary =
            "graphics: " + std::to_string(m_QueueFamilyIndices.GraphicsFamily.value()) +
            " | present: " + std::to_string(m_QueueFamilyIndices.PresentFamily.value()) +
            (m_QueueFamilyIndices.IsCombined() ? " (combined)" : " (separate)");
        if (m_QueueFamilyIndices.ComputeFamily.has_value())
            queueSummary += " | compute(dedicated): " + std::to_string(m_QueueFamilyIndices.ComputeFamily.value());
        else
            queueSummary += " | compute: aliased to graphics";
        if (m_QueueFamilyIndices.TransferFamily.has_value())
            queueSummary += " | transfer(dedicated): " + std::to_string(m_QueueFamilyIndices.TransferFamily.value());
        else
            queueSummary += " | transfer: aliased to graphics";

        VCKLog::Notice("Device", "Queues OK - " + queueSummary);

        // Rule 23: every device extension VCK enabled is announced by name,
        // labelled with where the request came from (required vs cfg) so the
        // user can grep the log to see exactly what's bound to the device.
        for (const char* ext : k_RequiredDeviceExtensions)
            VCKLog::Notice("Device", std::string("ext enabled (required): ") + ext);
        for (const char* extra : m_CfgDevice.extraDeviceExtensions)
            VCKLog::Notice("Device", std::string("ext enabled (cfg.extraDeviceExtensions): ") + extra);

        // Rule 23: timeline-semaphore decision is announced including the
        // fallback path (per-slot fences) when the GPU does not support it.
        if (m_TimelineSemaphoresEnabled) {
            VCKLog::Notice("Device", "feature enabled: VK_KHR_timeline_semaphore (cfg.device.enableTimelineSemaphores)");
        } else if (timelineRequested) {
            VCKLog::Notice("Device", "feature unavailable: VK_KHR_timeline_semaphore - FrameScheduler will use per-slot fences");
        } else {
            VCKLog::Notice("Device", "feature disabled by cfg: VK_KHR_timeline_semaphore - FrameScheduler will use per-slot fences");
        }

        // Rule 23: Theme S synchronization2 decision announced same shape as
        // the timeline path - hot-path code (VMM acquire-barrier, image layout
        // transitions, FrameScheduler submit) gates on HasSynchronization2()
        // and falls back to vkCmdPipelineBarrier / VkSubmitInfo when off.
        if (m_Sync2Enabled) {
            VCKLog::Notice("Device", "feature enabled: VK_KHR_synchronization2 (cfg.device.preferSync2)");
        } else if (sync2Requested) {
            VCKLog::Notice("Device", "feature unavailable: VK_KHR_synchronization2 - hot-path uses vkCmdPipelineBarrier / VkSubmitInfo fallback");
        } else {
            VCKLog::Notice("Device", "feature disabled by cfg: VK_KHR_synchronization2 - hot-path uses vkCmdPipelineBarrier / VkSubmitInfo fallback");
        }

        // Rule 23: Theme E (E1) dynamic-rendering decision announced same shape
        // as the timeline path - VulkanPipeline gates on HasDynamicRendering()
        // and falls back to Classic VkRenderPass + VkFramebuffer when off.
        if (m_DynamicRenderingEnabled) {
            VCKLog::Notice("Device", "feature enabled: VK_KHR_dynamic_rendering (cfg.rendering.mode=Dynamic)");
        } else if (dynamicRenderingRequested) {
            VCKLog::Notice("Device", "feature unavailable: VK_KHR_dynamic_rendering - VulkanPipeline falls back to VkRenderPass + VkFramebuffer");
        } else {
            VCKLog::Notice("Device", "cfg.rendering.mode=Classic - VulkanPipeline emits VkRenderPass + VkFramebuffer");
        }

        return true;
    }

    // ─────────────────────────────────────────────────────────────────────────────
    //  VMA allocator
    // ─────────────────────────────────────────────────────────────────────────────

    bool VulkanDevice::CreateAllocator(VkInstance instance)
    {
        VmaAllocatorCreateInfo allocatorInfo{};
        allocatorInfo.physicalDevice = m_PhysicalDevice;
        allocatorInfo.device = m_LogicalDevice;
        allocatorInfo.instance = instance;
        allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_2;

        if (!VK_OK(vmaCreateAllocator(&allocatorInfo, &m_Allocator)))
        {
            VCKLog::Warn("Device", "vmaCreateAllocator failed");
            return false;
        }
        VCKLog::Info("Device", "VMA allocator ready");
        return true;
    }

    // ─────────────────────────────────────────────────────────────────────────────
    //  Swapchain support query (called by VulkanSwapchain)
    // ─────────────────────────────────────────────────────────────────────────────

    VulkanDevice::SwapchainSupportDetails VulkanDevice::QuerySwapchainSupport(VkSurfaceKHR surface) const
    {
        SwapchainSupportDetails details;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_PhysicalDevice, surface, &details.Capabilities);

        uint32_t formatCount = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(m_PhysicalDevice, surface, &formatCount, nullptr);
        if (formatCount > 0)
        {
            details.Formats.resize(formatCount);
            vkGetPhysicalDeviceSurfaceFormatsKHR(m_PhysicalDevice, surface, &formatCount, details.Formats.data());
        }

        uint32_t presentModeCount = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(m_PhysicalDevice, surface, &presentModeCount, nullptr);
        if (presentModeCount > 0)
        {
            details.PresentModes.resize(presentModeCount);
            vkGetPhysicalDeviceSurfacePresentModesKHR(m_PhysicalDevice, surface, &presentModeCount, details.PresentModes.data());
        }

        return details;
    }

} // namespace VCK