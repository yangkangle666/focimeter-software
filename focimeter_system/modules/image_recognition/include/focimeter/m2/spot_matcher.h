#pragma once

#include <vector>

#include "focimeter/m2/types.h"

namespace focimeter::m2 {

class SpotMatcher {
public:
    [[nodiscard]] bool assignCalibrationRoles(
        const std::vector<SpotObservation>& observations,
        std::vector<Spot>& spots,
        ErrorInfo& error) const;

    [[nodiscard]] bool matchMeasurement(
        const std::vector<Spot>& calibration_spots,
        const std::vector<SpotObservation>& measurement_observations,
        const ProcessingConfig& config,
        std::vector<Spot>& measurement_spots,
        MatchDiagnostics& diagnostics,
        ErrorInfo& error) const;
};

}  // namespace focimeter::m2
