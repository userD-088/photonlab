#include "photonlab/core/units.hpp"

#include <cmath>

namespace photonlab {
double to_db(double power_ratio) { return 10.0 * std::log10(power_ratio); }
double from_db(double db) { return std::pow(10.0, db / 10.0); }
}  // namespace photonlab
