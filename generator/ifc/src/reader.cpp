// Adapted from Microsoft IFC SDK's reader.cxx. Copyright Microsoft Corporation.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// The SDK rejects new optional MSVC code-generation traits. Reflection does not consume those traits.
// Keep strict handling for every semantic partition, and validate all file ranges before mapping.
#include <ifc/reader.hxx>
#include <stdexcept>

namespace ifc
{
    Reader::Reader(const InputIfc& input) : ifc(input)
    {
        if (!input.header())
            throw InputIfc::MissingIfcHeader{};
        read_table_of_contents();
    }

    void Reader::read_table_of_contents()
    {
        for (const auto& part : ifc.partition_table())
        {
            const auto bytes =
                static_cast<std::uint64_t>(to_underlying(part.cardinality)) * to_underlying(part.entry_size);
            if (to_underlying(part.offset) + bytes > ifc.contents().size())
                throw std::runtime_error("IFC partition exceeds file size");
            const std::string_view name = get(part.name);
            if (name.starts_with(".msvc.code-analysis."))
                continue;
            try
            {
                summary_by_partition_name(toc, name) = part;
            }
            catch (const InvalidPartitionName&)
            {
                if (name != ".msvc.trait.evaluated-initializer" &&
                    name != ".msvc.trait.class-template-inheriting-ctors")
                    throw std::runtime_error("unsupported IFC semantic partition: " + std::string(name));
            }
        }
    }
} // namespace ifc
