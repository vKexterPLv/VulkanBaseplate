#include "VulkanDevice.h"
#include "VulkanHelpers.h"

#include <set>
#include <algorithm>

namespace VulkanBaseplate {

    // ─────────────────────────────────────────────────────────────────────────────
    //  Public API
    // ─────────────────────────────────────────────────────────────────────────────

    bool VulkanDevice::Initialize(VkInstance instance, VkSurfaceKHR surface)
    {
        LogVk("[Device] Selecting physical device...");
        if (!PickPhysicalDevice(instance, surface))
        {
            LogVk("[Device] ERROR — no suitable GPU found");
            return false;
        }

        LogVk("[Device] Creating logical device...");
        if (!CreateLogicalDevice())
        {
            LogVk("[Device] ERROR — logical device creation failed");
            return false;
        }

        LogVk("[Device] Creating VMA allocator...");
        if (!CreateAllocator(instance))
        {
            LogVk("[Device] ERROR — VMA allocator creation failed");
            return false;
        }

        LogVk("[Device] Initialized OK");
        return true;
    }

    void VulkanDevice::Shutdown()
    {
        if (m_Allocator != VK_NULL_HANDLE)
        {
            vmaDestroyAllocator(m_Allocator);
            m_Allocator = VK_NULL_HANDLE;
            LogVk("[Device] VMA allocator destroyed");
        }

        if (m_LogicalDevice != VK_NULL_HANDLE)
        {
            vkDestroyDevice(m_LogicalDevice, nullptr);
            m_LogicalDevice = VK_NULL_HANDLE;
            m_GraphicsQueue = VK_NULL_HANDLE;
            m_PresentQueue = VK_NULL_HANDLE;
            LogVk("[Device] Logical device destroyed");
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
            LogVk("[Device] No Vulkan-capable GPUs found");
            return false;
        }

        std::vector<VkPhysicalDevice> devices(deviceCount);
        vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

        LogVk("[Device] Found " + std::to_string(deviceCount) + " GPU(s):");

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

            LogVk(std::string("  [") + properties.deviceName + "] " +
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
        LogVk(std::string("[Device] Selected: ") + chosen.deviceName);

        return true;
    }

    int VulkanDevice::ScorePhysicalDevice(VkPhysicalDevice device, VkSurfaceKHR surface) const
    {
        if (!IsPhysicalDeviceSuitable(device, surface))
            return -1;

        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(device, &properties);

        int score = 0;
        if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
            score += 10000;
        else if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
            score += 1000;

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

        for (const char* required : k_RequiredDeviceExtensions)
        {
            bool found = false;
            for (const VkExtensionProperties& ext : available)
            {
                if (strcmp(ext.extensionName, required) == 0) { found = true; break; }
            }
            if (!found)
            {
                LogVk(std::string("[Device] Missing required extension: ") + required);
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

        for (uint32_t i = 0; i < static_cast<uint32_t>(families.size()); ++i)
        {
            if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
                indices.GraphicsFamily = i;

            VkBool32 presentSupport = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);
            if (presentSupport)
                indices.PresentFamily = i;

            if (indices.IsCombined())
                break;
        }
        return indices;
    }

    // ─────────────────────────────────────────────────────────────────────────────
    //  Logical device + queues
    // ─────────────────────────────────────────────────────────────────────────────

    bool VulkanDevice::CreateLogicalDevice()
    {
        std::set<uint32_t> uniqueFamilies = {
            m_QueueFamilyIndices.GraphicsFamily.value(),
            m_QueueFamilyIndices.PresentFamily.value()
        };

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

        VkDeviceCreateInfo deviceInfo{};
        deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        deviceInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
        deviceInfo.pQueueCreateInfos = queueCreateInfos.data();
        deviceInfo.enabledExtensionCount = static_cast<uint32_t>(std::size(k_RequiredDeviceExtensions));
        deviceInfo.ppEnabledExtensionNames = k_RequiredDeviceExtensions;
        deviceInfo.pEnabledFeatures = &deviceFeatures;

        VK_CHECK(vkCreateDevice(m_PhysicalDevice, &deviceInfo, nullptr, &m_LogicalDevice));

        vkGetDeviceQueue(m_LogicalDevice, m_QueueFamilyIndices.GraphicsFamily.value(), 0, &m_GraphicsQueue);
        vkGetDeviceQueue(m_LogicalDevice, m_QueueFamilyIndices.PresentFamily.value(), 0, &m_PresentQueue);

        LogVk("[Device] Queues OK — graphics: " +
            std::to_string(m_QueueFamilyIndices.GraphicsFamily.value()) +
            " | present: " +
            std::to_string(m_QueueFamilyIndices.PresentFamily.value()) +
            (m_QueueFamilyIndices.IsCombined() ? " (combined)" : " (separate)"));

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
        LogVk("[Device] VMA allocator ready");
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

} // namespace VulkanBaseplate