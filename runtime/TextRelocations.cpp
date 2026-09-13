#include "TextRelocations.h"

#include "Debug.h"

#if defined(__linux__) && defined(__x86_64__)

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <vector>

#include <fcntl.h>
#include <link.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <elf.h>

#include "Function.h"

static const char* relocTypeName(uint32_t type) {
    switch(type) {
        case R_X86_64_PC32:
            return "R_X86_64_PC32";
        case R_X86_64_PLT32:
            return "R_X86_64_PLT32";
        case R_X86_64_GOTPCREL:
            return "R_X86_64_GOTPCREL";
        case R_X86_64_GOTPCRELX:
            return "R_X86_64_GOTPCRELX";
        case R_X86_64_REX_GOTPCRELX:
            return "R_X86_64_REX_GOTPCRELX";
        case R_X86_64_GOTPC32:
            return "R_X86_64_GOTPC32";
        case R_X86_64_TLSGD:
            return "R_X86_64_TLSGD";
        case R_X86_64_TLSLD:
            return "R_X86_64_TLSLD";
        case R_X86_64_GOTTPOFF:
            return "R_X86_64_GOTTPOFF";
        case R_X86_64_GOTPC32_TLSDESC:
            return "R_X86_64_GOTPC32_TLSDESC";
        default:
            return "UNKNOWN";
    }
}

static bool relocSupported(uint32_t type) {
    switch(type) {
        case R_X86_64_PC32:
        case R_X86_64_PLT32:
        case R_X86_64_GOTPCREL:
        case R_X86_64_GOTPCRELX:
        case R_X86_64_REX_GOTPCRELX:
        case R_X86_64_GOTPC32:
        case R_X86_64_TLSGD:
        case R_X86_64_TLSLD:
        case R_X86_64_GOTTPOFF:
        case R_X86_64_GOTPC32_TLSDESC:
            return true;
        default:
            return false;
    }
}

static bool relocSafeToIgnore(uint32_t type) {
    // Definitely safe: no relocation.
    return type == R_X86_64_NONE;
}

static uintptr_t get_main_load_bias() {
    struct BiasCtx {
        uintptr_t bias = 0;
        bool found = false;
    } ctx;

    dl_iterate_phdr(
        [](struct dl_phdr_info* info, size_t, void* data) -> int {
            BiasCtx* c = (BiasCtx*)data;
            // The main executable typically has an empty name.
            if(info->dlpi_name == NULL || info->dlpi_name[0] == '\0') {
                c->bias = (uintptr_t)info->dlpi_addr;
                c->found = true;
                return 1; // stop
            }
            return 0;
        },
        &ctx
    );

    return ctx.found ? ctx.bias : 0;
}

bool stabilizer_init_text_relocations(const std::set<Function*>& functions) {
    if(functions.empty()) {
        return true;
    }

    // Build a sorted vector of functions by base address to make range checks fast.
    std::vector<Function*> sorted;
    sorted.reserve(functions.size());
    for(Function* f : functions) {
        sorted.push_back(f);
    }
    std::sort(sorted.begin(), sorted.end(), [](Function* a, Function* b) {
        return (uintptr_t)a->getCodeBase() < (uintptr_t)b->getCodeBase();
    });

    int fd = open("/proc/self/exe", O_RDONLY);
    if(fd < 0) {
        DEBUG("Unable to open /proc/self/exe: %s", strerror(errno));
        return false;
    }

    struct stat st;
    if(fstat(fd, &st) != 0) {
        DEBUG("Unable to stat /proc/self/exe: %s", strerror(errno));
        close(fd);
        return false;
    }

    void* file = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if(file == MAP_FAILED) {
        DEBUG("Unable to mmap /proc/self/exe: %s", strerror(errno));
        return false;
    }

    uint8_t* bytes = (uint8_t*)file;
    if((size_t)st.st_size < sizeof(Elf64_Ehdr)) {
        munmap(file, st.st_size);
        return false;
    }

    Elf64_Ehdr* eh = (Elf64_Ehdr*)bytes;
    if(!(eh->e_ident[EI_MAG0] == ELFMAG0 &&
         eh->e_ident[EI_MAG1] == ELFMAG1 &&
         eh->e_ident[EI_MAG2] == ELFMAG2 &&
         eh->e_ident[EI_MAG3] == ELFMAG3)) {
        munmap(file, st.st_size);
        return false;
    }
    if(eh->e_ident[EI_CLASS] != ELFCLASS64) {
        munmap(file, st.st_size);
        return false;
    }

    if(eh->e_shoff == 0 || eh->e_shentsize != sizeof(Elf64_Shdr) || eh->e_shnum == 0) {
        munmap(file, st.st_size);
        return false;
    }
    if((size_t)eh->e_shoff + (size_t)eh->e_shnum * sizeof(Elf64_Shdr) > (size_t)st.st_size) {
        munmap(file, st.st_size);
        return false;
    }

    Elf64_Shdr* sh = (Elf64_Shdr*)(bytes + eh->e_shoff);

    uintptr_t load_bias = 0;
    if(eh->e_type == ET_DYN) {
        load_bias = get_main_load_bias();
    }

    // Resolve every registered function against an authoritative final-link
    // STT_FUNC symbol before using its extent.  The old implementation used a
    // separate dummy function as the limit, but linkers are free to reorder
    // functions and could place that dummy before the function or interleave
    // unrelated text.
    for(size_t i = 1; i < sorted.size(); i++) {
        if(sorted[i - 1]->getCodeBase() == sorted[i]->getCodeBase()) {
            ABORT("Multiple randomized functions have the same linked address %p. "
                "Function aliases and identical-code folding are unsupported with -Rcode; disable ICF.",
                sorted[i]->getCodeBase());
        }
    }

    std::vector<size_t> resolved_sizes(sorted.size(), 0);
    std::vector<bool> resolved(sorted.size(), false);

    for(uint16_t i = 0; i < eh->e_shnum; i++) {
        // The complete static symbol table is required: .dynsym omits local
        // functions and cannot establish bounds for every registration.
        if(sh[i].sh_type != SHT_SYMTAB) {
            continue;
        }
        if(sh[i].sh_entsize != sizeof(Elf64_Sym) || sh[i].sh_link >= eh->e_shnum) {
            continue;
        }
        if(sh[i].sh_offset > (size_t)st.st_size ||
           sh[i].sh_size > (size_t)st.st_size - sh[i].sh_offset) {
            continue;
        }

        const Elf64_Sym* symbols = (const Elf64_Sym*)(bytes + sh[i].sh_offset);
        size_t count = sh[i].sh_size / sizeof(Elf64_Sym);

        for(size_t s = 0; s < count; s++) {
            const Elf64_Sym& symbol = symbols[s];
            if(ELF64_ST_TYPE(symbol.st_info) != STT_FUNC || symbol.st_size == 0 ||
               symbol.st_shndx == SHN_UNDEF || symbol.st_shndx >= eh->e_shnum) {
                continue;
            }

            const Elf64_Shdr& section = sh[symbol.st_shndx];
            if((section.sh_flags & SHF_EXECINSTR) == 0) {
                continue;
            }
            if(symbol.st_value > UINTPTR_MAX - load_bias ||
               symbol.st_size > UINTPTR_MAX - (load_bias + symbol.st_value)) {
                ABORT("ELF function symbol address overflows the runtime address space");
            }

            uintptr_t base = load_bias + (uintptr_t)symbol.st_value;
            auto it = std::lower_bound(
                sorted.begin(),
                sorted.end(),
                base,
                [](Function* f, uintptr_t addr) {
                    return (uintptr_t)f->getCodeBase() < addr;
                }
            );
            if(it == sorted.end() || (uintptr_t)(*it)->getCodeBase() != base) {
                continue;
            }

            if(section.sh_addr > UINTPTR_MAX - load_bias) {
                ABORT("ELF executable section address overflows the runtime address space");
            }
            uintptr_t section_base = load_bias + (uintptr_t)section.sh_addr;
            if(section.sh_size > UINTPTR_MAX - section_base ||
               base < section_base || base + symbol.st_size > section_base + section.sh_size) {
                ABORT("ELF function extent [%p, %p) lies outside its executable section",
                    (void*)base, (void*)(base + symbol.st_size));
            }

            size_t index = (size_t)(it - sorted.begin());
            if(resolved[index] && resolved_sizes[index] != symbol.st_size) {
                ABORT("Conflicting ELF sizes for randomized function at %p (%zu and %zu)",
                    (void*)base, resolved_sizes[index], (size_t)symbol.st_size);
            }
            resolved[index] = true;
            resolved_sizes[index] = (size_t)symbol.st_size;
        }
    }

    for(size_t i = 0; i < sorted.size(); i++) {
        if((uintptr_t)sorted[i]->getCodeBase() % alignof(FunctionHeader) != 0) {
            ABORT("Randomized function at %p is not aligned to %zu bytes; "
                "rebuild with the current Stabilizer pass for atomic entry publication",
                sorted[i]->getCodeBase(), alignof(FunctionHeader));
        }
        if(!resolved[i]) {
            ABORT("No non-empty ELF STT_FUNC symbol found for randomized function at %p. "
                "Do not strip binaries used with -Rcode.", sorted[i]->getCodeBase());
        }
        if(resolved_sizes[i] <= PATCHABLE_ENTRY_SIZE) {
            ABORT("Randomized function at %p has only %zu linked bytes; expected a body "
                "after the %zu-byte patchable entry emitted by the Stabilizer pass",
                sorted[i]->getCodeBase(), resolved_sizes[i], PATCHABLE_ENTRY_SIZE);
        }
        sorted[i]->setCodeSize(resolved_sizes[i]);

        if(i > 0) {
            uintptr_t previous_base = (uintptr_t)sorted[i - 1]->getCodeBase();
            uintptr_t previous_end = previous_base + sorted[i - 1]->getCodeSize();
            uintptr_t current_base = (uintptr_t)sorted[i]->getCodeBase();
            if(previous_end > current_base) {
                ABORT("Overlapping randomized ELF function extents [%p, %p) and [%p, ...)",
                    (void*)previous_base, (void*)previous_end, (void*)current_base);
            }
        }
    }

    auto find_function_for_addr = [&](uintptr_t P) -> Function* {
        // upper_bound: first function with base > P
        auto it = std::upper_bound(
            sorted.begin(),
            sorted.end(),
            P,
            [](uintptr_t addr, Function* f) {
                return addr < (uintptr_t)f->getCodeBase();
            }
        );
        if(it == sorted.begin()) {
            return NULL;
        }
        Function* cand = *(it - 1);
        uintptr_t base = (uintptr_t)cand->getCodeBase();
        uintptr_t end = base + cand->getCodeSize();
        if(P >= base && P < end) {
            return cand;
        }
        return NULL;
    };

    size_t exec_rela_sections = 0;
    size_t exec_rela_entries = 0;
    size_t supported_relocs_added = 0;

    // Scan all SHT_RELA sections that target executable sections.
    for(uint16_t i = 0; i < eh->e_shnum; i++) {
        if(sh[i].sh_type != SHT_RELA) {
            continue;
        }
        if(sh[i].sh_info >= eh->e_shnum) {
            continue;
        }

        const Elf64_Shdr& target = sh[sh[i].sh_info];
        if((target.sh_flags & SHF_EXECINSTR) == 0) {
            continue;
        }

        exec_rela_sections++;

        // Bounds check relocation section contents.
        if(sh[i].sh_offset + sh[i].sh_size > (size_t)st.st_size) {
            continue;
        }

        const Elf64_Rela* relas = (const Elf64_Rela*)(bytes + sh[i].sh_offset);
        size_t n = sh[i].sh_size / sizeof(Elf64_Rela);
        exec_rela_entries += n;

        for(size_t r = 0; r < n; r++) {
            uint32_t type = ELF64_R_TYPE(relas[r].r_info);

            // In final linked ELF binaries (ET_EXEC/ET_DYN), r_offset is a virtual
            // address. For PIE (ET_DYN), add the load bias.
            uintptr_t P = load_bias + (uintptr_t)relas[r].r_offset;
            Function* f = find_function_for_addr(P);
            if(f == NULL) {
                continue;
            }

            uintptr_t body = (uintptr_t)f->getCodeBodyBase();
            if(P < body && !relocSafeToIgnore(type)) {
                ABORT("Text relocation type %s (%u) appears inside a randomized "
                    "function's patchable entry: P=%p func=%p off=%zu",
                    relocTypeName(type), (unsigned)type, (void*)P,
                    f->getCodeBase(), (size_t)(P - (uintptr_t)f->getCodeBase()));
            }

            if(relocSupported(type)) {
                bool internal = false;

                if(type == R_X86_64_PC32 || type == R_X86_64_PLT32) {
                    if(sh[i].sh_link >= eh->e_shnum) {
                        ABORT("Executable relocation section has no valid symbol table");
                    }
                    const Elf64_Shdr& symbols_section = sh[sh[i].sh_link];
                    if(symbols_section.sh_entsize != sizeof(Elf64_Sym) ||
                       symbols_section.sh_offset > (size_t)st.st_size ||
                       symbols_section.sh_size > (size_t)st.st_size - symbols_section.sh_offset) {
                        ABORT("Executable relocation section has a malformed symbol table");
                    }

                    size_t symbol_index = ELF64_R_SYM(relas[r].r_info);
                    size_t symbol_count = symbols_section.sh_size / sizeof(Elf64_Sym);
                    if(symbol_index >= symbol_count) {
                        ABORT("Executable relocation references an out-of-range symbol");
                    }

                    const Elf64_Sym* symbols =
                        (const Elf64_Sym*)(bytes + symbols_section.sh_offset);
                    const Elf64_Sym& symbol = symbols[symbol_index];
                    if(symbol.st_shndx != SHN_UNDEF) {
                        // S+A moves with the function only when every possible
                        // x86 instruction-end adjustment keeps the referenced
                        // location in the copied body.  An x86 instruction is at
                        // most 15 bytes; boundary ambiguity is safer to reject
                        // than to silently mis-relocate.
                        __int128 value = (__int128)load_bias + symbol.st_value + relas[r].r_addend;
                        __int128 first = value;
                        __int128 last = value + 15;
                        __int128 body_end = (__int128)body + f->getCodeBodySize();

                        if(first >= (__int128)body && last < body_end) {
                            internal = true;
                        } else if(!(last < (__int128)body || first >= body_end)) {
                            ABORT("Ambiguous PC-relative target at function boundary: "
                                "func=%p P=%p S+A=%p",
                                f->getCodeBase(), (void*)P, (void*)(uintptr_t)value);
                        }
                    }
                }
                size_t off = (size_t)(P - body);
                f->addTextReloc(off, type, relas[r].r_addend, internal);
                supported_relocs_added++;
                continue;
            }

            if(relocSafeToIgnore(type)) {
                continue;
            }

            // We found a relocation inside randomized code that we don't know how
            // to patch after moving the function. Fail fast so we never silently
            // mis-relocate code.
            size_t off = (size_t)(P - (uintptr_t)f->getCodeBodyBase());
            ABORT("Unsupported x86_64 text relocation type %s (%u) inside randomized function: shdr=%u rel=%zu P=%p func=%p off=%zu addend=%ld. Disable -Rcode or extend relocation support.",
                relocTypeName(type), (unsigned)type, (unsigned)i, r, (void*)P, f->getCodeBase(), off, (long)relas[r].r_addend);
        }
    }

    munmap(file, st.st_size);

    // Diagnostics: these are best-effort; failure to load relocations should not
    // crash programs that don't need them.
    if(exec_rela_sections == 0) {
        DEBUG("No executable RELA sections found in /proc/self/exe (missing -Wl,--emit-relocs?)");
        return false;
    } else if(supported_relocs_added == 0) {
        DEBUG("Found %zu executable RELA sections (%zu entries) but recorded 0 supported relocations", exec_rela_sections, exec_rela_entries);
    }

    return true;
}

#else

bool stabilizer_init_text_relocations(const std::set<Function*>& functions) {
    // Code randomization registers functions via the compiler pass. If no
    // functions are registered, nothing to do.
    if(functions.empty()) {
        return true;
    }

    DEBUG("Code randomization (-Rcode) requires ELF text relocation fixups, which are currently only implemented on Linux x86_64. Disable -Rcode.");
    return false;
}

#endif
