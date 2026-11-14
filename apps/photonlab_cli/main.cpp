// PhotonLab Kommandozeile: Projektdateien rechnen, Sweeps als CSV, Validierung gegen analytische Lösungen.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <future>
#include <string>
#include <string_view>
#include <vector>

#include "photonlab/circuit/analysis.hpp"
#include "photonlab/circuit/monte_carlo.hpp"
#include "photonlab/circuit/reference_designs.hpp"
#include "photonlab/circuit/registry.hpp"
#include "photonlab/circuit/solver.hpp"
#include "photonlab/circuit/sweep.hpp"
#include "photonlab/core/complex.hpp"
#include "photonlab/core/thread_pool.hpp"
#include "photonlab/core/units.hpp"
#include "photonlab/field/fdtd.hpp"
#include "photonlab/field/reference_scenes.hpp"
#include "photonlab/field/sparameters.hpp"
#include "photonlab/field/spectrum.hpp"
#include "photonlab/io/project.hpp"
#include "photonlab/logic/field_gate.hpp"
#include "photonlab/logic/reference_gates.hpp"
#include "photonlab/logic/report.hpp"
#include "photonlab/materials/material.hpp"
#include "photonlab/modes/cross_section.hpp"
#include "photonlab/pwe/pwe.hpp"

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
      "  photonlab_cli example <name>                 Referenz-Design als .plab.json ausgeben (mzi, "
      "mzi_unbalanced,\n"
      "                                               ring_allpass, ring_adddrop, xor_interference,\n"
      "                                               and_interference, xor_directed, field_waveguide,\n"
      "                                               field_interface, field_interference, field_ring,\n"
      "                                               field_gap, field_gap_pair, field_coupler, "
      "field_phc_w1,\n"
      "                                               field_phc_bend, field_phc_y, field_phc_y_xor)\n"
      "  photonlab_cli run <datei.plab.json>          Schaltung bei der Arbeitswellenlänge rechnen\n"
      "  photonlab_cli run <datei> --sweep [--csv <aus.csv>]   Wellenlängen-Sweep aus der Datei\n"
      "  photonlab_cli run <datei> --resonances       Sweep auswerten: Resonanzen, FSR, Güte, Extinktion\n"
      "  photonlab_cli run <datei> --monte-carlo [--csv <aus.csv>]   Streuung aus der Datei rechnen\n"
      "  photonlab_cli run <datei> --logic [--gate <aus.gate.json>]  Wahrheitstabelle und Gatter-Zeugnis\n"
      "  photonlab_cli run <datei> --field [--csv <aus.csv>]         2D-FDTD der Feld-Szene über ihre "
      "Laufzeit\n"
      "  photonlab_cli run <datei> --modes            Moden des Wellenleiter-Querschnitts der Datei\n"
      "  photonlab_cli run <datei> --sparam [--out <aus.sparam.json>] [--name <name>]\n"
      "                                               S-Parameter der Feld-Szene an ihren Ports extrahieren\n"
      "  photonlab_cli bands [--lattice triangular|square] [--radius 0.48] [--eps 13] [--hole-eps 1]\n"
      "                    [--pol ez|hz|both] [--cutoff 7] [--a-nm <nm>] [--csv <aus.csv>]\n"
      "                                               Bandstruktur eines photonischen Kristalls (PWE)\n"
      "  photonlab_cli field-gate [<name>] [--csv <aus.csv>]\n"
      "                                               Gatter auf Feld-Ebene (Kristall-Gatter): "
      "Wahrheitstabelle aus\n"
      "                                               FDTD-Rechnungen; ohne Namen: Liste (phc_or_and, "
      "phc_xor, phc_not,\n"
      "                                               phc_halfadder)\n"
      "  photonlab_cli materials [wellenlaenge_nm]    Brechzahl und Gruppenindex der Materialbibliothek\n"
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
  std::fputs(io::project_to_json(io::reference_project(name)).c_str(), stdout);
  return 0;
}

int cmd_run(const std::string& file, bool do_sweep, const std::string& csv_path) {
  const io::Project project = io::load_project(file);
  const circuit::CompiledCircuit compiled(project.circuit, *io::make_registry(project));
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

// Sweep rechnen und jede Leistungs-Messgröße auf Resonanzen untersuchen
int cmd_resonances(const std::string& file) {
  const io::Project project = io::load_project(file);
  const circuit::CompiledCircuit compiled(project.circuit, *io::make_registry(project));
  ThreadPool pool;
  const auto solutions = circuit::sweep(compiled, project.sweep, &pool);
  std::vector<double> wavelengths;
  std::vector<std::string> names;
  std::vector<std::vector<double>> series;
  for (const auto& solution : solutions) {
    wavelengths.push_back(solution.wavelength);
    std::size_t k = 0;
    for (const auto& m : compiled.measurements(solution)) {
      if (m.measurement.unit != "W") continue;
      if (series.size() <= k) {
        series.emplace_back();
        names.push_back(m.instance + " " + m.measurement.name);
      }
      series[k++].push_back(m.measurement.value);
    }
  }
  for (std::size_t k = 0; k < series.size(); ++k) {
    {
      circuit::ResonanceKind kind = circuit::ResonanceKind::dip;
      const auto resonances = circuit::find_dominant_resonances(wavelengths, series[k], &kind);
      if (resonances.empty()) continue;
      std::printf("%s: %zu %s, mittlerer Abstand (FSR) %.4f nm\n", names[k].c_str(), resonances.size(),
                  kind == circuit::ResonanceKind::dip ? "Einbrüche" : "Spitzen",
                  circuit::mean_spacing(resonances) / units::nm);
      for (const auto& r : resonances)
        std::printf("  %.4f nm   Breite %.2f pm   Q %.0f   Extinktion %.2f dB\n", r.position / units::nm,
                    r.fwhm / 1e-12, r.quality, r.extinction_db);
    }
  }
  std::printf("Hinweis: unter etwa 10 Punkten pro Halbwertsbreite sind Breite und Extinktion ungenau.\n");
  return 0;
}

int cmd_monte_carlo(const std::string& file, const std::string& csv_path) {
  const io::Project project = io::load_project(file);
  const auto registry_ptr = io::make_registry(project);
  const auto& registry = *registry_ptr;
  if (project.monte_carlo.variations.empty())
    std::fprintf(stderr, "Hinweis: Die Datei enthält keine Streuungen, alle Stichproben sind gleich.\n");
  ThreadPool pool;
  const circuit::MonteCarloResult result =
      circuit::monte_carlo(project.circuit, registry, project.wavelength, project.monte_carlo, &pool);
  std::printf("Monte-Carlo: %d Stichproben, Startwert %llu, Wellenlänge %.3f nm, nicht lösbar: %d\n",
              project.monte_carlo.samples, static_cast<unsigned long long>(project.monte_carlo.seed),
              project.wavelength / units::nm, result.failed);
  for (std::size_t k = 0; k < result.names.size(); ++k) {
    const circuit::Statistics st = circuit::statistics(result.values[k]);
    std::printf("  %-28s Mittel %.6e  Streuung %.3e  Min %.6e  Max %.6e %s\n", result.names[k].c_str(),
                st.mean, st.stddev, st.min, st.max, result.units[k].c_str());
  }
  if (!csv_path.empty()) {
    std::ofstream csv(csv_path);
    if (!csv) {
      std::fprintf(stderr, "Fehler: %s kann nicht geschrieben werden\n", csv_path.c_str());
      return 1;
    }
    csv.precision(12);
    csv << "sample";
    for (std::size_t k = 0; k < result.names.size(); ++k)
      csv << ',' << result.names[k] << '_' << result.units[k];
    csv << '\n';
    for (int i = 0; i < project.monte_carlo.samples; ++i) {
      csv << i;
      for (const auto& values : result.values) csv << ',' << values[static_cast<std::size_t>(i)];
      csv << '\n';
    }
    std::printf("Stichproben nach %s geschrieben\n", csv_path.c_str());
  }
  return 0;
}

// Referenzfall Ring (Bauplan Abschnitt 16 und Abnahme Phase 2): FSR, Güte und Extinktion gegen die Formeln.
bool validate_ring(const std::string& out_dir) {
  const auto& registry = circuit::ComponentRegistry::builtin();
  const circuit::Circuit ring = circuit::reference_ring_allpass();
  const circuit::Instance& inst = *ring.find_instance("ring");
  const circuit::ComponentType& type = *registry.find("ring_allpass");
  auto value = [&](const char* name) {
    const auto it = inst.params.find(name);
    return it != inst.params.end() ? it->second : type.find_param(name)->default_value;
  };
  const double length = 2.0 * constants::pi * value("radius");
  const double t = std::sqrt(1.0 - value("power_coupling"));
  const double a = std::pow(10.0, -value("loss_db_per_cm") * (length / 1e-2) / 20.0);
  const double n_group = value("n_group");

  const circuit::CompiledCircuit compiled(ring, registry);
  ThreadPool pool;
  const circuit::SweepSettings settings{1540e-9, 1560e-9, 40001};  // 0,5 pm Schrittweite
  const auto solutions = circuit::sweep(compiled, settings, &pool);
  std::vector<double> wavelengths, transmission;
  const int det = compiled.port_index("det", "in");
  for (const auto& solution : solutions) {
    wavelengths.push_back(solution.wavelength);
    transmission.push_back(power(solution.a(det)) / 1e-3);
  }
  const auto resonances = circuit::find_resonances(wavelengths, transmission, circuit::ResonanceKind::dip);
  if (resonances.size() < 2) {
    std::printf("Ring: weniger als zwei Resonanzen gefunden\n");
    return false;
  }
  const auto& r1 = resonances[0];
  const auto& r2 = resonances[1];
  const double fsr_expected = r1.position * r2.position / (n_group * length);
  const double fwhm_expected =
      r1.position * r1.position * (1.0 - t * a) / (constants::pi * n_group * length * std::sqrt(t * a));
  const double extinction_expected = 10.0 * std::log10(std::pow((1.0 - t * a) / (a - t), 2));
  const double fsr_error = std::abs((r2.position - r1.position) / fsr_expected - 1.0);
  const double fwhm_error = std::abs(r1.fwhm / fwhm_expected - 1.0);
  const double extinction_error = std::abs(r1.extinction_db - extinction_expected);

  std::printf("Ring FSR:        %.5f nm, Formel %.5f nm (rel. Abweichung %.1e, Toleranz 1e-4)\n",
              (r2.position - r1.position) / units::nm, fsr_expected / units::nm, fsr_error);
  std::printf("Ring Güte:       %.0f, Formel %.0f (rel. Abweichung der Breite %.1e, Toleranz 1e-2)\n",
              r1.quality, r1.position / fwhm_expected, fwhm_error);
  std::printf("Ring Extinktion: %.3f dB, Formel %.3f dB (Abweichung %.1e dB, Toleranz 0,05 dB)\n",
              r1.extinction_db, extinction_expected, extinction_error);
  if (!out_dir.empty()) {
    std::ofstream csv(std::filesystem::path(out_dir) / "ring.csv");
    csv.precision(12);
    csv << "quantity,simulation,analytic\n";
    csv << "fsr_m," << r2.position - r1.position << ',' << fsr_expected << '\n';
    csv << "fwhm_m," << r1.fwhm << ',' << fwhm_expected << '\n';
    csv << "quality," << r1.quality << ',' << r1.position / fwhm_expected << '\n';
    csv << "extinction_db," << r1.extinction_db << ',' << extinction_expected << '\n';
    std::printf("Bericht: %s/ring.csv\n", out_dir.c_str());
  }
  return fsr_error < 1e-4 && fwhm_error < 1e-2 && extinction_error < 0.05;
}

void print_report(const logic::GateDefinition& gate, const logic::GateReport& report) {
  // Wahrheitstabelle mit gemessenen Leistungen
  for (const auto& in : gate.inputs) std::printf("%-3s", in.name.c_str());
  for (const auto& out : gate.outputs) std::printf(" | %-22s", out.name.c_str());
  std::printf("\n");
  for (std::size_t row = 0; row < report.table.rows.size(); ++row) {
    for (int bit : report.table.rows[row].bits) std::printf("%-3d", bit);
    for (std::size_t k = 0; k < gate.outputs.size(); ++k) {
      const double p = report.table.rows[row].power[k];
      const auto& a = report.outputs[k];
      const bool decided = p > a.threshold;
      std::printf(" | %10.4f µW -> %d %-4s", p / 1e-6, decided ? 1 : 0,
                  decided == (a.bits[row] != 0) ? "" : "FALSCH");
    }
    std::printf("\n");
  }
  for (std::size_t k = 0; k < gate.outputs.size(); ++k) {
    const auto& a = report.outputs[k];
    std::printf("%s: %s, Entscheidungspegel %.4f µW, Kontrast %s, Extinktion %s\n",
                gate.outputs[k].name.c_str(), a.function.empty() ? "unbekannte Funktion" : a.function.c_str(),
                a.threshold / 1e-6, logic::format_db(a.contrast_db).c_str(),
                logic::format_db(a.extinction_db).c_str());
  }
  std::printf("\nZeugnis \"%s\" bei %.3f nm\n", report.gate_name.c_str(), report.wavelength / units::nm);
  for (const auto& c : report.criteria) {
    const char* mark = c.rating == logic::Rating::good      ? "[ gut ]"
                       : c.rating == logic::Rating::limited ? "[ ~~~ ]"
                       : c.rating == logic::Rating::bad     ? "[NEIN ]"
                                                            : "[  -  ]";
    std::printf("%s %-30s %s\n          %s\n", mark, c.name.c_str(), c.value.c_str(), c.remark.c_str());
  }
  std::printf("\n%d von %d bewerteten Kriterien erfüllt: %s\n", report.good_count, report.rated_count,
              report.real_gate ? "erfüllt alle Anforderungen an ein Logikelement."
                               : "Demonstrator, kein beliebig kaskadierbares Logikelement.");
}

int cmd_logic(const std::string& file, const std::string& gate_path) {
  const io::Project project = io::load_project(file);
  if (project.gate.empty()) {
    std::fprintf(stderr, "Fehler: Die Datei enthält keine Gatter-Definition (Abschnitt \"logic\").\n");
    return 1;
  }
  const logic::GateReport report =
      logic::characterize(project.circuit, *io::make_registry(project), project.gate, project.wavelength);
  print_report(project.gate, report);
  if (!gate_path.empty()) {
    io::save_gate_report(project.gate, report, gate_path);
    std::printf("Charakterisiertes Gatter nach %s geschrieben\n", gate_path.c_str());
  }
  return 0;
}

int cmd_materials(double wavelength) {
  std::printf("Materialbibliothek bei %.1f nm\n", wavelength / units::nm);
  for (const auto& m : materials::library()) {
    std::printf("  %-6s n = %.5f   n_g = %.5f   dn/dT = %.2e 1/K   %s\n", m.name.c_str(), m.index(wavelength),
                m.group_index(wavelength), m.thermo_optic,
                m.in_valid_range(wavelength) ? "" : "(außerhalb des Gültigkeitsbereichs!)");
    std::printf("         %s\n", m.source.c_str());
  }
  return 0;
}

void print_modes(const modes::CrossSection& section, const modes::CrossSectionResult& result) {
  std::printf("Querschnitt: %s %.0f x %.0f nm", section.core.c_str(), section.width / units::nm,
              section.height / units::nm);
  if (section.slab_height > 0.0) std::printf(" (Rippe, Schicht %.0f nm)", section.slab_height / units::nm);
  std::printf(" auf %s, Deckschicht %s, %.1f nm, %s\n", section.lower.c_str(), section.upper.c_str(),
              section.wavelength / units::nm,
              section.polarization == modes::FdePolarization::quasi_te ? "quasi-TE (E waagerecht)"
                                                                       : "quasi-TM (E senkrecht)");
  std::printf("Gitter %d x %d Zellen zu %.0f nm\n", result.grid.nx, result.grid.ny,
              section.cell_size / units::nm);
  std::printf("  Nr  n_eff     n_g      geführt  Einschluss  Modenfläche\n");
  for (std::size_t k = 0; k < result.modes.size(); ++k) {
    const auto& m = result.modes[k];
    std::printf("  %-3zu %.5f   %.4f   %-7s  %5.1f %%     %.4f µm²\n", k, m.n_eff, m.n_group,
                m.guided ? "ja" : "nein", 100.0 * m.confinement, m.effective_area / 1e-12);
  }
  if (result.modes.empty()) std::printf("  keine konvergierte Mode gefunden\n");
  std::printf(
      "Hinweis: semi-vektorieller Löser; bei stark führenden Wellenleitern liegt n_eff einige Prozent "
      "über vollvektoriellen Werten.\n");
}

int cmd_sparam(const std::string& file, const std::string& out_path, const std::string& name_option) {
  const io::Project project = io::load_project(file);
  const field::FieldScene& scene = project.field;
  if (scene.empty() || scene.ports.empty()) {
    std::fprintf(stderr, "Fehler: Die Datei enthält keine Feld-Szene mit Ports.\n");
    return 1;
  }
  const auto& settings = scene.sparameters;
  std::printf("Szene: %s, %zu Ports, %d Wellenlängen von %.1f bis %.1f nm\n", scene.name.c_str(),
              scene.ports.size(), settings.points, settings.wavelength_start / units::nm,
              settings.wavelength_stop / units::nm);
  ThreadPool pool;
  const field::SParameterResult result = field::extract_sparameters(scene, &pool);
  std::printf("%zu Zeitschritte, längste Anregung %.0f fs\n", result.steps,
              result.simulated_time / units::fs);
  for (const auto& w : result.warnings) std::printf("Hinweis: %s\n", w.c_str());
  std::printf("Hinweis: 2D-Rechnung; Abstrahlung nach oben und unten fehlt, Brechzahlen ohne Dispersion.\n");

  const std::size_t n = result.port_count();
  const std::size_t stride = std::max<std::size_t>(1, result.wavelengths.size() / 5);
  for (std::size_t k = 0; k < result.wavelengths.size(); k += stride) {
    std::printf("\n%.1f nm  |S_ij|² (Zeile = Ausgang, Spalte = Eingang)\n        ",
                result.wavelengths[k] / units::nm);
    for (const auto& p : result.ports) std::printf("%10s", p.c_str());
    std::printf("\n");
    for (std::size_t i = 0; i < n; ++i) {
      std::printf("  %-6s", result.ports[i].c_str());
      for (std::size_t j = 0; j < n; ++j) std::printf("%10.5f", std::norm(result.at(k, i, j)));
      std::printf("\n");
    }
  }
  // Reziprozität und Passivität über alle Wellenlängen
  double asymmetry = 0.0, worst_column = 0.0;
  for (std::size_t k = 0; k < result.wavelengths.size(); ++k)
    for (std::size_t j = 0; j < n; ++j) {
      double column = 0.0;
      for (std::size_t i = 0; i < n; ++i) {
        column += std::norm(result.at(k, i, j));
        asymmetry = std::max(asymmetry, std::abs(result.at(k, i, j) - result.at(k, j, i)));
      }
      worst_column = std::max(worst_column, column);
    }
  std::printf("\nReziprozität: größte Abweichung |S_ij - S_ji| = %.1e\n", asymmetry);
  std::printf("Passivität: größte Summe einer Spalte = %.4f (muss <= 1 sein; der Rest wird abgestrahlt)\n",
              worst_column);

  if (!out_path.empty()) {
    std::string name = name_option;
    if (name.empty()) {
      name = std::filesystem::path(out_path).filename().string();
      for (const char* suffix : {".sparam.json", ".json"})
        if (name.size() > std::strlen(suffix) && name.ends_with(suffix)) {
          name.resize(name.size() - std::strlen(suffix));
          break;
        }
    }
    io::save_sparameters(io::make_sparameter_table(scene, result, name), &scene, out_path);
    std::printf("S-Parameter als Bauteil 'sparam:%s' nach %s geschrieben\n", name.c_str(), out_path.c_str());
  }
  return 0;
}

// Abnahme Phase 6: zwei extrahierte Spalte, in der Schaltung hintereinander, gegen das FDTD des
// Gesamtaufbaus.
bool validate_sparameters(const std::string& out_dir) {
  ThreadPool pool;
  field::FieldScene single = field::reference_scene("field_gap");
  field::FieldScene pair = field::reference_scene("field_gap_pair");
  single.sparameters.points = pair.sparameters.points = 11;
  const field::SParameterResult one = field::extract_sparameters(single, &pool);
  const field::SParameterResult both = field::extract_sparameters(pair, &pool);
  io::Project project;
  project.sparameters = {io::make_sparameter_table(single, one, "spalt")};
  const auto registry = io::make_registry(project);
  circuit::Circuit c;
  c.add("a", "sparam:spalt");
  c.add("b", "sparam:spalt");
  c.connect("a.aus", "b.ein");
  const circuit::CompiledCircuit compiled(c, *registry);
  std::ofstream csv;
  if (!out_dir.empty()) {
    csv.open(std::filesystem::path(out_dir) / "sparameters.csv");
    csv.precision(8);
    csv << "wavelength_m,t2_circuit,t2_fdtd,r2_circuit,r2_fdtd\n";
  }
  double worst = 0.0;
  for (std::size_t k = 0; k < one.wavelengths.size(); ++k) {
    circuit::CVector a = circuit::CVector::Zero(compiled.port_count());
    a(compiled.port_index("a", "ein")) = 1.0;
    const circuit::Solution solution = compiled.solve(one.wavelengths[k], &a);
    const double t_circuit = std::norm(solution.b(compiled.port_index("b", "aus")));
    const double r_circuit = std::norm(solution.b(compiled.port_index("a", "ein")));
    const double t_fdtd = std::norm(both.at(k, 1, 0)), r_fdtd = std::norm(both.at(k, 0, 0));
    worst = std::max({worst, std::abs(t_circuit - t_fdtd), std::abs(r_circuit - r_fdtd)});
    if (csv.is_open())
      csv << one.wavelengths[k] << ',' << t_circuit << ',' << t_fdtd << ',' << r_circuit << ',' << r_fdtd
          << '\n';
  }
  std::printf(
      "Brücke 1: zwei extrahierte Spalte in der Schaltung gegen das Gesamt-FDTD: größte Abweichung der "
      "Leistung %.4f (Toleranz 0,01)\n",
      worst);
  if (csv.is_open()) std::printf("Bericht: %s/sparameters.csv\n", out_dir.c_str());
  return worst < 0.01;
}

int cmd_modes(const std::string& file) {
  const io::Project project = io::load_project(file);
  print_modes(project.cross_section, modes::analyze(project.cross_section, true));
  return 0;
}

// Abnahme Phase 5: Si-Streifen 500 x 220 nm in SiO2, n_eff der TE0-Mode im Bereich der Literatur (ca. 2,44).
// Bandstruktur eines 2D-Kristalls mit einem Loch (oder Stab) je Elementarzelle.
int cmd_bands(const std::string& lattice, const std::string& radius, const std::string& eps,
              const std::string& hole_eps, const std::string& pol, const std::string& cutoff,
              const std::string& a_nm, const std::string& csv_path) {
  const bool square = lattice == "square";
  if (!lattice.empty() && lattice != "square" && lattice != "triangular")
    throw std::runtime_error("--lattice: triangular oder square erwartet");
  const double r = radius.empty() ? 0.48 : std::atof(radius.c_str());
  const double e_bg = eps.empty() ? 13.0 : std::atof(eps.c_str());
  const double e_in = hole_eps.empty() ? 1.0 : std::atof(hole_eps.c_str());
  const pwe::UnitCell cell = square ? pwe::square_cell(r, e_bg, e_in) : pwe::triangular_cell(r, e_bg, e_in);
  pwe::BandSettings settings;
  settings.cutoff = cutoff.empty() ? 7.0 : std::atof(cutoff.c_str());
  settings.bands = 8;
  const double a = a_nm.empty() ? 0.0 : std::atof(a_nm.c_str()) * units::nm;
  const pwe::BandPath path =
      pwe::standard_path(cell, square ? pwe::PathKind::square : pwe::PathKind::triangular, 16);
  ThreadPool pool;

  std::ofstream csv;
  if (!csv_path.empty()) {
    csv.open(csv_path);
    csv << "polarization,distance,kx,ky";
    for (int b = 1; b <= settings.bands; ++b) csv << ",band" << b;
    csv << "\n";
    csv.precision(10);
  }
  std::printf("%s-Gitter, %s %.3f a, Hintergrund eps %.3f, Einschluss eps %.3f\n",
              square ? "Quadrat" : "Dreiecks", e_in < e_bg ? "Loecher mit r =" : "Staebe mit r =", r, e_bg,
              e_in);
  for (const pwe::Polarization p : {pwe::Polarization::ez, pwe::Polarization::hz}) {
    if (pol == "ez" && p != pwe::Polarization::ez) continue;
    if (pol == "hz" && p != pwe::Polarization::hz) continue;
    settings.polarization = p;
    const pwe::BandResult result = pwe::solve_bands(cell, settings, path, &pool);
    const char* name =
        p == pwe::Polarization::ez ? "ez (E senkrecht zur Ebene, TM)" : "hz (H senkrecht zur Ebene, TE)";
    std::printf("\nPolarisation %s, %d ebene Wellen\n", name, result.plane_waves);
    if (result.gaps.empty()) std::printf("  keine vollstaendige Luecke unter den berechneten Baendern\n");
    for (const pwe::Gap& g : result.gaps) {
      std::printf("  Luecke Band %d/%d: a/lambda %.4f ... %.4f  (Mitte %.4f, %.1f %% relativ)", g.band,
                  g.band + 1, g.lower, g.upper, g.midgap(), 100.0 * g.ratio());
      if (a > 0.0)
        std::printf("  -> lambda %.0f ... %.0f nm", a / g.upper / units::nm, a / g.lower / units::nm);
      std::printf("\n");
    }
    if (!result.gaps.empty() && a <= 0.0) {
      const pwe::Gap& g = result.gaps.front();
      std::printf("  Tipp: fuer lambda = 1550 nm in der Mitte der ersten Luecke a = %.0f nm waehlen\n",
                  1550.0 * g.midgap());
    }
    if (csv.is_open())
      for (std::size_t i = 0; i < path.points.size(); ++i) {
        csv << (p == pwe::Polarization::ez ? "ez" : "hz") << ',' << path.distance[i] << ','
            << path.points[i].kx << ',' << path.points[i].ky;
        for (const double f : result.frequency[i]) csv << ',' << f;
        csv << '\n';
      }
  }
  if (csv.is_open()) std::printf("\nBandstruktur nach %s geschrieben\n", csv_path.c_str());
  return 0;
}

// Bandstruktur (PWE): leeres Gitter exakt, vollständige Lücke des Dreiecksgitters gegen Joannopoulos.
bool validate_pwe() {
  // 1) leeres Gitter: tiefste Frequenzen bei Gamma sind 0 und |G1|/sqrt(eps) = 1/(sin 60 deg * n)
  const pwe::UnitCell empty = pwe::triangular_cell(0.3, 4.0, 4.0);
  pwe::BandSettings settings;
  settings.bands = 3;
  const std::vector<double> f = pwe::solve_k(empty, settings, {0.0, 0.0});
  const double expected = 1.0 / (std::sin(constants::pi / 3.0) * 2.0);
  const double empty_error = std::abs(f[1] - expected);

  // 2) Dreiecksgitter aus Luftlöchern r = 0,48a in eps = 13: vollständige Lücke (TE und TM überlappen)
  const pwe::UnitCell cell = pwe::triangular_cell(0.48, 13.0, 1.0);
  settings.cutoff = 7.0;
  settings.bands = 6;
  const pwe::BandPath path = pwe::standard_path(cell, pwe::PathKind::triangular, 12);
  settings.polarization = pwe::Polarization::ez;
  const pwe::BandResult tm = pwe::solve_bands(cell, settings, path);
  settings.polarization = pwe::Polarization::hz;
  const pwe::BandResult te = pwe::solve_bands(cell, settings, path);
  double lower = 0.0, upper = 0.0;
  for (const pwe::Gap& a : tm.gaps)
    for (const pwe::Gap& b : te.gaps) {
      const double lo = std::max(a.lower, b.lower), hi = std::min(a.upper, b.upper);
      if (hi - lo > upper - lower) lower = lo, upper = hi;
    }
  const bool gap_ok =
      upper > lower && std::abs(lower / 0.43 - 1.0) < 0.03 && std::abs(upper / 0.52 - 1.0) < 0.03;
  std::printf("PWE leeres Gitter: |f - exakt| = %.1e (Toleranz 1e-9)\n", empty_error);
  std::printf(
      "PWE Dreiecksgitter r = 0,48a, eps 13: vollstaendige Luecke a/lambda %.4f ... %.4f (Joannopoulos ca. "
      "0,43 ... 0,52, "
      "Toleranz 3 %%)\n",
      lower, upper);
  return empty_error < 1e-9 && gap_ok;
}

// Gatter auf Feld-Ebene: je Eingangskombination eine FDTD-Rechnung, dann Pegel, Kontrast und Ampeln.
int cmd_field_gate(const std::string& name, const std::string& csv_path) {
  if (name.empty()) {
    std::printf("Feld-Gatter:\n");
    for (const std::string& n : logic::field_gate_names()) {
      const logic::FieldGate g = logic::field_gate(n);
      std::printf("  %-14s %s\n", n.c_str(), g.description.c_str());
    }
    return 0;
  }
  const logic::FieldGate gate = logic::field_gate(name);
  std::printf("%s\n%s\n", gate.name.c_str(), gate.description.c_str());
  std::printf("Rechne %zu Zeilen x %zu Szene(n), je etwa eine halbe Minute ...\n", gate.row_count(),
              gate.scenes.size());
  ThreadPool pool;
  JobControl control;
  const logic::FieldGateReport report = logic::characterize_field_gate(gate, &pool, &control);

  std::printf("\nWahrheitstabelle (Leistung relativ zum groessten Pegel des Ausgangs)\n  ");
  for (const auto& in : gate.inputs) std::printf("%-3s", in.name.c_str());
  for (const auto& out : gate.outputs) std::printf("| %-10s soll ", out.name.c_str());
  std::printf("\n");
  for (std::size_t r = 0; r < report.table.rows.size(); ++r) {
    std::printf("  ");
    for (const int b : report.table.rows[r].bits) std::printf("%-3d", b);
    for (std::size_t k = 0; k < gate.outputs.size(); ++k) {
      const double rel = report.table.rows[r].power[k] / report.outputs[k].p_max;
      std::printf("| %-10.4f %-4d ", rel,
                  gate.outputs[k].expected.empty() ? -1 : gate.outputs[k].expected[r]);
    }
    std::printf("\n");
  }
  std::printf("\n");
  for (std::size_t k = 0; k < gate.outputs.size(); ++k) {
    const auto& a = report.outputs[k];
    std::printf("%s: Funktion %s, schlechteste 1 / schlechteste 0 = Kontrast %s, Schwelle %.3g W/m\n",
                gate.outputs[k].name.c_str(), a.separable ? "korrekt" : "FALSCH",
                logic::format_db(a.contrast_db).c_str(), a.threshold);
  }
  std::printf("\nZeugnis (Wellenlaenge %.0f nm):\n", report.wavelength / units::nm);
  for (const auto& c : report.criteria)
    std::printf("  %-26s %-14s %s%s%s\n", c.name.c_str(), std::string(logic::to_string(c.rating)).c_str(),
                c.value.c_str(), c.remark.empty() ? "" : "  - ", c.remark.c_str());
  if (!csv_path.empty()) {
    std::ofstream csv(csv_path);
    csv.precision(8);
    for (const auto& in : gate.inputs) csv << in.name << ',';
    for (std::size_t k = 0; k < gate.outputs.size(); ++k)
      csv << gate.outputs[k].name << "_W_per_m,erwartet" << (k + 1 < gate.outputs.size() ? "," : "");
    csv << '\n';
    for (std::size_t r = 0; r < report.table.rows.size(); ++r) {
      for (const int b : report.table.rows[r].bits) csv << b << ',';
      for (std::size_t k = 0; k < gate.outputs.size(); ++k)
        csv << report.table.rows[r].power[k] << ','
            << (gate.outputs[k].expected.empty() ? -1 : gate.outputs[k].expected[r])
            << (k + 1 < gate.outputs.size() ? "," : "");
      csv << '\n';
    }
    std::printf("Tabelle nach %s geschrieben\n", csv_path.c_str());
  }
  return report.function_correct ? 0 : 1;
}

// Kristall-Wellenleiter: ein 60°-Knick im W1-Liniendefekt überträgt bei 1580 nm fast alles (gegen den geraden
// Aufbau).
bool validate_phc_bend() {
  ThreadPool pool;
  double flux[2] = {0.0, 0.0};
  std::vector<std::future<void>> jobs;
  const char* names[2] = {"field_phc_w1", "field_phc_bend"};
  for (int i = 0; i < 2; ++i)
    jobs.push_back(pool.submit([&, i] {
      const field::FieldScene scene = field::reference_scene(names[i]);
      field::Fdtd sim(scene);
      sim.run(static_cast<std::size_t>(scene.duration / sim.time_step()));
      const double period = scene.sources.front().wavelength / constants::c0;
      for (const auto& m : sim.monitors())
        if (m.name == "Fluss Ausgang")
          flux[i] = std::abs(field::average_over_periods(m.values, m.sample_interval, period, 4));
    }));
  for (auto& j : jobs) j.get();
  const double transmission = flux[1] / flux[0];
  std::printf(
      "PhC-W1 mit 60°-Knick bei 1580 nm: Transmission gegen geraden W1 %.3f (erwartet 0,9 ... 1,05)\n",
      transmission);
  return transmission > 0.9 && transmission < 1.05;
}

bool validate_modes(const std::string& out_dir) {
  const modes::CrossSection section;
  const modes::CrossSectionResult result = modes::analyze(section, true);
  if (result.modes.empty()) {
    std::printf("Modenlöser: keine Mode gefunden\n");
    return false;
  }
  const auto& te0 = result.modes.front();
  const double literature_n = 2.44, literature_g = 4.2;  // Chrostowski/Hochberg 2015, Kap. 3 (vollvektoriell)
  const double n_error = std::abs(te0.n_eff / literature_n - 1.0);
  const double g_error = std::abs(te0.n_group / literature_g - 1.0);
  std::printf(
      "Si-Streifen 500 x 220 nm, TE0: n_eff %.4f (Literatur ca. %.2f, Abweichung %.1f %%, Toleranz 3 %%)\n",
      te0.n_eff, literature_n, 100.0 * n_error);
  std::printf(
      "                               n_g   %.4f (Literatur ca. %.1f, Abweichung %.1f %%, Toleranz 5 %%)\n",
      te0.n_group, literature_g, 100.0 * g_error);

  const double n_si = materials::get("Si").index(section.wavelength);
  const double n_ox = materials::get("SiO2").index(section.wavelength);
  const double slab =
      modes::slab_modes(n_si, n_ox, n_ox, 220e-9, section.wavelength, modes::SlabPolarization::te)
          .front()
          .n_eff;
  std::printf("Si-Schicht 220 nm, TE0 (Eigenwertgleichung): n_eff %.4f\n", slab);
  if (!out_dir.empty()) {
    std::ofstream csv(std::filesystem::path(out_dir) / "modes.csv");
    csv.precision(8);
    csv << "quantity,simulation,literature\n";
    csv << "strip_500x220_te0_n_eff," << te0.n_eff << ',' << literature_n << '\n';
    csv << "strip_500x220_te0_n_group," << te0.n_group << ',' << literature_g << '\n';
    csv << "slab_220_te0_n_eff," << slab << ",2.85\n";
    std::printf("Bericht: %s/modes.csv\n", out_dir.c_str());
  }
  return te0.guided && n_error < 0.03 && g_error < 0.05 && std::abs(slab - 2.85) < 0.03;
}

// Feld-Szene der Datei über ihre Laufzeit rechnen und die Monitore auswerten
int cmd_field(const std::string& file, const std::string& csv_path) {
  const io::Project project = io::load_project(file);
  const field::FieldScene& scene = project.field;
  if (scene.empty()) {
    std::fprintf(stderr, "Fehler: Die Datei enthält keine Feld-Szene (Abschnitt \"field\").\n");
    return 1;
  }
  if (!(scene.duration > 0.0)) {
    std::fprintf(stderr,
                 "Fehler: Die Szene hat keine Laufzeit (\"duration\"); ohne sie endet die Rechnung nie.\n");
    return 1;
  }
  field::Fdtd sim(scene);
  const auto steps = static_cast<std::size_t>(std::ceil(scene.duration / sim.time_step()));
  std::printf("Szene: %s\n", scene.name.c_str());
  std::printf(
      "Gitter %d x %d Zellen zu %.1f nm, Zeitschritt %.4f fs, %zu Schritte (%.1f fs), Polarisation %s\n",
      sim.nx(), sim.ny(), sim.cell_size() / units::nm, sim.time_step() / units::fs, steps,
      scene.duration / units::fs, scene.polarization == field::Polarization::ez ? "Ez" : "Hz");
  for (const auto& w : field::check(scene))
    std::printf("%s %s\n", w.severe ? "WARNUNG:" : "Hinweis:", w.text.c_str());
  std::printf(
      "Hinweis: 2D-Rechnung; Flüsse gelten je Meter Bauteilhöhe, Abstrahlung nach oben und unten fehlt.\n");
  sim.run(steps);

  // Auswertung: Spitzenwert, bei Dauerstrich zusätzlich der Mittelwert über die letzten Perioden
  double period = 0.0;
  bool continuous = false;
  for (const auto& source : scene.sources) {
    period = source.wavelength / constants::c0;
    continuous = continuous || source.signal == field::SignalKind::continuous;
  }
  for (const auto& m : sim.monitors()) {
    double peak = 0.0;
    for (double v : m.values) peak = std::max(peak, std::abs(v));
    std::printf("  %-24s Spitze %.6e %s", m.name.c_str(), peak, m.unit.c_str());
    if (m.kind == field::MonitorKind::flux && continuous && period > 0.0)
      std::printf(", Mittel der letzten 4 Perioden %.6e %s",
                  field::average_over_periods(m.values, m.sample_interval, period, 4), m.unit.c_str());
    std::printf("\n");
  }
  std::printf("Feldenergie am Ende: %.6e J/m\n", sim.energy());
  if (!csv_path.empty()) {
    std::ofstream csv(csv_path);
    if (!csv) {
      std::fprintf(stderr, "Fehler: %s kann nicht geschrieben werden\n", csv_path.c_str());
      return 1;
    }
    csv.precision(10);
    csv << "time_s";
    for (const auto& m : sim.monitors()) csv << ',' << m.name << '_' << m.unit;
    csv << '\n';
    for (std::size_t k = 0; k < steps; ++k) {
      csv << static_cast<double>(k + 1) * sim.time_step();
      for (const auto& m : sim.monitors()) csv << ',' << (k < m.values.size() ? m.values[k] : 0.0);
      csv << '\n';
    }
    std::printf("Zeitreihen nach %s geschrieben (Fluss-Monitore liegen einen halben Zeitschritt früher)\n",
                csv_path.c_str());
  }
  return 0;
}

// Abnahme Phase 4: Reflexion der Randschicht unter -40 dB. Punktquelle mit Puls; die Differenz zu einem
// größeren Gebiet, dessen Rand im Messfenster kein Echo liefern kann, ist die Reflexion.
bool validate_fdtd() {
  bool ok = true;
  for (field::Polarization polarization : {field::Polarization::ez, field::Polarization::hz}) {
    const double dx = 40e-9;
    auto scene = [&](int cells, int shift) {
      const double offset = shift + 0.25;
      field::FieldScene s;
      s.width = cells * dx, s.height = cells * dx, s.cell_size = dx;
      s.polarization = polarization;
      field::Source source;
      source.kind = field::SourceKind::point;
      source.x1 = (offset + 40) * dx, source.y1 = (offset + 40) * dx;
      source.signal = field::SignalKind::pulse;
      source.pulse_duration = 6e-15;
      s.sources = {source};
      s.monitors = {{"Probe", field::MonitorKind::point, (offset + 16) * dx, (offset + 60) * dx, 0, 0}};
      return s;
    };
    field::Fdtd small(scene(80, 0)), large(scene(480, 200));
    const std::size_t steps = 500;
    small.run(steps);
    large.run(steps);
    double difference = 0.0, peak = 0.0;
    for (std::size_t k = 0; k < steps; ++k) {
      difference =
          std::max(difference, std::abs(small.monitors()[0].values[k] - large.monitors()[0].values[k]));
      peak = std::max(peak, std::abs(large.monitors()[0].values[k]));
    }
    const double reflection_db = 20.0 * std::log10(difference / peak);
    std::printf("FDTD Randschicht (%s): Reflexion %.1f dB (gefordert < -40 dB)\n",
                polarization == field::Polarization::ez ? "Ez" : "Hz", reflection_db);
    ok = ok && reflection_db < -40.0;
  }
  return ok;
}

// Abnahme Phase 3: Interferenz-XOR mit Kontrast > 20 dB im Idealfall, und das Zeugnis markiert die
// fehlende Pegelregeneration.
bool validate_xor() {
  const io::Project project = io::reference_project("xor_interference");
  const logic::GateReport report = logic::characterize(project.circuit, circuit::ComponentRegistry::builtin(),
                                                       project.gate, project.wavelength);
  const logic::Criterion* regeneration = report.find("regeneration");
  const bool contrast_ok = report.function_correct && report.worst_contrast_db > 20.0;
  const bool regeneration_flagged = regeneration && regeneration->rating == logic::Rating::bad;
  std::printf("XOR Kontrast:         %s (gefordert > 20 dB)\n",
              logic::format_db(report.worst_contrast_db).c_str());
  std::printf("XOR Pegelregeneration: %s (erwartet: nicht erfüllt)\n",
              regeneration ? std::string(logic::to_string(regeneration->rating)).c_str() : "fehlt");
  return contrast_ok && regeneration_flagged;
}

// Referenzfall MZI (Bauplan Abschnitt 16): Ausgänge folgen sin²/cos² der Phasendifferenz, Energie bleibt
// erhalten.
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
    worst_transfer =
        std::max({worst_transfer, std::abs(bar - bar_expected), std::abs(cross - cross_expected)});
    worst_energy = std::max(worst_energy, energy_error);
    if (csv.is_open())
      csv << phase << ',' << bar << ',' << bar_expected << ',' << cross << ',' << cross_expected << ','
          << energy_error << '\n';
  }

  bool ok = worst_transfer < tolerance && worst_energy < tolerance;
  std::printf("MZI gegen cos²(Δφ/2):   größte Abweichung %.3e (Toleranz %.0e)\n", worst_transfer, tolerance);
  std::printf("MZI Energieerhaltung:   größte Abweichung %.3e (Toleranz %.0e)\n", worst_energy, tolerance);
  if (csv.is_open()) std::printf("Bericht: %s/mzi.csv\n", out_dir.c_str());
  ok = validate_ring(out_dir) && ok;
  ok = validate_xor() && ok;
  ok = validate_fdtd() && ok;
  ok = validate_modes(out_dir) && ok;
  ok = validate_pwe() && ok;
  ok = validate_phc_bend() && ok;
  ok = validate_sparameters(out_dir) && ok;
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
    std::printf("PhotonLab CLI 0.8.0 (Phase 7)\n");
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
    if (command == "materials")
      return cmd_materials(args.size() >= 2 ? std::atof(args[1].c_str()) * units::nm : 1550e-9);
    if (command == "run" && args.size() >= 2 && flag("--modes")) return cmd_modes(args[1]);
    if (command == "run" && args.size() >= 2 && flag("--sparam"))
      return cmd_sparam(args[1], option("--out"), option("--name"));
    if (command == "run" && args.size() >= 2 && flag("--field")) return cmd_field(args[1], option("--csv"));
    if (command == "run" && args.size() >= 2 && flag("--logic")) return cmd_logic(args[1], option("--gate"));
    if (command == "run" && args.size() >= 2 && flag("--resonances")) return cmd_resonances(args[1]);
    if (command == "run" && args.size() >= 2 && flag("--monte-carlo"))
      return cmd_monte_carlo(args[1], option("--csv"));
    if (command == "run" && args.size() >= 2) return cmd_run(args[1], flag("--sweep"), option("--csv"));
    if (command == "field-gate")
      return cmd_field_gate(args.size() >= 2 && args[1].rfind("--", 0) != 0 ? args[1] : "", option("--csv"));
    if (command == "bands")
      return cmd_bands(option("--lattice"), option("--radius"), option("--eps"), option("--hole-eps"),
                       option("--pol"), option("--cutoff"), option("--a-nm"), option("--csv"));
    if (command == "validate") return cmd_validate(option("--out"));
    print_usage();
    return 2;
  } catch (const std::exception& e) {
    std::fprintf(stderr, "Fehler: %s\n", e.what());
    return 1;
  }
}
