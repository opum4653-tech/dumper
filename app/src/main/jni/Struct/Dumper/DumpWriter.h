#pragma once

// Buffered text writer for the dump outputs.
//
// Every write goes into an in-memory buffer and is flushed in chunks, because
// the dump is tens of megabytes of small formatted strings and a syscall per
// line turns a two-second dump into a minute-long stall on the render thread.
// Write failures are tracked rather than ignored, so the engine can tell the
// user the dump is incomplete instead of reporting success.

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <string>
#include <vector>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

namespace DumpWriter {

class File {
public:
    File() = default;
    ~File() { Close(); }

    File(const File&) = delete;
    File& operator=(const File&) = delete;

    bool Open(const char* path) {
        Close();
        fd_ = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        ok_ = (fd_ >= 0);
        buffer_.clear();
        buffer_.reserve(kFlushThreshold + 4096);
        return ok_;
    }

    void Close() {
        Flush();
        if (fd_ >= 0) {
            close(fd_);
            fd_ = -1;
        }
    }

    bool IsOpen() const { return fd_ >= 0; }
    bool Ok() const { return ok_; }
    size_t BytesWritten() const { return bytesWritten_; }

    void Put(const char* data, size_t size) {
        if (!ok_ || !data || size == 0) return;
        buffer_.append(data, size);
        if (buffer_.size() >= kFlushThreshold) Flush();
    }

    void Put(const std::string& text) { Put(text.data(), text.size()); }
    void Put(const char* text) { if (text) Put(text, strlen(text)); }
    void PutChar(char c) { if (ok_) { buffer_.push_back(c); if (buffer_.size() >= kFlushThreshold) Flush(); } }

    void Printf(const char* fmt, ...) {
        if (!ok_ || !fmt) return;
        char stackBuffer[2048];
        va_list args;
        va_start(args, fmt);
        int needed = vsnprintf(stackBuffer, sizeof(stackBuffer), fmt, args);
        va_end(args);
        if (needed <= 0) return;

        if ((size_t)needed < sizeof(stackBuffer)) {
            Put(stackBuffer, (size_t)needed);
            return;
        }

        std::vector<char> heap((size_t)needed + 1);
        va_start(args, fmt);
        vsnprintf(heap.data(), heap.size(), fmt, args);
        va_end(args);
        Put(heap.data(), (size_t)needed);
    }

    void Flush() {
        if (fd_ < 0 || buffer_.empty()) return;
        size_t offset = 0;
        while (offset < buffer_.size()) {
            ssize_t n = write(fd_, buffer_.data() + offset, buffer_.size() - offset);
            if (n <= 0) { ok_ = false; break; }
            offset += (size_t)n;
            bytesWritten_ += (size_t)n;
        }
        buffer_.clear();
    }

private:
    static constexpr size_t kFlushThreshold = 64 * 1024;

    int                  fd_ = -1;
    bool                 ok_ = false;
    size_t               bytesWritten_ = 0;
    std::string          buffer_;
};

// Creates a directory (single level, no recursion needed for the dump root).
inline bool EnsureDirectory(const char* path) {
    if (!path || !*path) return false;
    struct stat st;
    if (stat(path, &st) == 0) return S_ISDIR(st.st_mode);
    if (mkdir(path, 0777) == 0) {
        chmod(path, 0777);
        return true;
    }
    return false;
}

} // namespace DumpWriter
