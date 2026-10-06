# 선택적 interop CLI 통합 시험. CTest 실행 때만 생성하며 컴파일·실행·배포하지 않는다.
# cmake -DGENERATOR=<reflgen> -DHEADER=<interop_types.h> -DWORK=<dir> -P check_interop.cmake
cmake_minimum_required(VERSION 3.25)

function(expect_text text fragment)
    string(FIND "${text}" "${fragment}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "expected '${fragment}' in:\n${text}")
    endif()
endfunction()

function(reject_text text fragment)
    string(FIND "${text}" "${fragment}" position)
    if(NOT position EQUAL -1)
        message(FATAL_ERROR "unexpected '${fragment}' in:\n${text}")
    endif()
endfunction()

function(run_generator)
    execute_process(
        COMMAND "${GENERATOR}" --module export_test --output "${output_dir}" ${ARGN} "${fixture}"
                -- --target=x86_64-unknown-linux-gnu
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE output)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "interop generation failed:\n${output}")
    endif()
endfunction()

file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}/source")
set(fixture "${WORK}/source/interop_types.h")
file(READ "${HEADER}" fixture_text)
file(WRITE "${fixture}" "${fixture_text}")
set(output_dir "${WORK}/output")
set(manifest "${WORK}/declarations.json")
set(base "${output_dir}/reflgen_export_test")

# opt-in 이전에는 reflection 만 생긴다. 생성할 ABI 대상은 SDK 가 필요 없는 알려진 64-bit target 으로 고정한다.
run_generator(--declarations-json "${manifest}")
file(GLOB unexpected "${output_dir}/*_interop.*")
if(unexpected)
    message(FATAL_ERROR "interop files were generated without --interop: ${unexpected}")
endif()
set(reflection_files
    "${output_dir}/interop_types.reflgen.h"
    "${base}.h"
    "${base}.cpp"
    "${base}.attributes.tsv"
    "${manifest}")
foreach(path IN LISTS reflection_files)
    file(SHA256 "${path}" hash)
    set("before_${path}" "${hash}")
endforeach()
run_generator(--interop --interop-library export_native --interop-namespace Interop.Tests
              --declarations-json "${manifest}")
foreach(path IN LISTS reflection_files)
    file(SHA256 "${path}" hash)
    if(NOT hash STREQUAL "${before_${path}}")
        message(FATAL_ERROR "enabling interop changed reflection output ${path}")
    endif()
endforeach()
foreach(extension h cpp cs)
    if(NOT EXISTS "${base}_interop.${extension}")
        message(FATAL_ERROR "missing interop output ${base}_interop.${extension}")
    endif()
endforeach()
file(READ "${base}_interop.h" c_header)
file(READ "${base}_interop.cpp" cpp)
file(READ "${base}_interop.cs" csharp)
set(prefix "reflgen_export_test_interop_tests")
foreach(method value set_value toggle scale constant unsigned_value)
    expect_text("${c_header}" "${prefix}_counter_${method}(")
    expect_text("${cpp}" "->${method})(")
    expect_text("${csharp}" "EntryPoint = \"${prefix}_counter_${method}\"")
endforeach()
expect_text("${c_header}" "${prefix}_counter_native_only(")
expect_text("${c_header}" "${prefix}_native_counter_native_value(")
reject_text("${csharp}" "${prefix}_counter_native_only")
reject_text("${csharp}" "${prefix}_native_counter")
expect_text("${c_header}" "typedef struct ${prefix}_counter_handle")
expect_text("${c_header}" "uint8_t argument_0, uint8_t* result")
expect_text("${cpp}" "receiver == nullptr || result == nullptr")
expect_text("${cpp}" "catch (...)")
expect_text("${cpp}" "argument_0 != 0")
expect_text("${cpp}" "value ? 1 : 0")
expect_text("${csharp}" "namespace @Interop.@Tests")
expect_text("${csharp}" "\"export_native\"")
expect_text("${csharp}" "CallingConvention.Cdecl")
expect_text("${csharp}" "ExactSpelling = true")
expect_text("${csharp}" "byte argument_0, out byte result")
set(all_surfaces "${c_header}\n${cpp}\n${csharp}")
reject_text("${all_surfaces}" "_counter_reset(")
reject_text("${all_surfaces}" "->value_")
reject_text("${all_surfaces}" "_reflection_only_")
file(READ "${manifest}" json)
string(JSON class_count LENGTH "${json}" headers 0 classes)
string(JSON reflected_name GET "${json}" headers 0 classes 0 name)
if(NOT class_count EQUAL 1 OR NOT reflected_name STREQUAL "reflection_only")
    message(FATAL_ERROR "export-only classes leaked into the reflection manifest: ${json}")
endif()
file(READ "${output_dir}/interop_types.reflgen.h" reflection)
reject_text("${reflection}" "interop_tests::counter")
reject_text("${reflection}" "interop_tests::native_counter")
expect_text("${reflection}" "interop_tests::reflection_only")

# 성공 후 동일한 요청은 출력 byte 와 시각을 보존한다.
set(outputs ${reflection_files} "${base}_interop.h" "${base}_interop.cpp" "${base}_interop.cs")
foreach(path IN LISTS outputs)
    file(SHA256 "${path}" hash)
    file(TIMESTAMP "${path}" stamp "%Y-%m-%dT%H:%M:%S" UTC)
    set("hash_${path}" "${hash}")
    set("stamp_${path}" "${stamp}")
endforeach()
execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 2)
run_generator(--interop --interop-library export_native --interop-namespace Interop.Tests
              --declarations-json "${manifest}")
foreach(path IN LISTS outputs)
    file(SHA256 "${path}" hash)
    file(TIMESTAMP "${path}" stamp "%Y-%m-%dT%H:%M:%S" UTC)
    if(NOT hash STREQUAL "${hash_${path}}" OR NOT stamp STREQUAL "${stamp_${path}}")
        message(FATAL_ERROR "identical interop generation rewrote ${path}")
    endif()
endforeach()

# export-only header 도 --discover 가 찾아야 한다. C-only 모듈은 P/Invoke 를 만들지 않는다.
set(c_only "${WORK}/source/c_only.h")
file(WRITE "${c_only}" [=[
namespace only_c
{
    struct [[reflgen::interop("c"), reflgen::lifetime("borrowed")]] sample
    {
        [[reflgen::interop("c")]] int read() const;
        void unselected();
    };
}
]=])
execute_process(
    COMMAND "${GENERATOR}" --module only_c --output "${WORK}/only_c" --interop --discover
            --declarations-json "${WORK}/only_c.json" "${c_only}" -- --target=x86_64-unknown-linux-gnu
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE output)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "C-only interop discovery failed:\n${output}")
endif()
file(READ "${WORK}/only_c/reflgen_only_c_interop.h" c_only_header)
file(READ "${WORK}/only_c/reflgen_only_c_interop.cs" c_only_managed)
expect_text("${c_only_header}" "reflgen_only_c_only_c_sample_read(")
reject_text("${c_only_header}" "_unselected(")
reject_text("${c_only_managed}" "DllImport")
file(READ "${WORK}/only_c.json" c_only_json)
string(JSON exported_reflection_count LENGTH "${c_only_json}" headers 0 classes)
if(NOT exported_reflection_count EQUAL 0)
    message(FATAL_ERROR "C-only export unexpectedly became reflected: ${c_only_json}")
endif()

# 각 실패는 자기 입력 위치의 interop 진단을 내고 아무 생성물도 공개하지 않는다.
function(expect_rejected name source)
    set(directory "${WORK}/rejected/${name}")
    file(MAKE_DIRECTORY "${directory}")
    set(header "${directory}/input.h")
    file(WRITE "${header}" "namespace rejected\n{\n${source}\n}\n")
    execute_process(
        COMMAND "${GENERATOR}" --module rejected --output "${directory}/out" --interop --discover
                --declarations-json "${directory}/out/declarations.json" "${header}"
                -- --target=x86_64-unknown-linux-gnu
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE output)
    if(result EQUAL 0 OR NOT output MATCHES "input\\.h\\([0-9]+,[0-9]+\\): error RG020[0-3]:")
        message(FATAL_ERROR "${name} was not rejected with a located interop diagnostic:\n${output}")
    endif()
    file(GLOB published "${directory}/out/*")
    if(published)
        message(FATAL_ERROR "${name} published files despite failed validation: ${published}")
    endif()
endfunction()

set(selected [=[[[reflgen::interop("csharp"), reflgen::lifetime("borrowed")]]]=])
set(method [=[[[reflgen::interop("csharp")]]]=])
expect_rejected(missing_owner "    struct sample { ${method} int read(); };")
expect_rejected(missing_lifetime [=[
    struct [[reflgen::interop("csharp")]] sample { [[reflgen::interop("csharp")]] int read(); };
]=])
expect_rejected(lifetime_only [=[
    struct [[reflgen::lifetime("borrowed")]] sample {};
]=])
expect_rejected(duplicate_target [=[
    struct [[reflgen::interop("c"), reflgen::interop("c"), reflgen::lifetime("borrowed")]] sample {};
]=])
expect_rejected(conflicting_target [=[
    struct [[reflgen::interop("c"), reflgen::interop("csharp"), reflgen::lifetime("borrowed")]] sample {};
]=])
expect_rejected(duplicate_lifetime [=[
    struct [[reflgen::interop("c"), reflgen::lifetime("borrowed"), reflgen::lifetime("borrowed")]] sample {};
]=])
expect_rejected(missing_argument [=[
    struct [[reflgen::interop, reflgen::lifetime("borrowed")]] sample {};
]=])
expect_rejected(nonliteral_argument [=[
    struct [[reflgen::interop(42), reflgen::lifetime("borrowed")]] sample {};
]=])
expect_rejected(concatenated_argument [=[
    struct [[reflgen::interop("c" "sharp"), reflgen::lifetime("borrowed")]] sample {};
]=])
expect_rejected(escaped_argument [=[
    struct [[reflgen::interop("c\x73harp"), reflgen::lifetime("borrowed")]] sample {};
]=])
expect_rejected(unknown_target [=[
    struct [[reflgen::interop("python"), reflgen::lifetime("borrowed")]] sample {};
]=])
expect_rejected(owned_lifetime [=[
    struct [[reflgen::interop("csharp"), reflgen::lifetime("owned")]] sample {};
]=])
expect_rejected(method_lifetime "    struct ${selected} sample { ${method} [[reflgen::lifetime(\"borrowed\")]] int read(); };")
expect_rejected(target_mismatch [=[
    struct [[reflgen::interop("c"), reflgen::lifetime("borrowed")]] sample
    {
        [[reflgen::interop("csharp")]] int read();
    };
]=])
expect_rejected(overload "    struct ${selected} sample { ${method} int read(int); int read(double); };")
expect_rejected(imported_overload "    struct base { int read(int, int = 0); }; struct ${selected} sample : base { using base::read; ${method} int read(int); };")
expect_rejected(static_method "    struct ${selected} sample { ${method} static int read(); };")
expect_rejected(private_method "    class ${selected} sample { ${method} int read(); };")
expect_rejected(protected_method "    struct ${selected} sample { protected: ${method} int read(); };")
expect_rejected(operator_method "    struct ${selected} sample { ${method} int operator+(int) const; };")
expect_rejected(class_template "    template<class T> struct ${selected} sample { ${method} int read(); };")
expect_rejected(method_template "    struct ${selected} sample { template<class T> ${method} int read(T); };")
expect_rejected(deleted_method "    struct ${selected} sample { ${method} int read() = delete; };")
expect_rejected(consteval_method "    struct ${selected} sample { ${method} consteval int read() const { return 1; } };")
expect_rejected(macro_consteval "#define IMMEDIATE consteval\n    struct ${selected} sample { ${method} IMMEDIATE int read() const { return 1; } };\n#undef IMMEDIATE")
expect_rejected(variadic_method "    struct ${selected} sample { ${method} int read(int, ...); };")
expect_rejected(volatile_method "    struct ${selected} sample { ${method} int read() volatile; };")
expect_rejected(lvalue_method "    struct ${selected} sample { ${method} int read() &; };")
expect_rejected(rvalue_method "    struct ${selected} sample { ${method} int read() &&; };")
expect_rejected(reference_parameter "    struct ${selected} sample { ${method} int read(const int&); };")
expect_rejected(reference_return "    struct ${selected} sample { ${method} int& read(); };")
expect_rejected(string_parameter "    struct ${selected} sample { ${method} int read(const char* text); };")
expect_rejected(string_return "    struct ${selected} sample { ${method} const char* read(); };")
expect_rejected(record_parameter "    struct data { int value; }; struct ${selected} sample { ${method} int read(data); };")
expect_rejected(record_return "    struct data { int value; }; struct ${selected} sample { ${method} data read(); };")
expect_rejected(enum_parameter "    enum class mode { value }; struct ${selected} sample { ${method} int read(mode); };")
expect_rejected(enumerator_marker "    enum class mode { value ${method} };")
expect_rejected(callback_parameter "    struct ${selected} sample { ${method} void read(int (*callback)(int)); };")
expect_rejected(field_marker "    struct sample { ${method} int value; };")
expect_rejected(free_marker "    ${method} int read();")
expect_rejected(parameter_marker "    struct sample { int read(${method} int value); };")
expect_rejected(constructor_marker "    struct ${selected} sample { ${method} sample(); };")
expect_rejected(destructor_marker "    struct ${selected} sample { ${method} ~sample(); };")
expect_rejected(nested_class "    struct outer { struct ${selected} sample { ${method} int read(); }; };")

# 실패한 재생성은 이미 검토한 성공 산출물도 부분적으로 덮어쓰면 안 된다.
file(WRITE "${fixture}" "struct [[reflgen::interop(\"csharp\")]] missing_lifetime {};\n")
execute_process(
    COMMAND "${GENERATOR}" --module export_test --output "${output_dir}" --interop
            --declarations-json "${manifest}" "${fixture}" -- --target=x86_64-unknown-linux-gnu
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE output)
if(result EQUAL 0 OR NOT output MATCHES "error RG0200:")
    message(FATAL_ERROR "invalid regeneration unexpectedly succeeded:\n${output}")
endif()
foreach(path IN LISTS outputs)
    file(SHA256 "${path}" hash)
    file(TIMESTAMP "${path}" stamp "%Y-%m-%dT%H:%M:%S" UTC)
    if(NOT hash STREQUAL "${hash_${path}}" OR NOT stamp STREQUAL "${stamp_${path}}")
        message(FATAL_ERROR "failed validation modified previous output ${path}")
    endif()
endforeach()
message(STATUS "optional interop: explicit exports and fail-closed validation")
