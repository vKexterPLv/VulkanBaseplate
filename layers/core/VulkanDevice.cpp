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
        return Initialize(instance, surface);
    }

    bool VulkanDevice::Initialize(VkInstance instance, VkSurfaceKHR surface)
    {
        VCKLog::Info("Device", "Selecting physical device...");
        if (!PickPhysicalDevice(instance, surface))
        {
            VCKLog::Error("Device", "No suitable GPU found");
            return false;
        }

        VCKLog::Info("Device", "Creating logical device...");
        if (!CreateLogicalDevice())
        {
            VCKLog::Error("Device", "Logical device creation failed");
            return false;
        }

        VCKLog::Info("Device", "Creating VMA allocator...");
        if (!CreateAllocator(instance))
        {
            VCKLog::Error("Device", "VMA allocator creation failed");
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
            VCKLog::Error("Device", "No Vulkan-capable GPUs found");
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
            return false;

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
                VCKLog::Error("Device", std::string("Missing required extension: ") + needed);
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

#ifdef VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME
        (void)tryBundle(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
#endif
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

        if (m_CfgRendering.mode == RenderingMode::Dynamic)
        {
#ifdef VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME
            (void)tryGated(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME, "cfg.rendering.mode=Dynamic");
#endif
            // R23 fallback path: today VulkanPipeline / Frame still emit the
            // Classic VkRenderPass + VkFramebuffer codepath.  The Dynamic
            // codepath (vkCmdBeginRendering, on-the-fly attachment description)
            // ships in v0.4; until then the extension is enabled but VCK still
            // renders Classic to keep behaviour identical for early adopters.
            VCKLog::Notice("Device",
                "cfg.rendering.mode=Dynamic acknowledged - dynamic rendering codepath ships in v0.4; rendering falls back to Classic (R23)");
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

        VkDeviceCreateInfo deviceInfo{};
        deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        deviceInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
        deviceInfo.pQueueCreateInfos = queueCreateInfos.data();
        deviceInfo.enabledExtensionCount = static_cast<uint32_t>(enabledExts.size());
        deviceInfo.ppEnabledExtensionNames = enabledExts.data();

        // Timeline semaphore feature chain.  When enabled, the feature struct
        // must live in pNext and pEnabledFeatures must be nullptr (use the
        // VkPhysicalDeviceFeatures2 path instead).  When disabled, keep the
        // legacy pEnabledFeatures path untouched (stable for existing setups).
        VkPhysicalDeviceTimelineSemaphoreFeatures tsFeatures{};
        VkPhysicalDeviceFeatures2                 features2{};
        if (timelineRequested && timelineSupported)
        {
            tsFeatures.sType             = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES;
            tsFeatures.timelineSemaphore = VK_TRUE;

            features2.sType    = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
            features2.features = deviceFeatures;
            features2.pNext    = &tsFeatures;

            deviceInfo.pNext            = &features2;
            deviceInfo.pEnabledFeatures = nullptr;  // must be null with features2
        }
        else
        {
            deviceInfo.pEnabledFeatures = &deviceFeatures;
        }

        VK_CHECK(vkCreateDevice(m_PhysicalDevice, &deviceInfo, nullptr, &m_LogicalDevice));

        m_TimelineSemaphoresEnabled = timelineRequested && timelineSupported;

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

        VK_CHECK(vmaCreateAllocator(&allocatorInfo, &m_Allocator));
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