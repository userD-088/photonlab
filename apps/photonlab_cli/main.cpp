// PhotonLab Kommandozeile: später Batch-Läufe und Validierung, jetzt ein Lebenszeichen.
#include <cstdio>
#include <string_view>

#include "photonlab/core/complex.hpp"
#include "photonlab/core/units.hpp"

int main(int argc, char** argv) {
  using namespace photonlab;
  std::printf("PhotonLab CLI 0.1.0 (Phase 0)\n");

  if (argc > 1 && std::string_view(argv[1]) == "--info") {
    const double lambda = 1550 * units::nm;
    std::printf("c0         = %.0f m/s\n", constants::c0);
    std::printf("eps0       = %.6e F/m\n", constants::eps0);
    std::printf("1550 nm    = %.3f THz\n", frequency_from_wavelength(lambda) / 1e12);
    // kleines Interferenz-Beispiel: zwei gleichphasige Felder addieren sich zu 4-facher Leistung
    const cplx a = expi(0.0), b = expi(0.0);
    std::printf("|a+b|^2    = %.1f (konstruktive Interferenz)\n", power(a + b));
  } else {
    std::printf("Optionen: --info\n");
  }
  return 0;
}
