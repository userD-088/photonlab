# Startaufgaben als GitHub-Issues

Jede Zeile wird ein Issue. Claude Code bekommt immer nur ein Issue auf einmal.

## Phase 0 – Fundament
- [ ] #1 Projekt baut lokal unter Windows (VS Code + CMake Tools), GUI-Fenster öffnet sich
- [x] #2 CI auf GitHub ist grün (Windows + Linux)
- [x] #3 Job-System: Simulation im Threadpool mit Fortschrittsbalken in der GUI (Wellenlängen-Sweep im Fenster „Spektrum“)
- [x] #4 ImPlot einbinden und im Chip-Designer die MZI-Kennlinie plotten (Fenster „Kennlinie“)

## Phase 1 – Schaltung MVP
- [x] #5 `libs/circuit`: Port, Component-Interface, Component-Registry
- [x] #6 Bauteile: Wellenleiter, Richtkoppler, Phasenschieber, Laser, Detektor
- [x] #7 Netzliste + linearer Frequenzlöser (I − S·C)·b = S·a_ext mit Eigen
- [x] #8 Validierungstest MZI auf 1e-12, Energieerhaltung
- [x] #9 Raster-Editor im Chip-Designer (Bauteile platzieren, Ports verbinden)
- [x] #10 Speichern/Laden als `.plab.json`

Offen aus Phase 1 (Folge-Issues):
- [ ] #11 Chip-Designer: Rückgängig/Wiederholen
- [ ] #12 Chip-Designer: Bauteile drehen/spiegeln, Verbindungen per Klick auswählen
- [ ] #13 Validierungsbericht als CI-Artefakt hochladen (`photonlab_cli validate --out`)

## Phase 2 – Schaltung erweitert
- [x] #14 Ringresonator (All-Pass, Add-Drop) als Bauteil; FSR, Güte, Extinktion gegen Formeln
- [x] #15 Y-Splitter (1x2-MMI), Bandpassfilter, Gitterkoppler
- [x] #16 Sweep-Leistung: 10.000 Punkte < 1 s als Test abgesichert (dichter Löser reicht dafür)
- [x] #17 Monte-Carlo über Parameterstreuung mit Ausbeute (GUI-Fenster, CLI, Projektdatei)
- [x] #18 Phase/Leistung im Editor: Zahlenwerte an den Verbindungen einblendbar („Werte“)
- [x] #19 Spektrum auswerten: Resonanzen, FSR, Güte, Extinktion (GUI-Tabelle, `run --resonances`)

Offen aus Phase 2 (Folge-Issues):
- [ ] #20 Dünnbesetzter Löser für große Schaltungen (erst nötig ab einigen hundert Ports)
- [ ] #21 Monte-Carlo über das ganze Spektrum (z. B. Streuung der Resonanzlage) statt nur einer Wellenlänge
- [ ] #22 Wellenlängenabhängige Koppler (Koppellänge aus Supermoden)

## Phase 3 – Logik-Labor (linear)
- [x] #23 `libs/logic`: Codierungen (Intensität, Phase, elektrisch), Wahrheitstabellen-Läufer auf Schaltungs-Ebene
- [x] #24 Schwellwert-Analyse und Kontrastverhältnis CR = 10·log10(P1_min / P0_max)
- [x] #25 Gatter-Zeugnis mit Ampeln (Kriterien aus Bauplan 7.8), Kaskaden-Test
- [x] #26 Referenz-Design Interferenz-XOR/XNOR; Abnahme: CR > 20 dB im Idealfall, fehlende Regeneration wird markiert
- [x] #27 Ring-Directed-Logic (elektrisch abgestimmte Add-Drop-Ringe, XOR/XNOR)
- [x] #28 Logik-Labor in der GUI: Gatter-Definition, Wahrheitstabelle mit Messwerten, Zeugnis
- [x] #29 Optimierer: Parameter auf bestes Kontrastverhältnis abstimmen
- [x] #30 Export des charakterisierten Gatters als `.gate.json`

Offen aus Phase 3 (Folge-Issues):
- [ ] #31 Wellenlängen-Codierung (Mehrkanal-Löser, Bauplan 5.5) und Polarisations-Codierung
- [ ] #32 Toleranz gegen Rauschen als Statistik (Monte-Carlo über die Wahrheitstabelle, Ausbeute des Gatters)
- [ ] #33 Weitere Referenz-Gatter: NOT/OR mit Bias-Strahl, Directed-Logic AND/OR
- [ ] #34 Logik-Labor: eigenes Fensterlayout je Arbeitsbereich (Platz für Tabelle und Zeugnis)

## Phase 4 – Feld: 2D-FDTD
- [ ] #35 `libs/field/fdtd`: Yee-Gitter 2D, TE und TM getrennt, Leapfrog, Courant-Prüfung
- [ ] #36 CPML-Ränder; Abnahme: Reflexion < −40 dB
- [ ] #37 Quellen: Punkt, Gauß-Puls, CW mit einstellbarer Phase; mehrere Quellen
- [ ] #38 Monitore: Schnappschuss, Poynting-Fluss, DFT-Spektrum
- [ ] #39 Validierung: numerische Dispersion des Yee-Schemas, Fresnel-Reflexion, Energieerhaltung
- [ ] #40 Bauteil-Labor in der GUI: Geometrie-Editor, Live-Feld als Farbkarte
