#include "focimeter/m2/output_lock.h"

#include <string>
#include <system_error>
#include <utility>

#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace focimeter::m2 {
namespace {

ErrorInfo lockError(std::string message, const std::string& reason = {}) {
    ErrorInfo error;
    error.code = "UNKNOWN_ERROR";
    error.message = std::move(message);
    error.recoverable = true;
    if (!reason.empty()) {
        error.string_details["reason"] = reason;
    }
    return error;
}

}  // namespace

OutputDirectoryLock::~OutputDirectoryLock() {
#ifdef _WIN32
    const auto handle = static_cast<HANDLE>(handle_);
    if (handle != nullptr) {
        OVERLAPPED overlap{};
        UnlockFileEx(handle, 0, MAXDWORD, MAXDWORD, &overlap);
        CloseHandle(handle);
    }
#else
    if (descriptor_ >= 0) {
        flock(descriptor_, LOCK_UN);
        close(descriptor_);
    }
#endif
}

bool OutputDirectoryLock::acquire(
    const std::filesystem::path& output_directory,
    ErrorInfo& error) {
    error = {};
#ifdef _WIN32
    if (handle_ != nullptr) {
#else
    if (descriptor_ >= 0) {
#endif
        error = lockError("This output-directory lock is already acquired.");
        return false;
    }
    if (output_directory.empty()) {
        error = lockError("Output directory must not be empty.");
        return false;
    }

    std::error_code directory_error;
    std::filesystem::create_directories(output_directory, directory_error);
    if (directory_error) {
        error = lockError("Could not create the M2 output directory.", directory_error.message());
        return false;
    }
    const auto lock_path = output_directory / ".focimeter_m2.lock";

#ifdef _WIN32
    const HANDLE handle = CreateFileW(
        lock_path.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_HIDDEN,
        nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        error = lockError(
            "Could not open the M2 output-directory lock.",
            std::system_category().message(static_cast<int>(GetLastError())));
        return false;
    }
    OVERLAPPED overlap{};
    if (!LockFileEx(
            handle,
            LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY,
            0,
            MAXDWORD,
            MAXDWORD,
            &overlap)) {
        const auto reason = std::system_category().message(static_cast<int>(GetLastError()));
        CloseHandle(handle);
        error = lockError("Another M2 process is already writing this output directory.", reason);
        return false;
    }
    handle_ = handle;
#else
    const int descriptor = open(lock_path.c_str(), O_CREAT | O_RDWR, 0666);
    if (descriptor < 0) {
        error = lockError("Could not open the M2 output-directory lock.", std::generic_category().message(errno));
        return false;
    }
    if (flock(descriptor, LOCK_EX | LOCK_NB) != 0) {
        const auto reason = std::generic_category().message(errno);
        close(descriptor);
        error = lockError("Another M2 process is already writing this output directory.", reason);
        return false;
    }
    descriptor_ = descriptor;
#endif
    return true;
}

}  // namespace focimeter::m2
