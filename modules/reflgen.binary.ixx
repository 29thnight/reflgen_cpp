module;
#include "reflgen/binary.h"
export module reflgen.binary;
export import reflgen.core;
export namespace reflgen::binary
{
    using ::reflgen::binary::from_bytes;
    using ::reflgen::binary::reader;
    using ::reflgen::binary::to_bytes;
    using ::reflgen::binary::writer;
} // namespace reflgen::binary
