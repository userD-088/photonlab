#include "photonlab/logic/field_gate.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>

#include "photonlab/core/parallel_for.hpp"
#include "photonlab/core/step_team.hpp"
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
  auto in_any_scene = [&](const std::string& source) {
    return std::any_of(gate.scenes.begin(), gate.scenes.end(),
                       [&](const field::FieldScene& scene) { return has_source(scene, source); });
  };
  for (const auto& input : gate.inputs)
    if (!in_any_scene(input.source))
      throw FieldError("Eingang '" + input.name + "': Quelle '" + input.source + "' fehlt");
  for (const auto& bias : gate.bias_sources)
    if (!in_any_scene(bias)) throw FieldError("Vorspannungsquelle '" + bias + "' fehlt");
  for (const auto& link : gate.links) {
    const auto out = std::find_if(gate.outputs.begin(), gate.outputs.end(),
                                  [&](const FieldGateOutput& o) { return o.name == link.output; });
    if (out == gate.outputs.end())
      throw FieldError("Verbindung: Ausgang '" + link.output + "' gibt es nicht");
    if (!in_any_scene(link.source)) throw FieldError("Verbindung: Quelle '" + link.source + "' fehlt");
    for (std::size_t s = 0; s <= out->scene && s < gate.scenes.size(); ++s)
      if (has_source(gate.scenes[s], link.source))
        throw FieldError("Verbindung '" + link.output + "': die Quelle liegt nicht in einer späteren Szene");
  }
  for (const auto& out : gate.outputs) {
    if (out.scene >= gate.scenes.size())
      throw FieldError("Ausgang '" + out.name + "': Szene existiert nicht");
    const auto& monitors = gate.scenes[out.scene].monitors;
    const auto it = std::find_if(monitors.begin(), monitors.end(), [&](const field::Monitor& m) {
      return m.name == out.monitor && m.kind != field::MonitorKind::point;
    });
    if (it == monitors.end())
      throw FieldError("Ausgang '" + out.name + "': Fluss- oder Modenmonitor '" + out.monitor + "' fehlt");
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
  const std::size_t rows = gate.row_count(), outputs = gate.outputs.size();
  std::vector<std::vector<double>> out_power(rows, std::vector<double>(outputs, 0.0));
  std::vector<std::vector<int>> out_bit(rows, std::vector<int>(outputs, 0));
  auto input_bit = [&](std::size_t row, std::size_t k) {
    return static_cast<int>((row >> (gate.inputs.size() - 1 - k)) & 1U);  // erster Eingang = höchstwertig
  };
  auto output_index = [&](const std::string& name) {
    return static_cast<std::size_t>(std::find_if(gate.outputs.begin(), gate.outputs.end(),
                                                 [&](const FieldGateOutput& o) { return o.name == name; }) -
                                    gate.outputs.begin());
  };

  for (std::size_t s = 0; s < gate.scenes.size(); ++s) {
    // Quellenbelegung je Zeile; gleiche Belegungen werden nur einmal gerechnet
    std::vector<field::FieldScene> configs;
    std::vector<std::size_t> config_of_row(rows);
    for (std::size_t r = 0; r < rows; ++r) {
      field::FieldScene scene = gate.scenes[s];
      for (auto& source : scene.sources) {
        for (std::size_t k = 0; k < gate.inputs.size(); ++k)
          if (source.name == gate.inputs[k].source && input_bit(r, k) == 0) source.amplitude = 0.0;
        for (const auto& link : gate.links)
          if (source.name == link.source && out_bit[r][output_index(link.output)] == 0)
            source.amplitude = 0.0;
      }
      const auto found = std::find(configs.begin(), configs.end(), scene);
      config_of_row[r] = static_cast<std::size_t>(found - configs.begin());
      if (found == configs.end()) configs.push_back(std::move(scene));
    }
    std::vector<std::vector<double>> power(configs.size());  // [Belegung][Monitor]
    std::atomic<std::size_t> done{0};
    const bool completed = parallel_for(
        configs.size(),
        [&](std::size_t c) {
          const field::FieldScene& scene = configs[c];
          auto& result = power[c];
          result.assign(scene.monitors.size(), 0.0);
          const bool any_light = std::any_of(scene.sources.begin(), scene.sources.end(),
                                             [](const field::Source& src) { return src.amplitude != 0.0; });
          if (any_light) {
            field::Fdtd solver(scene);
            solver.set_threads(StepTeam::share_for(configs.size(), pool ? pool->size() : 0));
            solver.run(static_cast<std::size_t>(scene.duration / solver.time_step()), control);
            const double period = scene.sources.front().wavelength / constants::c0;
            for (std::size_t m = 0; m < solver.monitors().size(); ++m) {
              const auto& data = solver.monitors()[m];
              if (data.kind == field::MonitorKind::flux)
                result[m] =
                    std::abs(field::average_over_periods(data.values, data.sample_interval, period, 4));
              if (data.kind == field::MonitorKind::mode) {
                // Leistung der geführten Mode ist proportional zum zeitlichen Mittel von a^2 (Streulicht
                // im Mantel zählt nicht, wie bei einem Detektor hinter einem einmodigen Wellenleiter)
                std::vector<double> squared(data.values.size());
                std::transform(data.values.begin(), data.values.end(), squared.begin(),
                               [](double a) { return a * a; });
                result[m] = field::average_over_periods(squared, data.sample_interval, period, 4);
              }
            }
          }
          if (control)
            control->progress =
                (static_cast<float>(s) + static_cast<float>(++done) / static_cast<float>(configs.size())) /
                static_cast<float>(gate.scenes.size());
        },
        pool, control);
    if (!completed || (control && control->cancel_requested)) return {};

    // Ausgänge dieser Szene: Leistung je Zeile, dann Bit an der Schwelle (für spätere Stufen)
    for (std::size_t o = 0; o < outputs; ++o) {
      const auto& out = gate.outputs[o];
      if (out.scene != s) continue;
      const auto& monitors = gate.scenes[s].monitors;
      const auto m = static_cast<std::size_t>(
          std::find_if(monitors.begin(), monitors.end(),
                       [&](const field::Monitor& mon) { return mon.name == out.monitor; }) -
          monitors.begin());
      TruthTable column;
      for (std::size_t r = 0; r < rows; ++r) {
        out_power[r][o] = power[config_of_row[r]][m];
        TruthRow row;
        row.power = {out_power[r][o]};
        column.rows.push_back(std::move(row));
      }
      const OutputAnalysis a = analyze_output(column, 0, out.expected);
      for (std::size_t r = 0; r < rows; ++r) out_bit[r][o] = out_power[r][o] > a.threshold ? 1 : 0;
    }
  }

  TruthTable table;
  for (std::size_t r = 0; r < rows; ++r) {
    TruthRow row;
    for (std::size_t k = 0; k < gate.inputs.size(); ++k) row.bits.push_back(input_bit(r, k));
    row.power = out_power[r];
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

std::vector<std::string> field_gate_names() {
  return {"phc_or_and",         "phc_xor",           "phc_not",
          "phc_halfadder",      "chip_halfadder",    "chip_fulladder",
          "phc_chip_halfadder", "phc_chip_fulladder"};
}

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
  } else if (name == "chip_halfadder") {
    g.description =
        "Halbaddierer als ein Chip aus Streifenwellenleitern: jeder Eingang teilt sich in einem Y-Teiler auf "
        "zwei Arme. Oben XOR (Summe; ein verbreitertes Stück verschiebt die Phase von B um pi), unten "
        "gleichphasiges UND (Übertrag; Schwellwert auf Pegel 1/4 gegen 1, höchstens 6 dB Kontrast). Die "
        "Aufteilung der Eingänge und das Übersprechen zwischen den Gattern sind mitgerechnet.";
    g.scenes = {field::reference_scene("field_halfadder")};
    g.inputs = {{"A", "A"}, {"B", "B"}};
    g.outputs = {{"Summe", 0, "Mode Summe", {0, 1, 1, 0}}, {"Übertrag", 0, "Mode Übertrag", {0, 0, 0, 1}}};
  } else if (name == "phc_chip_halfadder") {
    g.description =
        "Halbaddierer als ein Kristall-Chip: W1-Wellenleiter mit Y-Teilern, 60°-Knicken und zwei "
        "Y-Kombinierern. Oben XOR (Summe; veränderte Löcher neben einem Arm von B verschieben dessen Phase), "
        "unten gleichphasiges UND (Übertrag, höchstens 6 dB).";
    g.scenes = {field::reference_scene("field_phc_halfadder")};
    g.inputs = {{"A", "A"}, {"B", "B"}};
    g.outputs = {{"Summe", 0, "Fluss Summe", {0, 1, 1, 0}}, {"Übertrag", 0, "Fluss Übertrag", {0, 0, 0, 1}}};
  } else if (name == "chip_fulladder" || name == "phc_chip_fulladder") {
    g.description =
        "Volladdierer aus drei Halbaddierer-Chips mit Regeneration (Detektor + neuer Sender) zwischen den "
        "Stufen: Stufe 1 rechnet A + B (S1, C1), Stufe 2 S1 + Cin (Summe, C2), Stufe 3 liest am "
        "gleichphasigen Ausgang C1 ODER C2 (Übertrag). Ohne Regeneration geht es nicht: die Summe dreier "
        "Bits "
        "lässt sich mit linearer Interferenz an keinem Ausgang von den geraden Fällen trennen.";
    const bool crystal = name == "phc_chip_fulladder";
    if (crystal)
      g.description.replace(g.description.find("Halbaddierer-Chips"), 18, "Kristall-Halbaddierern");
    const field::FieldScene chip =
        field::reference_scene(crystal ? "field_phc_halfadder" : "field_halfadder");
    const char* sum_monitor = crystal ? "Fluss Summe" : "Mode Summe";
    const char* carry_monitor = crystal ? "Fluss Übertrag" : "Mode Übertrag";
    auto renamed = [&](const char* a, const char* b) {
      field::FieldScene scene = chip;
      scene.sources[0].name = a, scene.sources[1].name = b;
      return scene;
    };
    g.scenes = {chip, renamed("S1", "Cin"), renamed("C1", "C2")};
    g.inputs = {{"A", "A"}, {"B", "B"}, {"Cin", "Cin"}};
    // Soll-Bits in Zeilenreihenfolge A B Cin = 000 ... 111
    std::vector<int> s1, c1, sum, c2, cout;
    for (int r = 0; r < 8; ++r) {
      const int a = (r >> 2) & 1, b = (r >> 1) & 1, c = r & 1;
      s1.push_back(a ^ b), c1.push_back(a & b), sum.push_back(a ^ b ^ c), c2.push_back((a ^ b) & c);
      cout.push_back((a & b) | ((a ^ b) & c));
    }
    g.outputs = {{"S1", 0, sum_monitor, s1},
                 {"C1", 0, carry_monitor, c1},
                 {"Summe", 1, sum_monitor, sum},
                 {"C2", 1, carry_monitor, c2},
                 {"Übertrag", 2, carry_monitor, cout}};
    g.links = {{"S1", "S1"}, {"C1", "C1"}, {"C2", "C2"}};
  } else {
    throw FieldError("Unbekanntes Feld-Gatter '" + std::string(name) + "'");
  }
  return g;
}

}  // namespace photonlab::logic
