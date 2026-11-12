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
- [ ] #14 Ringresonator (All-Pass, Add-Drop) als Bauteil; FSR, Güte, Extinktion gegen Formeln
- [ ] #15 Y-Splitter/MMI, Bandpassfilter, Gitterkoppler
- [ ] #16 Sweep-Leistung: 10.000 Punkte < 1 s als Test absichern (dünnbesetzter Löser bei großen Schaltungen)
- [ ] #17 Monte-Carlo über Fertigungsstreuung
- [ ] #18 Phase/Leistung im Editor: Zahlenwerte an den Verbindungen einblendbar
