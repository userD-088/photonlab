#include "photonlab/logic/logic_sim.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <map>
#include <set>

#include "photonlab/circuit/reference_designs.hpp"
#include "photonlab/circuit/solver.hpp"
#include "photonlab/circuit/sweep.hpp"
#include "photonlab/core/units.hpp"
#include "photonlab/logic/reference_gates.hpp"
#include "photonlab/logic/truth_table.hpp"

namespace photonlab::logic {

namespace {

using circuit::CircuitError;

std::string fmt_power(double w) {
  char b[48];
  if (std::abs(w) >= 1e-3)
    std::snprintf(b, sizeof b, "%.3g mW", w / 1e-3);
  else
    std::snprintf(b, sizeof b, "%.3g uW", w / 1e-6);
  return b;
}

// Liest ein Bit an einem Eingang: Intensität/elektrisch über die Schwelle, Phase über das Vorzeichen von
// cos(phi).
int decode(const BlockPort& port, const Signal& s) {
  if (port.encoding == Encoding::phase) return std::cos(s.phase) < 0.0 ? 1 : 0;
  return s.power > port.threshold ? 1 : 0;
}

int row_of(const std::vector<int>& bits) {
  int row = 0;
  for (const int b : bits) row = 2 * row + (b ? 1 : 0);
  return row;
}

// --- Tabellen-Block (ideal oder regeneriert) ------------------------------------------------------------
class TableBlock final : public LogicBlock {
 public:
  TableBlock(std::string type_name, Family fam, int n, std::vector<std::vector<int>> truth, double threshold)
      : truth_(std::move(truth)) {
    type = std::move(type_name);
    family = std::string(label(fam));
    for (int k = 0; k < n; ++k) {
      BlockPort p;
      p.name = n == 1 ? "in" : std::string(1, static_cast<char>('A' + k));
      p.threshold = threshold;
      inputs.push_back(p);
    }
    for (std::size_t k = 0; k < truth_.size(); ++k) {
      BlockPort p;
      p.name = truth_.size() == 1 ? "out" : "out" + std::to_string(k);
      p.worst_one = 1e-3, p.worst_zero = 0.0;
      outputs.push_back(p);
    }
  }
  std::vector<Signal> evaluate(const std::vector<Signal>& in, std::vector<double>&) const override {
    std::vector<int> bits;
    for (std::size_t k = 0; k < in.size(); ++k) bits.push_back(decode(inputs[k], in[k]));
    std::vector<Signal> out(truth_.size());
    for (std::size_t o = 0; o < truth_.size(); ++o)
      out[o].power = truth_[o][static_cast<std::size_t>(row_of(bits))] ? outputs[o].one_level : 0.0;
    return out;
  }
  std::vector<int> ideal(const std::vector<int>& bits, int&) const override {
    std::vector<int> out;
    for (const auto& t : truth_) out.push_back(t[static_cast<std::size_t>(row_of(bits))]);
    return out;
  }

 private:
  std::vector<std::vector<int>> truth_;
};

// --- Flip-Flop ideal/regeneriert: S setzt, R setzt zurück, sonst halten; Ausgang Qn = NICHT Q
// ----------------
class TableLatch final : public LogicBlock {
 public:
  TableLatch(Family fam, double threshold) {
    type = "SR";
    family = std::string(label(fam));
    inputs = {{"S", Encoding::intensity, threshold}, {"R", Encoding::intensity, threshold}};
    outputs = {{"Qn"}};
  }
  int state_size() const override { return 1; }
  std::vector<Signal> evaluate(const std::vector<Signal>& in, std::vector<double>& state) const override {
    const int s = decode(inputs[0], in[0]), r = decode(inputs[1], in[1]);
    if (r)
      state[0] = 0.0;
    else if (s)
      state[0] = 1.0;
    Signal q;
    q.power = state[0] > 0.5 ? 0.0 : outputs[0].one_level;
    return {q};
  }
  std::vector<int> ideal(const std::vector<int>& bits, int& st) const override {
    if (bits[1])
      st = 0;
    else if (bits[0])
      st = 1;
    return {st ? 0 : 1};
  }
};

// --- Passiver Block aus einer Schaltung des Chip-Designers (Brücke 2) -----------------------------------
// Die Eingänge sind Laser der Schaltung (Leistung und Phase werden vom Netz übernommen), der Ausgang ist das
// Licht am Detektor. Schwellen und schlechteste Pegel aus der Charakterisierung bei Nenneingängen.
class CircuitBlock final : public LogicBlock {
 public:
  CircuitBlock(std::string type_name, circuit::Circuit c, GateDefinition gate, std::size_t output,
               std::vector<int> expected)
      : circuit_(std::move(c)), gate_(std::move(gate)), output_(output), expected_(std::move(expected)) {
    type = std::move(type_name);
    family = std::string(label(Family::passive));
    regenerating = false;
    delay = 2e-12;  // optischer Weg durch einen kleinen Koppler-Aufbau (Größenordnung, Annahme)
    const auto& registry = circuit::ComponentRegistry::builtin();
    for (const auto& in : gate_.inputs) {
      BlockPort p;
      p.name = in.name;
      p.encoding = in.encoding;
      if (in.encoding == Encoding::intensity) {
        p.one_level = in.value1, p.zero_level = in.value0, p.threshold = 0.5 * (in.value0 + in.value1);
      } else {  // Phase: Leistung des Lasers bleibt, das Bit steckt in 0/pi
        const circuit::Instance* laser = circuit_.find_instance(in.instance);
        p.one_level = p.zero_level = laser ? laser->params.at("power") : 1e-3;
        p.threshold = 0.0;
      }
      inputs.push_back(p);
    }
    // Charakterisierung bei Nenneingängen
    const TruthTable table = run_truth_table(circuit_, registry, gate_, 1550e-9);
    const OutputAnalysis a = analyze_output(table, output_, expected_);
    BlockPort out;
    out.name = gate_.outputs[output_].name;
    out.threshold = a.threshold;
    out.worst_one = a.p1_min, out.worst_zero = a.p0_max;
    out.one_level = a.p_max;
    outputs = {out};
    characterized_correct_ = a.separable;
    note = "Ausgang über den Schaltungslöser; Pegel hängen von den Eingangspegeln ab (keine Regeneration).";
  }
  std::vector<Signal> evaluate(const std::vector<Signal>& in, std::vector<double>&) const override {
    circuit::Circuit c = circuit_;
    for (std::size_t k = 0; k < gate_.inputs.size(); ++k) {
      circuit::Instance* laser = c.find_instance(gate_.inputs[k].instance);
      laser->params["power"] = std::max(in[k].power, 0.0);
      laser->params["phase"] = in[k].phase;
    }
    const circuit::CompiledCircuit compiled(c, circuit::ComponentRegistry::builtin());
    const circuit::Solution sol = compiled.solve(1550e-9);
    const std::complex<double> field = sol.a(compiled.port_index(gate_.outputs[output_].instance, "in"));
    Signal s;
    s.power = std::norm(field);
    s.phase = std::arg(field);
    return {s};
  }
  std::vector<int> ideal(const std::vector<int>& bits, int&) const override {
    return {expected_[static_cast<std::size_t>(row_of(bits))]};
  }

 private:
  circuit::Circuit circuit_;
  GateDefinition gate_;
  std::size_t output_;
  std::vector<int> expected_;
  bool characterized_correct_ = true;
};

// --- SR-Flip-Flop aus einem Kerr-bistabilen Ring (Phase 8) ---------------------------------------------
// Haltelaser zwischen den Klappleistungen; S addiert Leistung (inkohärent angenommen) und klappt den Ring
// nach oben, R (elektrisch) schaltet den Haltelaser kurz ab und setzt zurück. Ausgang: Durchgang am Bus (Qn:
// hoch, solange der Ring unten ist). Ohne TPA und Erwärmung (Kerr-Material), Zeitkonstanten nicht modelliert
// (delay fest).
class KerrLatch final : public LogicBlock {
 public:
  KerrLatch() {
    type = "SR";
    family = std::string(label(Family::passive));
    regenerating = true;  // Ausgangspegel durch den Haltelaser bestimmt
    delay = 50e-12;       // Photonenlebensdauer und Einschwingen (Größenordnung, Annahme)
    const double radius = 10e-6, coupling = 0.02, loss_db_per_cm = 2.0, n_eff = 2.44;
    const double length = 2.0 * constants::pi * radius;
    const double t = std::sqrt(1.0 - coupling), a = std::pow(10.0, -loss_db_per_cm * length / 1e-2 / 20.0);
    const double half = (1.0 - t * a) / std::sqrt(t * a);
    const double phi = std::fmod(2.0 * constants::pi * n_eff * length / 1550e-9, 2.0 * constants::pi);
    circuit_.add("laser", "laser", {{"power", 0.0}});
    circuit_.add("ring", "ring_kerr",
                 {{"radius", radius},
                  {"power_coupling", coupling},
                  {"loss_db_per_cm", loss_db_per_cm},
                  {"tuning_phase", -phi - 3.0 * half},
                  {"beta_tpa", 0.0},
                  {"thermal_resistance", 0.0}});
    circuit_.add("det", "detector");
    circuit_.connect("laser.out", "ring.in");
    circuit_.connect("ring.out", "det.in");
    // Klappleistungen aus einer Rampe hinauf und hinunter
    const circuit::CompiledCircuit compiled(with_power(1.0), circuit::ComponentRegistry::builtin());
    std::vector<double> up, down;
    const int steps = 400;
    const double p_max = 0.1;
    for (int i = 0; i <= steps; ++i) up.push_back(std::sqrt(p_max * i / steps));
    down.assign(up.rbegin(), up.rend());
    circuit::CircuitState state = compiled.zero_state();
    const auto r_up = circuit::power_ramp(compiled, 1550e-9, up, &state);
    const auto r_down = circuit::power_ramp(compiled, 1550e-9, down, &state);
    auto jump = [&](const circuit::RampResult& r, const std::vector<double>& s) {
      double best = 0.0, at = 0.0;
      for (std::size_t i = 1; i < r.states.size(); ++i) {
        const double d = std::abs(r.states[i][1][0] - r.states[i - 1][1][0]);
        if (d > best) best = d, at = s[i] * s[i];
      }
      return at;
    };
    p_up_ = jump(r_up, up), p_down_ = jump(r_down, down);
    hold_ = 0.5 * (p_up_ + p_down_);
    const double set_one = 1.5 * (p_up_ - hold_);
    inputs = {{"S", Encoding::intensity, 0.5 * set_one, set_one, 0.0},
              {"R", Encoding::electrical, 0.5e-3, 1e-3, 0.0}};
    // Ausgangspegel im unteren und oberen Zustand beim Halten
    std::vector<double> st(2, 0.0);
    const double q_low_state = through(hold_, st);  // Ring unten -> Durchgang hoch
    std::vector<double> st2(2, 0.0);
    through(p_up_ * 1.3, st2);
    const double q_high_state = through(hold_, st2);  // Ring oben -> Durchgang tief
    BlockPort q;
    q.name = "Qn";
    q.one_level = q_low_state;
    q.worst_one = q_low_state, q.worst_zero = q_high_state;
    q.threshold = 0.5 * (q_low_state + q_high_state);
    outputs = {q};
    static_power = hold_;
    char text[200];
    std::snprintf(text, sizeof text,
                  "Kerr-bistabiler Ring: Klappen bei %.1f mW hinauf, %.1f mW hinunter; Halten mit %.1f mW, "
                  "Setzpuls %.1f mW.",
                  p_up_ / 1e-3, p_down_ / 1e-3, hold_ / 1e-3, set_one / 1e-3);
    note = text;
  }
  int state_size() const override { return 2; }
  std::vector<Signal> evaluate(const std::vector<Signal>& in, std::vector<double>& state) const override {
    const bool reset = decode(inputs[1], in[1]) == 1;
    const double total = (reset ? 0.0 : hold_) + std::max(in[0].power, 0.0);
    Signal q;
    q.power = through(total, state);
    // Nach einem Setz- oder Rücksetzpuls kehrt der Eingang zum Halten zurück: der Zustand bleibt (Hysterese)
    return {q};
  }
  std::vector<int> ideal(const std::vector<int>& bits, int& st) const override {
    if (bits[1])
      st = 0;
    else if (bits[0])
      st = 1;
    return {st ? 0 : 1};
  }

 private:
  circuit::Circuit with_power(double p) const {
    circuit::Circuit c = circuit_;
    c.find_instance("laser")->params["power"] = p;
    return c;
  }
  // Durchgangsleistung bei Eingangsleistung p; state (2 Werte) ist Start und Ergebnis der Ast-Verfolgung
  double through(double p, std::vector<double>& state) const {
    const circuit::CompiledCircuit compiled(with_power(p), circuit::ComponentRegistry::builtin());
    circuit::CircuitState s = compiled.zero_state();
    s[1] = state;
    const circuit::Solution sol = compiled.solve(1550e-9, s);
    state = s[1];
    return std::norm(sol.a(compiled.port_index("det", "in")));
  }
  circuit::Circuit circuit_;
  double p_up_ = 0.0, p_down_ = 0.0, hold_ = 0.0;
};

std::vector<std::vector<int>> one_table(std::string_view f, int n) {
  return {function_bits(f, static_cast<std::size_t>(n))};
}

}  // namespace

std::string_view to_string(Family family) {
  switch (family) {
    case Family::ideal:
      return "ideal";
    case Family::regenerated:
      return "regeneriert";
    case Family::passive:
      return "passiv";
  }
  return "ideal";
}

std::string_view label(Family family) {
  switch (family) {
    case Family::ideal:
      return "ideal (Referenz)";
    case Family::regenerated:
      return "regeneriert (O/E/O)";
    case Family::passive:
      return "passiv optisch";
  }
  return "";
}

Family family_from_string(std::string_view text) {
  if (text == "ideal") return Family::ideal;
  if (text == "regeneriert" || text == "regenerated") return Family::regenerated;
  if (text == "passiv" || text == "passive") return Family::passive;
  throw CircuitError("Unbekannte Gatter-Familie '" + std::string(text) + "' (ideal, regeneriert, passiv)");
}

BlockPtr make_block(Family family, std::string_view type) {
  if (family == Family::passive) {
    if (type == "AND" || type == "OR") {
      GateDefinition g = reference_gate("and_interference");
      return std::make_shared<CircuitBlock>(std::string(type), circuit::reference_design("and_interference"),
                                            g, 0, function_bits(type, 2));
    }
    if (type == "XOR") {
      GateDefinition g = reference_gate("xor_interference");
      return std::make_shared<CircuitBlock>("XOR", circuit::reference_design("xor_interference"), g, 0,
                                            function_bits("XOR", 2));
    }
    if (type == "SR") return std::make_shared<KerrLatch>();
    throw CircuitError("Passiv optisch gibt es kein Gatter '" + std::string(type) +
                       "' (verfügbar: AND, OR, XOR, SR)");
  }
  std::shared_ptr<LogicBlock> block;
  // Entscheidungsschwelle 0,1 mW (regeneriert: Empfängerempfindlichkeit, Fan-out bis etwa 5 bei 1 mW Sender;
  // ideal: gleiche Schwelle, damit Referenz und regenerierte Familie dieselbe Logik sehen)
  const double threshold = 0.1e-3;
  if (type == "SR")
    block = std::make_shared<TableLatch>(family, threshold);
  else if (type == "NOT")
    block = std::make_shared<TableBlock>("NOT", family, 1, one_table("NOT", 1), threshold);
  else if (type == "AND" || type == "OR" || type == "XOR" || type == "NAND" || type == "NOR" ||
           type == "XNOR")
    block = std::make_shared<TableBlock>(std::string(type), family, 2, one_table(type, 2), threshold);
  else
    throw CircuitError("Unbekannter Blocktyp '" + std::string(type) + "'");
  if (family == Family::regenerated) {
    // O/E/O: Photodiode + Entscheider + Treiber + Modulator mit eigenem Laser. Größenordnungen (Annahme): 30
    // ps Laufzeit, 1 pJ je Pegelwechsel, 1 mW Laser plus 1 mW Elektronik dauernd.
    block->delay = 30e-12;
    block->switching_energy = 1e-12;
    block->static_power = 2e-3;
    block->note =
        "Liest optisch, entscheidet elektrisch, sendet mit eigenem Laser neu: Pegel werden "
        "wiederhergestellt.";
  } else {
    block->note = "Ideale Logik ohne Kosten (Referenz).";
  }
  return block;
}

// --- Designs ---------------------------------------------------------------------------------------------

LogicCircuit half_adder() {
  LogicCircuit c;
  c.name = "Halbaddierer";
  c.inputs = {"A", "B"};
  c.outputs = {"S", "C"};
  c.instances = {{"xor", "XOR"}, {"and", "AND"}};
  c.wires = {{"in:A", "xor.A"}, {"in:B", "xor.B"},    {"in:A", "and.A"},
             {"in:B", "and.B"}, {"xor.out", "out:S"}, {"and.out", "out:C"}};
  return c;
}

namespace {
// Volladdierer mit Präfix: zwei Halbaddierer und ein ODER. a, b, cin: Quellen; liefert (Summe, Übertrag) als
// Quellen.
std::pair<std::string, std::string> add_full_adder(LogicCircuit& c, const std::string& p,
                                                   const std::string& a, const std::string& b,
                                                   const std::string& cin) {
  c.instances.push_back({p + "x1", "XOR"});
  c.instances.push_back({p + "a1", "AND"});
  c.instances.push_back({p + "x2", "XOR"});
  c.instances.push_back({p + "a2", "AND"});
  c.instances.push_back({p + "o", "OR"});
  c.wires.push_back({a, p + "x1.A"});
  c.wires.push_back({b, p + "x1.B"});
  c.wires.push_back({a, p + "a1.A"});
  c.wires.push_back({b, p + "a1.B"});
  c.wires.push_back({p + "x1.out", p + "x2.A"});
  c.wires.push_back({cin, p + "x2.B"});
  c.wires.push_back({p + "x1.out", p + "a2.A"});
  c.wires.push_back({cin, p + "a2.B"});
  c.wires.push_back({p + "a1.out", p + "o.A"});
  c.wires.push_back({p + "a2.out", p + "o.B"});
  return {p + "x2.out", p + "o.out"};
}
}  // namespace

LogicCircuit full_adder() {
  LogicCircuit c;
  c.name = "Volladdierer";
  c.inputs = {"A", "B", "Cin"};
  c.outputs = {"S", "Cout"};
  const auto [s, co] = add_full_adder(c, "fa_", "in:A", "in:B", "in:Cin");
  c.wires.push_back({s, "out:S"});
  c.wires.push_back({co, "out:Cout"});
  return c;
}

LogicCircuit ripple_adder(int bits) {
  if (bits < 1 || bits > 8) throw CircuitError("Addierer mit 1 bis 8 Bit");
  LogicCircuit c;
  c.name = std::to_string(bits) + "-Bit-Addierer (Übertragskette)";
  for (int i = bits - 1; i >= 0; --i) c.inputs.push_back("A" + std::to_string(i));
  for (int i = bits - 1; i >= 0; --i) c.inputs.push_back("B" + std::to_string(i));
  c.inputs.push_back("Cin");
  for (int i = 0; i < bits; ++i) c.outputs.push_back("S" + std::to_string(i));
  c.outputs.push_back("Cout");
  std::string carry = "in:Cin";
  for (int i = 0; i < bits; ++i) {
    const auto [s, co] = add_full_adder(c, "fa" + std::to_string(i) + "_", "in:A" + std::to_string(i),
                                        "in:B" + std::to_string(i), carry);
    c.wires.push_back({s, "out:S" + std::to_string(i)});
    carry = co;
  }
  c.wires.push_back({carry, "out:Cout"});
  return c;
}

LogicCircuit and_chain(int stages) {
  if (stages < 1 || stages > 16) throw CircuitError("UND-Kette mit 1 bis 16 Stufen");
  LogicCircuit c;
  c.name = "UND-Kette (" + std::to_string(stages) + " Stufen)";
  for (int i = 0; i <= stages; ++i) c.inputs.push_back(std::string(1, static_cast<char>('A' + i)));
  c.outputs = {"Y"};
  std::string prev = "in:A";
  for (int i = 1; i <= stages; ++i) {
    const std::string g = "und" + std::to_string(i);
    c.instances.push_back({g, "AND"});
    c.wires.push_back({prev, g + ".A"});
    c.wires.push_back({"in:" + c.inputs[static_cast<std::size_t>(i)], g + ".B"});
    prev = g + ".out";
  }
  c.wires.push_back({prev, "out:Y"});
  return c;
}

LogicCircuit sr_latch() {
  LogicCircuit c;
  c.name = "SR-Flip-Flop";
  c.inputs = {"S", "R"};
  c.outputs = {"Qn"};
  c.instances = {{"ff", "SR"}};
  c.wires = {{"in:S", "ff.S"}, {"in:R", "ff.R"}, {"ff.Qn", "out:Qn"}};
  return c;
}

std::vector<std::string> design_names() {
  return {"half_adder", "full_adder", "adder4", "and_chain", "sr_latch"};
}

LogicCircuit design(std::string_view name) {
  if (name == "half_adder") return half_adder();
  if (name == "full_adder") return full_adder();
  if (name == "adder4") return ripple_adder(4);
  if (name == "and_chain") return and_chain(4);
  if (name == "sr_latch") return sr_latch();
  throw CircuitError("Unbekanntes Logik-Design '" + std::string(name) + "'");
}

std::vector<std::vector<int>> all_vectors(std::size_t inputs) {
  std::vector<std::vector<int>> v;
  for (std::size_t row = 0; row < (std::size_t{1} << inputs); ++row) {
    std::vector<int> bits;
    for (std::size_t k = 0; k < inputs; ++k) bits.push_back(static_cast<int>((row >> (inputs - 1 - k)) & 1U));
    v.push_back(bits);
  }
  return v;
}

// --- Aufbereitete Netzliste --------------------------------------------------------------------------------

namespace {

struct Endpoint {
  int instance = -1;  // -1: primärer Ein- bzw. Ausgang
  int port = 0;       // Port-Index bzw. Index des primären Ein-/Ausgangs
};

struct Built {
  std::vector<BlockPtr> blocks;
  std::vector<std::string> names;
  // Netze: je Treiber (primärer Eingang oder Blockausgang) eines
  std::vector<Endpoint> driver;
  std::vector<std::string> net_name;
  std::vector<std::vector<Endpoint>> consumers;  // instance >= 0: Blockeingang; -1: primärer Ausgang
  std::vector<std::vector<int>> input_net;       // [Instanz][Eingang] -> Netz (-1 = offen)
  std::vector<std::vector<int>> output_net;      // [Instanz][Ausgang] -> Netz
  std::vector<int> primary_in_net, primary_out_net;
  std::vector<Encoding> primary_encoding;
  std::vector<double> primary_one;
  std::vector<int> level;  // Stufe je Instanz (Abstand von den Eingängen), für die Fehlersuche
  std::vector<Issue> issues;
};

Built build(const LogicCircuit& c, Family family, const SimSettings& settings) {
  Built b;
  std::map<std::string, int> index;
  std::map<std::string, BlockPtr> cache;
  for (const auto& inst : c.instances) {
    if (index.count(inst.name)) throw CircuitError("Instanz '" + inst.name + "' doppelt");
    index[inst.name] = static_cast<int>(b.blocks.size());
    auto it = cache.find(inst.type);
    if (it == cache.end()) it = cache.emplace(inst.type, make_block(family, inst.type)).first;
    b.blocks.push_back(it->second);
    b.names.push_back(inst.name);
  }
  const std::size_t n = b.blocks.size();
  b.input_net.resize(n);
  b.output_net.resize(n);
  for (std::size_t k = 0; k < n; ++k) {
    b.input_net[k].assign(b.blocks[k]->inputs.size(), -1);
    b.output_net[k].assign(b.blocks[k]->outputs.size(), -1);
  }
  auto port_of = [&](const BlockPtr& blk, const std::string& port, bool output) {
    const auto& list = output ? blk->outputs : blk->inputs;
    for (std::size_t p = 0; p < list.size(); ++p)
      if (list[p].name == port) return static_cast<int>(p);
    // Abkürzung "out" für den einzigen Ausgang
    if (output && list.size() == 1 && port == "out") return 0;
    return -1;
  };
  auto parse = [&](const std::string& ref, bool output, Endpoint* e) {
    if (ref.rfind("in:", 0) == 0) {
      const auto it = std::find(c.inputs.begin(), c.inputs.end(), ref.substr(3));
      if (it == c.inputs.end()) throw CircuitError("Unbekannter Eingang '" + ref + "'");
      *e = {-1, static_cast<int>(it - c.inputs.begin())};
      return;
    }
    if (ref.rfind("out:", 0) == 0) {
      const auto it = std::find(c.outputs.begin(), c.outputs.end(), ref.substr(4));
      if (it == c.outputs.end()) throw CircuitError("Unbekannter Ausgang '" + ref + "'");
      *e = {-1, static_cast<int>(it - c.outputs.begin())};
      return;
    }
    const auto dot = ref.find('.');
    if (dot == std::string::npos) throw CircuitError("Anschluss '" + ref + "' braucht die Form Instanz.Port");
    const auto it = index.find(ref.substr(0, dot));
    if (it == index.end()) throw CircuitError("Unbekannte Instanz in '" + ref + "'");
    const int p = port_of(b.blocks[static_cast<std::size_t>(it->second)], ref.substr(dot + 1), output);
    if (p < 0) throw CircuitError("Unbekannter Port in '" + ref + "'");
    *e = {it->second, p};
  };
  // Netze: zuerst primäre Eingänge, dann alle Blockausgänge
  for (std::size_t k = 0; k < c.inputs.size(); ++k) {
    b.primary_in_net.push_back(static_cast<int>(b.driver.size()));
    b.driver.push_back({-1, static_cast<int>(k)});
    b.net_name.push_back("in:" + c.inputs[k]);
  }
  for (std::size_t k = 0; k < n; ++k)
    for (std::size_t o = 0; o < b.blocks[k]->outputs.size(); ++o) {
      b.output_net[k][o] = static_cast<int>(b.driver.size());
      b.driver.push_back({static_cast<int>(k), static_cast<int>(o)});
      b.net_name.push_back(b.names[k] + "." + b.blocks[k]->outputs[o].name);
    }
  b.consumers.resize(b.driver.size());
  b.primary_out_net.assign(c.outputs.size(), -1);
  for (const auto& w : c.wires) {
    Endpoint from, to;
    parse(w.from, true, &from);
    if (w.from.rfind("out:", 0) == 0)
      throw CircuitError("Ein primärer Ausgang kann nichts treiben: " + w.from);
    parse(w.to, false, &to);
    if (w.to.rfind("in:", 0) == 0)
      throw CircuitError("Ein primärer Eingang kann nicht getrieben werden: " + w.to);
    const int net =
        from.instance < 0
            ? b.primary_in_net[static_cast<std::size_t>(from.port)]
            : b.output_net[static_cast<std::size_t>(from.instance)][static_cast<std::size_t>(from.port)];
    if (to.instance < 0) {
      if (w.to.rfind("out:", 0) != 0) throw CircuitError("Ungültiges Ziel " + w.to);
      b.primary_out_net[static_cast<std::size_t>(to.port)] = net;
      b.consumers[static_cast<std::size_t>(net)].push_back({-1, to.port});
    } else {
      int& slot = b.input_net[static_cast<std::size_t>(to.instance)][static_cast<std::size_t>(to.port)];
      if (slot >= 0) throw CircuitError("Eingang " + w.to + " ist doppelt angeschlossen");
      slot = net;
      b.consumers[static_cast<std::size_t>(net)].push_back(to);
    }
  }
  for (std::size_t k = 0; k < n; ++k)
    for (std::size_t p = 0; p < b.input_net[k].size(); ++p)
      if (b.input_net[k][p] < 0)
        b.issues.push_back(
            {Issue::Severity::error, b.names[k], "Eingang " + b.blocks[k]->inputs[p].name + " ist offen"});
  for (std::size_t o = 0; o < c.outputs.size(); ++o)
    if (b.primary_out_net[o] < 0)
      b.issues.push_back({Issue::Severity::error, "out:" + c.outputs[o], "Ausgang ist nicht angeschlossen"});

  // Codierung der primären Eingänge: die der angeschlossenen Blöcke; widersprechen sie sich, ist das ein
  // Fehler
  b.primary_encoding.assign(c.inputs.size(), Encoding::intensity);
  b.primary_one.assign(c.inputs.size(), settings.input_one);
  for (std::size_t k = 0; k < c.inputs.size(); ++k) {
    std::set<int> seen;
    const auto& cons = b.consumers[static_cast<std::size_t>(b.primary_in_net[k])];
    for (const auto& e : cons)
      if (e.instance >= 0)
        seen.insert(static_cast<int>(b.blocks[static_cast<std::size_t>(e.instance)]
                                         ->inputs[static_cast<std::size_t>(e.port)]
                                         .encoding));
    if (!seen.empty()) b.primary_encoding[k] = static_cast<Encoding>(*seen.begin());
    if (seen.size() > 1)
      b.issues.push_back(
          {Issue::Severity::error, "in:" + c.inputs[k],
           "treibt Blöcke mit unterschiedlicher Codierung (z. B. Phase und Intensität); ohne Umsetzer kann "
           "ein Signal nicht beide bedienen"});
    // Primäre Eingänge sind äußere Quellen, die jedem Verbraucher seine Nennleistung liefern (bei optischer
    // Aufteilung also Fan-out mal so viel), z. B. den Setzpuls des Flip-Flops
    std::size_t fan_in = 0;
    for (const auto& e : cons)
      if (e.instance >= 0) ++fan_in;
    for (const auto& e : cons)
      if (e.instance >= 0) {
        const BlockPort& port =
            b.blocks[static_cast<std::size_t>(e.instance)]->inputs[static_cast<std::size_t>(e.port)];
        b.primary_one[k] =
            port.one_level * (port.encoding == Encoding::electrical ? 1.0 : static_cast<double>(fan_in));
        break;
      }
  }

  // Stufen (Abstand von den Eingängen) für die Reihenfolge der Fehlersuche
  b.level.assign(n, 0);
  for (std::size_t pass = 0; pass < n + 1; ++pass)
    for (std::size_t k = 0; k < n; ++k)
      for (const int net : b.input_net[k]) {
        if (net < 0) continue;
        const Endpoint d = b.driver[static_cast<std::size_t>(net)];
        if (d.instance >= 0)
          b.level[k] = std::max(b.level[k], b.level[static_cast<std::size_t>(d.instance)] + 1);
      }
  return b;
}

// Statische Prüfung auf der aufbereiteten Netzliste
void static_checks(const LogicCircuit& c, Built& b) {
  for (std::size_t net = 0; net < b.driver.size(); ++net) {
    const Endpoint d = b.driver[net];
    std::size_t fan = 0;
    for (const auto& e : b.consumers[net])
      if (e.instance >= 0) ++fan;
    if (fan == 0) continue;
    Encoding enc;
    double worst_one, worst_zero;
    bool regenerating = true;
    if (d.instance < 0) {
      enc = b.primary_encoding[static_cast<std::size_t>(d.port)];
      worst_one = b.primary_one[static_cast<std::size_t>(d.port)], worst_zero = 0.0;
    } else {
      const BlockPtr& blk = b.blocks[static_cast<std::size_t>(d.instance)];
      const BlockPort& out = blk->outputs[static_cast<std::size_t>(d.port)];
      enc = out.encoding;
      worst_one = out.worst_one, worst_zero = out.worst_zero;
      regenerating = blk->regenerating;
    }
    const double split = (enc == Encoding::electrical) ? 1.0 : static_cast<double>(fan);
    if (fan > 1 && enc != Encoding::electrical)
      b.issues.push_back({Issue::Severity::info, b.net_name[net],
                          "Fan-out 1:" + std::to_string(fan) + " teilt die Leistung (je Verbraucher 1/" +
                              std::to_string(fan) + ")"});
    for (const auto& e : b.consumers[net]) {
      if (e.instance < 0) continue;
      const BlockPtr& blk = b.blocks[static_cast<std::size_t>(e.instance)];
      const BlockPort& in = blk->inputs[static_cast<std::size_t>(e.port)];
      const std::string where = b.names[static_cast<std::size_t>(e.instance)] + "." + in.name;
      if (enc != in.encoding) {
        b.issues.push_back({Issue::Severity::error, where,
                            "Codierung passt nicht: " + std::string(label(enc)) + " von " + b.net_name[net] +
                                ", erwartet " + std::string(label(in.encoding))});
        continue;
      }
      if (in.encoding == Encoding::phase) continue;  // Pegel spielen bei Phasencodierung keine Rolle
      if (worst_one / split <= in.threshold)
        b.issues.push_back({Issue::Severity::error, where,
                            "Pegel der 1 zu klein: schlechteste 1 " + fmt_power(worst_one / split) +
                                " (nach Aufteilung) <= Schwelle " + fmt_power(in.threshold)});
      else if (worst_zero / split >= in.threshold)
        b.issues.push_back({Issue::Severity::error, where,
                            "Pegel der 0 zu groß: schlechteste 0 " + fmt_power(worst_zero / split) +
                                " >= Schwelle " + fmt_power(in.threshold)});
      else if (worst_one / split < 1.2 * in.threshold || worst_zero / split > 0.8 * in.threshold)
        b.issues.push_back(
            {Issue::Severity::warning, where, "knapper Abstand der Pegel zur Schwelle (unter 20 %)"});
      if (!regenerating && d.instance >= 0)
        b.issues.push_back(
            {Issue::Severity::warning, where,
             "Treiber " + b.net_name[net] +
                 " regeneriert nicht: seine Pegel hängen von seinen Eingängen ab, Fehler können sich in der "
                 "Kaskade aufbauen"});
    }
  }
  (void)c;
}

}  // namespace

std::vector<Issue> check(const LogicCircuit& circuit, Family family, const SimSettings& settings) {
  Built b = build(circuit, family, settings);
  static_checks(circuit, b);
  return b.issues;
}

SimResult simulate(const LogicCircuit& circuit, Family family, const std::vector<std::vector<int>>& vectors,
                   const SimSettings& settings, JobControl* control) {
  Built b = build(circuit, family, settings);
  static_checks(circuit, b);
  SimResult result;
  result.static_issues = b.issues;
  result.block_count = static_cast<int>(b.blocks.size());
  for (const auto& issue : b.issues)
    if (issue.severity == Issue::Severity::error && issue.text.find("offen") != std::string::npos)
      return result;

  const std::size_t nets = b.driver.size(), n = b.blocks.size();
  std::vector<Signal> value(nets);
  std::vector<std::vector<double>> state(n);
  std::vector<int> ideal_state(n, 0);
  for (std::size_t k = 0; k < n; ++k)
    state[k].assign(static_cast<std::size_t>(b.blocks[k]->state_size()), 0.0);
  std::vector<std::size_t> fan(nets, 0);
  for (std::size_t net = 0; net < nets; ++net)
    for (const auto& e : b.consumers[net])
      if (e.instance >= 0) ++fan[net];
  if (settings.keep_traces) {
    result.traces.resize(nets);
    for (std::size_t net = 0; net < nets; ++net) result.traces[net].name = b.net_name[net];
  }

  // Eingangssignale eines Blocks: Netzwert, bei optischer Aufteilung Leistung / Fan-out
  auto inputs_of = [&](std::size_t k) {
    std::vector<Signal> in(b.blocks[k]->inputs.size());
    for (std::size_t p = 0; p < in.size(); ++p) {
      const int net = b.input_net[k][p];
      if (net < 0) continue;
      in[p] = value[static_cast<std::size_t>(net)];
      const Endpoint d = b.driver[static_cast<std::size_t>(net)];
      const Encoding enc = d.instance < 0 ? b.primary_encoding[static_cast<std::size_t>(d.port)]
                                          : b.blocks[static_cast<std::size_t>(d.instance)]
                                                ->outputs[static_cast<std::size_t>(d.port)]
                                                .encoding;
      if (enc != Encoding::electrical && fan[static_cast<std::size_t>(net)] > 1)
        in[p].power /= static_cast<double>(fan[static_cast<std::size_t>(net)]);
    }
    return in;
  };
  auto record = [&](std::size_t net, double t) {
    if (!settings.keep_traces || result.vectors.size() > 64) return;
    auto& tr = result.traces[net];
    tr.time.push_back(t);
    tr.power.push_back(value[net].power);
    tr.bit.push_back(value[net].bit);
  };
  for (std::size_t net = 0; net < nets; ++net) record(net, 0.0);

  double energy = 0.0, static_power = 0.0;
  for (const auto& blk : b.blocks) static_power += blk->static_power;

  for (std::size_t v = 0; v < vectors.size(); ++v) {
    if (control) {
      if (control->cancel_requested) {
        result.cancelled = true;
        return result;
      }
      control->progress = static_cast<float>(v) / static_cast<float>(vectors.size());
    }
    const auto& vec = vectors[v];
    if (vec.size() != circuit.inputs.size()) throw CircuitError("Vektor hat nicht die Zahl der Eingänge");
    const double t0 = static_cast<double>(v) * settings.period;
    // Ereignisse: Zeit -> Netz -> neues Signal (spätere Zuweisung zur selben Zeit gewinnt)
    std::map<double, std::map<int, Signal>> queue;
    for (std::size_t k = 0; k < vec.size(); ++k) {
      Signal s;
      s.bit = vec[k];
      if (b.primary_encoding[k] == Encoding::phase) {
        s.power = b.primary_one[k];
        s.phase = vec[k] ? constants::pi : 0.0;
      } else {
        s.power = vec[k] ? b.primary_one[k] : 0.0;
      }
      queue[t0][b.primary_in_net[k]] = s;
    }
    double last = t0;
    bool oscillating = false;
    while (!queue.empty()) {
      const auto it = queue.begin();
      const double t = it->first;
      if (t > t0 + 0.999 * settings.period) {
        oscillating = true;
        break;
      }
      std::set<std::size_t> touched;
      for (const auto& [net, s] : it->second) {
        const Signal old = value[static_cast<std::size_t>(net)];
        value[static_cast<std::size_t>(net)] = s;
        record(static_cast<std::size_t>(net), t);
        last = std::max(last, t);
        // Schaltenergie: Pegelwechsel eines Blockausgangs
        const Endpoint d = b.driver[static_cast<std::size_t>(net)];
        if (d.instance >= 0) {
          const BlockPtr& blk = b.blocks[static_cast<std::size_t>(d.instance)];
          const BlockPort& out = blk->outputs[static_cast<std::size_t>(d.port)];
          if ((old.power > out.threshold) != (s.power > out.threshold)) energy += blk->switching_energy;
        }
        for (const auto& e : b.consumers[static_cast<std::size_t>(net)])
          if (e.instance >= 0) touched.insert(static_cast<std::size_t>(e.instance));
      }
      queue.erase(it);
      for (const std::size_t k : touched) {
        const BlockPtr& blk = b.blocks[k];
        const std::vector<Signal> in = inputs_of(k);
        std::vector<Signal> out = blk->evaluate(in, state[k]);
        std::vector<int> bits;
        for (const auto& s : in) bits.push_back(s.bit);
        const std::vector<int> ideal_bits = blk->ideal(bits, ideal_state[k]);
        for (std::size_t o = 0; o < out.size(); ++o) {
          out[o].bit = ideal_bits[o];
          const std::size_t net = static_cast<std::size_t>(b.output_net[k][o]);
          const Signal& cur = value[net];
          const double scale = std::max({std::abs(cur.power), std::abs(out[o].power), 1e-12});
          if (std::abs(cur.power - out[o].power) > 1e-9 * scale || cur.bit != out[o].bit ||
              std::abs(std::remainder(cur.phase - out[o].phase, 2.0 * constants::pi)) > 1e-9)
            queue[t + blk->delay][static_cast<int>(net)] = out[o];
        }
      }
    }
    VectorResult r;
    r.inputs = vec;
    r.settle_time = last - t0;
    if (oscillating) r.first_error = "Schaltung kommt innerhalb der Periode nicht zur Ruhe (Schwingung?)";
    for (std::size_t o = 0; o < circuit.outputs.size(); ++o) {
      const int net = b.primary_out_net[o];
      const Signal s = net >= 0 ? value[static_cast<std::size_t>(net)] : Signal{};
      int decoded = 0;
      if (net >= 0) {
        const Endpoint d = b.driver[static_cast<std::size_t>(net)];
        const BlockPort port =
            d.instance < 0
                ? BlockPort{}
                : b.blocks[static_cast<std::size_t>(d.instance)]->outputs[static_cast<std::size_t>(d.port)];
        decoded = d.instance < 0 ? s.bit : decode(port, s);
      }
      r.expected.push_back(s.bit);
      r.decoded.push_back(decoded);
      r.power.push_back(s.power);
      if (decoded != s.bit) r.correct = false;
    }
    if (oscillating) r.correct = false;
    if (settings.check_idle_only && std::any_of(vec.begin(), vec.end(), [](int x) { return x != 0; })) {
      r.checked = false;  // Steuerphase (Setz-/Rücksetzpuls): Ausgang nicht bewertet
      r.correct = true;
    }
    // Fehlersuche: erster Blockeingang (kleinste Stufe), der anders liest als die ideale Logik
    if (r.first_error.empty()) {
      int best_level = 1 << 30;
      for (std::size_t k = 0; k < n; ++k) {
        const std::vector<Signal> in = inputs_of(k);
        for (std::size_t p = 0; p < in.size(); ++p) {
          const BlockPort& port = b.blocks[k]->inputs[p];
          const int got = decode(port, in[p]);
          if (got == in[p].bit || b.level[k] >= best_level) continue;
          best_level = b.level[k];
          char text[320];
          if (port.encoding == Encoding::phase)
            std::snprintf(text, sizeof text, "%s.%s liest %d statt %d: Phase %.2f rad, Leistung %s (Netz %s)",
                          b.names[k].c_str(), port.name.c_str(), got, in[p].bit, in[p].phase,
                          fmt_power(in[p].power).c_str(),
                          b.net_name[static_cast<std::size_t>(b.input_net[k][p])].c_str());
          else
            std::snprintf(text, sizeof text, "%s.%s liest %d statt %d: Pegel %s, Schwelle %s (Netz %s)",
                          b.names[k].c_str(), port.name.c_str(), got, in[p].bit,
                          fmt_power(in[p].power).c_str(), fmt_power(port.threshold).c_str(),
                          b.net_name[static_cast<std::size_t>(b.input_net[k][p])].c_str());
          r.first_error = text;
        }
      }
      if (r.first_error.empty() && !r.correct)
        for (std::size_t o = 0; o < r.decoded.size(); ++o)
          if (r.decoded[o] != r.expected[o]) {
            char text[200];
            std::snprintf(text, sizeof text, "Ausgang %s: Pegel %s gelesen als %d, erwartet %d",
                          circuit.outputs[o].c_str(), fmt_power(r.power[o]).c_str(), r.decoded[o],
                          r.expected[o]);
            r.first_error = text;
            break;
          }
    }
    if (r.checked) ++result.checked;
    if (r.checked && r.correct) ++result.correct;
    result.latency = std::max(result.latency, r.settle_time);
    result.vectors.push_back(std::move(r));
  }
  result.energy_per_vector =
      vectors.empty() ? 0.0 : energy / static_cast<double>(vectors.size()) + static_power * settings.period;
  if (control) control->progress = 1.0f;
  return result;
}

}  // namespace photonlab::logic
