// Registry.cpp — add one #include and one entry<>() line per Numin.
// scripts/new-numin.sh does this for you (it looks for the @marker comments).

#include "Registry.h"

#include "Honey/Honey.h"
// @numin-includes

#include <cctype>

namespace numinos
{

namespace
{
template <typename T> NuminEntry entry()
{
    static const T prototype; // only used to read name/category
    return {prototype.name(), prototype.category(), []() -> std::unique_ptr<Numin> {
                auto n = std::make_unique<T>();
                n->resetParamsToDefaults();
                return n;
            }};
}
} // namespace

const std::vector<NuminEntry>& allNumins()
{
    static const std::vector<NuminEntry> list = {
        entry<Honey>(),
        // @numin-entries
    };
    return list;
}

std::unique_ptr<Numin> createNumin(const std::string& name)
{
    auto lower = [](std::string s) {
        for (auto& c : s)
            c = (char)std::tolower((unsigned char)c);
        return s;
    };
    for (const auto& e : allNumins())
        if (lower(e.name) == lower(name))
            return e.create();
    return nullptr;
}

} // namespace numinos
