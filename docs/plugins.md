# Bauteil-Plugins (Phase 13)

Eigene Bauteile für den Chip-Designer, ohne PhotonLab neu zu bauen: eine gemeinsame Bibliothek (.dll, .so,
.dylib), die eine C-Funktion exportiert. Schnittstelle: `libs/circuit/include/photonlab/circuit/plugin_api.h`
(reines C, Version 1). Vorlage: `plugins/example/example_plugin.cpp` (Bragg-Gitter, 1x2-MMI), wird mitgebaut
nach `build/plugins/`.

## Ein Plugin schreiben

```cpp
#include "photonlab/circuit/plugin_api.h"

static int my_s(double wavelength, const double* params, double* s_re, double* s_im) {
  // n*n Werte zeilenweise füllen: S[i][j] = s_re[i*n+j] + i*s_im[i*n+j]; 0 = in Ordnung
  return 0;
}
static const char* const ports[] = {"in", "out"};
static const plab_param params[] = {{"length", "Länge", 10e-6, 1e-6, 1e-3, "µm", 1e-6}};
static const plab_component components[] = {
    {"mein_bauteil", "Mein Bauteil", "Ein Satz zum Modell", 2, ports, 1, 1, params, my_s}};
static const plab_plugin plugin = {PHOTONLAB_PLUGIN_API_VERSION, "Mein Plugin", "1.0", "Beschreibung", 1,
                                   components};
extern "C" PHOTONLAB_PLUGIN_EXPORT const plab_plugin* photonlab_plugin(void) { return &plugin; }
```

Regeln (wie für eingebaute Bauteile):
- Parameter in SI; `display_unit`/`display_scale` nur für die Anzeige. Anzeigenamen nur mit Latin-1-Zeichen.
- b = S·a mit |a|² = Leistung in Watt; Ports in der Reihenfolge der Zeilen/Spalten von S.
- `s_matrix` muss threadsicher sein (Sweeps und Monte-Carlo rufen parallel) und darf nichts global ändern.
- Alle Zeichenketten und Tabellen statisch: PhotonLab hält Zeiger darauf bis zum Programmende.
- Neue Physik mit einem Test gegen eine bekannte Lösung (siehe `tests/test_plugin.cpp`).

Bauen: eine CMake-Bibliothek vom Typ `MODULE` mit dem Include-Pfad `libs/circuit/include`; nichts sonst
muss gelinkt werden. Das Beispiel steht in `plugins/example/CMakeLists.txt`.

## Laden

Beim Programmstart, in dieser Reihenfolge:
1. alle .dll/.so/.dylib im Ordner `plugins` neben dem Programm,
2. Umgebungsvariable `PHOTONLAB_PLUGINS` (Dateien oder Ordner, getrennt durch `;` unter Windows, sonst `:`),
3. Option `--plugin <datei>` (Oberfläche und Kommandozeile; bei der Kommandozeile hinter dem Befehl).

Beispiel: `photonlab_cli plugins --plugin build/plugins/photonlab_example_plugin.so`.
Die Oberfläche zeigt geladene Plugins unter Hilfe → Plugins. Die Bauteile erscheinen in der Bauteilbibliothek
und verhalten sich wie eingebaute: Löser, Sweeps, Monte-Carlo, Logik-Labor, Projektdatei. Eine Projektdatei mit
Plugin-Bauteilen lässt sich ohne das Plugin nicht öffnen (unbekannter Bauteiltyp).

Prüfungen beim Laden: Einstiegsfunktion vorhanden, Schnittstellen-Version, Namen nicht leer oder doppelt (auch
nicht gleich einem eingebauten Typ), Standardwerte im Wertebereich, Streumatrix bei 1550 nm endlich. Ist sie
nicht passiv (größter Singulärwert > 1), gibt es eine Warnung. Bei einem Fehler wird kein Bauteil des Plugins
angemeldet.

## Beispiel: Bragg-Gitter

Gleichmäßiges Gitter nach Koppelmodentheorie (Erdogan, J. Lightwave Technol. 15, 1277 (1997)):
δ = 2π n_eff(λ)/λ − π/Λ, s = √(κ² − δ²),
r = −κ sinh(sL) / (δ sinh(sL) + i s cosh(sL)), t = i s / (δ sinh(sL) + i s cosh(sL)), mit sinh(z)/z
geschrieben, damit die Bandkante s = 0 keine Division durch 0 ergibt. Getestet: Spitzenreflexion tanh²(κL)
auf 1e-12, erste Nullstellen bei δL = ±√(π² + (κL)²), S unitär und reziprok von 1500 bis 1600 nm.

## Grenzen

- Nur lineare Bauteile (keine Zustände wie `ring_kerr`, keine Quellen, keine Messwerte, keine Handlungen);
  Version 2 der Schnittstelle könnte das ergänzen (#119).
- Plugins werden nur beim Start geladen und nie entladen. Ein Plugin läuft mit allen Rechten des Programms:
  nur Plugins aus vertrauenswürdigen Quellen laden.
