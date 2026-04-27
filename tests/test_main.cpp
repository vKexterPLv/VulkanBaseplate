// VCK R14 unit-test harness entry point.
//
// All test cases register themselves via the TEST(...) macro at static-init
// time (see vck_test.h).  main() just runs them and forwards the failed-count
// as the exit code so CI flips red on regression.

#include "vck_test.h"

int main(int argc, char** argv)
{
    return VCK::Test::RunAll(argc, argv);
}
