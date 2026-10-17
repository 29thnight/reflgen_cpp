module;
#include "reflgen/json.h"
export module reflgen.json;
export import reflgen.core;
export namespace reflgen::json
{
    using ::reflgen::json::from_string;
    using ::reflgen::json::reader;
    using ::reflgen::json::to_string;
    using ::reflgen::json::writer;
} // namespace reflgen::json
