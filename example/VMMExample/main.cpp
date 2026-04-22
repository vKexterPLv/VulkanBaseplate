#include "App.h"
#include "VulkanModule.h"   // VulkanBaseplate_EnsureConsole + LogVk

int main()
{
    // Attach / allocate a Windows console and point stdio at it so LogVk
    // output is visible in a real console window (not just the VS Output
    // pane via OutputDebugStringA).
    VulkanBaseplate_EnsureConsole();

    LogVk("VMMExample starting...");
    VulkanBaseplate::VMMExample::Run();
    LogVk("VMMExample exited cleanly");
    return 0;
}
