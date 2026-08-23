#include "test_framework.h"
#include "tests.h"

#include <cstdlib>
#include <iostream>

int main() {
    using namespace anyadance::tests;

    TestMath3d();
    TestProtocol();
    TestFrameState();
    TestSafety();
    TestTPose();
    TestInput();
    TestManipulation();
    TestFingerBend();
    TestFingerGrip();
    TestApplyDanceFingerBends();
    TestDriverLogProtocol();
    TestLog();
    TestJson();
    TestMmdParse();
    TestMmdRetarget();
    TestNya();
    TestUiLayout();
    TestLocalization();
    TestDanceTransport();
    TestAudioPlayer();

    const int failures = anyadance::testing::Failures();
    if (failures != 0) {
        std::cerr << failures << " test failure(s)\n";
        return EXIT_FAILURE;
    }
    std::cout << "All AnyaDance tests passed\n";
    return EXIT_SUCCESS;
}
