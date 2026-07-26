#pragma once

#include "focimeter/m2/types.h"

namespace focimeter::m2 {

// Experimental detector for arbitrary-count Hartmann candidates. It deliberately
// does not assign cross-image physical identities or five-spot roles.
class MultispotDetector {
public:
    [[nodiscard]] ImageAnalysis detect(ImageAnalysis analysis, const ProcessingConfig& config) const;
};

}  // namespace focimeter::m2
