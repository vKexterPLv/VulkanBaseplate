// =============================================================================
//  test_a5_shader_tooling_r14.cpp
//
//  Theme A · A5 (shader-tooling sweep) R14 / R19 contract tests.
//
//  Pre-A5 the shader-tooling layer had:
//
//    - ReadSpirvFile (anonymous-namespace helper) hard-coded the
//      "ShaderLoader" subsystem tag, so an I/O failure inside
//      ShaderWatcher::Reload produced one Error tagged "ShaderLoader"
//      AND one Error tagged "ShaderWatcher" - a double-log per R14
//      that also sent the user looking in the wrong subsystem when
//      grepping logs.  Post-A5 the helper takes a tag parameter and
//      ShaderWatcher::Reload demotes its wrapping log to Notice.
//
//    - MergePush only flagged size mismatches; same-sized but
//      layout-divergent push-constant declarations were silently
//      collapsed to the first stage's bytes.  Post-A5 the merge also
//      compares the VkPushConstantRange offset/size tuple and warns
//      on layout divergence.
//
//  These tests pin the post-fix R14 / R19 contract on the
//  shader-tooling public types without requiring a real VkDevice
//  or a glslangValidator install (R16 - zero third-party test deps).
// =============================================================================
#include "vck_test.h"
#include "vck_log_capture.h"
#include "VCK.h"
#include "VCKExpansion.h"

#include <cstdio>
#include <filesystem>
#include <string>


// ---------------------------------------------------------------------------
//  Default-construct silence (R19 - no allocation, no log)
// ---------------------------------------------------------------------------
TEST(A5_ShaderTooling_R14, shader_loader_default_construct_silent)
{
    VCK::Test::LogCapture cap;
    VCK::ShaderLoader sl;
    ASSERT_EQ(cap.entries.size(), static_cast<std::size_t>(0));
}

TEST(A5_ShaderTooling_R14, shader_watcher_default_construct_silent)
{
    VCK::Test::LogCapture cap;
    VCK::ShaderWatcher sw;
    ASSERT_EQ(cap.entries.size(), static_cast<std::size_t>(0));
}

TEST(A5_ShaderTooling_R14, spec_constants_default_construct_silent)
{
    VCK::Test::LogCapture cap;
    VCK::SpecConstants sc;
    ASSERT_EQ(cap.entries.size(), static_cast<std::size_t>(0));
}

TEST(A5_ShaderTooling_R14, shader_stage_default_construct_silent)
{
    VCK::Test::LogCapture cap;
    VCK::ShaderStage st(VK_SHADER_STAGE_VERTEX_BIT);
    ASSERT_EQ(cap.entries.size(), static_cast<std::size_t>(0));
}

TEST(A5_ShaderTooling_R14, shader_interface_default_construct_silent)
{
    VCK::Test::LogCapture cap;
    VCK::ShaderInterface si{};
    ASSERT_EQ(cap.entries.size(), static_cast<std::size_t>(0));
}


// ---------------------------------------------------------------------------
//  ShaderLoader / ShaderWatcher Clear / Shutdown idempotency on
//  default-constructed objects (R19): zero log lines, repeatable.
// ---------------------------------------------------------------------------
TEST(A5_ShaderTooling_R14, shader_loader_clear_uninitialized_silent)
{
    VCK::Test::LogCapture cap;
    VCK::ShaderLoader sl;
    sl.Clear();
    sl.Clear();
    sl.Clear();
    ASSERT_EQ(cap.entries.size(), static_cast<std::size_t>(0));
}

TEST(A5_ShaderTooling_R14, shader_watcher_shutdown_uninitialized_silent)
{
    VCK::Test::LogCapture cap;
    VCK::ShaderWatcher sw;
    sw.Shutdown();
    sw.Shutdown();
    ASSERT_EQ(cap.entries.size(), static_cast<std::size_t>(0));
}

TEST(A5_ShaderTooling_R14, spec_constants_clear_uninitialized_silent)
{
    VCK::Test::LogCapture cap;
    VCK::SpecConstants sc;
    sc.Clear();
    sc.Clear();
    ASSERT_EQ(cap.entries.size(), static_cast<std::size_t>(0));
}


// ---------------------------------------------------------------------------
//  R14 single-Error contracts on the failure paths reachable without a
//  real VkDevice.
// ---------------------------------------------------------------------------
TEST(A5_ShaderTooling_R14, shader_loader_load_unsupported_stage_logs_one_error)
{
    VCK::Test::LogCapture cap;
    VCK::ShaderLoader sl;

    // Compute is unsupported by the (vert|frag)-only LoadFromFile.
    const bool ok = sl.LoadFromFile("does-not-matter.spv",
                                    VK_SHADER_STAGE_COMPUTE_BIT);
    ASSERT_EQ(ok, false);

    // Single subsystem-tagged Error, no double-log.
    ASSERT_EQ(cap.errors().size(), static_cast<std::size_t>(1));
    ASSERT_EQ(cap.errors()[0].tag, std::string("ShaderLoader"));
}

TEST(A5_ShaderTooling_R14, shader_loader_load_missing_file_logs_one_error)
{
    VCK::Test::LogCapture cap;
    VCK::ShaderLoader sl;

    const bool ok = sl.LoadFromFile(
        "/this/path/definitely/does/not/exist.spv",
        VK_SHADER_STAGE_VERTEX_BIT);
    ASSERT_EQ(ok, false);

    // Exactly one subsystem-tagged Error from ReadSpirvFile.
    ASSERT_EQ(cap.errors().size(), static_cast<std::size_t>(1));
    ASSERT_EQ(cap.errors()[0].tag, std::string("ShaderLoader"));
}

TEST(A5_ShaderTooling_R14, shader_watcher_watch_missing_file_logs_one_error)
{
    VCK::Test::LogCapture cap;
    VCK::ShaderWatcher sw;

    const bool ok = sw.Watch(
        "/this/path/definitely/does/not/exist.spv",
        VK_SHADER_STAGE_VERTEX_BIT);
    ASSERT_EQ(ok, false);

    // Exactly one subsystem-tagged Error, tagged "ShaderWatcher" (not
    // "ShaderLoader" - that's the A5 cross-subsystem fix).
    ASSERT_EQ(cap.errors().size(), static_cast<std::size_t>(1));
    ASSERT_EQ(cap.errors()[0].tag, std::string("ShaderWatcher"));
}

TEST(A5_ShaderTooling_R14, shader_watcher_reload_uses_watcher_tag)
{
    // A5 contract: an I/O failure inside the shared ReadSpirvFile helper,
    // when called from ShaderWatcher::Reload, must be tagged
    // "ShaderWatcher" (the caller), not "ShaderLoader" (the file the
    // helper happens to live in).  Pre-A5 the tag was hard-coded to
    // "ShaderLoader" and Reload then added its own Error -> double-log.

    namespace fs = std::filesystem;
    const fs::path tmp = fs::temp_directory_path() /
        ("vck_a5_watcher_test." +
         std::to_string(static_cast<unsigned long long>(
             std::chrono::steady_clock::now().time_since_epoch().count())) +
         ".spv");

    // Create an empty file - ReadSpirvFile rejects byte count <= 0.
    {
        std::FILE* f = std::fopen(tmp.string().c_str(), "wb");
        ASSERT_NE(f, static_cast<std::FILE*>(nullptr));
        std::fclose(f);
    }

    VCK::ShaderWatcher sw;
    ASSERT_EQ(sw.Watch(tmp.string(), VK_SHADER_STAGE_VERTEX_BIT), true);

    {
        VCK::Test::LogCapture cap;
        const bool ok = sw.Reload();
        ASSERT_EQ(ok, false);

        // Pre-A5: 2 Errors (one "ShaderLoader" + one "ShaderWatcher").
        // Post-A5: 1 Error tagged "ShaderWatcher", plus a Notice from
        // Reload's wrapping context.
        ASSERT_EQ(cap.errors().size(), static_cast<std::size_t>(1));
        ASSERT_EQ(cap.errors()[0].tag, std::string("ShaderWatcher"));
    }

    std::error_code ec;
    fs::remove(tmp, ec);
    sw.Shutdown();
}


// ---------------------------------------------------------------------------
//  SpecConstants R14: GetInfo() on an empty SpecConstants returns
//  nullptr (the contract Vulkan expects when no specialization info is
//  needed) AND emits zero log lines (R19).
// ---------------------------------------------------------------------------
TEST(A5_ShaderTooling_R14, spec_constants_empty_get_info_returns_null_silent)
{
    VCK::Test::LogCapture cap;
    VCK::SpecConstants sc;
    ASSERT_EQ(sc.GetInfo(), static_cast<const VkSpecializationInfo*>(nullptr));
    ASSERT_EQ(cap.entries.size(), static_cast<std::size_t>(0));
}

TEST(A5_ShaderTooling_R14, spec_constants_set_then_clear_silent)
{
    VCK::Test::LogCapture cap;
    VCK::SpecConstants sc;
    sc.Set(0u, static_cast<uint32_t>(123u));
    sc.Set(1u, 4.5f);
    sc.Set(2u, true);
    sc.Clear();
    // None of the typed Set or Clear variants should ever log.
    ASSERT_EQ(cap.entries.size(), static_cast<std::size_t>(0));
}
