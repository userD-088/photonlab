#pragma once
// Fenster "Hilfe": was PhotonLab kann, wie man es bedient, und wo die Rechnung an ihre Grenzen kommt.

namespace photonlab::gui {

enum class HelpTopic { overview, first_steps, field_lab, chip_designer, shortcuts, physics, command_line };

// open: Fenster offen; topic: gewähltes Thema (das Menü setzt es beim Öffnen).
void draw_help_window(bool* open, HelpTopic* topic);

}  // namespace photonlab::gui
