# 불완전 타입 진단 시험 — 서술을 쓰는 자리(등록 함수)에서 필드가 가리키는 타입이 불완전하면 컴파일이 reflgen 의
# 메시지로 멈추는가. 기본 빌드에서 뺀 target 을 빌드해 실패와 메시지를 본다. 메시지는 첫 오류여야 한다 — 표준
# 라이브러리의 형식 특성 오류(불완전한 클래스에 std::is_polymorphic)가 먼저 나오면 원인을 찾기 어렵다.
#
#   cmake -DBINARY_DIR=<build dir> -DTARGET=<target> -DEXPECTED=<regex> [-DCONFIG=<config>] -P check_incomplete.cmake

set(command "${CMAKE_COMMAND}" --build "${BINARY_DIR}" --target "${TARGET}")
if(CONFIG)
    list(APPEND command --config "${CONFIG}")
endif()
execute_process(COMMAND ${command} RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE output)

if(result EQUAL 0)
    message(FATAL_ERROR "${TARGET} compiled although its registration function uses an incomplete type")
endif()
if(NOT output MATCHES "${EXPECTED}")
    message(FATAL_ERROR "the build failed without reflgen's incomplete-type message:\n${output}")
endif()

# 첫 오류 줄 — MSVC 는 "error C", clang-cl 은 "error:" 로 적는다.
string(REGEX MATCH "[^\n]*(error C[0-9]+|error:)[^\n]*" first_error "${output}")
if(NOT first_error MATCHES "${EXPECTED}")
    message(FATAL_ERROR "reflgen's message is not the first error:\n${first_error}\n\n${output}")
endif()
