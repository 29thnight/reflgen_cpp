#pragma once
// 서술 공급 지점 — 타입을 서술하는 길은 둘이다.
//
//   ① 클래스 안 레시피: static consteval auto reflect() { return reflgen::schema<T>(...); }
//   ② 외부 특수화:     template<> struct reflgen::reflection<T> { static constexpr auto value = ...; };
//
// ②는 손댈 수 없는 타입(서드파티)과 코드 생성기의 출력이 들어가는 자리다. 열거형도
// ②로 정확한 열거자 표를 공급할 수 있다(그러면 값 범위 스캔을 하지 않는다).
// 생성기는 ②를 만들되 본문을 access::describer<T> 부분 특수화에 둔다(private 접근 때문).
// 소비자는 둘 중 어느 것인지 알 필요가 없다 — reflgen::schema_of<T> 가 단일 창구다.
#include <utility>

namespace reflgen
{
    template<class T>
    struct reflection;

    // 클래스 안 reflect() 를 private 으로 둘 수 있게 하는 통로. 클래스가
    //   friend struct reflgen::access;
    // 를 선언하면 된다. requires 식의 접근 검사는 식이 놓인 자리(이 클래스 안)에서
    // 이뤄지므로 friend 관계가 판별에도 그대로 통한다.
    struct access
    {
        template<class T>
        static constexpr bool has_member_reflect = requires { T::reflect(); };

        template<class T>
        static consteval auto reflect()
        {
            return T::reflect();
        }

        // 코드 생성기(reflgen-cli)가 타입마다 이름공간 범위에서 부분 특수화하는 자리. 정의는 없다.
        //
        //   template<class T> requires std::same_as<T, game::player>
        //   struct reflgen::access::describer<T> { static consteval auto describe() { ... } };
        //
        // 본문이 T 에 의존하므로 T 를 쓰는 자리(타입이 완전한 곳)에서 실체화된다 — 생성 header 는 원본 header 를
        // include 하지 않고 전방 선언만으로 선다(강제 include 가 모든 번역 단위에 원본 header 를 끌고 다니지 않는다).
        // access 의 멤버라서, 클래스가 friend struct reflgen::access; 를 선언하면 private 멤버의 포인터도 만들 수
        // 있다. 생성된 reflection<T>::value 가 describe() 를 부른다. 손으로 쓰는 서술은 이 자리를 쓰지 않는다(①·②).
        template<class T, class = void>
        struct describer;
    };

    namespace detail
    {
        template<class T>
        concept has_member_reflection = access::has_member_reflect<T>;

        template<class T>
        concept has_external_reflection = requires { reflection<T>::value; };

        // T 가 이 자리에서 완전한가. 주입 header 는 반영 타입을 전방 선언만 하므로 원본 header 를 보지 않은 번역
        // 단위에서는 불완전하다. 오류를 고르는 데만 쓴다 — 불완전하면 어느 쪽이든 컴파일이 멈추므로 이 판정이 번역
        // 단위마다 동작을 가르지 않는다(생성된 reflection<T> 의 static_assert, 스마트 포인터 요소의 직렬화 판정).
        template<class T>
        concept complete_type = requires { sizeof(T); };
    } // namespace detail
} // namespace reflgen
