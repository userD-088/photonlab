# PhotonLab

Realistischer Photonik-Simulator in C++20 mit SDL3: vom elektromagnetischen Feld eines Bauteils bis zum
photonischen Chip, der rechnet.

| Ebene | Arbeitsbereich | Inhalt |
|---|---|---|
| Feld | Bauteil-Labor | 2D-FDTD (auch auf der Grafikkarte), 3D-Rechnung der Szene, Moden, Bandstrukturen, inverses Design; fertige Bauteile gehen als S-Matrix in den Chip-Designer |
| Schaltung | Chip-Designer | S-Matrizen, Ringe, Interferometer, Heizer, MZI-Schalter, Photodioden, nichtlineare Bauteile; Fenster „Logik“ mit Wahrheitstabelle und Zeugnis |

Einen interaktiven Vorgeschmack gibt [`docs/prototypes/photonische_gatter.html`](docs/prototypes/photonische_gatter.html) (im Browser öffnen).

**Status:** Schwerpunkt ist jetzt, den Simulator praktisch nutzbar zu machen. Beispiele im Chip-Designer:
Halb-, Voll-, 2-, 4- und 8-Bit-Addierer aus thermo-optischen MZI-Schaltern (Directed Logic, baubar in einem
Standard-Siliziumprozess). Offene Punkte: [`docs/ISSUES.md`](docs/ISSUES.md).

## Voraussetzungen (Windows)

1. **Visual Studio 2022 Build Tools** mit der Workload „Desktopentwicklung mit C++“
2. **CMake** ab 3.24 (ist in den Build Tools enthalten oder von cmake.org)
3. **Git** (git-scm.com)
4. **VS Code** mit den empfohlenen Erweiterungen (VS Code schlägt sie beim Öffnen vor)

Grafikkarte (optional): Das 2D-FDTD rechnet über OpenCL auf der Grafikkarte, wenn der Grafiktreiber OpenCL mitbringt (AMD Adrenalin, NVIDIA, Intel). Nichts zusätzlich zu installieren; ohne passenden Treiber rechnet alles auf dem Prozessor. Umschalten unter Ansicht > Grafikkarte, in der Kommandozeile mit `--gpu`.

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
- `build/apps/photonlab_cli/(Release/)photonlab_cli` – Kommandozeile (`photonlab_cli bench` misst die Rechengeschwindigkeit auf dem eigenen Rechner)

Im Programm erklärt **Hilfe** (F1) alle Bereiche, erste Schritte und Tastenkürzel.

## Bauteil-Labor bedienen

Beim Start liegt eine leere Szene bereit (Oxid-Mantel, Hz, 1 ps). Beispiele unter Datei → Beispiele: Feld.

| Aktion | Bedienung |
|---|---|
| Rechnung | Werkzeugleiste: Start/Pause (Leertaste), Zurücksetzen (R); Tempo und Laufzeit links unter „Rechnung“ |
| 2D oder 3D | Umschalter in der Werkzeugleiste; 3D rechnet die Szene als Schicht (220 nm Si auf Oxid) |
| Neues Objekt | Knöpfe „+ Rechteck“ … „+ Port“ oder Umschalt+Buchstabe; „Bausteine …“ für fertige Gruppen |
| Auswählen, verschieben | im Bild klicken und ziehen; Strg/Umschalt für mehrere; Pfeiltasten |
| Rückgängig | Pfeile in der Werkzeugleiste, Strg+Z / Strg+Y |
| Titel | Feld ganz links in der Werkzeugleiste (wird auch der Name des Bauteils) |

Reiter: **Feld** (bauen und rechnen), **Bauteil für den Chip-Designer** (Ports automatisch setzen, S-Matrix
messen, mit einem Klick in die Schaltung), **Logik prüfen** (Wahrheitstabellen der Feld-Gatter),
**Moden und Bänder** (Modenlöser, Bandstruktur), **Inverses Design**.

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
Wellenlängen-Sweep, „Kennlinie“ zeigt die Detektorleistungen über einem Parameter des gewählten Bauteils,
„Monte-Carlo“ streut Parameter und zeigt die Ausbeute. Das Fenster **Logik** legt Eingänge (Heizer, Laser …)
und Ausgänge (Detektoren) fest: Bits und Zahlen von Hand schalten, das Licht auf der Zeichenfläche folgt;
bis 6 Eingänge zusätzlich Wahrheitstabelle und Zeugnis mit Ampeln.

Addierer-Beispiele (Datei → Beispiele: Schaltung): Halb-, Voll-, 2-, 4- und 8-Bit-Addierer aus
MZI-Schaltern mit Heizern, Gitterkoppler und Photodioden. Die Summanden sind Heizleistungen, der Übertrag
läuft als Licht auf zwei Schienen; der 8-Bit-Addierer rechnet z. B. 23 + 42 = 65 und prüft das Ergebnis.

## Kommandozeile

```
photonlab_cli components                                  # Bauteiltypen, Ports, Parameter
photonlab_cli run examples/02_mach_zehnder.plab.json      # Messwerte bei der Arbeitswellenlänge
photonlab_cli run examples/02b_mach_zehnder_unbalanciert.plab.json --sweep --csv spektrum.csv
photonlab_cli run examples/03b_ring_adddrop.plab.json --resonances    # FSR, Güte, Extinktion
photonlab_cli run examples/03_ring_allpass.plab.json --monte-carlo    # Streuung und Statistik
photonlab_cli run examples/05_interferenz_xor.plab.json --logic       # Wahrheitstabelle und Zeugnis
photonlab_cli run examples/01_wellenleiter_fdtd.plab.json --field     # 2D-FDTD über die Laufzeit der Szene
photonlab_cli run examples/04c_richtkoppler_fdtd.plab.json --field3d  # dieselbe Szene als Si-Schicht in 3D
photonlab_cli run examples/31_addierer_8bit_chip.plab.json            # 8-Bit-Addierer (Eingänge 0)
photonlab_cli run examples/02_mach_zehnder.plab.json --modes          # Moden des Querschnitts der Datei
photonlab_cli materials 1310                              # Brechzahlen der Materialbibliothek bei 1310 nm
photonlab_cli run examples/04c_richtkoppler_fdtd.plab.json --sparam --out koppler.sparam.json
photonlab_cli validate --out docs/validation              # Validierung gegen analytische Lösungen
```

## Mit Claude Code arbeiten

`CLAUDE.md` enthält Architektur, Regeln und Build-Befehle und wird von Claude Code automatisch gelesen.
Aufgaben stehen in `docs/ISSUES.md`; immer ein Issue pro Sitzung bzw. Branch.

## Lizenz

Noch festzulegen (Vorschlag: MIT).
