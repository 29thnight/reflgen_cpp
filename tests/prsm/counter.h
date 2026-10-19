#pragma once
namespace demo
{
    using count_type = int;
    class [[reflgen::interop("prsm"), reflgen::lifetime("borrowed"), reflgen::owner("demo::Owner")]] Counter
    {
      public:
        [[reflgen::interop("prsm"), reflgen::property_get("Count")]]
        count_type Get() const noexcept
        {
            return value_;
        }
        [[reflgen::interop("prsm"), reflgen::property_set("Count")]]
        void Set(count_type value)
        {
            value_ = value;
        }
        [[reflgen::interop("prsm")]]
        count_type Add(count_type value)
        {
            if (value < 0)
                throw 7;
            value_ += value;
            return value_;
        }
        [[reflgen::interop("prsm")]]
        bool Enabled(bool value)
        {
            return value;
        }
        [[reflgen::interop("prsm"), reflgen::effect("read")]]
        double Scale(float value, double scale) const
        {
            return value * scale;
        }
        int Hidden() const { return -1; }

      private:
        count_type value_ = 7;
    };
} // namespace demo
