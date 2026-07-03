#pragma once

// Every test case lives in its own translation unit (test_<module>.cpp) and is
// declared here so main() can run them all. Add a case by declaring it here and
// listing it in tests/main.cpp and CMakeLists.txt.
namespace anyadance::tests {

void TestMath3d();
void TestProtocol();
void TestFrameState();
void TestSafety();
void TestTPose();
void TestInput();
void TestManipulation();
void TestFingerBend();
void TestFingerGrip();
void TestApplyDanceFingerBends();
void TestLog();
void TestJson();
void TestMmdParse();
void TestMmdRetarget();
void TestNya();
void TestUiLayout();

} // namespace anyadance::tests
