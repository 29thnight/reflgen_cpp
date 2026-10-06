/* 생성된 C ABI header 만 보고 native 라이브러리를 부르는 순수 C 소비자. */
#include "reflgen_interop_runtime_interop.h"

#include <stdio.h>

void* interop_runtime_counter(void);
void* interop_runtime_native_counter(void);

static int failures = 0;

static void check(int ok, const char* what)
{
    if (!ok)
    {
        ++failures;
        printf("FAIL %s\n", what);
    }
}

int main(void)
{
    reflgen_interop_runtime_interop_tests_counter_handle* counter =
        (reflgen_interop_runtime_interop_tests_counter_handle*)interop_runtime_counter();
    reflgen_interop_runtime_interop_tests_native_counter_handle* native =
        (reflgen_interop_runtime_interop_tests_native_counter_handle*)interop_runtime_native_counter();
    int32_t integer = 0;
    uint32_t unsigned_integer = 0;
    uint8_t byte = 9;
    double real = 0.0;

    check(reflgen_interop_runtime_abi_fingerprint() != 0, "fingerprint");
    check(reflgen_interop_runtime_interop_tests_counter_set_value(counter, 21) == reflgen_interop_runtime_status_ok,
          "set_value");
    check(reflgen_interop_runtime_interop_tests_counter_value(counter, &integer) == reflgen_interop_runtime_status_ok &&
              integer == 21,
          "value");
    check(reflgen_interop_runtime_interop_tests_counter_native_only(counter, &integer) ==
                  reflgen_interop_runtime_status_ok &&
              integer == 21,
          "native_only");
    check(reflgen_interop_runtime_interop_tests_counter_toggle(counter, 7, &byte) == reflgen_interop_runtime_status_ok &&
              byte == 0,
          "toggle nonzero");
    check(reflgen_interop_runtime_interop_tests_counter_toggle(counter, 0, &byte) == reflgen_interop_runtime_status_ok &&
              byte == 1,
          "toggle zero");
    check(reflgen_interop_runtime_interop_tests_counter_scale(counter, 2.0, &real) == reflgen_interop_runtime_status_ok &&
              real == 42.0,
          "scale");
    check(reflgen_interop_runtime_interop_tests_counter_scale(counter, -1.0, &real) ==
              reflgen_interop_runtime_status_exception,
          "exception is contained");
    check(reflgen_interop_runtime_interop_tests_counter_constant(counter, &integer) ==
                  reflgen_interop_runtime_status_ok &&
              integer == 7,
          "constant");
    check(reflgen_interop_runtime_interop_tests_counter_unsigned_value(counter, &unsigned_integer) ==
                  reflgen_interop_runtime_status_ok &&
              unsigned_integer == 1,
          "unsigned_value");
    check(reflgen_interop_runtime_interop_tests_native_counter_native_value(native, &unsigned_integer) ==
                  reflgen_interop_runtime_status_ok &&
              unsigned_integer == 1,
          "native_counter");
    check(reflgen_interop_runtime_interop_tests_counter_value(NULL, &integer) ==
              reflgen_interop_runtime_status_invalid_argument,
          "null receiver");
    check(reflgen_interop_runtime_interop_tests_counter_value(counter, NULL) ==
              reflgen_interop_runtime_status_invalid_argument,
          "null result");

    if (failures != 0)
    {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("C ABI calls passed\n");
    return 0;
}
