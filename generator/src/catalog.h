#pragma once
// attribute 카탈로그 — 편집기 확장이 [[scope:: 뒤에서 제안할 것들을 파싱 결과에서 뽑는다.
//
// 대상은 attribute 이름공간(reflgen 과 ATTRIBUTE_SCOPES)에 바로 선언된 클래스 타입이다.
//   reflgen   reflgen/core/attributes.h 의 struct 만(그 이름공간에는 descriptor·registry 같은 타입도 있다).
//   사용자    [[reflgen::reflect]] 를 단 데이터 타입은 뺀다. [[reflgen::attribute]] 를 단 타입이 하나라도
//             있으면 그것들만, 없으면 나머지 전부 — 표지 없이도 돌고, 이름공간이 넓으면 표지로 좁힌다.
#include "model.h"
#include <clang-c/Index.h>
#include <functional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace reflgen::generator
{
    // 선언에 [[reflgen::<directive>]] 가 붙었는가. attribute 는 AST 에 남지 않아 추출기가 token 으로 가린다.
    using directive_query = std::function<bool(CXCursor declaration, std::string_view directive)>;

    // 생성기 지시어(reflect, ignore, attribute)가 앞에 오고, 그 뒤는 (scope, name) 순이다.
    std::vector<attribute_info> collect_attribute_catalog(CXTranslationUnit unit, const std::set<std::string>& scopes,
                                                          const directive_query& declares);

    // 탭으로 가른 줄 단위 텍스트. 첫 줄은 "reflgen-attributes\t1", 그다음은
    // kind \t scope \t name \t signature \t summary. 값 안의 '\\', '\t', '\n' 은 역슬래시로 적는다.
    std::string format_attribute_catalog(const std::vector<attribute_info>& attributes);
} // namespace reflgen::generator
