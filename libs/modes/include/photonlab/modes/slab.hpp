#pragma once
// Schichtwellenleiter (drei Schichten): geführte Moden aus der analytischen Eigenwertgleichung.
// Referenz für den Modenlöser und Grundlage der Effektiv-Index-Methode. Herleitung: docs/physics/modes.md
#include <stdexcept>
#include <vector>

namespace photonlab::modes {

struct ModeError : std::runtime_error {
  using std::runtime_error::runtime_error;
};

// Polarisation im Schichtwellenleiter, bezogen auf die Schichtebene.
enum class SlabPolarization {
  te,  // E parallel zu den Schichten (keine Feldkomponente senkrecht zur Schicht)
  tm,  // H parallel zu den Schichten
};

struct SlabMode {
  int order = 0;          // Zahl der Knoten des Feldes
  double n_eff = 0.0;     // effektiver Index beta / k0
};

// Geführte Moden einer Schicht der Dicke thickness [m] mit Brechzahl n_core zwischen den Halbräumen
// n_lower und n_upper, nach fallendem n_eff sortiert. Leer, wenn die Schicht keine Mode führt.
std::vector<SlabMode> slab_modes(double n_core, double n_lower, double n_upper, double thickness,
                                 double wavelength, SlabPolarization polarization);

// Linke Seite der Eigenwertgleichung minus rechte Seite (Null bei einer Mode); öffentlich für Tests.
double slab_dispersion_residual(double n_eff, int order, double n_core, double n_lower, double n_upper,
                                double thickness, double wavelength, SlabPolarization polarization);

// Feld einer Mode an der Stelle y (Schicht von 0 bis thickness), auf Maximum 1 normiert.
// TE: das elektrische Feld, TM: das magnetische Feld.
double slab_field(const SlabMode& mode, double y, double n_core, double n_lower, double n_upper,
                  double thickness, double wavelength, SlabPolarization polarization);

// Effektiv-Index-Methode für einen Streifen der Breite width und Höhe height: erst die senkrechte
// Schicht (Polarisation wie verlangt), dann die waagerechte mit der jeweils anderen Polarisation.
// Grobe Näherung für stark führende Wellenleiter; wirft ModeError, wenn keine Mode geführt wird.
double effective_index_method(double n_core, double n_lower, double n_upper, double n_side, double width,
                              double height, double wavelength, SlabPolarization polarization);

}  // namespace photonlab::modes
