#pragma once

#include <filesystem>

#include "focimeter/m2/types.h"

namespace focimeter::m2 {

class ImageProcessor {
public:
    [[nodiscard]] ImageAnalysis processFile(
        const std::filesystem::path& image_path,
        const ProcessingConfig& config) const;

    [[nodiscard]] ImageAnalysis processMat(
        const cv::Mat& image,
        const ProcessingConfig& config) const;

private:
    [[nodiscard]] static cv::Mat readImage(const std::filesystem::path& image_path);
};

}  // namespace focimeter::m2
