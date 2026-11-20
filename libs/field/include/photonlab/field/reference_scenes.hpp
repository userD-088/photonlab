#pragma once
// Referenz-Szenen der Feld-Ebene. Jede ist Demo und Ausgangspunkt für eigene Versuche.
#include <string>
#include <string_view>
#include <vector>

#include "photonlab/field/scene.hpp"

namespace photonlab::field {

std::vector<std::string> reference_scene_names();
FieldScene reference_scene(std::string_view name);  // wirft FieldError bei unbekanntem Namen
// Gruppe für das Menü: "Grundlagen", "Logische Bauteile" oder "Photonische Kristalle"
std::string reference_scene_category(std::string_view name);

// Bausteine für den Editor: fertige Geometrie (Kern 0,30 µm aus Si in SiO2, Draufsicht, für Hz und Ez
// einmodig) als eine Gruppe mit Bezugspunkt (x, y). Der Regenerator legt Detektor und Sender an.
std::vector<std::string> building_block_names();
std::string building_block_label(std::string_view name);
// Fügt den Baustein ein und liefert den Index seiner Gruppe; parent_group = übergeordnete Gruppe oder -1.
int add_building_block(FieldScene& scene, std::string_view name, double x, double y, int parent_group = -1);

}  // namespace photonlab::field
