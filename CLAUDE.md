# PhotonLab – Kontext für Claude

## Projekt
Photonik-Simulator in C++20 mit drei Ebenen + Quanten-Modus:
- Feld (Bauteil-Labor): Modenlöser, FDTD (linear + nichtlinear), PWE-Bandstruktur
- Schaltung (Chip-Designer): S-Matrizen, nichtlineare Kompaktmodelle
- Logik (Logik-Labor): Wahrheitstabellen, Gatter-Zeugnis, Event-Simulation
Brücken: S-Parameter-Extraktion (Feld → Schaltung), Gatter-Charakterisierung (Schaltung → Logik).
Ziel: physikalisch realistisch und ehrlich bewertet.

Bauplan: `docs/PhotonLab_Bauplan_v2.pdf` (Roadmap = Abschnitt 15).
Interaktiver Prototyp (JS, gleiche Physik): `docs/prototypes/photonische_gatter.html`.
Offene Aufgaben: `docs/ISSUES.md`.
**Aktuelle Phase: 5 (Modenlöser + Materialien).** Umgesetzt: Phase 1 und 2 (Schaltung), Phase 3 (Logik-Labor, linear), Phase 4 (2D-FDTD).

## Build
```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```
Ohne GUI (schneller, z. B. für reine Physik-Arbeit): `-DPHOTONLAB_BUILD_GUI=OFF`

## Struktur
- `libs/core` – Einheiten, Konstanten, komplexe Zahlen, Threadpool, JobControl, `parallel_for`
- `libs/circuit` – Component-Interface und -Registry, Netzliste, linearer Löser, Sweep, Resonanz-Auswertung, Monte-Carlo, Referenz-Designs
- `libs/field` – Feld-Szene (Formen, Quellen, Monitore), 2D-FDTD nach Yee mit CPML, Spektrum; hängt nur von `libs/core` ab
- `libs/logic` – Gatter-Definition, Wahrheitstabellen-Läufer, Schwellwert-Analyse, Gatter-Zeugnis, Optimierer
- `libs/io` – Projektdatei `.plab.json` und charakterisiertes Gatter `.gate.json` (alle Zahlen in SI)
- `apps/photonlab_gui` – SDL3 + Dear ImGui (Docking) + ImPlot; Bauteil-Labor in `field_lab.cpp`, Chip-Designer in `chip_designer.cpp`, Logik-Labor in `logic_lab.cpp`
- `apps/photonlab_cli` – Kommandozeile: `components`, `example`, `run [--sweep | --resonances | --monte-carlo | --logic | --field] [--csv]`, `validate`
- `examples` – Referenz-Designs als `.plab.json` (erzeugt mit `photonlab_cli example <name>`)
- `tests` – doctest; Physik-Validierung gegen analytische Lösungen
- `cmake/Dependencies.cmake` – alle externen Abhängigkeiten, Versionen gepinnt

## Regeln
- `libs/` bindet NIE SDL oder ImGui ein. Grafik nur in `apps/`.
- Intern nur SI. Nicht-SI-Variablen mit Einheit im Namen (`length_um`).
- Komplexe Zahlen: `photonlab::cplx` (= `std::complex<double>`), |a|² = Leistung in Watt.
- Neue Physik nur mit Test gegen eine bekannte Lösung.
- Materialkonstanten mit Quellenangabe im Kommentar.
- Bauteile über eine Component-Registry, nie Sonderfälle im Löser.
- Keine neuen Abhängigkeiten ohne Rückfrage; Versionen in `cmake/Dependencies.cmake` pinnen.
- Kleine Änderungen; ein Issue pro Branch.
- Nach jeder Änderung: bauen, alle Tests ausführen, Ergebnis berichten.
- Physik-Notiz in `docs/physics/` ergänzen, wenn Gleichungen dazukommen.
- Texte der Oberfläche und Anzeigenamen in der Registry nur mit Latin-1-Zeichen (kein Griechisch, keine Pfeile): die eingebaute ImGui-Schrift kennt nichts anderes.
- Neues Bauteil = neue Klasse in `libs/circuit/src/builtin_components.cpp` + Eintrag in `register_builtin_components`; Editor, CLI und Dateiformat übernehmen es automatisch.
- Lange Rechnungen als Job: `parallel_for` mit `JobControl` (Fortschritt, Abbruch); Zufall nur über `standard_normal` (plattformunabhängig reproduzierbar), nie `<random>`-Verteilungen.
- Bewertungen (Ampeln im Zeugnis) immer messen, nie annehmen; Schwellen und Messvorschrift in `docs/physics/logic.md` dokumentieren und als Festlegung kennzeichnen.
- Feld-Ebene: Polarisationen heißen nach ihrer Feldkomponente (`ez`, `hz`), nie TE/TM ohne Erklärung; Orte bei Hz in Zellmitten angeben (Rundung an Zellkanten).
- GUI-Jobs, die laufend Bilder liefern (FDTD): Löser, Steuerung und Schnappschuss per `shared_ptr` an den Job geben; der Job greift nie auf Objekte der Oberfläche zu.
- Code-Stil: `.clang-format`; Funktionen/Variablen snake_case, Klassen PascalCase; Kommentare auf Deutsch.

## Physik-Referenzen
- Courant 2D: dt <= dx / (c * sqrt(2)); Yee-Dispersion: sin²(w*dt/2) = S²*(sin²(kx*dx/2) + sin²(ky*dx/2)) (Details: `docs/physics/fdtd.md`)
- Richtkoppler: S = [[t, iκ], [iκ, t]]
- Schaltungslöser: (I - S*C) b = S a_ext + b_src (Details: `docs/physics/circuit_solver.md`)
- Wellenleiter: n_eff(lambda) = n_eff0 + (n_eff0 - n_g) * (lambda - lambda0) / lambda0
- Ring (All-Pass): tau = (t - H)/(1 - t*H), H = a*exp(i*phi); FSR = lambda²/(n_g*L) (Details: `docs/physics/resonators.md`)
- Kerr: n = n0 + n2 * I
- Kontrastverhältnis: CR = 10*log10(P1_min / P0_max)
