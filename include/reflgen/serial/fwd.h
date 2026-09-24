#pragma once
// 직렬화 진입점의 선언. 범주별 구현(serial/detail/*.h)이 원소를 재귀 직렬화할 때
// 이 선언을 부른다 — 정의(serializer.h)가 범주 구현보다 뒤에 오므로 선언이 먼저
// 보여야 한다.
namespace reflgen
{
    class writer;
    class reader;

    // 사용자 정의 직렬화의 확장 지점. std::hash·std::formatter 처럼 특수화한다:
    //
    //   template<>
    //   struct reflgen::serializer<my_type>
    //   {
    //       static void write(reflgen::writer& out, const my_type& value);
    //       static void read(reflgen::reader& in, my_type& value);
    //   };
    template<class T>
    struct serializer;

    template<class T>
    void serialize(writer& out, const T& value);

    template<class T>
    void deserialize(reader& in, T& value);
} // namespace reflgen
