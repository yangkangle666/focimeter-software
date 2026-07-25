#pragma once

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

#include "focimeter/m2/types.h"

namespace focimeter::m2 {

[[nodiscard]] bool readInputPackage(
    const std::filesystem::path& path,
    InputPackage& input,
    ErrorInfo& error);

[[nodiscard]] bool readProcessingConfig(
    const std::filesystem::path& path,
    ProcessingConfig& config,
    ErrorInfo& error);

[[nodiscard]] bool writeSpotSuccess(
    const std::filesystem::path& path,
    const std::string& schema_version,
    const std::string& task_id,
    const std::string& image_type,
    const std::vector<Spot>& spots,
    int expected_count,
    const std::vector<std::string>& warnings,
    ErrorInfo& error);

[[nodiscard]] bool writeSpotError(
    const std::filesystem::path& path,
    const std::string& task_id,
    const std::string& image_type,
    const ErrorInfo& module_error,
    ErrorInfo& write_error);

[[nodiscard]] bool writeImageDiagnostics(
    const std::filesystem::path& path,
    const std::string& task_id,
    const std::string& image_type,
    const ImageAnalysis& analysis,
    ErrorInfo& write_error);

[[nodiscard]] bool writeRunLog(
    const std::filesystem::path& path,
    const InputPackage* input,
    const RunOptions& options,
    const std::vector<std::filesystem::path>& outputs,
    const ErrorInfo& run_error,
    const std::vector<std::string>& warnings,
    std::chrono::system_clock::time_point started_at,
    ErrorInfo& write_error);

}  // namespace focimeter::m2
