#pragma once
// Referenz-Szenen der Feld-Ebene (Bauplan Abschnitt 11). Jede ist Demo und Ausgangspunkt für eigene Versuche.
#include <string>
#include <string_view>
#include <vector>

#include "photonlab/field/scene.hpp"

namespace photonlab::field {

std::vector<std::string> reference_scene_names();
FieldScene reference_scene(std::string_view name);  // wirft FieldError bei unbekanntem Namen

}  // namespace photonlab::field
