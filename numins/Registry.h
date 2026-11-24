// Registry.h — the list of every Numin in the collection.
#pragma once

#include "Numin.h"

#include <memory>
#include <string>
#include <vector>

namespace numinos
{

struct NuminEntry
{
    const char* name;
    const char* category;
    std::unique_ptr<Numin> (*create)(); // returns a Numin with default params
};

// Every registered Numin, in registration order.
const std::vector<NuminEntry>& allNumins();

// Case-insensitive lookup by name. Returns nullptr if there's no such Numin.
std::unique_ptr<Numin> createNumin(const std::string& name);

} // namespace numinos
