// Beispiel-Plugin für PhotonLab: Bragg-Gitter (Koppelmodentheorie) und 1x2-MMI.
// Gebaut nur gegen den C-Header plugin_api.h; Physik des Gitters: docs/plugins.md
#include <cmath>
#include <complex>

#include "photonlab/circuit/plugin_api.h"

namespace {

using cplx = std::complex<double>;
constexpr double pi = 3.14159265358979323846;

// sinh(z)/z, stetig bei z = 0
cplx sinhc(cplx z) {
  if (std::abs(z) < 1e-6) return 1.0 + z * z / 6.0;
  return std::sinh(z) / z;
}

void put(double* re, double* im, int n, int i, int j, cplx v) {
  re[i * n + j] = v.real();
  im[i * n + j] = v.imag();
}

// Gleichmäßiges Bragg-Gitter (Erdogan, J. Lightwave Technol. 15, 1277 (1997), Gl. 54-55):
//   delta = beta - pi/Lambda, s = sqrt(kappa² - delta²),
//   r = -kappa sinh(sL) / (delta sinh(sL) + i s cosh(sL)),  t = i s / (delta sinh(sL) + i s cosh(sL)).
// Mit sinhc geschrieben, damit s = 0 (Bandkante) keine Division durch 0 ergibt. Maximum |r|² = tanh²(kappa L)
// bei delta = 0. Die Reflexion von der anderen Seite folgt aus der Energieerhaltung (S unitär).
// Parameter: Periode, Länge, kappa, n_eff, n_g, Bezugswellenlänge (Dispersion wie beim Wellenleiter).
int bragg(double wavelength, const double* p, double* re, double* im) {
  const double period = p[0], length = p[1], kappa = p[2], n_eff0 = p[3], n_group = p[4], lambda0 = p[5];
  if (!(wavelength > 0.0) || !(period > 0.0)) return 1;
  const double n_eff = n_eff0 + (n_eff0 - n_group) * (wavelength - lambda0) / lambda0;
  const double delta = 2.0 * pi * n_eff / wavelength - pi / period;
  const cplx s = std::sqrt(cplx(kappa * kappa - delta * delta, 0.0));
  const cplx sl = s * length;
  const cplx shc = sinhc(sl);
  const cplx denominator = delta * length * shc + cplx(0.0, 1.0) * std::cosh(sl);
  const cplx r1 = -kappa * length * shc / denominator;
  const cplx t0 = cplx(0.0, 1.0) / denominator;
  const cplx t = t0 * std::polar(1.0, pi * length / period);  // Phase der Bragg-Welle über die Länge
  const cplx r2 = -std::conj(r1) * t / std::conj(t);
  put(re, im, 2, 0, 0, r1);
  put(re, im, 2, 0, 1, t);
  put(re, im, 2, 1, 0, t);
  put(re, im, 2, 1, 1, r2);
  return 0;
}

// Idealer 1x2-MMI-Teiler: je Ausgang die halbe Leistung mal Zusatzverlust, gleiche Phase, keine Reflexion.
int mmi(double /*wavelength*/, const double* p, double* re, double* im) {
  const double loss_db = p[0];
  const double t = std::sqrt(0.5 * std::pow(10.0, -loss_db / 10.0));
  for (int k = 0; k < 9; ++k) re[k] = im[k] = 0.0;
  const cplx v = std::polar(t, pi / 4.0);
  put(re, im, 3, 1, 0, v);
  put(re, im, 3, 2, 0, v);
  put(re, im, 3, 0, 1, v);
  put(re, im, 3, 0, 2, v);
  return 0;
}

const char* const bragg_ports[] = {"in", "out"};
const plab_param bragg_params[] = {
    {"period", "Periode", 317.623e-9, 100e-9, 1000e-9, "nm", 1e-9},
    {"length", "Länge", 20e-6, 0.1e-6, 5e-3, "µm", 1e-6},
    {"kappa", "Kopplung kappa", 1e5, 0.0, 1e7, "1/mm", 1e3},
    {"n_eff", "n_eff", 2.44, 1.0, 4.0, "", 1.0},
    {"n_group", "n_g", 4.2, 1.0, 6.0, "", 1.0},
    {"wavelength_ref", "Bezugswellenlänge", 1550e-9, 400e-9, 5e-6, "nm", 1e-9},
};
const char* const mmi_ports[] = {"in", "out1", "out2"};
const plab_param mmi_params[] = {{"excess_loss_db", "Zusatzverlust", 0.2, 0.0, 10.0, "dB", 1.0}};

const plab_component components[] = {
    {"bragg_grating", "Bragg-Gitter (Plugin)",
     "Gleichmäßiges Bragg-Gitter nach Koppelmodentheorie, verlustfrei, Dispersion linear in n_eff", 2,
     bragg_ports, 1, 6, bragg_params, bragg},
    {"mmi_1x2", "MMI 1x2 (Plugin)",
     "Idealer 1x2-Teiler mit Zusatzverlust, ohne Reflexion und Wellenlängengang", 3, mmi_ports, 1, 1,
     mmi_params, mmi},
};

const plab_plugin plugin = {PHOTONLAB_PLUGIN_API_VERSION,
                            "Beispiel",
                            "1.0",
                            "Bragg-Gitter und MMI als Vorlage für eigene Bauteile",
                            2,
                            components};

}  // namespace

extern "C" PHOTONLAB_PLUGIN_EXPORT const plab_plugin* photonlab_plugin(void) { return &plugin; }
