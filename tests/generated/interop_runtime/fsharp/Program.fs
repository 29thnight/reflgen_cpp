open System
open System.Runtime.InteropServices
open Reflgen.Tests.Interop
module Native = reflgen_fsharp_runtime_interop

[<Struct; StructLayout(LayoutKind.Sequential)>]
type Point =
    val mutable x : int32
    val mutable y : int32

[<Struct; StructLayout(LayoutKind.Sequential)>]
type PointAlignment =
    val mutable Prefix : byte
    val mutable Value : Point

[<Struct; StructLayout(LayoutKind.Sequential)>]
type WrongPoint =
    val mutable x : int64
    val mutable y : int32

[<DllImport(Native.LibraryName, ExactSpelling = true, CallingConvention = CallingConvention.Cdecl)>]
extern int fsharp_fixture_point_size()
[<DllImport(Native.LibraryName, ExactSpelling = true, CallingConvention = CallingConvention.Cdecl)>]
extern int fsharp_fixture_point_alignment()
[<DllImport(Native.LibraryName, ExactSpelling = true, CallingConvention = CallingConvention.Cdecl)>]
extern int fsharp_fixture_point_offset_x()
[<DllImport(Native.LibraryName, ExactSpelling = true, CallingConvention = CallingConvention.Cdecl)>]
extern int fsharp_fixture_point_offset_y()

[<DllImport(Native.LibraryName, ExactSpelling = true, CallingConvention = CallingConvention.Cdecl)>]
extern nativeint fsharp_fixture_counter()

[<DllImport(Native.LibraryName, ExactSpelling = true, CallingConvention = CallingConvention.Cdecl)>]
extern nativeint fsharp_fixture_create_owned()
[<DllImport(Native.LibraryName, ExactSpelling = true, CallingConvention = CallingConvention.Cdecl)>]
extern int fsharp_fixture_destroyed_count()

[<EntryPoint>]
let main _ =
    let check condition label = if not condition then failwith label
    check (Native.reflgen_fsharp_runtime_abi_fingerprint() = Native.AbiFingerprint) "fingerprint"
    let receiver = fsharp_fixture_counter()
    let mutable value = -99
    check (Native.reflgen_fsharp_runtime_fsharp_fixture_counter_add(receiver, 7, 1uy, &value) = 0 && value = 8) "add"
    check (Native.reflgen_fsharp_runtime_fsharp_fixture_counter_add(0n, 7, 1uy, &value) = 1 && value = 8) "null receiver"
    let address = Marshal.AllocHGlobal(4)
    try
        Marshal.WriteInt32(address, 1234)
        check (Native.reflgen_fsharp_runtime_fsharp_fixture_counter_read_pointer(receiver, address, &value) = 0 && value = 1234) "pointer"
        check (Native.reflgen_fsharp_runtime_fsharp_fixture_counter_read_pointer(receiver, 0n, &value) = 0 && value = -1) "null pointer"
        check (Native.reflgen_fsharp_runtime_fsharp_fixture_counter_read_typed(receiver, address, &value) = 0 && value = 1234) "typed pointer"
    finally Marshal.FreeHGlobal(address)
    let layoutMatches (managedType: Type) =
        Marshal.SizeOf(managedType) = fsharp_fixture_point_size() &&
        Marshal.OffsetOf<PointAlignment>("Value").ToInt32() = fsharp_fixture_point_alignment() &&
        Marshal.OffsetOf(managedType, "x").ToInt32() = fsharp_fixture_point_offset_x() &&
        Marshal.OffsetOf(managedType, "y").ToInt32() = fsharp_fixture_point_offset_y()
    check (layoutMatches typeof<Point> && Native.validatePointLayout typeof<Point>) "POD layout mismatch"
    check (not (layoutMatches typeof<WrongPoint>) && not (Native.validatePointLayout typeof<WrongPoint>)) "POD mismatch must be detected"
    let pointAddress = Marshal.AllocHGlobal(Marshal.SizeOf<Point>())
    try
        check (Native.isPointAligned pointAddress) "POD pointer alignment"
        check (not (Native.isPointAligned (pointAddress + 1n))) "POD misalignment must be detected"
        let mutable point = Point()
        point.x <- 19
        point.y <- 23
        Marshal.StructureToPtr(point, pointAddress, false)
        check (Native.reflgen_fsharp_runtime_fsharp_fixture_counter_sum_point(receiver, pointAddress, &value) = 0 && value = 42) "POD pointer"
        let resultAddress = Marshal.AllocHGlobal(Marshal.SizeOf<Point>())
        try
            check (Native.reflgen_fsharp_runtime_fsharp_fixture_counter_shifted(receiver, pointAddress, 4, resultAddress) = 0) "POD value status"
            let resultPoint = Marshal.PtrToStructure<Point>(resultAddress)
            check (resultPoint.x = 23 && resultPoint.y = 19) "POD value return"
            check (Native.reflgen_fsharp_runtime_fsharp_fixture_counter_shifted(receiver, 0n, 4, resultAddress) = 1) "POD null input"
            check (Native.reflgen_fsharp_runtime_fsharp_fixture_counter_shifted(receiver, pointAddress, 4, 0n) = 1) "POD null output"
            check (Native.reflgen_fsharp_runtime_fsharp_fixture_counter_failing_point(receiver, pointAddress, resultAddress) = 2) "POD exception"
            let afterFailure = Marshal.PtrToStructure<Point>(resultAddress)
            check (afterFailure.x = 23 && afterFailure.y = 19) "POD output unchanged on exception"
            check (Native.reflgen_fsharp_runtime_fsharp_fixture_counter_shifted(receiver, pointAddress, 4, pointAddress) = 0) "POD aliased buffer status"
            let aliasedPoint = Marshal.PtrToStructure<Point>(pointAddress)
            check (aliasedPoint.x = 23 && aliasedPoint.y = 19) "POD aliased buffer result"
        finally Marshal.FreeHGlobal(resultAddress)
    finally Marshal.FreeHGlobal(pointAddress)
    let mutable length = -1
    let status = Native.withUtf8 "가나다" (fun text -> Native.reflgen_fsharp_runtime_fsharp_fixture_counter_utf8_length(receiver, text, &length))
    check (status = 0 && length = 9) "UTF-8 bytes"
    let emptyStatus = Native.withUtf8 "" (fun text -> Native.reflgen_fsharp_runtime_fsharp_fixture_counter_utf8_length(receiver, text, &length))
    check (emptyStatus = 0 && length = 0) "empty string"
    let rejects input =
        try Native.withUtf8 input (fun _ -> ()) ; false
        with :? ArgumentException -> true
    check (rejects null) "null string rejected"
    check (rejects "a\000b") "embedded NUL rejected"
    check (rejects (String([| char 0xD800 |]))) "invalid UTF-16 rejected"
    let mutable propagated = false
    try Native.withUtf8 "test" (fun _ -> failwith "callback failure")
    with _ -> propagated <- true
    check propagated "exception propagated"
    let mutable mode = 0us
    check (Native.reflgen_fsharp_runtime_fsharp_fixture_counter_choose(receiver, 65530us, &mode) = 0 && mode = 65530us) "enum"
    let mutable callbackResult = 0
    let callbackStatus = Native.withSynchronousCallback (fun v -> v * 3) (fun callback ->
        Native.reflgen_fsharp_runtime_fsharp_fixture_counter_apply(receiver, callback, 5, &callbackResult))
    check (callbackStatus = 0 && callbackResult = 33) "callback round trip"
    check (Native.reflgen_fsharp_runtime_fsharp_fixture_counter_apply(receiver, 0n, 1, &callbackResult) = 1) "null callback"
    let mutable callbackRaised = false
    try
        Native.withSynchronousCallback (fun _ -> failwith "callback failed") (fun callback ->
            Native.reflgen_fsharp_runtime_fsharp_fixture_counter_apply(receiver, callback, 5, &callbackResult)) |> ignore
    with e -> callbackRaised <- e.Message = "callback failed"
    check callbackRaised "managed callback exception returns to managed caller"
    let beforeOwned = fsharp_fixture_destroyed_count()
    let mutable returnedOwned = 0n
    check (Native.reflgen_fsharp_runtime_fsharp_fixture_counter_create_owned(receiver, &returnedOwned) = 0 && returnedOwned <> 0n) "owned return"
    let owned = Native.Owned_fsharp_fixture_owned_counter.TakeOwnership(returnedOwned)
    returnedOwned <- 0n
    let mutable nullOwned = 123n
    check (Native.reflgen_fsharp_runtime_fsharp_fixture_counter_no_owned(receiver, &nullOwned) = 0 && nullOwned = 0n) "nullable owned return"
    let mutable untouched = 123n
    check (Native.reflgen_fsharp_runtime_fsharp_fixture_counter_create_owned(0n, &untouched) = 1 && untouched = 123n) "failed owned return preserves output"
    check (Native.reflgen_fsharp_runtime_fsharp_fixture_counter_failing_owned(receiver, &untouched) = 2 && untouched = 123n) "exception preserves owned output"
    Native.withOwned_fsharp_fixture_owned_counter owned (fun address ->
        let mutable ownedValue = 0
        check (Native.reflgen_fsharp_runtime_fsharp_fixture_owned_counter_read(address, &ownedValue) = 0 && ownedValue = 73) "owned receiver call"
        owned.Dispose()
        check (fsharp_fixture_destroyed_count() = beforeOwned) "dispose waits for active call")
    check (fsharp_fixture_destroyed_count() = beforeOwned + 1) "owned object destroyed exactly once"
    owned.Dispose()
    check (fsharp_fixture_destroyed_count() = beforeOwned + 1) "repeat dispose"
    let mutable rejectedClosed = false
    try Native.withOwned_fsharp_fixture_owned_counter owned (fun _ -> ())
    with :? ObjectDisposedException -> rejectedClosed <- true
    check rejectedClosed "disposed handle rejected"
    let mutable rawOwned = fsharp_fixture_create_owned()
    check (Native.reflgen_fsharp_runtime_fsharp_fixture_owned_counter_handle_destroy(&rawOwned) = 0 && rawOwned = 0n) "raw destroy clears handle"
    check (Native.reflgen_fsharp_runtime_fsharp_fixture_owned_counter_handle_destroy(&rawOwned) = 1) "raw repeated destroy rejected"
    check (fsharp_fixture_destroyed_count() = beforeOwned + 2) "raw destroy exactly once"
    let beforeConsume = fsharp_fixture_destroyed_count()
    let mutable consumed = fsharp_fixture_create_owned()
    let mutable consumeResult = -1
    let mutable receiverAlias = receiver
    check (Native.reflgen_fsharp_runtime_fsharp_fixture_counter_consume_owned(receiver, &receiverAlias, 0uy, &consumeResult) = 1 && receiverAlias = receiver) "receiver cannot be consumed"
    check (Native.reflgen_fsharp_runtime_fsharp_fixture_counter_consume_owned(0n, &consumed, 0uy, &consumeResult) = 1 && consumed <> 0n) "failed prevalidation retains ownership"
    check (Native.reflgen_fsharp_runtime_fsharp_fixture_counter_consume_owned(receiver, &consumed, 0uy, &consumeResult) = 0 && consumed = 0n && consumeResult = 73) "ownership transferred"
    check (fsharp_fixture_destroyed_count() = beforeConsume + 1) "transferred object destroyed"
    check (Native.reflgen_fsharp_runtime_fsharp_fixture_counter_consume_owned(receiver, &consumed, 0uy, &consumeResult) = 1) "repeated consume rejected"
    let mutable exceptional = fsharp_fixture_create_owned()
    check (Native.reflgen_fsharp_runtime_fsharp_fixture_counter_consume_owned(receiver, &exceptional, 1uy, &consumeResult) = 2 && exceptional = 0n) "exception after transfer"
    check (fsharp_fixture_destroyed_count() = beforeConsume + 2) "exception releases transferred object"
    let beforeManagedTransfer = fsharp_fixture_destroyed_count()
    let managedTransfer = Native.Owned_fsharp_fixture_owned_counter.TakeOwnership(fsharp_fixture_create_owned())
    let mutable managedResult = 0
    let preflightStatus = Native.transferOwned_fsharp_fixture_owned_counter managedTransfer (fun slot ->
        Native.reflgen_fsharp_runtime_fsharp_fixture_counter_consume_owned(0n, &slot.contents, 0uy, &managedResult))
    check (preflightStatus = 1 && not managedTransfer.IsClosed) "managed prevalidation retains ownership"
    let transferStatus = Native.transferOwned_fsharp_fixture_owned_counter managedTransfer (fun slot ->
        Native.reflgen_fsharp_runtime_fsharp_fixture_counter_consume_owned(receiver, &slot.contents, 0uy, &managedResult))
    check (transferStatus = 0 && managedTransfer.IsClosed && managedResult = 73) "managed ownership transferred"
    managedTransfer.Dispose()
    check (fsharp_fixture_destroyed_count() = beforeManagedTransfer + 1) "managed transfer no double delete"
    let managedExceptional = Native.Owned_fsharp_fixture_owned_counter.TakeOwnership(fsharp_fixture_create_owned())
    let mutable afterTransferRaised = false
    try
        Native.transferOwned_fsharp_fixture_owned_counter managedExceptional (fun slot ->
            let result = Native.reflgen_fsharp_runtime_fsharp_fixture_counter_consume_owned(receiver, &slot.contents, 1uy, &managedResult)
            check (result = 2) "native consuming exception status"
            failwith "managed failure after consuming")
    with e -> afterTransferRaised <- e.Message = "managed failure after consuming"
    check (afterTransferRaised && managedExceptional.IsClosed) "managed exception preserves transfer"
    managedExceptional.Dispose()
    check (fsharp_fixture_destroyed_count() = beforeManagedTransfer + 2) "exception transfer no double delete"
    let raceOwner = Native.Owned_fsharp_fixture_owned_counter.TakeOwnership(fsharp_fixture_create_owned())
    let raceStatus = Native.transferOwned_fsharp_fixture_owned_counter raceOwner (fun slot ->
        raceOwner.Dispose()
        Native.reflgen_fsharp_runtime_fsharp_fixture_counter_consume_owned(receiver, &slot.contents, 0uy, &managedResult))
    check (raceStatus = 0 && fsharp_fixture_destroyed_count() = beforeManagedTransfer + 3) "dispose during transfer"
    let reentrantOwner = Native.Owned_fsharp_fixture_owned_counter.TakeOwnership(fsharp_fixture_create_owned())
    let mutable reentrantRejected = false
    Native.withOwned_fsharp_fixture_owned_counter reentrantOwner (fun _ ->
        try Native.transferOwned_fsharp_fixture_owned_counter reentrantOwner (fun _ -> ())
        with :? InvalidOperationException -> reentrantRejected <- true)
    check reentrantRejected "reentrant transfer rejected"
    reentrantOwner.Dispose()
    check (fsharp_fixture_destroyed_count() = beforeManagedTransfer + 4) "reentrant test cleanup"
    let concurrentOwner = Native.Owned_fsharp_fixture_owned_counter.TakeOwnership(fsharp_fixture_create_owned())
    use callEntered = new System.Threading.ManualResetEventSlim(false)
    use releaseCall = new System.Threading.ManualResetEventSlim(false)
    use transferStarted = new System.Threading.ManualResetEventSlim(false)
    use transferFinished = new System.Threading.ManualResetEventSlim(false)
    let readTask = System.Threading.Tasks.Task.Run(Action(fun () ->
        Native.withOwned_fsharp_fixture_owned_counter concurrentOwner (fun address ->
            callEntered.Set()
            if not (releaseCall.Wait(5000)) then failwith "read wait timeout"
            let mutable value = 0
            check (Native.reflgen_fsharp_runtime_fsharp_fixture_owned_counter_read(address, &value) = 0 && value = 73) "concurrent read")))
    check (callEntered.Wait(5000)) "read entered"
    let transferTask = System.Threading.Tasks.Task.Run(Action(fun () ->
        transferStarted.Set()
        let mutable value = 0
        let status = Native.transferOwned_fsharp_fixture_owned_counter concurrentOwner (fun slot ->
            Native.reflgen_fsharp_runtime_fsharp_fixture_counter_consume_owned(receiver, &slot.contents, 0uy, &value))
        check (status = 0 && value = 73) "concurrent transfer"
        transferFinished.Set()))
    check (transferStarted.Wait(5000)) "transfer started"
    let wasBlocked = not (transferFinished.Wait(100))
    releaseCall.Set()
    check wasBlocked "transfer waits for active call"
    check (readTask.Wait(5000) && transferTask.Wait(5000)) "concurrent tasks completed"
    concurrentOwner.Dispose()
    check (fsharp_fixture_destroyed_count() = beforeManagedTransfer + 5) "concurrent transfer destroyed once"
    let beforeTyped = fsharp_fixture_destroyed_count()
    let typedOwner = Native.Owned_fsharp_fixture_owned_counter.TakeOwnership(fsharp_fixture_create_owned())
    let failedStatus, failedValue = Native.transfer_reflgen_fsharp_runtime_fsharp_fixture_counter_consume_owned 0n typedOwner 0uy
    check (failedStatus = 1 && failedValue = 0 && not typedOwner.IsClosed) "typed prevalidation retains ownership"
    let typedStatus, typedValue = Native.transfer_reflgen_fsharp_runtime_fsharp_fixture_counter_consume_owned receiver typedOwner 0uy
    check (typedStatus = 0 && typedValue = 73 && typedOwner.IsClosed) "typed transfer"
    typedOwner.Dispose()
    check (fsharp_fixture_destroyed_count() = beforeTyped + 1) "typed transfer no double delete"
    let typedExceptional = Native.Owned_fsharp_fixture_owned_counter.TakeOwnership(fsharp_fixture_create_owned())
    let exceptionStatus, _ = Native.transfer_reflgen_fsharp_runtime_fsharp_fixture_counter_consume_owned receiver typedExceptional 1uy
    check (exceptionStatus = 2 && typedExceptional.IsClosed) "typed exception commits transfer"
    typedExceptional.Dispose()
    check (fsharp_fixture_destroyed_count() = beforeTyped + 2) "typed exception no double delete"
    let beforeVariants = fsharp_fixture_destroyed_count()
    let voidOwner = Native.Owned_fsharp_fixture_owned_counter.TakeOwnership(fsharp_fixture_create_owned())
    check (Native.transfer_reflgen_fsharp_runtime_fsharp_fixture_counter_consume_void receiver voidOwner = 0 && voidOwner.IsClosed) "typed void transfer"
    voidOwner.Dispose()
    let pointOwner = Native.Owned_fsharp_fixture_owned_counter.TakeOwnership(fsharp_fixture_create_owned())
    let pointOutput = Marshal.AllocHGlobal(Marshal.SizeOf<Point>())
    try
        let pointStatus = Native.transfer_reflgen_fsharp_runtime_fsharp_fixture_counter_consume_point receiver pointOwner pointOutput
        let outputPoint = Marshal.PtrToStructure<Point>(pointOutput)
        check (pointStatus = 0 && pointOwner.IsClosed && outputPoint.x = 73 && outputPoint.y = 11) "typed POD result transfer"
    finally
        pointOwner.Dispose()
        Marshal.FreeHGlobal(pointOutput)
    let swapOwner = Native.Owned_fsharp_fixture_owned_counter.TakeOwnership(fsharp_fixture_create_owned())
    let swapStatus, swappedAddress = Native.transfer_reflgen_fsharp_runtime_fsharp_fixture_counter_swap_owned receiver swapOwner
    check (swapStatus = 0 && swapOwner.IsClosed && swappedAddress <> 0n) "typed owned result transfer"
    swapOwner.Dispose()
    let swappedOwner = Native.Owned_fsharp_fixture_owned_counter.TakeOwnership(swappedAddress)
    swappedOwner.Dispose()
    check (fsharp_fixture_destroyed_count() = beforeVariants + 4) "typed transfer variants destroy once"
    let mutable rejectedNull = false
    try Native.Owned_fsharp_fixture_owned_counter.TakeOwnership(0n) |> ignore
    with :? ArgumentException -> rejectedNull <- true
    check rejectedNull "null ownership rejected"
    check (Native.reflgen_fsharp_runtime_fsharp_fixture_counter_fail(receiver) = 2) "exception"
    printfn "F# P/Invoke calls passed"
    0
