#include "photonlab/circuit/sparameter_table.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "photonlab/core/units.hpp"

namespace photonlab::circuit {
namespace {

class SParameterComponent final : public Component {
 public:
  explicit SParameterComponent(std::shared_ptr<const SParameterTable> table) : table_(std::move(table)) {}
  int port_count() const override { return static_cast<int>(table_->ports.size()); }
  CMatrix s_matrix(double wavelength) const override { return interpolate(*table_, wavelength); }

 private:
  std::shared_ptr<const SParameterTable> table_;
};

}  // namespace

bool SParameterTable::operator==(const SParameterTable& other) const {
  if (name != other.name || label != other.label || description != other.description ||
      ports != other.ports || left_port_count != other.left_port_count || wavelengths != other.wavelengths ||
      source_hash != other.source_hash || s.size() != other.s.size())
    return false;
  for (std::size_t k = 0; k < s.size(); ++k)
    if (s[k].rows() != other.s[k].rows() || s[k].cols() != other.s[k].cols() || s[k] != other.s[k])
      return false;
  return true;
}

std::string sparameter_type_name(const std::string& table_name) { return "sparam:" + table_name; }

void validate(const SParameterTable& table) {
  const std::string who = "S-Parameter '" + table.name + "': ";
  if (table.name.empty()) throw CircuitError("S-Parameter-Tabelle ohne Namen");
  if (table.name.find('.') != std::string::npos)
    throw CircuitError(who + "der Name darf keinen Punkt enthalten");
  if (table.ports.empty()) throw CircuitError(who + "keine Ports");
  for (std::size_t i = 0; i < table.ports.size(); ++i)
    for (std::size_t j = 0; j < i; ++j)
      if (table.ports[i] == table.ports[j])
        throw CircuitError(who + "Portname '" + table.ports[i] + "' doppelt");
  if (table.wavelengths.size() < 2) throw CircuitError(who + "mindestens zwei Wellenlängen nötig");
  if (table.wavelengths.size() != table.s.size())
    throw CircuitError(who + "Zahl der Matrizen passt nicht zu den Wellenlängen");
  const auto n = static_cast<Eigen::Index>(table.ports.size());
  for (std::size_t k = 0; k < table.s.size(); ++k) {
    if (table.s[k].rows() != n || table.s[k].cols() != n)
      throw CircuitError(who + "Matrix hat die falsche Größe");
    if (!table.s[k].allFinite()) throw CircuitError(who + "Matrix enthält ungültige Zahlen");
    if (k > 0 && !(table.wavelengths[k] > table.wavelengths[k - 1]))
      throw CircuitError(who + "Wellenlängen müssen streng steigen");
  }
  if (table.left_port_count > static_cast<int>(table.ports.size()))
    throw CircuitError(who + "zu viele linke Ports");
}

CMatrix interpolate(const SParameterTable& table, double wavelength) {
  const auto& w = table.wavelengths;
  // kleine Toleranz an den Rändern, damit Rundungsfehler im Sweep nicht stören
  const double tolerance = 1e-9 * (w.back() - w.front());
  if (wavelength < w.front() - tolerance || wavelength > w.back() + tolerance) {
    char text[200];
    std::snprintf(text, sizeof text, "S-Parameter '%s' gelten nur von %.2f bis %.2f nm, nicht bei %.2f nm",
                  table.name.c_str(), w.front() / units::nm, w.back() / units::nm, wavelength / units::nm);
    throw CircuitError(text);
  }
  const double x = std::clamp(wavelength, w.front(), w.back());
  const auto upper = std::upper_bound(w.begin(), w.end(), x);
  const std::size_t k = std::min<std::size_t>(
      static_cast<std::size_t>(std::max<std::ptrdiff_t>(upper - w.begin(), 1)) - 1, w.size() - 2);
  const double t = (x - w[k]) / (w[k + 1] - w[k]);
  const CMatrix& a = table.s[k];
  const CMatrix& b = table.s[k + 1];
  CMatrix result(a.rows(), a.cols());
  for (Eigen::Index i = 0; i < a.rows(); ++i)
    for (Eigen::Index j = 0; j < a.cols(); ++j) {
      // Betrag und Phase getrennt: lineare Interpolation von Real- und Imaginärteil ließe den Betrag
      // zwischen den Stützstellen einbrechen, wenn sich die Phase stark dreht
      const double magnitude = (1.0 - t) * std::abs(a(i, j)) + t * std::abs(b(i, j));
      const double phase_a = std::arg(a(i, j));
      const double step = std::remainder(std::arg(b(i, j)) - phase_a, 2.0 * constants::pi);
      result(i, j) = std::polar(magnitude, phase_a + t * step);
    }
  return result;
}

void register_sparameter_table(ComponentRegistry& registry, std::shared_ptr<const SParameterTable> table) {
  if (!table) throw CircuitError("Leere S-Parameter-Tabelle");
  validate(*table);
  char range[96];
  std::snprintf(range, sizeof range, " Gültig von %.1f bis %.1f nm.", table->wavelengths.front() / units::nm,
                table->wavelengths.back() / units::nm);
  ComponentType type{
      sparameter_type_name(table->name),
      table->label.empty() ? table->name : table->label,
      (table->description.empty() ? std::string("Gemessene S-Matrix.") : table->description) + range,
      table->ports,
      {},
      [table](const ParamMap&) { return std::make_unique<SParameterComponent>(table); }};
  type.left_port_count = table->left_port_count;
  registry.add(std::move(type));
}

}  // namespace photonlab::circuit
