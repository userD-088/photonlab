#include "photonlab/logic/field_gate.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "photonlab/core/parallel_for.hpp"
#include "photonlab/core/units.hpp"
#include "photonlab/field/fdtd.hpp"
#include "photonlab/field/reference_scenes.hpp"
#include "photonlab/field/spectrum.hpp"

namespace photonlab::logic {

namespace {

using field::FieldError;

bool has_source(const field::FieldScene& scene, const std::string& name) {
  return std::any_of(scene.sources.begin(), scene.sources.end(),
                     [&](const field::Source& s) { return s.name == name; });
}

}  // namespace

void validate(const FieldGate& gate) {
  if (gate.scenes.empty()) throw FieldError("Das Feld-Gatter hat keine Szene");
  if (gate.inputs.empty() || gate.inputs.size() > max_inputs) throw FieldError("Zahl der Eingänge ungültig");
  if (gate.outputs.empty()) throw FieldError("Das Feld-Gatter hat keinen Ausgang");
  for (const auto& scene : gate.scenes) {
    field::validate(scene);
    if (!(scene.duration > 0.0)) throw FieldError("Die Szene braucht eine Laufzeit (duration > 0)");
    if (scene.sources.empty() || scene.sources.front().signal != field::SignalKind::continuous)
      throw FieldError("Feld-Gatter rechnen mit Dauerstrich-Quellen");
  }
  for (const auto& input : gate.inputs)
    for (const auto& scene : gate.scenes)
      if (!has_source(scene, input.source))
        throw FieldError("Eingang '" + input.name + "': Quelle '" + input.source + "' fehlt in einer Szene");
  for (const auto& bias : gate.bias_sources)
    for (const auto& scene : gate.scenes)
      if (!has_source(scene, bias))
        throw FieldError("Vorspannungsquelle '" + bias + "' fehlt in einer Szene");
  for (const auto& out : gate.outputs) {
    if (out.scene >= gate.scenes.size())
      throw FieldError("Ausgang '" + out.name + "': Szene existiert nicht");
    const auto& monitors = gate.scenes[out.scene].monitors;
    const auto it = std::find_if(monitors.begin(), monitors.end(), [&](const field::Monitor& m) {
      return m.name == out.monitor && m.kind == field::MonitorKind::flux;
    });
    if (it == monitors.end())
      throw FieldError("Ausgang '" + out.name + "': Flussmonitor '" + out.monitor + "' fehlt");
    if (!out.expected.empty() && out.expected.size() != gate.row_count())
      throw FieldError("Ausgang '" + out.name + "': Soll-Bits passen nicht zur Zeilenzahl");
  }
}

std::vector<field::FieldScene> scenes_for_row(const FieldGate& gate, std::size_t row) {
  std::vector<field::FieldScene> scenes = gate.scenes;
  for (auto& scene : scenes) {
    for (std::size_t k = 0; k < gate.inputs.size(); ++k) {
      const std::size_t bit = (row >> (gate.inputs.size() - 1 - k)) & 1U;  // erster Eingang = höchstwertig
      for (auto& source : scene.sources)
        if (source.name == gate.inputs[k].source && bit == 0) source.amplitude = 0.0;
    }
  }
  return scenes;
}

TruthTable run_field_truth_table(const FieldGate& gate, ThreadPool* pool, JobControl* control) {
  validate(gate);
  const std::size_t rows = gate.row_count(), scene_count = gate.scenes.size();
  // power[row][scene][monitor index]
  std::vector<std::vector<std::vector<double>>> power(rows);
  std::vector<std::vector<field::FieldScene>> row_scenes(rows);
  for (std::size_t r = 0; r < rows; ++r) {
    row_scenes[r] = scenes_for_row(gate, r);
    power[r].resize(scene_count);
  }
  std::atomic<std::size_t> done{0};
  const std::size_t tasks = rows * scene_count;
  const bool completed = parallel_for(
      tasks,
      [&](std::size_t t) {
        const std::size_t r = t / scene_count, s = t % scene_count;
        const field::FieldScene& scene = row_scenes[r][s];
        auto& result = power[r][s];
        result.assign(scene.monitors.size(), 0.0);
        const bool any_light = std::any_of(scene.sources.begin(), scene.sources.end(),
                                           [](const field::Source& src) { return src.amplitude != 0.0; });
        if (any_light) {
          field::Fdtd solver(scene);
          solver.run(static_cast<std::size_t>(scene.duration / solver.time_step()), control);
          const double period = scene.sources.front().wavelength / constants::c0;
          for (std::size_t m = 0; m < solver.monitors().size(); ++m) {
            const auto& data = solver.monitors()[m];
            if (data.kind == field::MonitorKind::flux)
              result[m] = std::abs(field::average_over_periods(data.values, data.sample_interval, period, 4));
          }
        }
        if (control) control->progress = static_cast<float>(++done) / static_cast<float>(tasks);
      },
      pool, control);
  TruthTable table;
  if (!completed || (control && control->cancel_requested)) return table;

  for (std::size_t r = 0; r < rows; ++r) {
    TruthRow row;
    for (std::size_t k = 0; k < gate.inputs.size(); ++k)
      row.bits.push_back(static_cast<int>((r >> (gate.inputs.size() - 1 - k)) & 1U));
    for (const auto& out : gate.outputs) {
      const auto& monitors = gate.scenes[out.scene].monitors;
      const std::size_t m = static_cast<std::size_t>(
          std::find_if(monitors.begin(), monitors.end(),
                       [&](const field::Monitor& mon) { return mon.name == out.monitor; }) -
          monitors.begin());
      row.power.push_back(power[r][out.scene][m]);
    }
    table.rows.push_back(std::move(row));
  }
  return table;
}

FieldGateReport characterize_field_gate(const FieldGate& gate, ThreadPool* pool, JobControl* control) {
  FieldGateReport report;
  report.gate_name = gate.name;
  report.wavelength = gate.scenes.front().sources.front().wavelength;
  report.table = run_field_truth_table(gate, pool, control);
  if (report.table.rows.empty()) return report;  // abgebrochen
  report.function_correct = true;
  report.worst_contrast_db = std::numeric_limits<double>::infinity();
  for (std::size_t k = 0; k < gate.outputs.size(); ++k) {
    const OutputAnalysis a = analyze_output(report.table, k, gate.outputs[k].expected);
    report.outputs.push_back(a);
    report.function_correct = report.function_correct && a.separable;
    if (a.contrast_db == a.contrast_db)
      report.worst_contrast_db = std::min(report.worst_contrast_db, a.contrast_db);
  }
  // Schwellen: gleiche Festlegung wie das Zeugnis der Schaltungs-Ebene (report.cpp)
  auto rate = [](double value) {
    return value >= 20.0 ? Rating::good : value >= 10.0 ? Rating::limited : Rating::bad;
  };
  Criterion function{"function", "Funktion", report.function_correct ? Rating::good : Rating::bad,
                     report.function_correct ? "alle Ausgänge trennen 0 und 1"
                                             : "mindestens ein Ausgang trennt 0 und 1 nicht",
                     ""};
  Criterion contrast{"contrast", "Kontrastverhältnis",
                     report.function_correct ? rate(report.worst_contrast_db) : Rating::bad,
                     format_db(report.worst_contrast_db),
                     "schlechtester Ausgang; gut ab 20 dB, eingeschränkt ab 10 dB"};
  Criterion radiation{"radiation", "Abstrahlung aus der Ebene", Rating::not_rated, "nicht modelliert",
                      "2D-Rechnung: echte Platten verlieren zusätzlich Licht nach oben und unten"};
  Criterion tolerance{
      "tolerance", "Fertigungstoleranz", Rating::not_rated, "nicht gemessen",
      "Photonische Kristalle reagieren empfindlich auf Lochradius und -lage (Monte-Carlo fehlt noch)"};
  report.criteria = {function, contrast, radiation, tolerance};
  return report;
}

std::vector<std::string> field_gate_names() { return {"phc_or_and", "phc_xor", "phc_not", "phc_halfadder"}; }

FieldGate field_gate(std::string_view name) {
  const std::string monitor = "Fluss Ausgang";
  FieldGate g;
  g.name = std::string(name);
  if (name == "phc_or_and") {
    g.description =
        "Y-Kombinierer im Dreiecksgitter, Eingänge gleichphasig: Ausgangsleistung |a+b|^2/2. Eine Quelle "
        "liefert "
        "den Pegel 1/2, beide den Pegel 2: ODER mit niedriger, UND mit hoher Schwelle. Das UND ist nur ein "
        "Schwellwert auf Interferenz; sein Kontrast ist durch das Pegelverhältnis 4:1 (6 dB) begrenzt. Ein "
        "UND mit gutem Kontrast braucht Nichtlinearität.";
    g.scenes = {field::reference_scene("field_phc_y")};
    g.inputs = {{"A", "A"}, {"B", "B"}};
    g.outputs = {{"ODER", 0, monitor, {0, 1, 1, 1}}, {"UND", 0, monitor, {0, 0, 0, 1}}};
  } else if (name == "phc_xor") {
    g.description =
        "Y-Kombinierer, Eingang B um pi phasenverschoben: bei A = B = 1 löschen sich die Felder im "
        "Ausgangsarm "
        "aus (XOR).";
    g.scenes = {field::reference_scene("field_phc_y_xor")};
    g.inputs = {{"A", "A"}, {"B", "B"}};
    g.outputs = {{"XOR", 0, monitor, {0, 1, 1, 0}}};
  } else if (name == "phc_not") {
    g.description =
        "Wie XOR, aber B ist eine dauernd eingeschaltete Vorspannung: A = 0 lässt die Vorspannung durch, A = "
        "1 "
        "löscht sie aus (NICHT).";
    g.scenes = {field::reference_scene("field_phc_y_xor")};
    g.inputs = {{"A", "A"}};
    g.bias_sources = {"B"};
    g.outputs = {{"NICHT A", 0, monitor, {1, 0}}};
  } else if (name == "phc_halfadder") {
    g.description =
        "Halbaddierer aus zwei parallelen Y-Gattern mit gemeinsamen Eingangsbits: Summe = XOR (gegenphasig), "
        "Übertrag = UND (gleichphasig). Die Aufteilung jedes Eingangs auf beide Gatter ist nicht "
        "mitgerechnet; "
        "jedes Gatter wird mit voller Eingangsleistung betrieben.";
    g.scenes = {field::reference_scene("field_phc_y_xor"), field::reference_scene("field_phc_y")};
    g.inputs = {{"A", "A"}, {"B", "B"}};
    g.outputs = {{"Summe", 0, monitor, {0, 1, 1, 0}}, {"Übertrag", 1, monitor, {0, 0, 0, 1}}};
  } else {
    throw FieldError("Unbekanntes Feld-Gatter '" + std::string(name) + "'");
  }
  return g;
}

}  // namespace photonlab::logic
