#pragma once
#include <complex>

namespace photonlab {
using cplx = std::complex<double>;
inline constexpr cplx I{0.0, 1.0};

// |a|^2 ist in PhotonLab immer eine Leistung in Watt
inline double power(cplx a) { return std::norm(a); }
inline cplx expi(double phase) { return std::polar(1.0, phase); }
}  // namespace photonlab
