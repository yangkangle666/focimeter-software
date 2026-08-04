#pragma once

#include <filesystem>

#include "focimeter/m2/types.h"

namespace focimeter::m2 {

class OutputDirectoryLock {
public:
    OutputDirectoryLock() = default;
    ~OutputDirectoryLock();

    OutputDirectoryLock(const OutputDirectoryLock&) = delete;
    OutputDirectoryLock& operator=(const OutputDirectoryLock&) = delete;

    [[nodiscard]] bool acquire(
        const std::filesystem::path& output_directory,
        ErrorInfo& error);

private:
#ifdef _WIN32
    void* handle_{nullptr};
#else
    int descriptor_{-1};
#endif
};

}  // namespace focimeter::m2
