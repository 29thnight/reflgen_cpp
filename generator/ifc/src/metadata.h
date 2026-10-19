#pragma once
#include "attribute_scan.h"
#include "model.h"
#include <filesystem>
#include <map>
#include <set>

namespace reflgen::generator
{
    // MSVC discards unknown attributes in IFC. Only the compiler's /P output supplies metadata;
    // semantic declarations and visibility still come exclusively from IFC.
    class module_metadata
    {
      public:
        explicit module_metadata(const std::filesystem::path& path);
        std::vector<attribute_group> declaration(const source_position& position, std::string_view name,
                                                 bool type = false) const;
        std::vector<attribute_use> uses(const std::vector<attribute_group>& groups, const std::set<std::string>& scopes,
                                        const qualifier_lookup& lookup = {}, bool interop = false) const;
        const scanned_attribute* directive(const std::vector<attribute_group>& groups, std::string_view name) const;
        std::string arguments(const scanned_attribute& attribute, const qualifier_lookup& lookup = {}) const;
        const std::string& module() const { return module_; }
        std::string catalog(const std::set<std::string>& scopes) const;
        void validate_prsm_selection() const;

      private:
        struct location
        {
            std::string file;
            unsigned line;
        };
        std::string module_;
        std::vector<token> tokens_;
        std::vector<location> locations_;
        std::vector<attribute_group> groups_;
        mutable std::set<std::size_t> consumed_bindings_;
        std::map<std::pair<std::string, unsigned>, std::vector<std::size_t>> lines_;
    };
} // namespace reflgen::generator
