// Registry.h — the list of every Numin in the collection.
#pragma once

#include "Numin.h"

#include <memory>
#include <string>
#include <vector>

namespace numinos
{

// Whether a Numin ships. Public ones are in every build. Ones in the shop are still being made:
// they're in your own builds (listed under "In the shop") but left out of release builds, so
// people who install Numinos never see them. Promoting one is changing one word in Registry.cpp.
enum class Status
{
    Public,
    InTheShop
};

struct NuminEntry
{
    const char* name;
    const char* category;
    Status status;
    std::unique_ptr<Numin> (*create)(); // returns a Numin with default params
};

// Every Numin in this build, in registration order: all of them in your own builds, only the
// public ones in a release (built with NUMINOS_INCLUDE_SHOP=OFF, as scripts/package.sh does).
const std::vector<NuminEntry>& allNumins();

// Case-insensitive lookup by name. Returns nullptr if there's no such Numin.
std::unique_ptr<Numin> createNumin(const std::string& name);

} // namespace numinos
