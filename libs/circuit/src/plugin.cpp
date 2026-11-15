#include "photonlab/circuit/plugin.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <set>

#include "photonlab/circuit/plugin_api.h"
#include "photonlab/circuit/registry.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace photonlab::circuit {
namespace {

std::vector<PluginInfo>& plugin_list() {
  static std::vector<PluginInfo> list;
  return list;
}

std::mutex& load_mutex() {
  static std::mutex m;
  return m;
}

// Einstiegsfunktion aus der Bibliothek holen (Bibliothek bleibt bis Programmende geladen)
plab_entry_fn open_library(const std::string& path) {
#ifdef _WIN32
  const std::wstring wide = path_of(path).wstring();
  HMODULE handle = LoadLibraryW(wide.c_str());
  if (!handle)
    throw CircuitError("Plugin '" + path + "' lässt sich nicht laden (Fehler " +
                       std::to_string(GetLastError()) + "; falsche Architektur oder fehlende DLL?)");
  auto entry =
      reinterpret_cast<plab_entry_fn>(reinterpret_cast<void*>(GetProcAddress(handle, "photonlab_plugin")));
  if (!entry) {
    FreeLibrary(handle);
    throw CircuitError("'" + path + "' ist kein PhotonLab-Plugin (Funktion photonlab_plugin fehlt)");
  }
  return entry;
#else
  void* handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (!handle) {
    const char* why = dlerror();
    throw CircuitError("Plugin '" + path + "' lässt sich nicht laden: " + (why ? why : "unbekannter Fehler"));
  }
  void* symbol = dlsym(handle, "photonlab_plugin");
  if (!symbol) {
    dlclose(handle);
    throw CircuitError("'" + path + "' ist kein PhotonLab-Plugin (Funktion photonlab_plugin fehlt)");
  }
  plab_entry_fn entry = nullptr;
  static_assert(sizeof(entry) == sizeof(symbol));
  std::memcpy(&entry, &symbol, sizeof entry);
  return entry;
#endif
}

std::string text(const char* s) { return s ? std::string(s) : std::string(); }

// Pfade sind intern UTF-8 (wie in der Oberfläche)
std::filesystem::path path_of(const std::string& utf8) {
  return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}
std::string utf8_of(const std::filesystem::path& path) {
  const std::u8string u = path.u8string();
  return std::string(u.begin(), u.end());
}

// Bauteil, dessen Streumatrix die Funktion des Plugins liefert
class PluginComponent final : public Component {
 public:
  PluginComponent(const plab_component* type, std::vector<double> params)
      : type_(type), params_(std::move(params)) {}
  int port_count() const override { return type_->port_count; }
  CMatrix s_matrix(double wavelength) const override {
    const int n = type_->port_count;
    std::vector<double> re(static_cast<std::size_t>(n * n), 0.0), im(re.size(), 0.0);
    const int status =
        type_->s_matrix(wavelength, params_.empty() ? nullptr : params_.data(), re.data(), im.data());
    if (status != 0)
      throw CircuitError("Plugin-Bauteil '" + text(type_->name) + "' meldet Fehler " +
                         std::to_string(status) + " bei " + std::to_string(wavelength * 1e9) + " nm");
    CMatrix s(n, n);
    for (int i = 0; i < n; ++i)
      for (int j = 0; j < n; ++j) {
        const std::size_t k = static_cast<std::size_t>(i * n + j);
        if (!std::isfinite(re[k]) || !std::isfinite(im[k]))
          throw CircuitError("Plugin-Bauteil '" + text(type_->name) + "' liefert keine endliche Streumatrix");
        s(i, j) = cplx(re[k], im[k]);
      }
    return s;
  }

 private:
  const plab_component* type_;
  std::vector<double> params_;
};

ComponentType make_type(const plab_component& c, const std::string& plugin) {
  const std::string where = "Plugin '" + plugin + "', Bauteil '" + text(c.name) + "': ";
  if (!c.name || !*c.name) throw CircuitError("Plugin '" + plugin + "': Bauteil ohne Namen");
  if (c.port_count < 1 || c.port_count > 64) throw CircuitError(where + "1 bis 64 Ports erlaubt");
  if (!c.port_names) throw CircuitError(where + "Portnamen fehlen");
  if (!c.s_matrix) throw CircuitError(where + "Funktion s_matrix fehlt");
  if (c.param_count < 0 || (c.param_count > 0 && !c.params))
    throw CircuitError(where + "Parameterliste fehlt");
  ComponentType type;
  type.name = c.name;
  type.label = c.label && *c.label ? c.label : c.name;
  type.description = text(c.description) + " (Plugin " + plugin + ")";
  std::set<std::string> seen;
  for (int p = 0; p < c.port_count; ++p) {
    const std::string port = text(c.port_names[p]);
    if (port.empty() || !seen.insert(port).second) throw CircuitError(where + "Portnamen leer oder doppelt");
    type.port_names.push_back(port);
  }
  type.left_port_count = c.left_port_count < 0 ? -1 : std::min(c.left_port_count, c.port_count);
  seen.clear();
  for (int p = 0; p < c.param_count; ++p) {
    const plab_param& q = c.params[p];
    ParamSpec spec;
    spec.name = text(q.name);
    spec.label = q.label && *q.label ? q.label : spec.name;
    spec.default_value = q.default_value, spec.min_value = q.min_value, spec.max_value = q.max_value;
    spec.display_unit = text(q.display_unit);
    spec.display_scale = q.display_scale != 0.0 ? q.display_scale : 1.0;
    if (spec.name.empty() || !seen.insert(spec.name).second)
      throw CircuitError(where + "Parametername leer oder doppelt");
    if (!(spec.min_value <= spec.default_value && spec.default_value <= spec.max_value))
      throw CircuitError(where + "Standardwert von '" + spec.name + "' liegt außerhalb des Wertebereichs");
    type.params.push_back(spec);
  }
  const plab_component* raw = &c;
  const std::vector<ParamSpec> specs = type.params;
  type.create = [raw, specs](const ParamMap& params) -> std::unique_ptr<Component> {
    std::vector<double> values;
    for (const auto& spec : specs) {
      const auto it = params.find(spec.name);
      values.push_back(it != params.end() ? it->second : spec.default_value);
    }
    return std::make_unique<PluginComponent>(raw, std::move(values));
  };
  return type;
}

}  // namespace

PluginInfo load_plugin(const std::string& path) {
  std::lock_guard lock(load_mutex());
  if (!std::filesystem::exists(path_of(path))) throw CircuitError("Plugin nicht gefunden: " + path);
  const plab_entry_fn entry = open_library(path);
  const plab_plugin* plugin = entry();
  if (!plugin) throw CircuitError("Plugin '" + path + "' liefert keine Beschreibung");
  if (plugin->api_version != PHOTONLAB_PLUGIN_API_VERSION)
    throw CircuitError("Plugin '" + path + "' ist für Schnittstelle " + std::to_string(plugin->api_version) +
                       " gebaut, PhotonLab erwartet " + std::to_string(PHOTONLAB_PLUGIN_API_VERSION));
  PluginInfo info;
  info.path = path;
  info.name = plugin->name && *plugin->name ? plugin->name : utf8_of(path_of(path).stem());
  info.version = text(plugin->version);
  info.description = text(plugin->description);
  if (plugin->component_count < 0 || (plugin->component_count > 0 && !plugin->components))
    throw CircuitError("Plugin '" + info.name + "': Bauteilliste fehlt");

  // erst alles prüfen, dann anmelden: bei einem Fehler bleibt die Registry unverändert
  std::vector<ComponentType> types;
  for (int k = 0; k < plugin->component_count; ++k) {
    ComponentType type = make_type(plugin->components[k], info.name);
    if (ComponentRegistry::builtin().find(type.name))
      throw CircuitError("Plugin '" + info.name + "': Bauteiltyp '" + type.name + "' gibt es schon");
    for (const auto& other : types)
      if (other.name == type.name)
        throw CircuitError("Plugin '" + info.name + "': Typ '" + type.name + "' doppelt");
    // Probe bei 1550 nm mit Standardwerten: endlich und passiv?
    const auto component = type.create(type.default_params());
    const CMatrix s = component->s_matrix(1550e-9);
    const double gain = Eigen::JacobiSVD<CMatrix>(s).singularValues().maxCoeff();
    if (gain > 1.0 + 1e-9)
      info.warnings.push_back(type.name + ": Streumatrix nicht passiv bei 1550 nm (größter Singulärwert " +
                              std::to_string(gain) + "), das Bauteil verstärkt");
    types.push_back(std::move(type));
  }
  for (auto& type : types) {
    info.components.push_back(type.name);
    ComponentRegistry::extend_builtin(std::move(type));
  }
  plugin_list().push_back(info);
  return info;
}

std::vector<PluginInfo> load_plugins_from(const std::string& directory, std::vector<std::string>* errors) {
  std::vector<PluginInfo> loaded;
  std::error_code ec;
  const std::filesystem::path dir = path_of(directory);
  if (!std::filesystem::is_directory(dir, ec)) return loaded;
  std::vector<std::filesystem::path> files;
  for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
    const std::string ext = entry.path().extension().string();
    if (entry.is_regular_file() && (ext == ".dll" || ext == ".so" || ext == ".dylib"))
      files.push_back(entry.path());
  }
  std::sort(files.begin(), files.end());  // feste Reihenfolge
  for (const auto& file : files) {
    try {
      loaded.push_back(load_plugin(utf8_of(file)));
    } catch (const CircuitError& e) {
      if (errors) errors->push_back(e.what());
    }
  }
  return loaded;
}

const std::vector<PluginInfo>& loaded_plugins() { return plugin_list(); }

}  // namespace photonlab::circuit
