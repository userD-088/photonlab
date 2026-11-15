#pragma once
// Laden von Bauteil-Plugins (plugin_api.h). Geladene Bauteiltypen kommen in die eingebaute Registry
// (ComponentRegistry::builtin()), damit alle Programmteile sie kennen. Plugins werden nur beim Programmstart
// geladen, bevor Jobs laufen: Die Registry ist danach nur noch zum Lesen da. Bibliotheken bleiben bis zum
// Programmende geladen. Beschreibung: docs/plugins.md
#include <string>
#include <vector>

namespace photonlab::circuit {

struct PluginInfo {
  std::string path;
  std::string name, version, description;
  std::vector<std::string> components;  // Typnamen
  std::vector<std::string> warnings;    // z. B. "verstärkt Leistung" (S nicht passiv)
};

// Lädt ein Plugin und meldet seine Bauteile an. Wirft CircuitError mit Klartext (Datei fehlt, keine
// Plugin-Datei, falsche API-Version, ungültige Beschreibung, Name schon vergeben). Bei einem Fehler wird kein
// Bauteil dieses Plugins angemeldet.
PluginInfo load_plugin(const std::string& path);

// Lädt alle Plugins (.dll/.so/.dylib) eines Ordners; Fehler einzelner Dateien landen in errors.
std::vector<PluginInfo> load_plugins_from(const std::string& directory, std::vector<std::string>* errors);

// Bisher geladene Plugins.
const std::vector<PluginInfo>& loaded_plugins();

}  // namespace photonlab::circuit
