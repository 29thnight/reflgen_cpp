// 생성된 P/Invoke 표면만 쓴다. receiver 주소는 native 시험 도우미가 빌려준다.
using System;
using System.Linq;
using System.Runtime.InteropServices;
using Native = Reflgen.Tests.Interop.reflgen_interop_runtime_interop;

static class Program
{
    [DllImport(Native.LibraryName, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    static extern nint interop_runtime_counter();

    static int failures;

    static void Check(bool ok, string what)
    {
        if (!ok)
        {
            failures++;
            Console.WriteLine($"FAIL {what}");
        }
    }

    static int Main()
    {
        Check(Native.reflgen_interop_runtime_abi_fingerprint() == Native.AbiFingerprint, "fingerprint");
        nint counter = interop_runtime_counter();

        Check(Native.reflgen_interop_runtime_interop_tests_counter_set_value(counter, -5) == Native.StatusOk, "set_value");
        Check(Native.reflgen_interop_runtime_interop_tests_counter_value(counter, out int value) == Native.StatusOk &&
              value == -5, "value");
        Check(Native.reflgen_interop_runtime_interop_tests_counter_toggle(counter, 200, out byte toggled) ==
              Native.StatusOk && toggled == 0, "toggle nonzero");
        Check(Native.reflgen_interop_runtime_interop_tests_counter_toggle(counter, 0, out toggled) == Native.StatusOk &&
              toggled == 1, "toggle zero");
        Check(Native.reflgen_interop_runtime_interop_tests_counter_scale(counter, 1.5, out double scaled) ==
              Native.StatusOk && scaled == -7.5, "scale");
        Check(Native.reflgen_interop_runtime_interop_tests_counter_scale(counter, -1.0, out _) == Native.StatusException,
              "exception is contained");
        Check(Native.reflgen_interop_runtime_interop_tests_counter_constant(counter, out value) == Native.StatusOk &&
              value == 7, "constant");
        Check(Native.reflgen_interop_runtime_interop_tests_counter_unsigned_value(counter, out uint unsigned) ==
              Native.StatusOk && unsigned == 1, "unsigned_value");
        Check(Native.reflgen_interop_runtime_interop_tests_counter_value(0, out _) == Native.StatusInvalidArgument,
              "null receiver");

        // interop("c") 로만 고른 선언은 managed 표면에 없어야 한다.
        string[] names = typeof(Native).GetMethods().Select(method => method.Name).ToArray();
        Check(!names.Any(name => name.Contains("native_only") || name.Contains("native_counter")), "C-only selections");

        if (failures != 0)
        {
            Console.WriteLine($"{failures} failure(s)");
            return 1;
        }
        Console.WriteLine("C# P/Invoke calls passed");
        return 0;
    }
}
