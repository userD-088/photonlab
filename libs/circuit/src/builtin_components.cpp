// Eingebaute Bauteile der Phase 1: Wellenleiter, Richtkoppler, Phasenschieber, Laser, Detektor.
// Gleichungen und Annahmen: docs/physics/circuit_solver.md
#include <cmath>

#include "photonlab/circuit/registry.hpp"
#include "photonlab/core/units.hpp"

namespace photonlab::circuit {
namespace {

// Feld-Transmission zu einem Leistungsverlust in dB (positiv = Verlust).
double amplitude_from_loss_db(double loss_db) { return std::pow(10.0, -loss_db / 20.0); }

// Zweitor ohne Reflexion mit Transmission tau in beide Richtungen (reziprok).
CMatrix two_port(cplx tau) {
  CMatrix s = CMatrix::Zero(2, 2);
  s(0, 1) = tau;
  s(1, 0) = tau;
  return s;
}

// Wellenleiter: phi = 2*pi*n_eff(lambda)*L/lambda mit linearer Dispersion um lambda0,
// n_eff(lambda) = n_eff0 + (n_eff0 - n_g)*(lambda - lambda0)/lambda0  (folgt aus n_g = n -
// lambda*dn/dlambda).
class Waveguide final : public Component {
 public:
  explicit Waveguide(const ParamMap& p)
      : length_(p.at("length")),
        n_eff_(p.at("n_eff")),
        n_group_(p.at("n_group")),
        wavelength_ref_(p.at("wavelength_ref")),
        loss_db_per_cm_(p.at("loss_db_per_cm")) {}

  int port_count() const override { return 2; }

  CMatrix s_matrix(double wavelength) const override {
    const double n_eff = n_eff_ + (n_eff_ - n_group_) * (wavelength - wavelength_ref_) / wavelength_ref_;
    const double phase = 2.0 * constants::pi * n_eff * length_ / wavelength;
    const double loss_db = loss_db_per_cm_ * length_ / 1e-2;
    return two_port(amplitude_from_loss_db(loss_db) * expi(phase));
  }

 private:
  double length_, n_eff_, n_group_, wavelength_ref_, loss_db_per_cm_;
};

// Richtkoppler: S = [[t, i*kappa], [i*kappa, t]] zwischen (in1, in2) und (out1, out2),
// t = sqrt(1 - K), kappa = sqrt(K), K = Leistungs-Kreuzkopplung = sin²(kL). Reflexionsfrei, reziprok.
class DirectionalCoupler final : public Component {
 public:
  explicit DirectionalCoupler(const ParamMap& p)
      : power_coupling_(p.at("power_coupling")), excess_loss_db_(p.at("excess_loss_db")) {}

  int port_count() const override { return 4; }

  CMatrix s_matrix(double /*wavelength*/) const override {
    const double amplitude = amplitude_from_loss_db(excess_loss_db_);
    const cplx t = amplitude * std::sqrt(1.0 - power_coupling_);
    const cplx ik = amplitude * I * std::sqrt(power_coupling_);
    // Portreihenfolge: in1, in2, out1, out2
    CMatrix s = CMatrix::Zero(4, 4);
    s(2, 0) = t;
    s(3, 0) = ik;
    s(2, 1) = ik;
    s(3, 1) = t;
    s(0, 2) = t;
    s(0, 3) = ik;
    s(1, 2) = ik;
    s(1, 3) = t;
    return s;
  }

 private:
  double power_coupling_, excess_loss_db_;
};

// Phasenschieber: idealer Faktor e^{i*phi}, wellenlängenunabhängig, mit optionalem Einfügeverlust.
class PhaseShifter final : public Component {
 public:
  explicit PhaseShifter(const ParamMap& p)
      : phase_(p.at("phase")), insertion_loss_db_(p.at("insertion_loss_db")) {}

  int port_count() const override { return 2; }

  CMatrix s_matrix(double /*wavelength*/) const override {
    return two_port(amplitude_from_loss_db(insertion_loss_db_) * expi(phase_));
  }

 private:
  double phase_, insertion_loss_db_;
};

// Laser: ideale monochromatische Quelle bei der Rechenwellenlänge (im Sweep = durchstimmbarer Laser).
// Auslaufende Welle sqrt(P)*e^{i*phi}; zurücklaufendes Licht wird vollständig absorbiert (idealer Isolator).
class Laser final : public Component {
 public:
  explicit Laser(const ParamMap& p) : power_(p.at("power")), phase_(p.at("phase")) {}

  int port_count() const override { return 1; }
  CMatrix s_matrix(double /*wavelength*/) const override { return CMatrix::Zero(1, 1); }

  CVector source(double /*wavelength*/) const override {
    CVector b(1);
    b(0) = std::sqrt(power_) * expi(phase_);
    return b;
  }

  std::vector<Measurement> measure(const CVector& a, const CVector& /*b*/) const override {
    return {{"Rückreflexion", std::norm(a(0)), "W"}};
  }

 private:
  double power_, phase_;
};

// Detektor: idealer Absorber ohne Reflexion. Photostrom I = R*P + I_dunkel (noch ohne Rauschen und
// Bandbreite).
class Detector final : public Component {
 public:
  explicit Detector(const ParamMap& p)
      : responsivity_(p.at("responsivity")), dark_current_(p.at("dark_current")) {}

  int port_count() const override { return 1; }
  CMatrix s_matrix(double /*wavelength*/) const override { return CMatrix::Zero(1, 1); }

  std::vector<Measurement> measure(const CVector& a, const CVector& /*b*/) const override {
    const double optical_power = std::norm(a(0));
    return {{"Leistung", optical_power, "W"},
            {"Photostrom", responsivity_ * optical_power + dark_current_, "A"}};
  }

 private:
  double responsivity_, dark_current_;
};

template <class T>
std::unique_ptr<Component> make(const ParamMap& p) {
  return std::make_unique<T>(p);
}

}  // namespace

// Anzeigetexte bleiben im Latin-1-Zeichenvorrat (kein Griechisch), damit jede Schrift der Oberfläche sie
// darstellt.
void register_builtin_components(ComponentRegistry& registry) {
  const double two_pi = 2.0 * constants::pi;

  // Standardwerte Wellenleiter: Silizium-Streifen 500 nm x 220 nm, TE0 bei 1550 nm,
  // n_eff ca. 2,44 und n_g ca. 4,2; Verlust ca. 2 dB/cm.
  // Quelle: L. Chrostowski, M. Hochberg, "Silicon Photonics Design", Cambridge University Press 2015, Kap. 3.
  registry.add({"waveguide",
                "Wellenleiter",
                "Phase aus n_eff mit linearer Dispersion (n_g), Verlust in dB/cm; keine Reflexion.",
                {"in", "out"},
                {{"length", "Länge", 100e-6, 0.0, 1.0, "µm", 1e-6},
                 {"n_eff", "Effektiver Index", 2.44, 1.0, 5.0, "", 1.0},
                 {"n_group", "Gruppenindex", 4.2, 1.0, 10.0, "", 1.0},
                 {"wavelength_ref", "Bezugswellenlänge", 1550e-9, 100e-9, 20e-6, "nm", 1e-9},
                 {"loss_db_per_cm", "Verlust", 2.0, 0.0, 1000.0, "dB/cm", 1.0}},
                make<Waveguide>});

  registry.add({"coupler",
                "Richtkoppler",
                "S = [[t, i*k], [i*k, t]] mit k² = Kreuzkopplung, wellenlängenunabhängig; keine Reflexion.",
                {"in1", "in2", "out1", "out2"},
                {{"power_coupling", "Kreuzkopplung (Leistung)", 0.5, 0.0, 1.0, "", 1.0},
                 {"excess_loss_db", "Zusatzverlust", 0.0, 0.0, 60.0, "dB", 1.0}},
                make<DirectionalCoupler>});

  registry.add({"phase_shifter",
                "Phasenschieber",
                "Idealer Phasenfaktor exp(i*Phase), wellenlängenunabhängig (noch ohne Heizer-Modell).",
                {"in", "out"},
                {{"phase", "Phase", 0.0, -2.0 * two_pi, 2.0 * two_pi, "rad", 1.0},
                 {"insertion_loss_db", "Einfügeverlust", 0.0, 0.0, 60.0, "dB", 1.0}},
                make<PhaseShifter>});

  registry.add({"laser",
                "Laser",
                "Ideale monochromatische Quelle mit idealem Isolator; keine Linienbreite, kein RIN.",
                {"out"},
                {{"power", "Leistung", 1e-3, 0.0, 10.0, "mW", 1e-3},
                 {"phase", "Phase", 0.0, -2.0 * two_pi, 2.0 * two_pi, "rad", 1.0}},
                make<Laser>});

  // Standard-Responsivität 1,0 A/W: typischer Wert für Ge-auf-Si-Photodioden bei 1550 nm.
  // Quelle: Chrostowski/Hochberg, "Silicon Photonics Design", Kap. 7.
  registry.add({"detector",
                "Detektor",
                "Idealer Absorber, I = R*P + Dunkelstrom; noch ohne Rauschen und Bandbreite.",
                {"in"},
                {{"responsivity", "Responsivität", 1.0, 0.0, 10.0, "A/W", 1.0},
                 {"dark_current", "Dunkelstrom", 0.0, 0.0, 1e-3, "nA", 1e-9}},
                make<Detector>});
}

}  // namespace photonlab::circuit
