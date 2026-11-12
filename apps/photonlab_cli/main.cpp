// PhotonLab Kommandozeile: Projektdateien rechnen, Sweeps als CSV, Validierung gegen analytische Lösungen.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include "photonlab/circuit/reference_designs.hpp"
#include "photonlab/circuit/registry.hpp"
#include "photonlab/circuit/solver.hpp"
#include "photonlab/circuit/sweep.hpp"
#include "photonlab/core/complex.hpp"
#include "photonlab/core/thread_pool.hpp"
#include "photonlab/core/units.hpp"
#include "photonlab/io/project.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace {

using namespace photonlab;

void print_usage() {
  std::printf(
      "Aufruf:\n"
      "  photonlab_cli --info                         Konstanten und ein Interferenz-Beispiel\n"
      "  photonlab_cli components                     alle Bauteiltypen mit Ports und Parametern\n"
      "  photonlab_cli example <name>                 Referenz-Design als .plab.json ausgeben (mzi, mzi_unbalanced)\n"
      "  photonlab_cli run <datei.plab.json>          Schaltung bei der Arbeitswellenlänge rechnen\n"
      "  photonlab_cli run <datei> --sweep [--csv <aus.csv>]   Wellenlängen-Sweep aus der Datei\n"
      "  photonlab_cli validate [--out <ordner>]      Validierung gegen analytische Lösungen\n");
}

int cmd_info() {
  const double lambda = 1550 * units::nm;
  std::printf("c0         = %.0f m/s\n", constants::c0);
  std::printf("eps0       = %.6e F/m\n", constants::eps0);
  std::printf("1550 nm    = %.3f THz\n", frequency_from_wavelength(lambda) / 1e12);
  // kleines Interferenz-Beispiel: zwei gleichphasige Felder addieren sich zu 4-facher Leistung
  const cplx a = expi(0.0), b = expi(0.0);
  std::printf("|a+b|^2    = %.1f (konstruktive Interferenz)\n", power(a + b));
  return 0;
}

int cmd_components() {
  for (const auto& type : circuit::ComponentRegistry::builtin().types()) {
    std::printf("%s  (%s)\n  %s\n  Ports:", type.name.c_str(), type.label.c_str(), type.description.c_str());
    for (const auto& port : type.port_names) std::printf(" %s", port.c_str());
    std::printf("\n");
    for (const auto& p : type.params)
      std::printf("  %-18s %-22s Standard %g %s\n", p.name.c_str(), p.label.c_str(),
                  p.default_value / p.display_scale, p.display_unit.c_str());
    std::printf("\n");
  }
  return 0;
}

int cmd_example(std::string_view name) {
  io::Project project;
  project.circuit = circuit::reference_design(name);
  std::fputs(io::project_to_json(project).c_str(), stdout);
  return 0;
}

int cmd_run(const std::string& file, bool do_sweep, const std::string& csv_path) {
  const io::Project project = io::load_project(file);
  const circuit::CompiledCircuit compiled(project.circuit, circuit::ComponentRegistry::builtin());
  // Kopfzeile nach stderr, damit ein Sweep auf stdout reines CSV bleibt
  std::fprintf(stderr, "Schaltung: %s (%d Bauteile, %d Ports)\n", project.circuit.name.c_str(),
               compiled.instance_count(), compiled.port_count());

  if (!do_sweep) {
    const circuit::Solution solution = compiled.solve(project.wavelength);
    std::printf("Wellenlänge: %.3f nm\n", project.wavelength / units::nm);
    for (const auto& m : compiled.measurements(solution))
      std::printf("  %-14s %-14s %.9e %s\n", m.instance.c_str(), m.measurement.name.c_str(),
                  m.measurement.value, m.measurement.unit.c_str());
    const circuit::PowerBalance balance = compiled.power_balance(solution);
    std::printf("Leistungsbilanz: abgegeben %.6e W, absorbiert %.6e W, über offene Ports %.6e W\n",
                balance.emitted, balance.absorbed, balance.leaving_open);
    return 0;
  }

  ThreadPool pool;
  const std::vector<circuit::Solution> solutions = circuit::sweep(compiled, project.sweep, &pool);
  std::FILE* out = stdout;
  if (!csv_path.empty()) {
    out = std::fopen(csv_path.c_str(), "w");
    if (!out) {
      std::fprintf(stderr, "Fehler: %s kann nicht geschrieben werden\n", csv_path.c_str());
      return 1;
    }
  }
  bool header_written = false;
  for (const auto& solution : solutions) {
    const auto measurements = compiled.measurements(solution);
    if (!header_written) {
      std::fprintf(out, "wavelength_m");
      for (const auto& m : measurements)
        std::fprintf(out, ",%s.%s_%s", m.instance.c_str(), m.measurement.name.c_str(),
                     m.measurement.unit.c_str());
      std::fprintf(out, "\n");
      header_written = true;
    }
    std::fprintf(out, "%.12e", solution.wavelength);
    for (const auto& m : measurements) std::fprintf(out, ",%.12e", m.measurement.value);
    std::fprintf(out, "\n");
  }
  if (out != stdout) {
    std::fclose(out);
    std::printf("%zu Punkte nach %s geschrieben\n", solutions.size(), csv_path.c_str());
  }
  return 0;
}

// Referenzfall MZI (Bauplan Abschnitt 16): Ausgänge folgen sin²/cos² der Phasendifferenz, Energie bleibt erhalten.
int cmd_validate(const std::string& out_dir) {
  const auto& registry = circuit::ComponentRegistry::builtin();
  const double laser_power = 1e-3;
  const double tolerance = 1e-12;
  const int steps = 360;

  std::ofstream csv;
  if (!out_dir.empty()) {
    std::filesystem::create_directories(out_dir);
    csv.open(std::filesystem::path(out_dir) / "mzi.csv");
    csv << "phase_rad,bar_simulation,bar_analytic,cross_simulation,cross_analytic,energy_error\n";
    csv.precision(15);
  }

  double worst_transfer = 0.0, worst_energy = 0.0;
  for (int k = 0; k <= steps; ++k) {
    const double phase = 2.0 * constants::pi * k / steps;
    circuit::Circuit mzi = circuit::reference_mzi();
    mzi.find_instance("phase")->params["phase"] = phase;
    const circuit::CompiledCircuit compiled(mzi, registry);
    const circuit::Solution solution = compiled.solve(1550 * units::nm);
    const double bar = power(solution.a(compiled.port_index("det_bar", "in"))) / laser_power;
    const double cross = power(solution.a(compiled.port_index("det_cross", "in"))) / laser_power;
    const double bar_expected = std::pow(std::sin(phase / 2.0), 2);
    const double cross_expected = std::pow(std::cos(phase / 2.0), 2);
    const double energy_error = std::abs(bar + cross - 1.0);
    worst_transfer = std::max({worst_transfer, std::abs(bar - bar_expected), std::abs(cross - cross_expected)});
    worst_energy = std::max(worst_energy, energy_error);
    if (csv.is_open())
      csv << phase << ',' << bar << ',' << bar_expected << ',' << cross << ',' << cross_expected << ','
          << energy_error << '\n';
  }

  const bool ok = worst_transfer < tolerance && worst_energy < tolerance;
  std::printf("MZI gegen cos²(Δφ/2):   größte Abweichung %.3e (Toleranz %.0e)\n", worst_transfer, tolerance);
  std::printf("MZI Energieerhaltung:   größte Abweichung %.3e (Toleranz %.0e)\n", worst_energy, tolerance);
  if (csv.is_open()) std::printf("Bericht: %s/mzi.csv\n", out_dir.c_str());
  std::printf("%s\n", ok ? "BESTANDEN" : "NICHT BESTANDEN");
  return ok ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
  SetConsoleOutputCP(CP_UTF8);  // Umlaute in der Windows-Konsole
#endif
  const std::vector<std::string> args(argv + 1, argv + argc);
  if (args.empty()) {
    std::printf("PhotonLab CLI 0.2.0 (Phase 1)\n");
    print_usage();
    return 0;
  }

  // Wert hinter einer Option wie "--csv <datei>"; leer, wenn die Option fehlt
  auto option = [&](std::string_view name) -> std::string {
    for (std::size_t i = 1; i + 1 < args.size(); ++i)
      if (args[i] == name) return args[i + 1];
    return {};
  };
  auto flag = [&](std::string_view name) {
    for (std::size_t i = 1; i < args.size(); ++i)
      if (args[i] == name) return true;
    return false;
  };

  try {
    const std::string& command = args[0];
    if (command == "--info") return cmd_info();
    if (command == "components") return cmd_components();
    if (command == "example" && args.size() >= 2) return cmd_example(args[1]);
    if (command == "run" && args.size() >= 2) return cmd_run(args[1], flag("--sweep"), option("--csv"));
    if (command == "validate") return cmd_validate(option("--out"));
    print_usage();
    return 2;
  } catch (const std::exception& e) {
    std::fprintf(stderr, "Fehler: %s\n", e.what());
    return 1;
  }
}
