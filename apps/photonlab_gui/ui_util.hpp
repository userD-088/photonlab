#pragma once
// Kleine Helfer, die mehrere Arbeitsbereiche der Oberfläche teilen: Zahlenformate und Ampelfarben.
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <iterator>
#include <string>

#include "imgui.h"
#include "photonlab/circuit/netlist.hpp"
#include "photonlab/circuit/registry.hpp"

namespace photonlab::gui {

inline const ImVec4 ui_color_good = ImVec4(0.36f, 0.77f, 0.55f, 1.0f);
inline const ImVec4 ui_color_limited = ImVec4(0.90f, 0.69f, 0.29f, 1.0f);
inline const ImVec4 ui_color_bad = ImVec4(1.0f, 0.48f, 0.42f, 1.0f);
inline const ImVec4 ui_color_neutral = ImVec4(0.47f, 0.51f, 0.57f, 1.0f);

// Zahl mit SI-Vorsatz, z. B. 1.0e-3 W -> "1.000 mW"
inline std::string format_si(double value, const std::string& unit) {
  struct Prefix {
    double factor;
    const char* symbol;
  };
  static const Prefix prefixes[] = {{1e9, "G"},  {1e6, "M"},         {1e3, "k"},  {1.0, ""},
                                    {1e-3, "m"}, {1e-6, "\xC2\xB5"}, {1e-9, "n"}, {1e-12, "p"}};
  char buffer[64];
  const double magnitude = std::abs(value);
  if (magnitude < 1e-15) {
    std::snprintf(buffer, sizeof buffer, "0 %s", unit.c_str());
    return buffer;
  }
  const Prefix* chosen = &prefixes[std::size(prefixes) - 1];
  for (const Prefix& p : prefixes)
    if (magnitude >= p.factor) {
      chosen = &p;
      break;
    }
  std::snprintf(buffer, sizeof buffer, "%.4g %s%s", value / chosen->factor, chosen->symbol, unit.c_str());
  return buffer;
}

inline std::string format_param(double si_value, const circuit::ParamSpec& spec) {
  char buffer[64];
  std::snprintf(buffer, sizeof buffer, "%.4g %s", si_value / spec.display_scale, spec.display_unit.c_str());
  return buffer;
}

inline double param_value(const circuit::Instance& instance, const circuit::ParamSpec& spec) {
  const auto it = instance.params.find(spec.name);
  return it != instance.params.end() ? it->second : spec.default_value;
}

// Pfade kommen von SDL als UTF-8; std::filesystem braucht das unter Windows ausdrücklich.
inline std::filesystem::path ui_path_from_utf8(const std::string& text) {
  return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

}  // namespace photonlab::gui
