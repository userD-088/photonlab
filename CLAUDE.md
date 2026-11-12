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
**Aktuelle Phase: 0 (Fundament).**

## Build
```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```
Ohne GUI (schneller, z. B. für reine Physik-Arbeit): `-DPHOTONLAB_BUILD_GUI=OFF`

## Struktur
- `libs/core` – Einheiten, Konstanten, komplexe Zahlen, Threadpool, JobControl
- `apps/photonlab_gui` – SDL3 + Dear ImGui (Docking)
- `apps/photonlab_cli` – Kommandozeile, später Batch und Validierung
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
- Code-Stil: `.clang-format`; Funktionen/Variablen snake_case, Klassen PascalCase; Kommentare auf Deutsch.

## Physik-Referenzen
- Courant 2D: dt <= dx / (c * sqrt(2))
- Richtkoppler: S = [[t, iκ], [iκ, t]]
- Schaltungslöser: (I - S*C) b = S a_ext
- Kerr: n = n0 + n2 * I
- Kontrastverhältnis: CR = 10*log10(P1_min / P0_max)
