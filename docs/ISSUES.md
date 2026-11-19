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
- [x] #68 Geometrie-Generatoren: Dreiecks- und Quadratgitter, Liniendefekte (gerade, 60°-Knick, Y), Punktdefekte (Oberfläche: Kristall mit W1/60°-Knick per Knopf in die Szene; Y-Verzweigung und Punktdefekte nur über Beispiele bzw. Code)
- [x] #69 PhC-Gatter (NOT, AND, OR, XOR) und Halbaddierer im FDTD; Wahrheitstabelle automatisch auf Feld-Ebene
- [x] #70 Bandstruktur-Ansicht im Bauteil-Labor (Reiter „Bandstruktur“: Banddiagramm, Lücken in nm, W1-Superzelle, Kristall in die Szene einsetzen; Feld-Gatter im Logik-Labor)
- [ ] #71 Fertigungstoleranz der Kristall-Gatter: Monte-Carlo über Lochradius und -lage, Wellenlängen-Sweep des Kontrasts (Zeugnis-Kriterium "Fertigungstoleranz" ist noch "nicht bewertet")
- [ ] #72 Halbaddierer mit gemeinsamer Geometrie (Splitter für die Eingänge, 3 dB Verlust mitgerechnet) statt zweier getrennter Gatter
- [ ] #73 Oberfläche: Y-Verzweigung, T-Verzweigung und Punktdefekt-Kavität im Generator; Liste der Löcher im Objektbaum gruppieren
- [ ] #74 PWE-Ergebnisse gegen MPB mit identischen Parametern vergleichen (Literaturzahlen der Tests stammen aus dem Gedächtnis; siehe #52)
- [ ] #75 Platten-Kristall: 3D-Effekte (Lichtkegel, Abstrahlungsverlust) abschätzen, z. B. über die Effektiv-Index-Methode mit Verlustabschätzung

## Phase 8 – Nichtlineare Optik
- [x] #76 Kompaktmodell Kerr/TPA/freie Ladungsträger (Coupled-Mode), stationäre Zustände, Bistabilität, Hysterese (`kerr_cavity`)
- [x] #77 Kerr-Effekt im FDTD (instantan, ε hängt von |E|² ab) mit Test gegen die analytische Phasenverschiebung (nur Ez; Hz offen, #82)
- [x] #78 TPA und freie Ladungsträger im FDTD (Ratengleichung je Zelle; Silizium-Konstanten)
- [x] #79 Nichtlinearer Ring als Bauteil der Schaltung (Selbstkonsistenz/Kontinuation im Löser, `ring_kerr`)
- [x] #80 Abnahme: FDTD und Kompaktmodell stimmen bei der Resonanzverschiebung überein (2,6 % bzw. 6 %); Hysterese im Feld reproduziert (Bragg-Resonator, nicht Ring)
- [x] #81 Thermischer Effekt (stationär im Ring) und SOA-Modell (stationäre Sättigung); thermische Zeitkonstante und SOA-Dynamik offen (#84)
- [x] #82 Kerr/TPA im FDTD auch für die Hz-Polarisation (E in der Ebene, |E|² aus Ex und Ey)
- [ ] #83 Nichtlinearer Ring im FDTD (ring in `field_ring` mit Kerr) und Vergleich mit dem Kompaktmodell des Rings statt des Bragg-Resonators
- [ ] #84 Dynamik in der Schaltung: thermische Zeitkonstante, SOA-Verstärkungsdynamik, freie Ladungsträger im Ring-Bauteil (Zeitbereich, Phase 9)

## Phase 9 – Aktive Photonik
- [x] #85 Heizer-Phasenschieber (P_pi kalibriert auf 24,8 mW, Harris 2014)
- [x] #86 PN-Phasenschieber und Ring-Modulator (Soref-Bennett, Verarmungszone, V_pi L = 2,6 V cm, RC-Grenzfrequenz)
- [x] #87 Photodiode mit Schrot- und thermischem Rauschen, SNR
- [x] #88 Zeitbereich der Strecke: PRBS, Treiber, Ring (Coupled-Mode) und MZM, Empfänger, Augendiagramm, Q, BER (`libs/signal`)
- [x] #89 PCM-Speicherzelle (GST): Pulsmodell, Programmieren mit Nachlesen, Zwischenstufen; Handlungen im Chip-Designer
- [ ] #90 Zeitbereich für beliebige Schaltungen (Vector Fitting der S-Parameter, Bauplan 5.3)
- [ ] #91 PN-Überlapp mit dem Modenlöser statt gleichmäßiger Verteilung; Wanderwellen-Elektrode für lange MZM
- [ ] #92 Thermische Zeitkonstante und Übersprechen der Heizer; Laser-RIN-Spektrum statt weiß

## Phase 10 – Logik-Ebene
- [x] #93 Brücke 2: Logikblöcke mit gemessenen Pegeln, Codierung, Laufzeit, Energie (ideal, regeneriert, passiv aus Schaltungen)
- [x] #94 Ereignisgesteuerter Simulator mit Physik und idealer Logik je Netz; erste Fehlerursache mit Pegel und Schwelle
- [x] #95 Pegel- und Codierungs-Prüfung je Verbindung (Fan-out, Regeneration)
- [x] #96 Halbaddierer, Volladdierer, 4-Bit-Addierer, UND-Kette, SR-Flip-Flop (Kerr-Ring); Abnahme: 512/512 regeneriert, passiv nachvollziehbar gemeldet
- [ ] #97 Schieberegister und Takt (Flip-Flops + Verzögerungsleitungen), Multiplexer/Router aus Ring-Weichen
- [ ] #98 Logikblöcke direkt aus dem Zeugnis eines eigenen Gatters im Logik-Labor übernehmen (bisher: Referenz-Gatter)
- [ ] #99 Rauschen und Phasenstabilität auf der Logik-Ebene; WDM-Kanäle unterscheiden

## Phase 11 – Quanten + MZI-Gitter
- [x] #100 Fock-Simulator mit Permanenten (Ryser), Unterscheidbarkeit, Verluste, Detektoren (Effizienz, Dunkelzählung, Schwellwert/zahlauflösend), Quellen mit Mehrphotonen-Anteil
- [x] #101 Hong-Ou-Mandel (Sichtbarkeit 1), linear-optisches CNOT (1/9), Bell-Zustand, Boson Sampling
- [x] #102 Übertragungsmatrix einer Schaltung des Chip-Designers (Laser -> Detektoren) für den Quanten-Modus
- [x] #103 Clements-Zerlegung, physikalisches MZI mit Fehlern, Kalibrierung, SVD-Gitter, Netz-Demo
- [x] #104 Arbeitsbereich Quanten-Modus in der Oberfläche
- [ ] #105 Exakte Behandlung teilweise unterscheidbarer Photonen (n > 2, Gram-Matrix), spektrale Moden
- [ ] #106 Hexagonale programmierbare Gitter, Reck-Zerlegung, Kalibrierung aus Messdaten (Statistik)

## Phase 12 – Leistung + 3D
- [x] #107 2D-FDTD: Randschicht nur am Rand, Puffertausch, Threads je Zeitschritt (StepTeam), Vektorisierung; bitgleich zum alten Kern und für jede Thread-Zahl; Abnahme 5,5x (Ez) bzw. 7,0x (Hz) auf 4 Kernen
- [x] #108 3D-FDTD (Yee, CPML in den Schichten, Quader, Threads) mit Tests: Hohlraum-Dispersion exakt, Randschicht -94 dB
- [x] #109 Abnahme 3D-Wellenleiter = Modenlöser: Schicht 0,05 % zur analytischen Lösung, Si-Draht 0,2 % zum vollvektoriellen Literaturwert
- [x] #110 `photonlab_cli bench` (mit Referenzkern von Phase 11), `photonlab_cli fdtd3d`, Reiter 3D-Wellenleiter, Option PHOTONLAB_NATIVE
- [ ] #111 GPU-Backend (SDL3-GPU-Compute in apps/ oder CUDA/OpenCL als neue Abhängigkeit – Rückfrage)
- [ ] #112 Zeitliches Blocken und float-Variante des FDTD (weniger Speicherverkehr, ändert Ergebnisse im letzten Bit)
- [ ] #113 3D: Dispersion, Verluste, Nichtlinearität; geneigte und gekrümmte Formen mit Mittelung (Subpixel)
- [ ] #114 3D-Szenen im Editor und im Dateiformat (.plab.json), Feldbilder live
- [ ] #115 3D: Monitore, Fluss, S-Parameter-Extraktion (Brücke 1 in 3D), varFDTD/2,5D

## Phase 13 – Kür
- [x] #116 GDSII-Export (libs/layout): Polygone aus Feld-Szenen und Pixelbildern, Stream nach Formatbeschreibung, Leser für Tests; CLI `gds`, Menü Datei
- [x] #117 Inverses Design mit der Adjoint-Methode (2D, Ez): Gradient = finite Differenzen auf 0,4 %, 90-Grad-Knick binär 0,995; CLI `inverse-design`, Reiter, GDSII- und Szenen-Übergabe
- [x] #118 Plugin-Schnittstelle (C, Version 1) für lineare Bauteile, Beispiel-Plugin Bragg-Gitter/MMI, Laden beim Start, Fenster Hilfe → Plugins
- [ ] #119 Plugin-Schnittstelle Version 2: nichtlineare Bauteile (Zustand), Quellen, Messwerte, Handlungen; Feld-Plugins
- [ ] #120 GDSII: Hierarchie, Texte, Pfade; Import; Verschmelzen überlappender Formen; Gegentest mit KLayout
- [ ] #121 Inverses Design: Mindeststrukturgröße als Nebenbedingung, MMA/Adam, weitere Aufgaben (Teiler, Modenwandler, WDM)
- [ ] #122 Inverses Design mit Hz (TE-artig) und im 3D-FDTD

## Phase 14 – Rückmeldung zur Oberfläche (Nov. 2026)
Ziel: 2D-Simulator ausbauen, 3D später gleich bedienbar; Chip-Designer, Logik-Labor, Querschnitt, Bandstruktur und S-Parameter bewerten und straffen.

Bauteil-Labor, Feld (2D-FDTD):
- [x] #123 Objekte: Mehrfachauswahl (Strg/Umschalt+Klick, Rechteck aufziehen), Rechtsklick-Menü (Deaktivieren, Duplizieren, Kopieren, Ausschneiden, Einfügen, nach vorn/hinten, Löschen), Strg+A/C/X/V/D, Entf, Esc, H; gemeinsames Verschieben per Ziehen oder Versatz
- [x] #124 Deaktivierte Objekte (`enabled` in Form/Quelle/Monitor): bleiben in der Szene und der Datei, wirken nicht auf FDTD, Brechzahl und GDSII (`field::active_scene`)
- [x] #125 Ansicht: Mausrad-Zoom um den Mauszeiger, Verschieben mit mittlerer Maustaste, Einpassen (F)
- [x] #126 Seitenleiste neu geordnet: Rechnung (mit Laufzeit) - Neues Objekt - Ausgewählt - Objektliste - Rechengebiet
- [x] #127 Gruppen (leeres Objekt mit Lage und Drehung, verschachtelbar): `FieldScene::groups`, `transform_group`; Objektliste als Baum, Strg+G gruppieren, Gruppe ziehen/drehen nimmt Mitglieder mit
- [ ] #128 Rückgängig/Wiederholen im Szenen-Editor (Szene als Schnappschuss je Änderung; siehe #11, #44)
- [ ] #129 Ladebildschirm/Fortschritt beim Öffnen großer Projekte und beim Aufsetzen des Lösers (Modenquellen, große Gitter) als Job
- [ ] #130 Weitere Materialien in `libs/materials` (Ge, InP, GaAs, LiNbO3, Al2O3, Polymere, Luft/Metalle mit Drude-Modell) - jede Zahl mit Quelle; Dispersion im FDTD (Lorentz/Drude) als eigenes Issue
- [x] #131a Formen Taper und Bogen (Ringausschnitt), auch im GDSII-Export
- [ ] #131 Weitere Formen: Polygon, Ellipse; boolesche Operationen (siehe #44)
- [ ] #132 Größere Gebiete: Speicher- und Zeitschätzung vor dem Start anzeigen; float-Kern (#112) halbiert den Speicher. Grenze heute 200 x 200 µm, sinnvoll mit 4 Kernen etwa 50 x 50 µm bei 40 nm Zellen
- [ ] #133 Quellen: Pulsfolgen (Bitmuster), breitbandige Quelle (Chirp, mehrere Wellenlängen), Gauß-Strahl unter Winkel
- [ ] #134 Monitor-Plots: Schwellen für 0 und 1 einzeichnen und Bits auswerten (Brücke zum Logik-Labor), Intensität über Zeit gemittelt, Export als CSV
- [ ] #135 Leistung: GPU-Backend (#111, braucht Rückfrage wegen Abhängigkeit), float-Kern/Zeitblöcke (#112); Mehrkern läuft schon (Phase 12)

Bewertung der Reiter (Festlegung nach Rückmeldung, umsetzen je als eigenes Issue):
- [ ] #136 Querschnitt (Moden): behalten, aber als Werkzeug zum Bauteil-Entwurf erklären - liefert n_eff und n_g eines Wellenleiter-Querschnitts (Breite, Höhe, Material) und übergibt sie an den Chip-Designer; Hilfeseite + Knopf "Werte übernehmen" sichtbarer
- [ ] #137 Bandstruktur: behalten als Entwurfswerkzeug für photonische Kristalle (wo ist die Bandlücke -> welche Wellenlängen sperrt das Gitter); Ergebnis direkt als Kristall-Szene ins Feld legen; Hilfeseite
- [ ] #138 S-Parameter: Brücke Feld -> Schaltung (ein im Feld gerechnetes Bauteil wird zum Bauteil im Chip-Designer); heute schwer zu finden. In den Feld-Reiter integrieren ("Als Bauteil exportieren"), eigener Reiter entfällt
- [ ] #139 Chip-Designer: Zweck in der Oberfläche erklären (viele Bauteile auf einem Chip schnell rechnen, ohne Feld); Tooltips mit Erklärung je Bauteil beim Verweilen; Kennlinie (Ausgang über einem Parameter) und Monte-Carlo (Fertigungsstreuung -> Ausbeute) mit Hilfetext
- [ ] #140 Logik-Labor: zusammenlegen - Schaltungs-Gatter und Feld-Gatter (Kristall) als zwei Ebenen desselben Gatters zeigen; Hilfeseite mit Ablauf Gatter wählen -> Wahrheitstabelle messen -> Zeugnis
- [ ] #141 Quanten-Modus: enthält HOM, CNOT, Boson Sampling, MZI-Gitter, Schaltung als Interferometer; Rückmeldung "leer" klären (Bildschirmfoto), Einstieg erklären

Obere Leiste, Bedienung:
- [x] #149 Seitenleiste: alle Bereiche einklappbar, Hinweise eingeklappt (schwere bleiben sichtbar); Leertaste = Start/Pause, R = Zurücksetzen; ein Tempo-Regler statt Schritte je Bild + volle Geschwindigkeit; Energie als Erklärung im Tooltip
- [x] #151 Neue Objekte landen in der gewählten Gruppe; Objekte in der Liste in Gruppen ziehen; Kürzel für alle Operationen (Rücktaste, Umschalt+Buchstabe für neue Objekte, Pfeiltasten, Q/E drehen, Bild auf/ab, +/-), Übersicht "Tastenkürzel"; Objektliste mit fester Höhe vor den Eigenschaften (springt nicht mehr)
- [x] #150 Objektliste: Strg+Klick einzeln, Umschalt+Klick Bereich; Flussdiagramm über eine Periode gemittelt (abschaltbar), Legende außerhalb
- [x] #142 Datei: Beispiele getrennt nach Feld/Schaltung; Strg+N, Strg+O, Strg+S, Strg+Umschalt+S global; Ansicht: Vollbild (F11)
- [x] #143 Beispiele: Feld-Szenen Taper, 90-Grad-Bogen, Bragg-Spiegel, Linse; Datei > Beispiele: Logik öffnet Halb-/Volladdierer, 4-Bit-Addierer, Flip-Flop und die Kristall-Gatter (inkl. Halbaddierer) im Logik-Labor
- [x] #143b Halbaddierer als ein Chip auf Feld-Ebene (Y-Teiler, XOR mit Phasenschieber, UND; Modenmonitore): Summe 22 dB, Übertrag 5,9 dB; Volladdierer aus drei Chips mit Regeneration (`FieldGateLink`), alle 8 Zeilen richtig
- [x] #143d Kristall-Halbaddierer als ein Chip (Summe 7,8 dB, Übertrag 5,5 dB) und Kristall-Volladdierer aus drei Chips mit Regeneration (alle 8 Zeilen richtig); Kristall-Löcher als Gruppe "Kristall" (auch aus der Bandstruktur)
- [ ] #143e Kristall-Halbaddierer: tiefere Auslöschung der Summe (Verzweigungen weiter weg von der Bandkante entwerfen, inverses Design der Y-Verzweigung)
- [ ] #143c Volladdierer ohne elektrische Regeneration (Kerr-Schwellwert im FDTD) und Halbaddierer-UND mit mehr als 6 dB (nichtlinear)
- [ ] #144 Hilfe-Menü je Arbeitsbereich (Feld, Schaltung, Logik, Quanten) mit Bildern; ImGui-Demo nur noch für Entwickler
- [ ] #145 Einstellungen-Fenster (Schriftgröße, Farbschema, Threads, Sprache) und Speichern der Einstellungen
- [ ] #146 Sprachen Deutsch/Englisch: alle Oberflächentexte über eine Übersetzungstabelle (Latin-1 bleibt Pflicht)
- [ ] #147 Design: Farbschema überarbeiten, Symbole in Werkzeugleisten
- [ ] #148 3D-Simulator mit derselben Bedienung wie 2D (Editor, Objekte, Monitore; baut auf #114, #115 auf)
