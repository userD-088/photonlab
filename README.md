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

**Status:** Phase 0 – Fundament.

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
Der erste Build lädt SDL3, Dear ImGui und doctest automatisch herunter und dauert einige Minuten.

Programme:
- `build/apps/photonlab_gui/(Release/)photonlab_gui` – Oberfläche
- `build/apps/photonlab_cli/(Release/)photonlab_cli --info` – Kommandozeile

## Mit Claude Code arbeiten

`CLAUDE.md` enthält Architektur, Regeln und Build-Befehle und wird von Claude Code automatisch gelesen.
Aufgaben stehen in `docs/ISSUES.md`; immer ein Issue pro Sitzung bzw. Branch.

## Lizenz

Noch festzulegen (Vorschlag: MIT).
