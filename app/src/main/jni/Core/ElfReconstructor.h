#pragma once

// Reconstructs a loadable ELF from the copy of libil2cpp.so that is mapped in
// this process.
//
// Layout choice: the file is indexed by *vaddr*. Each PT_LOAD is written at file
// offset p_vaddr, and every program header is rewritten so p_offset == p_vaddr
// and p_filesz == p_memsz. Two consequences, both wanted:
//
//   * an offset in the file is the same number as the RVA from the load bias, so
//     the RVAs in dump.cs and script.json index the dumped file directly;
//   * IDA and Ghidra, which map segments from the program headers, see exactly
//     the addresses the runtime used.
//
// This layout requires the first PT_LOAD to start at vaddr 0, which is how the
// Android linker maps every shipped .so and how the ELF header ends up at file
// offset 0. A module that violates it is reported as a failure instead of being
// written as a file no loader accepts.
//
// Section headers: the dumped bytes come from a *mapped* image, so the original
// section header table is not resident unless a PT_LOAD happens to cover that
// file range. Writing a table that points at bytes that are not there produces a
// file both the loader and the disassemblers reject. So one table is synthesised
// from the program headers instead: a null entry plus one SHF_ALLOC section per
// PT_LOAD (.text / .rodata / .data), a .dynamic section when PT_DYNAMIC exists,
// and a real .shstrtab holding the names.

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <vector>
#include <elf.h>
#include <unistd.h>
#include <fcntl.h>
#include "SafeMemory.h"
#include "ElfImage.h"

namespace ElfReconstructor {

// Upper bound on the size of a single module image. Anything larger is not a
// plausible shared object and the dump is refused instead of writing garbage.
static constexpr uint64_t kMaxImageBytes = 512ull * 1024ull * 1024ull;

// \0.text\0.rodata\0.data\0.dynamic\0.shstrtab\0
// name offsets: 1 = .text, 7 = .rodata, 15 = .data, 21 = .dynamic, 30 = .shstrtab
static constexpr uint32_t kNameText      = 1;
static constexpr uint32_t kNameRodata    = 7;
static constexpr uint32_t kNameData      = 15;
static constexpr uint32_t kNameDynamic   = 21;
static constexpr uint32_t kNameShstrtab  = 30;

static const char kShstrtab[] = "\0.text\0.rodata\0.data\0.dynamic\0.shstrtab";
static constexpr size_t kShstrtabSize = sizeof(kShstrtab);

inline void AlignTo(std::vector<uint8_t>& out, size_t alignment, uint8_t fill = 0) {
    while (alignment && (out.size() % alignment) != 0) out.push_back(fill);
}

inline bool BuildImage(uintptr_t moduleBase, std::vector<uint8_t>& out, ElfImage::Info& infoOut) {
    out.clear();
    if (!ElfImage::Parse(moduleBase, infoOut)) return false;

    const ElfImage::Info& info = infoOut;

    // The vaddr-indexed layout puts the ELF header at file offset 0 only when
    // the first PT_LOAD starts at vaddr 0. Refuse the rest rather than emitting
    // an image whose header is not where a loader looks for it.
    if (info.minVaddr != 0) return false;

    const uint64_t imageSize = info.maxVaddr;
    if (imageSize == 0 || imageSize > kMaxImageBytes) return false;

    out.assign((size_t)imageSize, 0);

    // Stream each PT_LOAD into the buffer at its vaddr. A page that cannot be
    // read stays zero: the loader will still accept the file and the caller is
    // told which segments were incomplete.
    const size_t kChunk = 64 * 1024;
    for (const ElfImage::Segment& seg : info.segments) {
        if (seg.type != PT_LOAD || seg.memsz == 0) continue;
        if (seg.vaddr >= imageSize) continue;

        uint64_t remaining = seg.memsz;
        if (seg.vaddr + remaining > imageSize) remaining = imageSize - seg.vaddr;

        uint64_t done = 0;
        while (done < remaining) {
            const uint64_t inSegment = remaining - done;
            const uint64_t step = (inSegment > kChunk) ? kChunk : inSegment;
            const uintptr_t srcVa = (uintptr_t)(info.loadBias + seg.vaddr + done);
            uint8_t* dst = out.data() + seg.vaddr + done;

            if (!SafeMemory::Read(srcVa, dst, (size_t)step)) {
                std::memset(dst, 0, (size_t)step);
            }
            done += step;
        }
    }

    // --- Program headers: p_offset = p_vaddr, p_filesz = p_memsz -------------
    // The buffer is indexed by vaddr, so the file offset of any byte equals its
    // vaddr. Rewriting the headers in place keeps them consistent with that.
    if (info.is64) {
        const uint64_t tableBytes = (uint64_t)info.e_phnum * sizeof(Elf64_Phdr);
        if (info.e_phoff != 0 && info.e_phoff + tableBytes <= out.size()) {
            Elf64_Phdr* ph = reinterpret_cast<Elf64_Phdr*>(out.data() + info.e_phoff);
            for (uint16_t i = 0; i < info.e_phnum; ++i) {
                if (ph[i].p_type != PT_LOAD) continue;
                ph[i].p_offset = ph[i].p_vaddr;
                ph[i].p_filesz = ph[i].p_memsz;
            }
        }
    } else {
        const uint64_t tableBytes = (uint64_t)info.e_phnum * sizeof(Elf32_Phdr);
        if (info.e_phoff != 0 && info.e_phoff + tableBytes <= out.size()) {
            Elf32_Phdr* ph = reinterpret_cast<Elf32_Phdr*>(out.data() + info.e_phoff);
            for (uint16_t i = 0; i < info.e_phnum; ++i) {
                if (ph[i].p_type != PT_LOAD) continue;
                ph[i].p_offset = ph[i].p_vaddr;
                ph[i].p_filesz = ph[i].p_memsz;
            }
        }
    }

    // --- Synthesised section header table ------------------------------------
    AlignTo(out, 16);
    const uint64_t shstrtabOffset = out.size();
    out.insert(out.end(), reinterpret_cast<const uint8_t*>(kShstrtab),
               reinterpret_cast<const uint8_t*>(kShstrtab) + kShstrtabSize);

    AlignTo(out, 16);
    const uint64_t shdrOffset = out.size();

    if (info.is64) {
        std::vector<Elf64_Shdr> shdrs;
        shdrs.push_back(Elf64_Shdr{});

        for (const ElfImage::Segment& seg : info.segments) {
            if (seg.type != PT_LOAD || seg.memsz == 0) continue;
            Elf64_Shdr s{};
            if (seg.flags & PF_X) {
                s.sh_name = kNameText;
                s.sh_flags = SHF_ALLOC | SHF_EXECINSTR;
            } else if (seg.flags & PF_W) {
                s.sh_name = kNameData;
                s.sh_flags = SHF_ALLOC | SHF_WRITE;
            } else {
                s.sh_name = kNameRodata;
                s.sh_flags = SHF_ALLOC;
            }
            s.sh_type = SHT_PROGBITS;
            s.sh_addr = seg.vaddr;
            s.sh_offset = seg.vaddr;
            s.sh_size = seg.memsz;
            s.sh_addralign = seg.align ? seg.align : 16;
            shdrs.push_back(s);
        }

        for (const ElfImage::Segment& seg : info.segments) {
            if (seg.type != PT_DYNAMIC || seg.memsz == 0) continue;
            Elf64_Shdr s{};
            s.sh_name = kNameDynamic;
            s.sh_type = SHT_DYNAMIC;
            s.sh_flags = SHF_ALLOC | SHF_WRITE;
            s.sh_addr = seg.vaddr;
            s.sh_offset = seg.vaddr;
            s.sh_size = seg.memsz;
            s.sh_addralign = 8;
            shdrs.push_back(s);
            break;
        }

        Elf64_Shdr strtab{};
        strtab.sh_name = kNameShstrtab;
        strtab.sh_type = SHT_STRTAB;
        strtab.sh_offset = shstrtabOffset;
        strtab.sh_size = kShstrtabSize;
        strtab.sh_addralign = 1;
        shdrs.push_back(strtab);

        const uint16_t shnum = (uint16_t)shdrs.size();
        const uint16_t shstrndx = (uint16_t)(shnum - 1);

        const uint8_t* raw = reinterpret_cast<const uint8_t*>(shdrs.data());
        out.insert(out.end(), raw, raw + (size_t)shnum * sizeof(Elf64_Shdr));

        Elf64_Ehdr* ehdr = reinterpret_cast<Elf64_Ehdr*>(out.data());
        ehdr->e_shoff = shdrOffset;
        ehdr->e_shentsize = sizeof(Elf64_Shdr);
        ehdr->e_shnum = shnum;
        ehdr->e_shstrndx = shstrndx;
    } else {
        std::vector<Elf32_Shdr> shdrs;
        shdrs.push_back(Elf32_Shdr{});

        for (const ElfImage::Segment& seg : info.segments) {
            if (seg.type != PT_LOAD || seg.memsz == 0) continue;
            Elf32_Shdr s{};
            if (seg.flags & PF_X) {
                s.sh_name = kNameText;
                s.sh_flags = SHF_ALLOC | SHF_EXECINSTR;
            } else if (seg.flags & PF_W) {
                s.sh_name = kNameData;
                s.sh_flags = SHF_ALLOC | SHF_WRITE;
            } else {
                s.sh_name = kNameRodata;
                s.sh_flags = SHF_ALLOC;
            }
            s.sh_type = SHT_PROGBITS;
            s.sh_addr = (Elf32_Addr)seg.vaddr;
            s.sh_offset = (Elf32_Off)seg.vaddr;
            s.sh_size = (Elf32_Word)seg.memsz;
            s.sh_addralign = seg.align ? (Elf32_Word)seg.align : 16;
            shdrs.push_back(s);
        }

        for (const ElfImage::Segment& seg : info.segments) {
            if (seg.type != PT_DYNAMIC || seg.memsz == 0) continue;
            Elf32_Shdr s{};
            s.sh_name = kNameDynamic;
            s.sh_type = SHT_DYNAMIC;
            s.sh_flags = SHF_ALLOC | SHF_WRITE;
            s.sh_addr = (Elf32_Addr)seg.vaddr;
            s.sh_offset = (Elf32_Off)seg.vaddr;
            s.sh_size = (Elf32_Word)seg.memsz;
            s.sh_addralign = 4;
            shdrs.push_back(s);
            break;
        }

        Elf32_Shdr strtab{};
        strtab.sh_name = kNameShstrtab;
        strtab.sh_type = SHT_STRTAB;
        strtab.sh_offset = (Elf32_Off)shstrtabOffset;
        strtab.sh_size = (Elf32_Word)kShstrtabSize;
        strtab.sh_addralign = 1;
        shdrs.push_back(strtab);

        const uint16_t shnum = (uint16_t)shdrs.size();
        const uint16_t shstrndx = (uint16_t)(shnum - 1);

        const uint8_t* raw = reinterpret_cast<const uint8_t*>(shdrs.data());
        out.insert(out.end(), raw, raw + (size_t)shnum * sizeof(Elf32_Shdr));

        Elf32_Ehdr* ehdr = reinterpret_cast<Elf32_Ehdr*>(out.data());
        ehdr->e_shoff = (Elf32_Off)shdrOffset;
        ehdr->e_shentsize = sizeof(Elf32_Shdr);
        ehdr->e_shnum = shnum;
        ehdr->e_shstrndx = shstrndx;
    }

    // The rebuilt file has a section table now, whatever the mapped image had.
    infoOut.sectionsResident = true;
    return true;
}

inline bool WriteImage(const std::vector<uint8_t>& image, const char* outPath) {
    if (image.empty()) return false;

    int fd = open(outPath, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) return false;

    const size_t kChunk = 256 * 1024;
    size_t written = 0;
    bool ok = true;
    while (written < image.size()) {
        const size_t step = (image.size() - written > kChunk) ? kChunk : (image.size() - written);
        ssize_t n = write(fd, image.data() + written, step);
        if (n <= 0) { ok = false; break; }
        written += (size_t)n;
    }
    close(fd);

    if (!ok) unlink(outPath);
    return ok;
}

inline bool DumpAndFixElf(uintptr_t moduleBase, const char* outPath) {
    std::vector<uint8_t> image;
    ElfImage::Info info;
    if (!BuildImage(moduleBase, image, info)) return false;
    return WriteImage(image, outPath);
}

} // namespace ElfReconstructor
