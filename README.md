# PhotonLab

Realistischer Photonik-Simulator in C++20 mit SDL3: vom elektromagnetischen Feld über den photonischen Chip bis zur optischen Logik.

| Ebene | Arbeitsbereich | Inhalt |
|---|---|---|
| Feld | Bauteil-Labor | Modenlöser, FDTD, Bandstrukturen photonischer Kristalle |
| Schaltung | Chip-Designer | S-Matrizen, Ringe, Interferometer, nichtlineare Bauteile |
| Logik | Logik-Labor | Wahrheitstabellen, Gatter-Zeugnis, Addierer, Speicher |
| Quanten | Quanten-Modus | Einzelphotonen, HOM, CNOT, Boson Sampling |

Der vollständige Bauplan steht in [`docs/PhotonLab_Bauplan_v2.pdf`](docs/PhotonLab_Bauplan_v2.pdf).
Einen interaktiven Vorgeschmack gibt [`docs/prototypes/photonische_gatter.html`](docs/prototypes/photonische_gatter.html) (im Browser öffnen).

**Status:** Phase 2 – Schaltung erweitert. Der Chip-Designer rechnet Schaltungen aus Lasern, Wellenleitern,
Kopplern, Phasenschiebern, Ringresonatoren, Y-Splittern, Filtern, Gitterkopplern und Detektoren, wertet
Spektren aus (FSR, Güte, Extinktion) und schätzt per Monte-Carlo die Ausbeute. Bauteil-Labor, Logik-Labor
und Quanten-Modus sind noch Platzhalter.

## Voraussetzungen (Windows)

1. **Visual Studio 2022 Build Tools** mit der Workload „Desktopentwicklung mit C++“
2. **CMake** ab 3.24 (ist in den Build Tools enthalten oder von cmake.org)
3. **Git** (git-scm.com)
4. **VS Code** mit den empfohlenen Erweiterungen (VS Code schlägt sie beim Öffnen vor)

Unter Linux: `gcc`/`clang`, `cmake`, `ninja` und die X11/Wayland-Entwicklungspakete (siehe `.github/workflows/ci.yml`).

## Bauen und starten

In VS Code: Ordner öffnen → unten in der Statusleiste Kit wählen (z. B. „Visual Studio Build Tools 2022 Release – amd64“) → **Build** → **Run**.

Oder im Terminal:
```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```
Der erste Build lädt SDL3, Dear ImGui, ImPlot, Eigen, nlohmann/json und doctest automatisch herunter und dauert einige Minuten.

Programme:
- `build/apps/photonlab_gui/(Release/)photonlab_gui [datei.plab.json]` – Oberfläche
- `build/apps/photonlab_cli/(Release/)photonlab_cli` – Kommandozeile

## Chip-Designer bedienen

| Aktion | Bedienung |
|---|---|
| Bauteil einfügen | Klick in der Bauteilbibliothek oder Rechtsklick auf die Fläche |
| Verbinden | von einem Port zum anderen ziehen |
| Trennen | Rechtsklick auf einen Port |
| Verschieben, Löschen | Bauteil ziehen; `Entf` oder Rechtsklick auf das Bauteil |
| Ansicht | Fläche ziehen verschiebt, Mausrad zoomt, „Alles zeigen“ passt ein |
| Parameter | Bauteil anklicken, Regler im Fenster „Eigenschaften“ |

Die Verbindungen zeigen das Licht: Farbton = Phase, Helligkeit = Leistung. „Spektrum“ rechnet einen
Wellenlängen-Sweep als Job im Threadpool, „Kennlinie“ zeigt die Detektorleistungen über einem Parameter
des ausgewählten Bauteils (beim Start: die cos²-Kennlinie des Mach-Zehnder-Interferometers).
Neben dem Spektrum steht die Tabelle der gefundenen Resonanzen; ein Klick auf eine Zeile legt die
Arbeitswellenlänge dorthin. „Monte-Carlo“ streut Parameter normalverteilt und zeigt Histogramm und
Ausbeute. Das Häkchen „Werte“ blendet Leistung und Phase an jeder Verbindung ein.

## Kommandozeile

```
photonlab_cli components                                  # Bauteiltypen, Ports, Parameter
photonlab_cli run examples/02_mach_zehnder.plab.json      # Messwerte bei der Arbeitswellenlänge
photonlab_cli run examples/02b_mach_zehnder_unbalanciert.plab.json --sweep --csv spektrum.csv
photonlab_cli run examples/03b_ring_adddrop.plab.json --resonances    # FSR, Güte, Extinktion
photonlab_cli run examples/03_ring_allpass.plab.json --monte-carlo    # Streuung und Statistik
photonlab_cli validate --out docs/validation              # Validierung gegen analytische Lösungen
```

## Mit Claude Code arbeiten

`CLAUDE.md` enthält Architektur, Regeln und Build-Befehle und wird von Claude Code automatisch gelesen.
Aufgaben stehen in `docs/ISSUES.md`; immer ein Issue pro Sitzung bzw. Branch.

## Lizenz

Noch festzulegen (Vorschlag: MIT).
