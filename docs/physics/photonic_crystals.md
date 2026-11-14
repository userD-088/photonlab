# Photonische Kristalle: Generatoren, Liniendefekte, Kristall-Gatter (Phase 7)

Code: `libs/field/src/photonic_crystal.cpp` (Generator), `libs/field/src/reference_scenes.cpp` (Szenen),
`libs/logic/src/field_gate.cpp` (Gatter auf Feld-Ebene), `libs/pwe` (Bandstruktur, siehe `pwe.md`).
Tests: `tests/test_photonic_crystal.cpp`, `tests/test_phc_field.cpp`; Validierung: `photonlab_cli validate`.

## Aufbau der Referenz-Szenen

Si-Platte in der Draufsicht als effektive Brechzahl (n = 2,85, Effektiv-Index-Methode der 220-nm-Schicht, siehe
`modes.md`), Löcher und Umgebung SiO2 (n = 1,444), Hz-Polarisation (E in der Ebene, TE-artig),
Gitterkonstante a = 430 nm, Lochradius r = 150 nm = 0,35 a, 20 Zellen je a. PWE für diese Brechzahlen:
Lücke a/λ = 0,261 … 0,310 (17 %), Mitte 0,285 entspricht 1507 nm. Betriebswellenlänge 1580 nm = a/λ 0,272.

Die Lochgitter entstehen aus `CrystalLayout` (Gitter, Radius, Gebiet, entfernte Löcher, Sonderradien);
Gitterrichtungen in 60°-Schritten (Dreieck) bzw. 90° (Quadrat). Defekte sind entfernte Löcher:
`remove_line` (W1 = eine Reihe), `remove_path` (Knick: Teilstücke in zwei Richtungen), `remove_site`
(Punktdefekt), `set_radius` (Kavität, Taper). Alle Orte sind auf Pikometer gerundet (Beispieldateien
unabhängig vom Compiler).

## Messungen (alle aus `photonlab_cli validate` bzw. den Tests)

| Größe | Ergebnis |
|---|---|
| Transmission durch 11 a Kristall ohne Defekt, in der Lücke (1507 nm) gegen im Band (2048 nm) | −73 dB (FDTD stimmt mit der PWE-Lücke überein) |
| gleicher Kristall mit W1-Defekt, 1580 nm | durchlässig; Fluss 7·10⁷-mal größer als in der Lücke ohne Defekt |
| 60°-Knick im W1 bei 1580 nm, Fluss am Ausgang gegen geraden W1 (CW, 2,4 ps) | 0,976 (bei 3,6 ps: 0,999) |
| derselbe Knick bei 1520 nm (a/λ 0,283, oberhalb der Einmoden-Grenze 0,279) | etwa 0,30 |
| 1480 nm / 1560 nm / 1600 nm | 0,36 / 0,81 / 0,87 |

Der Knick führt also nur im Einmoden-Bereich der Defektmode fast verlustfrei; darüber wandelt er in die zweite
Mode um und verliert den größten Teil. Die Zahlen gelten für 2D mit festem n; Streuung nach oben/unten fehlt.

## Gatter (Familie B, Bauplan 7.3)

Y-Kombinierer: drei W1-Arme unter 0°, 120°, 240° treffen sich an einem Gitterpunkt. Eingänge A und B sind
Punktquellen in den Armen, 4 Gitterschritte vom Knoten, Ausgang ist der Flussmonitor 6 a hinter dem Knoten.
Bit 1 = Quelle an, Bit 0 = aus; je Tabellenzeile eine FDTD-Rechnung (Dauerstrich, gemittelt über die letzten 4
Perioden), Zeilen ohne Licht werden nicht gerechnet.

| Gatter | Aufbau | gemessen (voll aufgelöst, 1580 nm) |
|---|---|---|
| ODER | gleichphasig, Schwelle unter dem Pegel 1/2 | Pegel 0 : ¼ : ¼ : 1 (relativ), CR sehr hoch (0-Pegel dunkel) |
| UND | gleichphasig, Schwelle zwischen ¼ und 1 | CR = 6,0 dB (Theorie 6,02 dB) |
| XOR | B um π verschoben | Pegel 0 : 1 : 1 : 0,002, CR = 27 dB |
| NICHT | wie XOR, B dauernd an (Vorspannung) | Pegel 1 : 0,002, CR = 27 dB |
| Halbaddierer | XOR (Summe) + UND (Übertrag), zwei Gatter parallel | wie die Einzelgatter |

Erklärung: Das Y addiert die Felder, Ausgangsleistung |a+b|²/2. Mit gleicher Phase ist der Pegel bei A=B=1 genau
das Vierfache eines einzelnen Eingangs (gemessen 4,00), mit π Versatz löschen sich die Felder aus. **Das UND ist nur
eine Schwelle auf Interferenz; sein Kontrast ist prinzipiell auf 10·log10(4) = 6 dB begrenzt** und wird nach den
Schwellen des Zeugnisses (≥ 20 dB gut, ≥ 10 dB eingeschränkt) mit "nicht erfüllt" bewertet. Ein UND mit gutem
Kontrast braucht Nichtlinearität (Phase 8). Der XOR-Kontrast hängt von der Genauigkeit der Auslöschung ab und
fällt bei schlechterer Gittersymmetrie, Phasenfehlern oder ungleichen Eingangsleistungen ab.

## Grenzen (ehrlich)

* 2D-Modell: keine Abstrahlung nach oben/unten, keine Lichtkegel-Verluste; reale Platten-Kristalle schneiden
  schlechter ab. Dispersionsfreie Brechzahl (feste Zahl bei 1550 nm).
* Der Halbaddierer ist aus zwei getrennten Gattern zusammengesetzt; die Aufteilung jedes Eingangs auf beide
  Gatter (zusätzliche Splitter, 3 dB Verlust) ist nicht mitgerechnet.
* Phasencodierung: die Eingangsquellen sind kohärent und haben feste relative Phase; ein realer Aufbau braucht
  Phasenstabilität über die Zuleitungen.
* Kontrast und Funktion gelten nur bei der gerechneten Wellenlänge; Wellenlängen- und Fertigungstoleranz
  (Monte-Carlo über Lochradius und -lage) sind noch nicht gemessen ("nicht bewertet" im Zeugnis).
* Die Rechenzeit ist hoch: je Gatter-Zeile etwa 40-70 s Einzelkern, die Zeilen laufen parallel.
