// =============================================================================
//  test_main.cpp  —  VCK test harness entry point
//
//  All test cases self-register via TEST(...) at static-init time.
//  RunAll() iterates the registry, runs each case, prints pass/fail,
//  and returns non-zero if anything failed so CI flips red on regression.
//
//  Test files and what rule/category they cover:
//
//    EXISTING (logging contract):
//      test_log_emit.cpp              LogSink behaviour
//      test_log_dedup.cpp             Dedup screen vs sink
//      test_vk_check.cpp              R14 — VK_CHECK contract
//      test_legacy_logvk.cpp          Legacy LogVk shim
//
//    RULE-BASED (added in rebuild):
//      test_r1_r6_r10_r24_explicitness.cpp   Category I  — Explicitness
//      test_r14_reliability.cpp              Category V  — R14 deep
//      test_r19_r15_r16_cost_scope.cpp       Category IV — Cost & Scope
//                                            (VertexLayout, PushConstants,
//                                             Primitives without GPU)
//      test_vckmath.cpp                      VCKMath correctness (R19)
//      test_r23_r24_transparency.cpp         Category VI — R23, R24
//      test_r11_r12_reliability.cpp          Category V  — R11, R12
// =============================================================================

#include "vck_test.h"

int main(int argc, char** argv)
{
    return VCK::Test::RunAll(argc, argv);
}
