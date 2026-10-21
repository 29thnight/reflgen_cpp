// 생성 코드의 모양 가운데 빌드가 기대는 것 — 주입 header 의 형식 검사.
#include "emit.h"
#include "harness.h"
#include "reflgen/core/version.h"
#include <stdexcept>
#include <string>

namespace
{
    using reflgen::generator::emit_module_header;
    using reflgen_test::check;
    using reflgen_test::test;

    // 주입 header 는 모든 번역 단위에 들어가므로 여기서 한 번 검사하면 모듈 전체가 검사된다. 생성기와 header 의
    // 판이 어긋난 설치(옛 생성기 + 새 header 등)를 컴파일 오류로 멈춘다.
    const test module_header_checks_format("emit: the injection header checks the generated code format", [] {
        const std::string text = emit_module_header("game", {}, {});
        check(text.find("#include \"reflgen/core/version.h\"") != std::string::npos);
        const std::string check_line =
            "static_assert(::reflgen::generated_code_format == " + std::to_string(reflgen::generated_code_format) + ',';
        check(text.find(check_line) != std::string::npos);
    });

    reflgen::generator::header_model player_header(bool nested)
    {
        reflgen::generator::class_model player;
        player.qualified_name = "game::player";
        player.name = "player";
        player.class_key = "class";
        player.schema_name = "\"game.player\"";
        player.namespaces = {"game"};
        player.nested = nested;
        player.fields.push_back({"hp", {"C:/src/player.h", 5, 9}, {}});
        return {"C:/src/player.h", {player}, {}};
    }

    // 가벼운 주입: 전방 선언으로 설 수 있으면 원본 header 를 include 하지 않는다(모든 번역 단위가 그것을 끌고
    // 다니지 않게). 서술은 T 에 의존하는 부분 특수화라 T 가 완전해지는 사용 자리에서 실체화된다.
    const test light_header("emit: a forward-declarable type does not include its source header", [] {
        const std::string text = reflgen::generator::emit_header(player_header(false), "C:/out/player.reflgen.h");
        check(text.find("#include \"C:/src/player.h\"") == std::string::npos, text);
        check(text.find("namespace game\n{\n    class player;\n}") != std::string::npos, text);
        check(text.find("requires ::std::same_as<T, ::game::player>\nstruct reflgen::access::describer<T>") !=
                  std::string::npos,
              text);
        check(text.find("::reflgen::field<&T::hp>") != std::string::npos, text);
    });

    const test fallback_header("emit: a type that cannot be forward-declared includes its source header", [] {
        const std::string text = reflgen::generator::emit_header(player_header(true), "C:/out/player.reflgen.h");
        check(text.find("// The source header is included because 'game::player' is declared inside a class.\n"
                        "#include \"C:/src/player.h\"") != std::string::npos,
              text);
        check(text.find("class player;") == std::string::npos, text);
    });

    const test enum_forward_declaration(
        "emit: enums are forward-declared with the enum-base they were written with", [] {
            reflgen::generator::enum_model color;
            color.qualified_name = "game::color";
            color.name = "color";
            color.namespaces = {"game"};
            color.scoped = true;
            color.underlying_type = "unsigned char";
            color.enumerators = {"red"};
            const std::string text =
                reflgen::generator::emit_header({"C:/src/color.h", {}, {color}}, "C:/out/color.reflgen.h");
            check(text.find("enum class color : unsigned char;") != std::string::npos, text);
            check(text.find("::reflgen::enum_entry<E>{\"red\", E::red}") != std::string::npos, text);
        });

    const test overloaded_methods("emit: header and IFC schemas select exact overloaded member pointers", [] {
        reflgen::generator::header_model header = player_header(false);
        reflgen::generator::method_model mutable_damage;
        mutable_damage.name = "damage";
        mutable_damage.parameters = {"amount"};
        mutable_damage.position = {"C:/src/player.h", 8, 9};
        mutable_damage.is_overloaded = true;
        mutable_damage.pointer_type = "int (T::*)(int) &";
        reflgen::generator::method_model const_damage = mutable_damage;
        const_damage.pointer_type = "int (T::*)(float) const & noexcept";
        const_damage.access = "private";
        header.classes.front().methods = {mutable_damage, const_damage};

        const std::string plain = reflgen::generator::emit_header(header, "C:/out/player.reflgen.h");
        const std::string imported =
            reflgen::generator::emit_ifc_header(header, "game.types", "game", "C:/out/reflgen_game.h");
        for (const std::string* text : {&plain, &imported})
        {
            check(text->find("::reflgen::method<static_cast<int (T::*)(int) &>(&T::damage)>") != std::string::npos,
                  *text);
            check(text->find("::reflgen::method<static_cast<int (T::*)(float) const & noexcept>(&T::damage)>") !=
                      std::string::npos,
                  *text);
            check(text->find(".named(\"damage\").parameters(\"amount\")") != std::string::npos, *text);
            check(text->find("struct reflgen::access::describer<T>") < text->find("&T::damage"), *text);
        }
    });

    const test unresolved_overload("emit: an overloaded method without an exact pointer type is rejected", [] {
        reflgen::generator::header_model header = player_header(false);
        reflgen::generator::method_model damage;
        damage.name = "damage";
        damage.is_overloaded = true;
        header.classes.front().methods.push_back(damage);
        reflgen_test::check_throws<std::invalid_argument>(
            [&] { reflgen::generator::emit_ifc_header(header, "game.types", "game", "C:/out/reflgen_game.h"); },
            "game::player::damage");
    });

    const test ifc_nested_base("emit: IFC schemas retain bases and friend access without redeclaring imported types", [] {
        reflgen::generator::header_model header = player_header(true);
        reflgen::generator::class_model& player = header.classes.front();
        player.qualified_name = "game::outer::player";
        player.fields.front().access = "private";
        reflgen::generator::type_reference base;
        base.qualified_name = "game::outer::base";
        base.name = "base";
        base.nested = true;
        player.bases.push_back(base);
        const std::string text =
            reflgen::generator::emit_ifc_header(header, "game.types", "game", "C:/out/reflgen_game.h");
        check(text.find("import game.types;") != std::string::npos, text);
        check(text.find("requires ::std::same_as<T, ::game::outer::player>") != std::string::npos, text);
        check(text.find("::reflgen::base<::game::outer::base>") != std::string::npos, text);
        check(text.find("struct reflgen::access::describer<T>") < text.find("::reflgen::field<&T::hp>"), text);
        check(text.find("class player;") == std::string::npos, text);
        check(text.find("struct base;") == std::string::npos, text);
        check(text.find("#include \"C:/src/player.h\"") == std::string::npos, text);
    });

    const test module_source("emit: the registration source sees complete types and offers both overloads", [] {
        const std::string text = reflgen::generator::emit_module_source("game", {player_header(false)}, {});
        check(text.find("#include \"C:/src/player.h\"") != std::string::npos, text);
        check(text.find("#include \"reflgen/runtime/registry.h\"") != std::string::npos, text);
        check(text.find("target.add<::game::player>();") != std::string::npos, text);
        check(text.find("register_game(::reflgen::default_registry());") != std::string::npos, text);
    });

    // 등록 header 는 서술자를 만들기 전에 — 반영 타입의 header 와 등록소 header 보다 앞에 온다.
    const test module_source_registration_headers(
        "emit: registration headers come first in the registration source", [] {
            const std::string text =
                reflgen::generator::emit_module_source("game", {player_header(false)}, {"C:/src/serializers.h"});
            const std::size_t registration = text.find("#include \"C:/src/serializers.h\"");
            check(registration != std::string::npos, text);
            check(registration < text.find("#include \"reflgen/runtime/registry.h\""), text);
            check(registration < text.find("#include \"C:/src/player.h\""), text);
        });

    const test ifc_source_registration_dependencies(
        "emit: IFC registration imports and schema precede serializer specialization headers", [] {
            reflgen::generator::header_model model = player_header(false);
            model.path = "C:/build/game.ifc";
            const std::string text = reflgen::generator::emit_ifc_source(
                model, "game.types", "game", "schema", {"C:/src/serializers.h"}, {"game.ownership", "game.resources"});
            const std::size_t provider = text.find("import game.types;");
            const std::size_t schema = text.find("#include \"reflgen_game.h\"");
            const std::size_t registration = text.find("#include \"C:/src/serializers.h\"");
            const std::size_t ownership = text.find("import game.ownership;");
            const std::size_t resources = text.find("import game.resources;");
            check(provider != std::string::npos, text);
            check(schema != std::string::npos, text);
            check(registration != std::string::npos, text);
            check(ownership != std::string::npos, text);
            check(resources != std::string::npos, text);
            check(text.find("#include \"reflgen/runtime/registry.h\"") < provider, text);
            check(provider < ownership && ownership < resources && resources < schema, text);
            check(schema < registration, text);
            check(registration < text.find("target.add<::game::player>();"), text);
            check(text.find("register_game(::reflgen::default_registry());") != std::string::npos, text);
            check(text.find("#include \"C:/build/game.ifc\"") == std::string::npos, text);
        });

    const test ifc_source_fingerprint("emit: IFC registration source changes when its schema changes", [] {
        const reflgen::generator::header_model model = player_header(false);
        const std::string first = reflgen::generator::emit_ifc_source(model, "game.types", "game", "old schema");
        const std::string second = reflgen::generator::emit_ifc_source(model, "game.types", "game", "new schema");
        check(first != second);
        reflgen_test::check_equal(first, reflgen::generator::emit_ifc_source(model, "game.types", "game", "old schema"));
    });

    const test ifc_source_without_classes("emit: empty IFC registration omits serializer dependencies", [] {
        const std::string text = reflgen::generator::emit_ifc_source(
            {"C:/build/game.ifc", {}, {}}, "game.types", "game", "schema", {"C:/src/serializers.h"}, {"game.ownership"});
        check(text.find("import game.types;") != std::string::npos, text);
        check(text.find("serializers.h") == std::string::npos, text);
        check(text.find("import game.ownership;") == std::string::npos, text);
        check(text.find("static_cast<void>(target);") != std::string::npos, text);
    });

    // 반영 클래스가 없는 모듈은 서술자를 만들지 않는다 — 등록 header 를 include 하지 않는다. 빌드 설정이 모든 프로젝트에
    // 같은 등록 header 를 주어도(그 header 의 include 경로가 없는 프로젝트라도) 빈 등록 함수는 컴파일된다.
    const test module_source_without_classes(
        "emit: a module without reflected classes does not include registration headers", [] {
            const std::string text =
                reflgen::generator::emit_module_source("game", {{"C:/src/plain.h", {}, {}}}, {"C:/src/serializers.h"});
            check(text.find("serializers.h") == std::string::npos, text);
            check(text.find("static_cast<void>(target);") != std::string::npos, text);
        });
} // namespace
