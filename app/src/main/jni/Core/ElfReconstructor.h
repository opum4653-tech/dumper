#pragma once

// Reconstructs a loadable ELF from the copy of libil2cpp.so that is mapped in
// this process.
//
// Why the output is laid out by vaddr: a mapped object is paged in at
// align_down(p_vaddr, page) for each PT_LOAD, and its vaddr space is what every
// pointer in the runtime refers to. Writing each segment at file offset
// p_vaddr makes the file's offsets and vaddrs identical, which is what the
// original p_offset == p_vaddr fixup below guarantees. A loader (and IDA,
// Ghidra, objdump) then reads the same addresses the runtime uses.
//
// Section headers: the dumped bytes come from a *mapped* image, so whatever
// the original e_shoff pointed at is not present unless a PT_LOAD happens to
// cover that file range. Writing a section header table that does not match the
// bytes produces a file that fails to load and that disassemblers reject. So
// when the table is not resident the header is zeroed: e_shoff = 0,
// e_shnum = 0, e_shstrndx = SHN_UNDEF. That is a valid, section-less ELF that
// every loader accepts, and both IDA and Ghidra synthesise segments from the
// program headers instead.

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

inline bool BuildImage(uintptr_t moduleBase, std::vector<uint8_t>& out, ElfImage::Info& infoOut) {
    out.clear();
    if (!ElfImage::Parse(moduleBase, infoOut)) return false;

    const ElfImage::Info& info = infoOut;
    const uint64_t imageSize = info.maxVaddr;
    if (imageSize == 0 || imageSize > kMaxImageBytes) return false;

    out.assign((size_t)imageSize, 0);

    // Stream each PT_LOAD into the buffer at its vaddr. Unreadable pages inside
    // a segment (rare, but a guard page can sit mid-segment) stay zero rather
    // than aborting the whole dump.
    const size_t kChunk = 64 * 1024;
    std::vector<uint8_t> chunkBuffer(kChunk);

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
                if (!SafeMemory::Read(srcVa, chunkBuffer.data(), (size_t)step)) {
                    std::memset(dst, 0, (size_t)step);
                } else {
                    std::memcpy(dst, chunkBuffer.data(), (size_t)step);
                }
            }
            done += step;
        }
    }

    // --- Program headers: p_offset = p_vaddr, p_filesz = p_memsz -------------
    // The buffer is indexed by vaddr, so the file offset of any byte equals its
    // vaddr. Rewriting the headers in place keeps them consistent with that.
    if (info.is64) {
        if (info.e_phoff + (uint64_t)info.e_phnum * sizeof(Elf64_Phdr) <= out.size()) {
            Elf64_Phdr* ph = reinterpret_cast<Elf64_Phdr*>(out.data() + info.e_phoff);
            for (uint16_t i = 0; i < info.e_phnum; ++i) {
                if (ph[i].p_type != PT_LOAD) continue;
                ph[i].p_offset = ph[i].p_vaddr;
                ph[i].p_filesz = ph[i].p_memsz;
            }
        }
    } else {
        if (info.e_phoff + (uint64_t)info.e_phnum * sizeof(Elf32_Phdr) <= out.size()) {
            Elf32_Phdr* ph = reinterpret_cast<Elf32_Phdr*>(out.data() + info.e_phoff);
            for (uint16_t i = 0; i < info.e_phnum; ++i) {
                if (ph[i].p_type != PT_LOAD) continue;
                ph[i].p_offset = ph[i].p_vaddr;
                ph[i].p_filesz = ph[i].p_memsz;
            }
        }
    }

    // --- Synthesize valid section header table + .shstrtab for IDA / Ghidra ---
    // String table layout:
    // \0.text\0.rodata\0.data\0.dynamic\0.shstrtab\0
    // offsets: [0]="", [1]=".text", [7]=".rodata", [15]=".data", [21]=".dynamic", [30]=".shstrtab"
    static const char kShstrtab[] = "\0.text\0.rodata\0.data\0.dynamic\0.shstrtab";
    const size_t kShstrtabLen = sizeof(kShstrtab);

    while (out.size() % 16 != 0) out.push_back(0);
    const uint64_t shstrtabOffset = out.size();
    out.insert(out.end(), reinterpret_cast<const uint8_t*>(kShstrtab),
               reinterpret_cast<const uint8_t*>(kShstrtab) + kShstrtabLen);

    while (out.size() % 16 != 0) out.push_back(0);
    const uint64_t shdrOffset = out.size();

    if (info.is64) {
        std::vector<Elf64_Shdr> shdrs;
        Elf64_Shdr nullShdr{};
        shdrs.push_back(nullShdr);

        for (const ElfImage::Segment& seg : info.segments) {
            if (seg.type != PT_LOAD || seg.memsz == 0) continue;
            Elf64_Shdr s{};
            if (seg.flags & PF_X) {
                s.sh_name = 1; // ".text"
                s.sh_type = SHT_PROGBITS;
                s.sh_flags = SHF_ALLOC | SHF_EXECINSTR;
            } else if (seg.flags & PF_W) {
                s.sh_name = 15; // ".data"
                s.sh_type = SHT_PROGBITS;
                s.sh_flags = SHF_ALLOC | SHF_WRITE;
            } else {
                s.sh_name = 7; // ".rodata"
                s.sh_type = SHT_PROGBITS;
                s.sh_flags = SHF_ALLOC;
            }
            s.sh_addr = seg.vaddr;
            s.sh_offset = seg.vaddr;
            s.sh_size = seg.memsz;
            s.sh_addralign = seg.align ? seg.align : 16;
            shdrs.push_back(s);
        }

        for (const ElfImage::Segment& seg : info.segments) {
            if (seg.type == PT_DYNAMIC && seg.memsz > 0) {
                Elf64_Shdr s{};
                s.sh_name = 21; // ".dynamic"
                s.sh_type = SHT_DYNAMIC;
                s.sh_flags = SHF_ALLOC | SHF_WRITE;
                s.sh_addr = seg.vaddr;
                s.sh_offset = seg.vaddr;
                s.sh_size = seg.memsz;
                s.sh_addralign = 8;
                shdrs.push_back(s);
                break;
            }
        }

        Elf64_Shdr strtabShdr{};
        strtabShdr.sh_name = 30; // ".shstrtab"
        strtabShdr.sh_type = SHT_STRTAB;
        strtabShdr.sh_flags = 0;
        strtabShdr.sh_addr = 0;
        strtabShdr.sh_offset = shstrtabOffset;
        strtabShdr.sh_size = kShstrtabLen;
        strtabShdr.sh_addralign = 1;
        shdrs.push_back(strtabShdr);

        const uint16_t shstrndx = (uint16_t)(shdrs.size() - 1);
        const uint16_t shnum = (uint16_t)shdrs.size();

        const uint8_t* rawShdrs = reinterpret_cast<const uint8_t*>(shdrs.data());
        out.insert(out.end(), rawShdrs, rawShdrs + shnum * sizeof(Elf64_Shdr));

        Elf64_Ehdr* ehdr = reinterpret_cast<Elf64_Ehdr*>(out.data());
        ehdr->e_shoff = shdrOffset;
        ehdr->e_shentsize = sizeof(Elf64_Shdr);
        ehdr->e_shnum = shnum;
        ehdr->e_shstrndx = shstrndx;
    } else {
        std::vector<Elf32_Shdr> shdrs;
        Elf32_Shdr nullShdr{};
        shdrs.push_back(nullShdr);

        for (const ElfImage::Segment& seg : info.segments) {
            if (seg.type != PT_LOAD || seg.memsz == 0) continue;
            Elf32_Shdr s{};
            if (seg.flags & PF_X) {
                s.sh_name = 1; // ".text"
                s.sh_type = SHT_PROGBITS;
                s.sh_flags = SHF_ALLOC | SHF_EXECINSTR;
            } else if (seg.flags & PF_W) {
                s.sh_name = 15; // ".data"
                s.sh_type = SHT_PROGBITS;
                s.sh_flags = SHF_ALLOC | SHF_WRITE;
            } else {
                s.sh_name = 7; // ".rodata"
                s.sh_type = SHT_PROGBITS;
                s.sh_flags = SHF_ALLOC;
            }
            s.sh_addr = (Elf32_Addr)seg.vaddr;
            s.sh_offset = (Elf32_Off)seg.vaddr;
            s.sh_size = (Elf32_Word)seg.memsz;
            s.sh_addralign = seg.align ? (Elf32_Word)seg.align : 16;
            shdrs.push_back(s);
        }

        for (const ElfImage::Segment& seg : info.segments) {
            if (seg.type == PT_DYNAMIC && seg.memsz > 0) {
                Elf32_Shdr s{};
                s.sh_name = 21; // ".dynamic"
                s.sh_type = SHT_DYNAMIC;
                s.sh_flags = SHF_ALLOC | SHF_WRITE;
                s.sh_addr = (Elf32_Addr)seg.vaddr;
                s.sh_offset = (Elf32_Off)seg.vaddr;
                s.sh_size = (Elf32_Word)seg.memsz;
                s.sh_addralign = 4;
                shdrs.push_back(s);
                break;
            }
        }

        Elf32_Shdr strtabShdr{};
        strtabShdr.sh_name = 30; // ".shstrtab"
        strtabShdr.sh_type = SHT_STRTAB;
        strtabShdr.sh_flags = 0;
        strtabShdr.sh_addr = 0;
        strtabShdr.sh_offset = (Elf32_Off)shstrtabOffset;
        strtabShdr.sh_size = (Elf32_Word)kShstrtabLen;
        strtabShdr.sh_addralign = 1;
        shdrs.push_back(strtabShdr);

        const uint16_t shstrndx = (uint16_t)(shdrs.size() - 1);
        const uint16_t shnum = (uint16_t)shdrs.size();

        const uint8_t* rawShdrs = reinterpret_cast<const uint8_t*>(shdrs.data());
        out.insert(out.end(), rawShdrs, rawShdrs + shnum * sizeof(Elf32_Shdr));

        Elf32_Ehdr* ehdr = reinterpret_cast<Elf32_Ehdr*>(out.data());
        ehdr->e_shoff = (Elf32_Off)shdrOffset;
        ehdr->e_shentsize = sizeof(Elf32_Shdr);
        ehdr->e_shnum = shnum;
        ehdr->e_shstrndx = shstrndx;
    }

    return true;
}

inline bool WriteImage(const std::vector<uint8_t>& image, const char* outPath) {
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
    return ok;
}

inline bool DumpAndFixElf(uintptr_t moduleBase, const char* outPath) {
    std::vector<uint8_t> image;
    ElfImage::Info info;
    if (!BuildImage(moduleBase, image, info)) return false;
    return WriteImage(image, outPath);
}

} // namespace ElfReconstructor
