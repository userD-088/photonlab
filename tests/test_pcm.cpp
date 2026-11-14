// Phase 9: PCM-Speicherzelle (GST) - Lesen, Schreiben, Löschen, Zwischenstufen, Nichtflüchtigkeit.
#include <doctest/doctest.h>

#include <cmath>
#include <complex>

#include "photonlab/circuit/pcm.hpp"
#include "photonlab/circuit/registry.hpp"
#include "photonlab/circuit/solver.hpp"
#include "photonlab/core/units.hpp"
#include "photonlab/materials/phase_change.hpp"

using namespace photonlab;
using namespace photonlab::circuit;

namespace {
const double lambda0 = 1550e-9;
}

TEST_CASE("PCM: Transmission = exp(-2 k0 Gamma k L) für die reinen Phasen") {
  const auto& gst = materials::gst225();
  PcmCell c;
  const double k0 = 2.0 * constants::pi / lambda0;
  c.fraction = 0.0;
  CHECK(pcm_transmission(c, lambda0) ==
        doctest::Approx(std::exp(-2.0 * k0 * c.confinement * gst.index_amorphous.imag() * c.length))
            .epsilon(1e-12)
            .scale(0.0));
  c.fraction = 1.0;
  CHECK(pcm_transmission(c, lambda0) ==
        doctest::Approx(std::exp(-2.0 * k0 * c.confinement * gst.index_crystalline.imag() * c.length))
            .epsilon(1e-12)
            .scale(0.0));
  // Zwischenstufen liegen monoton dazwischen
  double last = 2.0;
  for (double x = 0.0; x <= 1.0001; x += 0.1) {
    c.fraction = x;
    const double t = pcm_transmission(c, lambda0);
    CHECK(t < last);
    last = t;
  }
}

TEST_CASE("PCM: Lesen bei kleiner Leistung verändert den Zustand nicht (nichtflüchtig)") {
  PcmCell c;
  c.fraction = 0.4;
  for (int i = 0; i < 100; ++i) {
    const PulseResult r = apply_pcm_pulse(c, 0.1e-3, 10e-9, lambda0);  // 0,1 mW Lesepuls
    CHECK(r.peak_temperature < 320.0);
  }
  CHECK(c.fraction == doctest::Approx(0.4).epsilon(1e-12).scale(0.0));
}

TEST_CASE("PCM: ein einzelner starker Puls aus dem amorphen Zustand schmilzt statt zu kristallisieren") {
  // Amorphes GST absorbiert wenig; heizt ein Puls es ins Fenster, steigt beim Kristallisieren die Absorption
  // und es überhitzt: deshalb Programmieren mit Nachlesen
  PcmCell c;
  const PulseResult r = apply_pcm_pulse(c, 30e-3, 200e-9, lambda0);
  CHECK(r.melted);
  CHECK(c.fraction == 0.0);
}

TEST_CASE("PCM: Schreiben, Lesen, Löschen und Zwischenstufen (Abnahme Phase 9)") {
  PcmCell c;
  const double t_amorph = pcm_transmission(c, lambda0);
  // Kristallisieren (SET)
  ProgramResult r = program_pcm(c, 1.0, lambda0);
  CHECK(r.reached);
  for (const auto& p : r.pulses) CHECK_FALSE(p.melted);
  const double t_cryst = pcm_transmission(c, lambda0);
  MESSAGE("Transmission amorph " << t_amorph << ", kristallin " << t_cryst << " (" << r.pulses.size()
                                 << " Pulse)");
  CHECK(t_amorph - t_cryst > 0.2);  // deutlicher Lesekontrast
  // Löschen (RESET) durch Schmelzen
  r = program_pcm(c, 0.0, lambda0);
  CHECK(r.reached);
  CHECK(r.pulses.size() == 1);
  CHECK(r.pulses.front().melted);
  CHECK(pcm_transmission(c, lambda0) == doctest::Approx(t_amorph).epsilon(1e-9).scale(0.0));
  // Zwischenstufen (mehrere Pegel je Zelle), jeweils innerhalb der Toleranz und unterscheidbar
  double last_t = 2.0;
  for (const double target : {0.25, 0.5, 0.75}) {
    r = program_pcm(c, target, lambda0);
    CAPTURE(target);
    CHECK(r.reached);
    CHECK(c.fraction == doctest::Approx(target).epsilon(0.02).scale(1.0));
    const double t = pcm_transmission(c, lambda0);
    CHECK(t < last_t);
    last_t = t;
  }
  // zurück auf eine kleinere Stufe: erst löschen, dann programmieren
  r = program_pcm(c, 0.25, lambda0);
  CHECK(r.reached);
  CHECK(r.pulses.front().melted);
}

TEST_CASE("PCM-Bauteil: Handlungen ändern den gespeicherten Anteil, die Schaltung liest ihn") {
  const ComponentType* type = ComponentRegistry::builtin().find("pcm_cell");
  REQUIRE(type != nullptr);
  REQUIRE(type->actions.size() == 3);
  ParamMap params = type->default_params();
  params["write_target"] = 0.5;
  const std::string message = type->actions[0].apply(params, lambda0);
  CHECK(message.find("PCM") != std::string::npos);
  CHECK(params.at("crystalline_fraction") == doctest::Approx(0.5).epsilon(0.02).scale(1.0));
  Circuit c;
  c.add("laser", "laser", {{"power", 1e-4}});
  c.add("pcm", "pcm_cell", params);
  c.add("det", "detector");
  c.connect("laser.out", "pcm.in");
  c.connect("pcm.out", "det.in");
  const CompiledCircuit compiled(c, ComponentRegistry::builtin());
  const Solution sol = compiled.solve(lambda0);
  PcmCell cell;
  cell.fraction = params.at("crystalline_fraction");
  CHECK(std::norm(sol.a(compiled.port_index("det", "in"))) ==
        doctest::Approx(1e-4 * pcm_transmission(cell, lambda0)).epsilon(1e-9).scale(0.0));
  type->actions[1].apply(params, lambda0);  // Löschen
  CHECK(params.at("crystalline_fraction") == 0.0);
}
