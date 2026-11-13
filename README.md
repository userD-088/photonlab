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

**Status:** Phase 6 – Brücke vom Feld zur Schaltung. Im Bauteil-Labor lassen sich die S-Parameter eines
Bauteils aus dem 2D-FDTD messen und als Bauteil in den Chip-Designer übernehmen. Dazu: Modenlöser für
Querschnitte, Schaltungen aus Kompaktmodellen und das Logik-Labor. Der Quanten-Modus fehlt noch.

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

## Bauteil-Labor bedienen

Datei → Beispiele → „Feld: Interferenz zweier Quellen“ laden; das Feld läuft sofort los.

| Aktion | Bedienung |
|---|---|
| Rechnung | Start/Pause, Zurücksetzen, „Schritte je Bild“ für das Tempo |
| Objekt wählen und verschieben | im Bild anklicken und ziehen, oder in der Liste „Objekte“ wählen |
| Objekt ändern | Werte unter „Ausgewählt“ (Lage, Größe, Brechzahl, Wellenlänge, Phase ...) |
| Neues Objekt | Knöpfe „+ Rechteck“, „+ Kreis“, „+ Ring“, „+ Quelle“, „+ Monitor“ |
| Gitter und Ränder | Abschnitt „Rechengebiet“ |

Jede Änderung an der Szene startet die Rechnung neu. Unter „Hinweise“ steht, wenn die Auflösung zu grob ist
oder eine Quelle in der Randschicht liegt. Gerechnet wird in zwei Dimensionen: Abstrahlung nach oben und
unten fehlt, und Brechzahlen haben keine Dispersion.

### Querschnitt und Moden

Im Bauteil-Labor den Reiter „Querschnitt (Moden)“ wählen. Kern- und Mantelmaterial, Breite, Höhe und
Wellenlänge einstellen und „Moden berechnen“ drücken. Die Tabelle zeigt effektiven Index, Gruppenindex,
Einschluss und Modenfläche; das Bild zeigt das Feld der gewählten Mode. Der Knopf „n_eff und n_g in den
Chip-Designer übernehmen“ schreibt die Werte in alle Wellenleiter und Ringe der Schaltung.

Der Löser ist semi-vektoriell: bei stark führenden Silizium-Drähten liegt der effektive Index etwa 2 % über
vollvektoriellen Literaturwerten.

### S-Parameter: vom Bauteil zum Chip

1. Datei → Beispiele → „Feld: Richtkoppler mit S-Kurven“ laden. Die violetten Linien sind die Ports; der
   Pfeil zeigt ins Bauteil. Eigene Ports legt „+ Port“ im Reiter „Feld“ an.
2. Reiter „S-Parameter“ → „S-Parameter extrahieren“. Je Port läuft eine Rechnung (parallel); der Fortschritt
   steht daneben.
3. Ergebnis prüfen (Reziprozität und Passivität stehen über dem Plot), dann „In die Bauteilbibliothek
   übernehmen“. Im Chip-Designer erscheint das Bauteil in der Bauteilbibliothek und lässt sich wie jedes
   andere verschalten.

Die Bibliothek wird mit dem Projekt gespeichert; einzelne Bauteile lassen sich als `.sparam.json` speichern
und laden. Hat sich die Szene nicht geändert, bietet PhotonLab das vorhandene Ergebnis an (Cache).

## Logik-Labor bedienen

Datei → Beispiele → „Interferenz-XOR/XNOR“ laden und den Reiter „Logik-Labor“ öffnen.

- **Gatter-Definition**: Ein Eingang schaltet einen Parameter zwischen zwei Werten um (z. B. die Phase
  eines Lasers zwischen 0 und π), ein Ausgang ist die Leistung an einem Detektor.
- **Wahrheitstabelle**: PhotonLab simuliert alle Kombinationen und zeigt Leistung, entschiedenes Bit und
  Entscheidungspegel. Falsche Zeilen sind rot markiert.
- **Zeugnis**: Ampel je Kriterium (Kontrast, Kaskadierbarkeit, Fan-out, Pegelregeneration, Bias-Toleranz
  usw.) mit Messwert und Begründung. Der Kaskaden-Test schaltet zwei Gatter wirklich hintereinander.
- **Optimierer**: stimmt einen Parameter (z. B. die Bias-Phase) auf den besten Kontrast ab.

Die drei Beispiele zeigen drei typische Befunde: das Interferenz-XOR hat perfekten Kontrast, ist aber
nicht kaskadierbar; das lineare AND hat nur 6 dB Kontrast und scheitert im Kaskaden-Test; Directed Logic
mit Ringen ist realistisch, aber schmalbandig.

## Kommandozeile

```
photonlab_cli components                                  # Bauteiltypen, Ports, Parameter
photonlab_cli run examples/02_mach_zehnder.plab.json      # Messwerte bei der Arbeitswellenlänge
photonlab_cli run examples/02b_mach_zehnder_unbalanciert.plab.json --sweep --csv spektrum.csv
photonlab_cli run examples/03b_ring_adddrop.plab.json --resonances    # FSR, Güte, Extinktion
photonlab_cli run examples/03_ring_allpass.plab.json --monte-carlo    # Streuung und Statistik
photonlab_cli run examples/05_interferenz_xor.plab.json --logic       # Wahrheitstabelle und Zeugnis
photonlab_cli run examples/01_wellenleiter_fdtd.plab.json --field     # 2D-FDTD über die Laufzeit der Szene
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
