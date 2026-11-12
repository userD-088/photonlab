#pragma once
// Auswertung berechneter Spektren: Resonanzen finden und FSR, Halbwertsbreite, Güte, Extinktion bestimmen.
// Definitionen: docs/physics/resonators.md
#include <vector>

namespace photonlab::circuit {

enum class ResonanceKind { dip, peak };

struct Resonance {
  double position = 0.0;       // Lage des Extremums (Einheit der x-Achse, z. B. Wellenlänge)
  double value = 0.0;          // Wert im Extremum (parabolisch verfeinert)
  double baseline = 0.0;       // Bezugspegel zwischen den Nachbarresonanzen: bei Einbrüchen das Maximum, bei Spitzen das Minimum
  double fwhm = 0.0;           // volle Breite auf halber Höhe zwischen Extremum und Bezugspegel; NaN, wenn nicht messbar
  double quality = 0.0;        // Güte Q = position / fwhm
  double extinction_db = 0.0;  // 10*log10(größerer / kleinerer der beiden Pegel)
};

// Sucht Resonanzen in einem abgetasteten Spektrum y(x) mit streng steigendem x.
// min_prominence: Mindesttiefe relativ zur Gesamtspanne von y (0..1); flachere Extrema werden ignoriert.
// Die Genauigkeit hängt von der Abtastung ab: unter etwa 10 Punkten pro Halbwertsbreite werden
// Extremwert und Breite spürbar ungenau.
std::vector<Resonance> find_resonances(const std::vector<double>& x, const std::vector<double>& y,
                                       ResonanceKind kind, double min_prominence = 0.05);

// Zwischen zwei Spitzen liegt immer auch ein (breites) Minimum und umgekehrt. Diese Funktion liefert
// die Art mit den schmaleren Strukturen, also die eigentlichen Resonanzen, und meldet sie über kind.
std::vector<Resonance> find_dominant_resonances(const std::vector<double>& x, const std::vector<double>& y,
                                                ResonanceKind* kind = nullptr, double min_prominence = 0.05);

// Mittlerer Abstand benachbarter Resonanzen; 0, wenn es weniger als zwei gibt.
double mean_spacing(const std::vector<Resonance>& resonances);

}  // namespace photonlab::circuit
