#pragma once

#include "focimeter/m2/types.h"

namespace focimeter::m2 {

class ImageRecognitionModule {
public:
    [[nodiscard]] RunResult run(const RunOptions& options) const;
};

}  // namespace focimeter::m2
