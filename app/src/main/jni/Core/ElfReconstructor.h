#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <vector>
#include <elf.h>
#include <unistd.h>
#include <fcntl.h>
#include "SafeMemory.h"

namespace ElfReconstructor {

inline bool DumpAndFixElf(uintptr_t baseAddr, const char* outPath) {
    if (!baseAddr) return false;

    // Check ELF magic
    unsigned char magic[4] = {0};
    if (!SafeMemory::Read(baseAddr, magic, 4)) return false;
    if (magic[0] != ELFMAG0 || magic[1] != ELFMAG1 || magic[2] != ELFMAG2 || magic[3] != ELFMAG3) {
        return false;
    }

    bool is64Bit = (magic[EI_CLASS] == ELFCLASS64);

    uintptr_t maxVaddr = 0;
    size_t phnum = 0;

    if (is64Bit) {
        Elf64_Ehdr ehdr;
        if (!SafeMemory::Read(baseAddr, &ehdr, sizeof(ehdr))) return false;
        phnum = ehdr.e_phnum;
        uintptr_t phoff = baseAddr + ehdr.e_phoff;

        std::vector<Elf64_Phdr> phdrs(phnum);
        if (!SafeMemory::Read(phoff, phdrs.data(), phnum * sizeof(Elf64_Phdr))) return false;

        for (size_t i = 0; i < phnum; ++i) {
            if (phdrs[i].p_type == PT_LOAD) {
                uintptr_t segEnd = phdrs[i].p_vaddr + phdrs[i].p_memsz;
                if (segEnd > maxVaddr) maxVaddr = segEnd;
            }
        }
    } else {
        Elf32_Ehdr ehdr;
        if (!SafeMemory::Read(baseAddr, &ehdr, sizeof(ehdr))) return false;
        phnum = ehdr.e_phnum;
        uintptr_t phoff = baseAddr + ehdr.e_phoff;

        std::vector<Elf32_Phdr> phdrs(phnum);
        if (!SafeMemory::Read(phoff, phdrs.data(), phnum * sizeof(Elf32_Phdr))) return false;

        for (size_t i = 0; i < phnum; ++i) {
            if (phdrs[i].p_type == PT_LOAD) {
                uintptr_t segEnd = phdrs[i].p_vaddr + phdrs[i].p_memsz;
                if (segEnd > maxVaddr) maxVaddr = segEnd;
            }
        }
    }

    if (maxVaddr == 0 || maxVaddr > 0x20000000) { // Limit to 512 MB safety bound
        maxVaddr = 0x8000000; // 128 MB default fallback
    }

    int outFd = open(outPath, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (outFd < 0) return false;

    // Buffer chunk for streaming write
    const size_t kChunkSize = 64 * 1024;
    std::vector<uint8_t> buffer(kChunkSize);

    size_t bytesWritten = 0;
    while (bytesWritten < maxVaddr) {
        size_t toRead = std::min(kChunkSize, maxVaddr - bytesWritten);
        uintptr_t currentReadAddr = baseAddr + bytesWritten;

        if (!SafeMemory::Read(currentReadAddr, buffer.data(), toRead)) {
            // Fill unmapped gap with zeroes to preserve virtual address alignment
            std::memset(buffer.data(), 0, toRead);
        }

        // On first chunk, fix ELF Program Headers so p_offset == p_vaddr
        if (bytesWritten == 0) {
            if (is64Bit) {
                Elf64_Ehdr* ehdr = reinterpret_cast<Elf64_Ehdr*>(buffer.data());
                if (ehdr->e_phoff + (ehdr->e_phnum * sizeof(Elf64_Phdr)) <= toRead) {
                    Elf64_Phdr* ph = reinterpret_cast<Elf64_Phdr*>(buffer.data() + ehdr->e_phoff);
                    for (int i = 0; i < ehdr->e_phnum; ++i) {
                        if (ph[i].p_type == PT_LOAD) {
                            ph[i].p_offset = ph[i].p_vaddr;
                            ph[i].p_filesz = ph[i].p_memsz;
                        }
                    }
                }
            } else {
                Elf32_Ehdr* ehdr = reinterpret_cast<Elf32_Ehdr*>(buffer.data());
                if (ehdr->e_phoff + (ehdr->e_phnum * sizeof(Elf32_Phdr)) <= toRead) {
                    Elf32_Phdr* ph = reinterpret_cast<Elf32_Phdr*>(buffer.data() + ehdr->e_phoff);
                    for (int i = 0; i < ehdr->e_phnum; ++i) {
                        if (ph[i].p_type == PT_LOAD) {
                            ph[i].p_offset = ph[i].p_vaddr;
                            ph[i].p_filesz = ph[i].p_memsz;
                        }
                    }
                }
            }
        }

        write(outFd, buffer.data(), toRead);
        bytesWritten += toRead;
    }

    close(outFd);
    return true;
}

} // namespace ElfReconstructor
