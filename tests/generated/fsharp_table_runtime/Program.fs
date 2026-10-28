open System
open System.Reflection
open System.Runtime.InteropServices
open System.Text
open Reflgen.Tests.Native

module Native = reflgen_fsharp_table_runtime_native

module Fixture =
    [<Literal>]
    let Library = "reflgen_fsharp_table_runtime"

    [<DllImport(Library, ExactSpelling = true, CallingConvention = CallingConvention.Cdecl)>]
    extern nativeint fixture_get_table()
    [<DllImport(Library, ExactSpelling = true, CallingConvention = CallingConvention.Cdecl)>]
    extern int fixture_table_size()
    [<DllImport(Library, ExactSpelling = true, CallingConvention = CallingConvention.Cdecl)>]
    extern int fixture_layout(int item)
    [<DllImport(Library, ExactSpelling = true, CallingConvention = CallingConvention.Cdecl)>]
    extern uint64 reflgen_fsharp_table_runtime_native_abi_fingerprint()
    [<DllImport(Library, ExactSpelling = true, CallingConvention = CallingConvention.Cdecl)>]
    extern void fixture_set_callback(nativeint address)
    [<DllImport(Library, ExactSpelling = true, CallingConvention = CallingConvention.Cdecl)>]
    extern int fixture_invoke_callback(single dt)
    [<DllImport(Library, ExactSpelling = true, CallingConvention = CallingConvention.Cdecl)>]
    extern void fixture_clear_callback()

let mutable checks = 0
let check condition message =
    if not condition then failwith message
    checks <- checks + 1

let rejects action =
    try
        action ()
        false
    with
    | :? ArgumentException -> true
    | :? InvalidOperationException -> true

let withBuffer (size: int) invoke =
    let address = Marshal.AllocHGlobal(size)
    try
        Marshal.Copy(Array.zeroCreate<byte> size, 0, address, size)
        invoke address
    finally Marshal.FreeHGlobal(address)

let withValue<'T when 'T: struct> (value: 'T) invoke =
    withBuffer (Marshal.SizeOf<'T>()) (fun address ->
        Marshal.StructureToPtr(value, address, false)
        invoke address)

let withUtf8 (text: string) invoke =
    let bytes = UTF8Encoding(false, true).GetBytes(text + "\000")
    withBuffer bytes.Length (fun address ->
        Marshal.Copy(bytes, 0, address, bytes.Length)
        invoke address)

let vector x y z =
    let mutable value = Unchecked.defaultof<Native.fixture_Float3>
    value.x <- x
    value.y <- y
    value.z <- z
    value

let assetId first second =
    let mutable value = Unchecked.defaultof<Native.fixture_AssetId>
    value.first <- first
    value.second <- second
    value

let makeCallback () =
    let callback = Native.ScriptApi_Managed_Tick_Delegate(fun dt ->
        // The native caller receives a status; no managed exception crosses it.
        try
            if dt < 0.f then invalidOp "modeled managed failure"
            int (dt * 1000.f)
        with _ -> -77)
    let reference = WeakReference(callback)
    new Native.RootedCallback<Native.ScriptApi_Managed_Tick_Delegate>(callback), reference

[<EntryPoint>]
let main _ =
    let references = Assembly.GetExecutingAssembly().GetReferencedAssemblies()
    check (references |> Array.forall (fun reference ->
        reference.Name = "FSharp.Core" || reference.Name = "netstandard" ||
        reference.Name = "mscorlib" || reference.Name.StartsWith("System.", StringComparison.Ordinal)))
        "the direct F# executable must have no C# bridge/project reference"

    let fingerprint = Fixture.reflgen_fsharp_table_runtime_native_abi_fingerprint()
    let address = Fixture.fixture_get_table()
    let byteCount = Fixture.fixture_table_size()
    check (fingerprint = Native.AbiFingerprint) "native/generated ABI fingerprint"
    check (Native.ScriptApiVersion = 46) "evaluated named C++ API version"
    check (Native.ScriptApiSize = byteCount) "native/generated table size"
    let api = Native.ScriptApi.Bind(address, unativeint byteCount, fingerprint)

    check (rejects (fun () -> Native.ScriptApi.Bind(0n, unativeint byteCount, fingerprint) |> ignore)) "null table rejected"
    check (rejects (fun () -> Native.ScriptApi.Bind(address, 7un, fingerprint) |> ignore)) "short header rejected"
    check (rejects (fun () -> Native.ScriptApi.Bind(address, unativeint (byteCount - 1), fingerprint) |> ignore)) "truncated table storage rejected"
    check (rejects (fun () -> Native.ScriptApi.Bind(address, unativeint byteCount, fingerprint ^^^ 1UL) |> ignore)) "fingerprint mismatch rejected"
    let tableBytes = Array.zeroCreate<byte> byteCount
    Marshal.Copy(address, tableBytes, 0, byteCount)
    withBuffer byteCount (fun copy ->
        let reset () = Marshal.Copy(tableBytes, 0, copy, byteCount)
        let bind () = Native.ScriptApi.Bind(copy, unativeint byteCount, fingerprint) |> ignore
        reset ()
        Marshal.WriteInt32(copy, 0, 45)
        check (rejects bind) "wrong native API version rejected"
        reset ()
        Marshal.WriteInt32(copy, 4, byteCount - 1)
        check (rejects bind) "smaller advertised struct size rejected"
        reset ()
        Marshal.WriteInt32(copy, 4, byteCount + 8)
        check (rejects bind) "larger advertised struct size rejected"
        reset ()
        Marshal.WriteIntPtr(copy, Fixture.fixture_layout(10), 0n)
        check (rejects bind) "missing required function rejected")

    check (Marshal.SizeOf<Native.fixture_ObjectHandle>() = Fixture.fixture_layout(0)) "handle layout"
    check (Marshal.SizeOf<Native.fixture_CharacterState>() = Fixture.fixture_layout(1)) "nested/double character layout"
    check (Marshal.OffsetOf<Native.fixture_CharacterState>("forced_remaining").ToInt32() = Fixture.fixture_layout(2)) "double field offset"
    check (Marshal.SizeOf<Native.fixture_Contact>() = Fixture.fixture_layout(3)) "nested contact layout"
    check (Marshal.OffsetOf<Native.fixture_Contact>("tick").ToInt32() = Fixture.fixture_layout(4)) "nested contact tail offset"
    check (Marshal.SizeOf<Native.fixture_ScriptMessage>() = Fixture.fixture_layout(5)) "inline byte array layout"
    check (Marshal.SizeOf<Native.fixture_TreeNode>() = Fixture.fixture_layout(6)) "two inline arrays layout"
    check (Marshal.SizeOf<Native.fixture_BlackboardEntry>() = Fixture.fixture_layout(7)) "mixed fixed array lengths layout"
    check (Marshal.SizeOf<Native.fixture_AudioParameter>() = Fixture.fixture_layout(8)) "record pointer members layout"
    check (Marshal.OffsetOf<Native.fixture_AudioParameter>("text").ToInt32() = Fixture.fixture_layout(9)) "record pointer offset"
    check (Marshal.SizeOf<Native.fixture_AssetLink>() = Fixture.fixture_layout(11)) "nested asset identity layout"
    check (Marshal.SizeOf<Native.fixture_PhysicsHit>() = Fixture.fixture_layout(12)) "physics hit layout"

    let name = "한글-🎹"
    let owner = withUtf8 name api.Entity_FindByName.Invoke
    check (owner.index = 17u && owner.generation = 9u) "UTF-8 input and handle by-value return"
    check (api.Entity_IsAlive.Invoke(owner) = 1) "valid generation handle"
    let mutable stale = owner
    stale.generation <- stale.generation + 1u
    check (api.Entity_IsAlive.Invoke(stale) = 0) "stale generation is preserved and rejected natively"
    check (api.Engine_GetFrameCount.Invoke() = 0xf123456789abcdefUL) "unsigned long long result retains high bits"
    check (withUtf8 "marker" api.Profiler_Register.Invoke = 0xfedcba98u) "uint32 result retains high bits"
    check (api.Camera_GetFov.Invoke(owner) = 72.5f) "float result"
    let screen = api.Camera_GetScreenSize.Invoke()
    check (screen.x = 1920.f && screen.y = 1080.f) "two-float record return"
    let rotation = api.Transform_GetRotation.Invoke(owner)
    check (rotation.z = 0.f && rotation.w = 1.f) "four-float record return"
    let initialPosition = api.Transform_GetPosition.Invoke(owner)
    check (initialPosition.x = 1.25f && initialPosition.y = -2.5f && initialPosition.z = 3.75f) "three-float record return"
    api.Transform_SetPosition.Invoke(owner, vector 9.f 8.f 7.f)
    api.Transform_SetPosition.Invoke(stale, vector 0.f 0.f 0.f)
    let changedPosition = api.Transform_GetPosition.Invoke(owner)
    check (changedPosition.x = 9.f && changedPosition.y = 8.f && changedPosition.z = 7.f) "record by-value argument and void result"
    withBuffer 64 (fun buffer ->
        let written = api.Entity_GetName.Invoke(owner, buffer, 64)
        check (written = UTF8Encoding(false, true).GetByteCount(name)) "UTF-8 output byte length"
        check (Marshal.PtrToStringUTF8(buffer, written) = name) "UTF-8 output round trip"
        check (api.Entity_GetName.Invoke(owner, buffer, 1) = -1) "buffer capacity stays an explicit native contract")

    let instance = api.Body_Find.Invoke(owner)
    check (instance = 0xfedcba9876543210UL) "uint64 component instance identity"
    withBuffer (Marshal.SizeOf<Native.fixture_BodyState>()) (fun output ->
        check (api.Body_Read.Invoke(owner, instance, output) = 0) "nested body output"
        let body = Marshal.PtrToStructure<Native.fixture_BodyState>(output)
        check (body.mass = 7.5f && body.position.x = 9.f && body.rotation.w = 1.f && body.angular_velocity.z = 23.f) "nested body fields"
        check (api.Body_Read.Invoke(owner, instance - 1UL, output) = 2) "stale component instance rejected"
        check ((Marshal.PtrToStructure<Native.fixture_BodyState>(output)).mass = 0.f) "native failure output policy preserved")
    check (api.Character_Force.Invoke(owner, instance, vector 1.f 2.f 3.f, 1.125) = 0) "double parameter"
    withBuffer (Marshal.SizeOf<Native.fixture_CharacterState>()) (fun output ->
        check (api.Character_Read.Invoke(owner, instance, output) = 0) "aligned nested character output"
        let state = Marshal.PtrToStructure<Native.fixture_CharacterState>(output)
        check (state.tick = 0xffffeeee11112222UL && state.forced_remaining = 3.125 && state.movement_velocity.z = 6.f) "64-bit and double nested tail fields")

    let requestSize = Marshal.SizeOf<Native.fixture_QueryRequest>()
    withBuffer (requestSize * 2) (fun requests ->
        for index = 0 to 1 do
            let mutable request = Unchecked.defaultof<Native.fixture_QueryRequest>
            request.origin <- vector (single (index + 1)) 2.f 3.f
            request.direction <- vector 0.f 1.f 0.f
            request.distance_or_radius <- single (index + 10)
            Marshal.StructureToPtr(request, requests + nativeint (index * requestSize), false)
        withBuffer (Marshal.SizeOf<Native.fixture_PhysicsHit>() * 2) (fun hits ->
            withBuffer (Marshal.SizeOf<Native.fixture_BatchResult>() * 2) (fun summaries ->
                check (api.Physics_QueryBatch.Invoke(owner, requests, 2, hits, 2, summaries) = 0) "record-pointer/count batched call"
                for index = 0 to 1 do
                    let hit = Marshal.PtrToStructure<Native.fixture_PhysicsHit>(hits + nativeint (index * Marshal.SizeOf<Native.fixture_PhysicsHit>()))
                    let summary = Marshal.PtrToStructure<Native.fixture_BatchResult>(summaries + nativeint (index * Marshal.SizeOf<Native.fixture_BatchResult>()))
                    check (hit.``component`` = instance && hit.point.x = single (index + 1) && hit.distance = single (index + 10)) "batched record stride and values"
                    check (summary.error = 0 && summary.written = 1 && summary.truncated = 0) "parallel batch summaries")))

    let mutable settings = Unchecked.defaultof<Native.fixture_AudioSettings>
    settings.volume <- 0.25f
    withValue settings (fun settingsPointer ->
        withUtf8 "gain" (fun parameterName ->
            withUtf8 name (fun parameterText ->
                let mutable parameter = Unchecked.defaultof<Native.fixture_AudioParameter>
                parameter.name <- parameterName
                parameter.text <- parameterText
                parameter.integer <- 55
                withValue parameter (fun parameters ->
                    check (api.Audio_Play.Invoke(91UL, assetId 101UL 202UL, 3, 4, vector 9.f 8.f 7.f,
                                                 owner, settingsPointer, parameters, 1) = 0xf0e0d0c0b0a09080UL)
                        "nine-argument mixed ABI with nested borrowed string pointers"))))
    let mutable link = Unchecked.defaultof<Native.fixture_AssetLink>
    link.asset <- assetId 101UL 202UL
    link.subasset <- assetId 303UL 404UL
    link.kind <- 3u
    let mutable variant = Unchecked.defaultof<Native.fixture_TextureVariant>
    variant.role <- 6u
    withValue link (fun linkPointer ->
        withValue variant (fun variantPointer ->
            withBuffer (Marshal.SizeOf<Native.fixture_AssetToken>()) (fun tokenPointer ->
                check (api.Asset_Request.Invoke(linkPointer, 0x10002u, variantPointer, tokenPointer) = 0) "nested asset input/output"
                let token = Marshal.PtrToStructure<Native.fixture_AssetToken>(tokenPointer)
                check (token.index = 77u && token.generation = 88u && token.``type`` = 0x10002u) "reserved F# identifier and concrete asset identity")))

    withBuffer (Marshal.SizeOf<Native.fixture_TickStatus>()) (fun output ->
        check (api.Simulation_Status.Invoke(output) = 0) "simulation status pointer output"
        let status = Marshal.PtrToStructure<Native.fixture_TickStatus>(output)
        check (status.epoch = 8UL && status.committed = 11UL && status.active = 1u) "simulation status fields")
    let payload = [| 0uy; 255uy; 17uy; 0uy; 64uy |]
    withBuffer payload.Length (fun input ->
        Marshal.Copy(payload, 0, input, payload.Length)
        withBuffer 8 (fun sequence ->
            check (api.Simulation_Submit.Invoke(8UL, 13UL, 7u, input, payload.Length, sequence) = 0 && Marshal.ReadInt64(sequence) = 1L)
                "length-delimited binary payload with embedded NUL"
            withBuffer (Marshal.SizeOf<Native.fixture_TickSnapshot>()) (fun header ->
                withBuffer 32 (fun output ->
                    check (api.Simulation_Read.Invoke(8UL, 12UL, header, output, 32) = 0) "snapshot header and binary output"
                    let snapshot = Marshal.PtrToStructure<Native.fixture_TickSnapshot>(header)
                    let roundTrip = Array.zeroCreate<byte> payload.Length
                    Marshal.Copy(output, roundTrip, 0, roundTrip.Length)
                    check (snapshot.tick = 13UL && snapshot.bytes = uint32 payload.Length && roundTrip = payload) "binary payload round trip"))))

    withBuffer (Marshal.SizeOf<Native.fixture_Contact>()) (fun output ->
        check (api.ReadContact.Invoke(output) = 0) "deeply nested contact record"
        let contact = Marshal.PtrToStructure<Native.fixture_Contact>(output)
        check (contact.first.owner.index = owner.index && contact.first.``component`` = instance && contact.second.role.second = 404UL && contact.tick = 9001UL)
            "deeply nested contact identities")
    withBuffer (Marshal.SizeOf<Native.fixture_ScriptMessage>()) (fun output ->
        check (api.ReadMessage.Invoke(output) = 0) "inline string array output"
        let message = Marshal.PtrToStructure<Native.fixture_ScriptMessage>(output)
        check (message.instanceId = 314 && int message.name.Item63 = int 'Z') "inline final array element"
        check (Marshal.PtrToStringUTF8(output + 4n) = name) "inline UTF-8 array storage")
    withBuffer (Marshal.SizeOf<Native.fixture_TreeNode>()) (fun nodePointer ->
        withBuffer (Marshal.SizeOf<Native.fixture_BlackboardEntry>()) (fun entryPointer ->
            check (api.ReadTree.Invoke(nodePointer, entryPointer) = 0) "multiple fixed arrays"
            let node = Marshal.PtrToStructure<Native.fixture_TreeNode>(nodePointer)
            let entry = Marshal.PtrToStructure<Native.fixture_BlackboardEntry>(entryPointer)
            check (node.id = instance && int node.name.Item0 = int 'N' && int node.name.Item63 = int 'n' && int node.scriptName.Item63 = int 's') "independent same-sized inline arrays"
            check (entry.intValue = 88 && entry.w = 4.5f && int entry.key.Item63 = int 'k' && int entry.stringValue.Item127 = int 'v') "different-sized inline arrays retain tail bytes"))
    withBuffer 4 (fun input ->
        Marshal.WriteInt32(input, 12345)
        check (api.ReadPointer.Invoke(input) = 12345 && api.ReadPointer.Invoke(0n) = -1) "opaque void pointer and native null policy")

    let callback, weakCallback = makeCallback ()
    try
        check (api.Managed_Tick.Invoke(0.25f) = 250) "generated tick delegate calls native code"
        Fixture.fixture_set_callback(callback.Address)
        GC.Collect()
        GC.WaitForPendingFinalizers()
        GC.Collect()
        check weakCallback.IsAlive "retained callback remains rooted across full GC"
        check (Fixture.fixture_invoke_callback(0.125f) = 125) "native-to-F# retained callback after GC"
        check (Fixture.fixture_invoke_callback(-1.f) = -77) "managed callback exception contained as status"
        GC.KeepAlive(callback)
    finally
        // Native revocation must precede managed root release.
        Fixture.fixture_clear_callback()
        (callback :> IDisposable).Dispose()
    check (Fixture.fixture_invoke_callback(0.125f) = -1) "native callback revoked before root release"
    check (rejects (fun () -> callback.Address |> ignore)) "disposed callback address rejected"

    let guarded = Native.root_ScriptApi_Managed_Tick_callback (fun _ -> invalidOp "guarded callback failure") (-66)
    try
        Fixture.fixture_set_callback(guarded.Address)
        check (Fixture.fixture_invoke_callback(0.125f) = -66) "generated callback guard returns explicit fallback"
        check (guarded.LastException |> Option.exists (fun error -> error.Message = "guarded callback failure"))
            "generated callback guard retains diagnostic exception"
    finally
        Fixture.fixture_clear_callback()
        (guarded :> IDisposable).Dispose()

    check (api.Entity_IsAlive.Invoke(owner) = 1) "failed candidate binds preserve existing immutable binding"
    printfn "FSHARP_TABLE_OK checks=%d api=%d tableBytes=%d bridge=C#-free" checks Native.ScriptApiVersion byteCount
    0
