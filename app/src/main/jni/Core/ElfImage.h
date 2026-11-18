#pragma once

// ELF program-header reader for a module already mapped in this process.
//
// Two facts drive everything here:
//   1. A mapped ET_DYN object has a load bias such that runtime_address =
//      load_bias + vaddr. The lowest mapping returned by /proc/self/maps is
//      load_bias + align_down(first_pt_load.p_vaddr, page_size), which is not
//      the same number when the first load segment does not start at vaddr 0.
//   2. Method pointers, string pointers and metadata pointers all live in the
//      vaddr space, so an RVA is only meaningful once the bias is removed.
//
// Everything is read through SafeMemory, so an unmapped page returns false
// instead of faulting the game process.

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <vector>
#include <unistd.h>
#include <elf.h>
#include "SafeMemory.h"

namespace ElfImage {

struct Segment {
    uint32_t type;
    uint64_t vaddr;
    uint64_t memsz;
    uint64_t offset;
    uint64_t filesz;
    uint32_t flags;
    uint64_t align;
};

struct Info {
    bool     valid    = false;
    bool     is64     = false;
    uint16_t e_type   = 0;
    uint64_t e_entry  = 0;
    uint64_t e_phoff  = 0;
    uint16_t e_phnum  = 0;
    uint64_t e_shoff  = 0;
    uint16_t e_shnum  = 0;
    uint16_t e_shstrndx = 0;
    uint16_t e_shentsize = 0;

    uintptr_t moduleBase = 0;   // lowest mapping, as reported by /proc/self/maps
    uint64_t  loadBias   = 0;   // moduleBase - align_down(minLoadVaddr, page)
    uint64_t  minVaddr   = 0;   // lowest PT_LOAD p_vaddr
    uint64_t  maxVaddr   = 0;   // highest PT_LOAD (p_vaddr + p_memsz)

    std::vector<Segment> segments;

    // True when the on-disk section header table is actually resident in a
    // loaded segment. On a stripped Android build it normally is not, and the
    // table must be dropped rather than written out as garbage.
    bool sectionsResident = false;
};

inline uint64_t PageSize() {
    static const uint64_t s_page = (uint64_t)sysconf(_SC_PAGESIZE);
    return s_page ? s_page : 4096ull;
}

inline uint64_t AlignDown(uint64_t value, uint64_t align) {
    return align ? (value & ~(align - 1)) : value;
}

// Reads the ELF header and program headers of the module mapped at moduleBase.
inline bool Parse(uintptr_t moduleBase, Info& out) {
    out = Info();
    if (!moduleBase) return false;

    unsigned char ident[EI_NIDENT] = {0};
    if (!SafeMemory::Read(moduleBase, ident, sizeof(ident))) return false;
    if (ident[EI_MAG0] != ELFMAG0 || ident[EI_MAG1] != ELFMAG1 ||
        ident[EI_MAG2] != ELFMAG2 || ident[EI_MAG3] != ELFMAG3) {
        return false;
    }

    const bool is64 = (ident[EI_CLASS] == ELFCLASS64);
    out.is64 = is64;
    out.moduleBase = moduleBase;

    uint64_t phoff = 0;
    uint16_t phnum = 0;
    uint16_t phentsize = 0;

    if (is64) {
        Elf64_Ehdr ehdr;
        if (!SafeMemory::Read(moduleBase, &ehdr, sizeof(ehdr))) return false;
        out.e_type      = ehdr.e_type;
        out.e_entry     = ehdr.e_entry;
        out.e_phoff     = ehdr.e_phoff;
        out.e_phnum     = ehdr.e_phnum;
        out.e_shoff     = ehdr.e_shoff;
        out.e_shnum     = ehdr.e_shnum;
        out.e_shstrndx  = ehdr.e_shstrndx;
        out.e_shentsize = ehdr.e_shentsize;
        phoff = ehdr.e_phoff;
        phnum = ehdr.e_phnum;
        phentsize = ehdr.e_phentsize;
        if (phentsize != sizeof(Elf64_Phdr)) return false;
    } else {
        Elf32_Ehdr ehdr;
        if (!SafeMemory::Read(moduleBase, &ehdr, sizeof(ehdr))) return false;
        out.e_type      = ehdr.e_type;
        out.e_entry     = ehdr.e_entry;
        out.e_phoff     = ehdr.e_phoff;
        out.e_phnum     = ehdr.e_phnum;
        out.e_shoff     = ehdr.e_shoff;
        out.e_shnum     = ehdr.e_shnum;
        out.e_shstrndx  = ehdr.e_shstrndx;
        out.e_shentsize = ehdr.e_shentsize;
        phoff = ehdr.e_phoff;
        phnum = ehdr.e_phnum;
        phentsize = ehdr.e_phentsize;
        if (phentsize != sizeof(Elf32_Phdr)) return false;
    }

    if (phnum == 0 || phnum > 512) return false;

    out.segments.reserve(phnum);
    for (uint16_t i = 0; i < phnum; ++i) {
        Segment seg{};
        if (is64) {
            Elf64_Phdr ph;
            if (!SafeMemory::Read(moduleBase + phoff + (uint64_t)i * sizeof(ph), &ph, sizeof(ph))) return false;
            seg.type = ph.p_type; seg.vaddr = ph.p_vaddr; seg.memsz = ph.p_memsz;
            seg.offset = ph.p_offset; seg.filesz = ph.p_filesz;
            seg.flags = ph.p_flags; seg.align = ph.p_align;
        } else {
            Elf32_Phdr ph;
            if (!SafeMemory::Read(moduleBase + phoff + (uint64_t)i * sizeof(ph), &ph, sizeof(ph))) return false;
            seg.type = ph.p_type; seg.vaddr = ph.p_vaddr; seg.memsz = ph.p_memsz;
            seg.offset = ph.p_offset; seg.filesz = ph.p_filesz;
            seg.flags = ph.p_flags; seg.align = ph.p_align;
        }
        out.segments.push_back(seg);
    }

    bool haveLoad = false;
    uint64_t minVaddr = ~0ull;
    uint64_t maxVaddr = 0;
    for (const Segment& seg : out.segments) {
        if (seg.type != PT_LOAD) continue;
        haveLoad = true;
        if (seg.vaddr < minVaddr) minVaddr = seg.vaddr;
        const uint64_t end = seg.vaddr + seg.memsz;
        if (end > maxVaddr) maxVaddr = end;
    }
    if (!haveLoad || maxVaddr <= minVaddr) return false;

    out.minVaddr = minVaddr;
    out.maxVaddr = maxVaddr;

    const uint64_t biasAnchor = AlignDown(minVaddr, PageSize());
    out.loadBias = (moduleBase >= biasAnchor) ? (moduleBase - biasAnchor) : moduleBase;

    // A resident section table means e_shoff names a file offset that a loaded
    // segment actually covers. Verify the count, the entry size and the string
    // table index before trusting any of it.
    if (out.e_shoff != 0 && out.e_shnum != 0 && out.e_shstrndx < out.e_shnum &&
        out.e_shentsize == (is64 ? sizeof(Elf64_Shdr) : sizeof(Elf32_Shdr)) &&
        out.e_shnum < 4096) {
        const uint64_t tableBytes = (uint64_t)out.e_shnum * out.e_shentsize;
        for (const Segment& seg : out.segments) {
            if (seg.type != PT_LOAD || seg.filesz == 0) continue;
            if (out.e_shoff >= seg.offset && (out.e_shoff + tableBytes) <= (seg.offset + seg.filesz)) {
                out.sectionsResident = true;
                break;
            }
        }
    }

    out.valid = true;
    return true;
}

// Runtime address -> RVA (vaddr). Returns false when the address is not inside
// the module's loadable range.
inline bool VaddrToRva(const Info& info, uintptr_t va, uint64_t& rvaOut) {
    if (!info.valid || va < info.loadBias) return false;
    const uint64_t rva = (uint64_t)va - info.loadBias;
    if (rva < info.minVaddr || rva >= info.maxVaddr) return false;
    rvaOut = rva;
    return true;
}

// RVA -> offset inside the dumped file. The dumped file mirrors the vaddr
// layout, so the caller writes every segment at p_vaddr; the original file
// offset only matters when the module has not been rewritten yet.
inline bool RvaToFileOffset(const Info& info, uint64_t rva, uint64_t& offOut) {
    for (const Segment& seg : info.segments) {
        if (seg.type != PT_LOAD) continue;
        if (rva < seg.vaddr) continue;
        const uint64_t delta = rva - seg.vaddr;
        if (delta >= seg.memsz) continue;
        offOut = seg.offset + delta;
        return true;
    }
    return false;
}

inline const Segment* FindSegment(const Info& info, uint64_t rva) {
    for (const Segment& seg : info.segments) {
        if (seg.type != PT_LOAD) continue;
        if (rva >= seg.vaddr && (rva - seg.vaddr) < seg.memsz) return &seg;
    }
    return nullptr;
}

} // namespace ElfImage
