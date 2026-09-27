// 패키지 소비 시험 — 생성된 서술로 직렬화하고 등록한다. 생성 파일을 include 하지 않는다(주입이 들어온다).
#include "player.h"
#include "reflgen/json.h"
#include <cstdio>

int main()
{
    reflgen::generated::register_Consumer();
    const bool registered = reflgen::default_registry().find("consumer.player") != nullptr;
    const std::string text = reflgen::json::to_string(consumer::player{});
    const bool ok = registered && text == R"({"name":"ada","level":7})";
    std::printf("%s\nnuget consumer: %s\n", text.c_str(), ok ? "ok" : "FAILED");
    return ok ? 0 : 1;
}
