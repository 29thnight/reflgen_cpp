#include <ifc/reader.hxx>
#include <ifc/tooling.hxx>
#include <filesystem>
#include <stdexcept>
#include <string>

void ifc_assert(const char* message, const char*, int) { throw std::runtime_error(message); }

int main(int argc, char** argv)
{
    if (argc != 3) return 1;
    using namespace ifc;
    using namespace ifc::symbolic;
    tool::InputFile consumer_file{std::filesystem::path(argv[1])};
    tool::InputFile provider_file{std::filesystem::path(argv[2])};
    InputIfc consumer{consumer_file.contents()}, provider{provider_file.contents()};
    const auto consumer_path = std::filesystem::path(argv[1]).u8string();
    const auto provider_path = std::filesystem::path(argv[2]).u8string();
    const auto options = IfcOptions(to_underlying(IfcOptions::IntegrityCheck) |
                                    to_underlying(IfcOptions::AllowAnyPrimaryInterface));
    if (!consumer.validate<UnitSort::Primary>(Pathname{consumer_path.c_str()}, Architecture::Unknown, Pathname{}, options) ||
        !provider.validate<UnitSort::Primary>(Pathname{provider_path.c_str()}, Architecture::Unknown, Pathname{}, options))
        return 2;
    Reader source{consumer}, target{provider};
    if (std::string(provider.get(provider.header()->unit.module_name())) != "reflgen.test.imported_base") return 3;
    for (const auto& reference : source.partition<ReferenceDecl>())
    {
        if (std::string(source.get(reference.translation_unit.owner)) != "reflgen.test.imported_base" ||
            reference.local_index.sort() != DeclSort::Template)
            continue;
        const auto templates = target.partition<TemplateDecl>();
        const auto index = to_underlying(reference.local_index.index());
        if (index >= templates.size()) return 4;
        const auto& declaration = target.get<TemplateDecl>(reference.local_index);
        const auto* scope = target.get_if<ScopeDecl>(declaration.entity.decl);
        const auto* base = scope ? target.get_if<BaseType>(scope->base) : nullptr;
        const auto* type = base ? target.get_if<DesignatedType>(base->type) : nullptr;
        const auto* parameter = type ? target.get_if<ParameterDecl>(type->decl) : nullptr;
        if (!parameter || parameter->position != 2 || base->access != Access::Public ||
            !target.partition<PartialSpecializationDecl>().empty()) return 5;
        return 0;
    }
    return 6;
}
