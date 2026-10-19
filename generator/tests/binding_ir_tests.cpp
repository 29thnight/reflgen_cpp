#include "binding_ir.h"
#include "binding_hash.h"
#include "harness.h"
#include "reflgen/prsm/binding_abi.h"
#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

extern "C" int reflgen_prsm_binding_abi_c_probe(void);

namespace
{
    using namespace reflgen::generator;
    using reflgen_test::check;
    using reflgen_test::check_equal;
    using reflgen_test::require;
    using reflgen_test::test;

    const test c_contract("PrSM ABI: shared C/C++ fixed-width contract", [] {
        check_equal(REFLGEN_PRSM_BINDING_ABI_VERSION, 1u);
        check_equal(REFLGEN_PRSM_BINDING_DIGEST_BYTES, 32u);
        check_equal(reflgen_prsm_binding_abi_c_probe(), static_cast<int>(REFLGEN_PRSM_BINDING_ABI_MISMATCH));
        check_equal(static_cast<int>(REFLGEN_PRSM_BINDING_OK), 0);
        check_equal(static_cast<int>(REFLGEN_PRSM_BINDING_INVALID_CONTEXT), 6);
    });

    binding_module_input example()
    {
        binding_module_input input;
        input.name = "engine";
        input.target = {"x86_64-pc-windows-msvc", 64};
        input.sources = {{binding_source_kind::header, "engine/actor.h"}};
        input.types = {{"engine::Actor", "Engine::Actor", 0}};
        binding_callable call;
        call.receiver = "engine::Actor";
        call.cpp_name = "GetCount";
        call.cpp_signature = "int () const";
        call.script_name = "Count";
        call.symbol = "ce_prsm_bind_actor_get";
        call.operation = binding_operation::getter;
        call.effect = binding_effect::read;
        call.result = binding_scalar::int32;
        call.position = {"engine/actor.h", 10, 4};
        input.callables.push_back(call);
        return input;
    }

    binding_module valid(binding_module_input input)
    {
        diagnostics report;
        auto module = build_binding_module(std::move(input), report);
        require(module.has_value(),
                report.entries().empty() ? "missing binding module" : report.entries().front().message);
        return std::move(*module);
    }

    template<class F>
    void rejected(F mutate)
    {
        auto input = example();
        mutate(input);
        diagnostics report;
        check(!build_binding_module(std::move(input), report).has_value());
        require(!report.entries().empty());
        check_equal(report.entries().front().code, std::string("RG0300"));
    }

    std::vector<std::string> split(const std::string& line, char delimiter)
    {
        std::vector<std::string> fields;
        std::size_t start = 0;
        for (;;)
        {
            const auto end = line.find(delimiter, start);
            fields.push_back(line.substr(start, end == std::string::npos ? end : end - start));
            if (end == std::string::npos)
                return fields;
            start = end + 1;
        }
    }

    binding_scalar scalar(const std::string& name)
    {
        for (const auto type :
             {binding_scalar::void_, binding_scalar::boolean, binding_scalar::int8, binding_scalar::int16,
              binding_scalar::int32, binding_scalar::int64, binding_scalar::uint8, binding_scalar::uint16,
              binding_scalar::uint32, binding_scalar::uint64, binding_scalar::float32, binding_scalar::float64})
            if (binding_scalar_name(type) == name)
                return type;
        throw std::runtime_error("invalid fixture scalar: " + name);
    }

    const test hash_vectors("PrSM ABI: standard SHA-256 vectors and padding boundaries", [] {
        check_equal(binding_sha256(""),
                    std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
        check_equal(binding_sha256("abc"),
                    std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
        check_equal(binding_sha256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
                    std::string("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
        check_equal(binding_sha256(std::string(1000000, 'a')),
                    std::string("cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
    });

    const test frontend_independence("PrSM IR: header/module provenance does not change ABI", [] {
        auto header = example();
        auto module = header;
        module.sources = {{binding_source_kind::named_module, "engine.actor"}};
        module.callables[0].position = {"D:/checkout/actor.ixx", 99, 1};
        module.types[0].script_name = "Game::Actor";
        module.callables[0].script_name = "Counter";
        const auto a = valid(header);
        const auto b = valid(module);
        check_equal(a.callables()[0].abi_descriptor, b.callables()[0].abi_descriptor);
        check_equal(a.callables()[0].abi_sha256, b.callables()[0].abi_sha256);
        check(a.sources()[0].kind != b.sources()[0].kind);
    });

    const test contract_mutation("PrSM ABI: semantic and native shape changes invalidate signatures", [] {
        const auto original = valid(example()).callables()[0].abi_sha256;
        const auto differs = [&](auto mutate) {
            auto input = example();
            mutate(input);
            check(valid(input).callables()[0].abi_sha256 != original);
        };
        differs([](auto& x) { x.name = "other_engine"; });
        differs([](auto& x) { x.target.triple = "x86_64-pc-linux-gnu"; });
        differs([](auto& x) { x.callables[0].symbol += "_v2"; });
        differs([](auto& x) { x.callables[0].result = binding_scalar::uint64; });
        differs([](auto& x) { x.callables[0].cpp_signature = "int ()"; });
        differs([](auto& x) { x.callables[0].operation = binding_operation::method; });
        differs([](auto& x) {
            x.types[0].cpp_name = "engine::Other";
            x.callables[0].receiver = "engine::Other";
        });
    });

    const test invalid_contracts("PrSM IR: unsafe or inconsistent contracts fail closed", [] {
        rejected([](auto& x) { x.target.pointer_width = 32; });
        rejected([](auto& x) { x.target.triple = "unknown"; });
        rejected([](auto& x) { x.sources.clear(); });
        rejected([](auto& x) { x.sources[0] = {binding_source_kind::named_module, "bad;import"}; });
        rejected([](auto& x) { x.sources[0].kind = static_cast<binding_source_kind>(99); });
        rejected([](auto& x) { x.sources.push_back(x.sources[0]); });
        rejected([](auto& x) { x.types[0].source = 99; });
        rejected([](auto& x) { x.types.push_back(x.types[0]); });
        rejected([](auto& x) { x.callables[0].receiver = "engine::Other"; });
        rejected([](auto& x) { x.callables[0].symbol = "ce_gc_alloc"; });
        rejected([](auto& x) { x.callables[0].symbol = "ce_prsm_bind_bad;injection"; });
        rejected([](auto& x) { x.callables.push_back(x.callables[0]); });
        rejected([](auto& x) { x.callables[0].cpp_signature.clear(); });
        rejected([](auto& x) { x.callables[0].result = static_cast<binding_scalar>(99); });
        rejected([](auto& x) { x.callables[0].effect = static_cast<binding_effect>(99); });
        rejected([](auto& x) { x.callables[0].operation = static_cast<binding_operation>(99); });
        rejected([](auto& x) { x.callables[0].result = binding_scalar::void_; });
        rejected([](auto& x) { x.callables[0].parameters = {binding_scalar::void_}; });
        rejected([](auto& x) { x.callables[0].parameters.resize(65, binding_scalar::int32); });
    });

    const test argument_identity("PrSM ABI: effects and argument order/width/sign participate in identity", [] {
        auto input = example();
        auto& call = input.callables[0];
        call.operation = binding_operation::method;
        call.parameters = {binding_scalar::int8, binding_scalar::uint64};
        const auto original = valid(input).callables()[0].abi_sha256;
        call.effect = binding_effect::write;
        check(valid(input).callables()[0].abi_sha256 != original);
        call.effect = binding_effect::read;
        std::reverse(call.parameters.begin(), call.parameters.end());
        check(valid(input).callables()[0].abi_sha256 != original);
        call.parameters = {binding_scalar::uint8, binding_scalar::uint64};
        check(valid(input).callables()[0].abi_sha256 != original);
        call.parameters = {binding_scalar::int16, binding_scalar::uint64};
        check(valid(input).callables()[0].abi_sha256 != original);
    });

    const test properties("PrSM IR: paired properties must agree on type and access", [] {
        auto input = example();
        auto setter = input.callables.front();
        setter.cpp_name = "SetCount";
        setter.cpp_signature = "void (int)";
        setter.symbol = "ce_prsm_bind_actor_set";
        setter.operation = binding_operation::setter;
        setter.effect = binding_effect::write;
        setter.result = binding_scalar::void_;
        setter.parameters = {binding_scalar::int32};
        input.callables.push_back(setter);
        const auto a = valid(input);
        std::reverse(input.callables.begin(), input.callables.end());
        const auto b = valid(input);
        check_equal(a.callables()[0].abi_sha256, b.callables()[0].abi_sha256);
        input.callables[0].parameters = {binding_scalar::int64};
        diagnostics report;
        check(!build_binding_module(input, report));
        input.callables = {setter};
        diagnostics write_only;
        check(!build_binding_module(input, write_only));
    });

    const test owners("PrSM IR: owner ingress preserves nominal identity and rejects global collisions", [] {
        auto input = example();
        auto owner = input.callables.front();
        owner.receiver.clear();
        owner.cpp_name = "binding_owner";
        owner.cpp_signature = "handle:engine::Actor()";
        owner.script_name = "Engine::Owner";
        owner.symbol = "ce_prsm_bind_actor_owner";
        owner.operation = binding_operation::owner;
        owner.result = binding_scalar::uint64;
        owner.handle_result = "engine::Actor";
        input.callables.push_back(owner);
        check_equal(valid(input).callables().size(), std::size_t{2});
        for (int mutation = 0; mutation < 10; ++mutation)
        {
            auto bad = input;
            auto& call = bad.callables.back();
            switch (mutation)
            {
            case 0: call.handle_result = "engine::Missing"; break;
            case 1: call.receiver = "engine::Actor"; break;
            case 2: call.parameters = {binding_scalar::int32}; break;
            case 3: call.effect = binding_effect::write; break;
            case 4: call.is_const = true; break;
            case 5: call.is_noexcept = true; break;
            case 6: call.script_name = "Engine::Actor"; break;
            case 7: call.script_name = "Engine::Actor::Owner"; break;
            case 8: call.script_name = "std::Owner"; break;
            default: call.script_name = "uint64"; break;
            }
            diagnostics report;
            check(!build_binding_module(std::move(bad), report));
        }
    });

    const test receiver_kinds("PrSM IR: receiver kinds are ABI identity and must match their nominal type", [] {
        const auto handle = valid(example()).callables().front();
        check(handle.abi_descriptor.find("30:domain_generation_nominal_type;") != std::string::npos);
        std::set<std::string> digests{handle.abi_sha256};
        for (const auto kind : {binding_receiver::entity, binding_receiver::component})
        {
            auto input = example();
            input.types.front().receiver = kind;
            input.callables.front().receiver_kind = kind;
            const auto call = valid(input).callables().front();
            const auto field = binding_receiver_validation(kind);
            check(call.abi_descriptor.find(std::to_string(field.size()) + ":" + field + ";") != std::string::npos);
            check(digests.insert(call.abi_sha256).second);
            // A callable cannot claim another kind than its type declares.
            auto mismatched = input;
            mismatched.callables.front().receiver_kind = binding_receiver::handle;
            diagnostics report;
            check(!build_binding_module(std::move(mismatched), report));
        }
        check_equal(binding_receiver_name(binding_receiver::component), std::string("component"));
        check_equal(binding_receiver_validation(binding_receiver::entity), std::string("entity_generation"));
    });

    const test common_vectors("PrSM ABI: independently encoded cross-language vectors", [] {
        std::ifstream input(REFLGEN_PRSM_VECTOR_FILE, std::ios::binary);
        require(input.good(), "cannot read PrSM ABI vectors");
        std::string line;
        unsigned count = 0;
        while (std::getline(input, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.empty() || line.front() == '#')
                continue;
            const auto f = split(line, '\t');
            require(f.size() == 13, "malformed PrSM ABI vector");
            auto model = example();
            model.name = f[2];
            model.target.triple = f[1];
            auto& call = model.callables[0];
            call.symbol = f[3];
            call.operation = f[4] == "getter"   ? binding_operation::getter
                             : f[4] == "setter" ? binding_operation::setter
                                                : binding_operation::method;
            call.receiver = f[5];
            call.cpp_name = f[6];
            call.cpp_signature = f[7];
            call.effect = f[8] == "read" ? binding_effect::read : binding_effect::write;
            call.result = scalar(f[9]);
            if (!f[10].empty())
                for (const auto& name : split(f[10], ','))
                    call.parameters.push_back(scalar(name));
            if (call.operation == binding_operation::setter)
            {
                auto getter = example().callables[0];
                getter.symbol = "ce_prsm_bind_companion_getter";
                model.callables.push_back(getter);
            }
            const auto module = valid(model);
            const auto found = std::find_if(module.callables().begin(), module.callables().end(),
                                            [&](const auto& c) { return c.declaration.symbol == f[3]; });
            require(found != module.callables().end());
            check_equal(found->abi_descriptor, f[11]);
            check_equal(found->abi_sha256, f[12]);
            ++count;
        }
        check_equal(count, 7u);
    });
} // namespace
