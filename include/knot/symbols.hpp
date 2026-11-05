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

    struct Sym { std::uint64_t value, size; std::string name; };
    struct Load { std::uint64_t vaddr, memsz, offset, filesz; };

    // Data symbol containing file-relative virtual address `vaddr`, if any.
    const Sym* object_at(std::uint64_t vaddr) const;
    const std::vector<Load>& loads() const noexcept { return loads_; }
    bool has_symtab() const noexcept { return has_symtab_; }

private:
    std::vector<Sym> objects_;   // sorted by value
    std::vector<Load> loads_;
    bool has_symtab_ = false;
};

struct SymbolInfo {
    std::string name;        // demangled; "(anonymous namespace)::" removed for display
    std::string full_name;   // demangled, complete
    std::string object;      // basename of the ELF file
    std::uint64_t offset;    // address - symbol start
};

class SymbolResolver {
public:
    explicit SymbolResolver(const Source& src);

    // Name of the global/static variable containing runtime address `addr`.
    std::optional<SymbolInfo> resolve(std::uint64_t addr);
    // "libapp.so .bss+0x2840"-style location when no symbol is found.
    std::string location(std::uint64_t addr);

private:
    struct ObjectRange {
        std::string path;
        std::uint64_t bias = 0;             // runtime = file vaddr + bias
        std::uint64_t lo = 0, hi = 0;       // runtime range covered by its PT_LOADs
        const ElfObject* elf = nullptr;
    };
    const ObjectRange* object_for(std::uint64_t addr);

    std::string root_;
    std::vector<Mapping> maps_;
    std::map<std::string, std::unique_ptr<ElfObject>> cache_;
    std::vector<ObjectRange> ranges_;
    bool built_ = false;
};

std::string demangle(const std::string& mangled);

}  // namespace knot
