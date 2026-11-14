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
- [x] #35 `libs/field`: Yee-Gitter 2D, Ez- und Hz-Polarisation getrennt, Leapfrog, Courant-Prüfung
- [x] #36 CPML-Ränder; Abnahme: Reflexion < −40 dB (gemessen −90 dB); außerdem periodische Ränder
- [x] #37 Quellen: Punkt, Linie (auch Gauß-Profil), Dauerstrich und Puls mit einstellbarer Phase; mehrere Quellen
- [x] #38 Monitore: Feld am Punkt, Poynting-Fluss durch eine Linie, Spektrum per DFT
- [x] #39 Validierung: numerische Dispersion des Yee-Schemas, Fresnel-Reflexion, Energieerhaltung
- [x] #40 Bauteil-Labor in der GUI: Szene bearbeiten, Live-Feld als Farbkarte, Monitor-Plots
- [x] #34 Eigenes Layout je Arbeitsbereich: die Fenster des Chip-Designers klappen zu, wenn ein anderer vorn liegt

Offen aus Phase 4 (Folge-Issues):
- [ ] #41 Phase als Farbkarte (zyklische Skala) und zeitgemittelte Intensität
- [ ] #42 Transmissions-Spektren: Fluss-Monitore mit laufender DFT, normiert auf einen Referenzlauf
- [ ] #43 Glättung der Permittivität an Materialgrenzen (Subpixel) gegen Treppenstufen
- [ ] #44 Geometrie: Polygone, boolesche Operationen, Rückgängig im Szenen-Editor
- [ ] #45 Mehrere Threads für große Gitter (Phase 12)

## Phase 5 – Modenlöser + Materialien
- [x] #46 `libs/materials`: Sellmeier-Gleichungen für Si, SiO2, Si3N4 mit Quellen, Gruppenindex, dn/dT
- [x] #47 Analytischer Schichtwellenleiter (Eigenwertgleichung) als Referenz und für die Effektiv-Index-Methode
- [x] #48 FDE-Modenlöser (finite Differenzen, semi-vektoriell); Abnahme: Si-Streifen 500 x 220 nm, n_eff TE0 = 2,49 (Literatur ca. 2,44)
- [x] #49 Modenquelle und Modenüberlapp-Monitor im FDTD
- [x] #50 Bauteil-Labor: Querschnitt-Ansicht mit Modenprofilen, Übergabe von n_eff und n_g an den Chip-Designer

Offen aus Phase 5 (Folge-Issues):
- [ ] #51 Vollvektorieller Modenlöser (Ex und Ey gekoppelt), damit stark führende Si-Drähte genauer werden
- [ ] #52 Vergleich mit Meep/MPB als Validierungsbericht (Bauplan, Abnahme Phase 5)
- [ ] #53 Dispersionskurve n_eff(lambda) und Breiten-Sweep als Plot; Zuordnung der Moden über Feldüberlapp
- [ ] #54 Einseitig strahlende Modenquelle (Gesamtfeld/Streufeld) und richtungsgetrennter Modenmonitor
- [ ] #55 Materialdispersion im FDTD (Lorentz-Pole per ADE, Bauplan 4.4)

## Phase 6 – Brücke 1: vom Bauteil zum Chip
- [x] #56 S-Parameter-Extraktion: Breitband-Modenpuls je Port, laufende DFT an allen Ports, Zerlegung in Laufrichtungen
- [x] #57 Dateiformat `.sparam.json` (Tabelle und Szene) und Bauteilbibliothek im Projekt und im Chip-Designer
- [x] #58 Bauteil "S-Parameter" in der Component-Registry (Interpolation von Betrag und Phase über die Wellenlänge)
- [x] #59 Cache über den Fingerabdruck der Szene (ohne Name, Laufzeit, Quellen, Monitore)
- [x] #60 Konsistenztest: zwei extrahierte Spalte in der Schaltung = FDTD des Gesamtaufbaus auf 0,3 % (gefordert 1 %)

Offen aus Phase 6 (Folge-Issues):
- [ ] #61 Parametrisierte Bauteile: Geometrie-Sweep als Tabelle (z. B. Koppellänge), im Chip-Designer interpoliert
- [ ] #62 Mehrere Moden je Port (Modenumwandlung sichtbar machen statt als Verlust zu verbuchen)
- [ ] #63 Referenz-Design: Mach-Zehnder aus zwei extrahierten Kopplern, verglichen mit dem Gesamt-FDTD
- [ ] #64 Kompaktmodell-Parameter für nichtlineare Bauteile aus dem FDTD (Güte, Modenvolumen; Phase 8)
- [ ] #65 Objektliste im Bauteil-Labor: gleichnamige Formen (Segmente einer Kurve) zusammenfassen

## Phase 7 – Photonische Kristalle
- [x] #66 PWE-Löser (Plane Wave Expansion) für 2D-Kristalle, Bandstruktur entlang Gamma-M-K-Gamma
- [x] #67 Abnahme: Bandlücke des Dreiecksgitters stimmt mit der Literatur (Joannopoulos; Zahlen aus dem Gedächtnis, MPB-Vergleich offen, #52)
- [ ] #68 Geometrie-Generatoren: Dreiecks- und Quadratgitter, Liniendefekte (gerade, 60°-Knick, Y), Punktdefekte
- [ ] #69 PhC-Gatter (NOT, AND, OR, XOR) und Halbaddierer im FDTD; Wahrheitstabelle automatisch auf Feld-Ebene
- [ ] #70 Bandstruktur-Ansicht im Bauteil-Labor
