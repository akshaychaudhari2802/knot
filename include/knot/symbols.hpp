// knot/symbols.hpp — name GLOBAL variables (e.g. g_batch_mu) from ELF symbol tables.
//
// Deliberately small (no DWARF): reads .symtab, or .dynsym when stripped,
// keeping STT_OBJECT entries. Enough to name global and static locks in
// Tier 1 with no dependencies. Stacks and file:line come from Tier 2 (libdwfl).
#pragma once

#include "knot/source.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace knot {

class ElfObject {
public:
    static std::unique_ptr<ElfObject> load(const std::string& file_path);

