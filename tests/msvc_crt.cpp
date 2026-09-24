// MSVC 디버그 CRT 의 단언 실패를 대화상자 대신 stderr 로 보낸다. 대화상자는 무인
// 실행(CI, ctest)을 영원히 멈춰 세운다. MSVC 계열 빌드에서만 CMake 가 이 파일을 넣는다
// — 전처리기로 컴파일러를 가르지 않는다는 원칙을 테스트에도 지킨다.
#include <crtdbg.h>
#include <cstdlib>

namespace
{
const bool crt_reports_redirected = [] {
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    return true;
}();
} // namespace
