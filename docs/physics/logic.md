# Logik-Labor: Wahrheitstabelle, Kontrast und Gatter-Zeugnis (Phase 3)

Code: `libs/logic` · Tests: `tests/test_logic.cpp` · CLI: `photonlab_cli run <datei> --logic`

Das Logik-Labor rechnet auf der Schaltungs-Ebene: stationär, monochromatisch, linear
(siehe [`circuit_solver.md`](circuit_solver.md)). Alles hier Gesagte gilt für diesen Modellstand.

## Gatter-Definition

- **Eingang**: ein Parameter einer Instanz, der für Bit 0 und Bit 1 zwei Werte annimmt.
  Codierung Intensität (Laserleistung 0 / P), Phase (0 / π bei konstanter Leistung) oder elektrisch
  (Steuergröße eines Modulators, z. B. die Abstimmphase eines Rings).
- **Ausgang**: die optische Leistung an einem Detektor. Der Ausgang ist damit immer intensitätscodiert.
- **Erwartung**: optional die Wahrheitstabelle der gewünschten Funktion.

Die Tabelle hat 2^n Zeilen; der erste Eingang ist das höchstwertige Bit (A B = 00, 01, 10, 11).

## Schwellwert und Kontrast

Für einen Ausgang mit den Leistungen P der Zeilen:

    P1_min = kleinste Leistung aller Zeilen, die 1 sein sollen
    P0_max = größte Leistung aller Zeilen, die 0 sein sollen
    CR     = 10·log10(P1_min / P0_max)          Kontrastverhältnis
    P_th   = (P1_min + P0_max) / 2              Entscheidungspegel
    ER     = 10·log10(P_max / P_min)            Extinktionsverhältnis

Die Logik stimmt, wenn `P1_min > P0_max`. Ohne Erwartung trennt PhotonLab die sortierten Pegel an
der größten Lücke und benennt die Funktion, falls sie zu den bekannten gehört.

## Referenz-Gatter und ihre analytischen Pegel

**Interferenz-XOR/XNOR** (Bauplan Abb. 3). Zwei Laser gleicher Leistung P mit Phasen φ_A, φ_B ∈ {0, π},
Bias π/2 im Arm B, Koppler `[[t, iκ], [iκ, t]]`:

    b_xor  = √P·(t·e^{iφ_A} − κ·e^{iφ_B})        b_xnor = i·√P·(κ·e^{iφ_A} + t·e^{iφ_B})

Für den 50:50-Koppler: `P_xor = 2P·sin²(Δφ/2)`, `P_xnor = 2P·cos²(Δφ/2)`, im Ideal `CR → ∞`.
Mit ungleichem Koppler: `CR = 20·log10((t + κ)/(t − κ))`. Mit Bias-Fehler β statt π/2:
`CR = 10·log10((1 + sin β)/(1 − sin β))`.

**Interferenz-AND** (gleicher Aufbau, Intensitätscodierung, Bias −π/2). Am Ausgang überlagern sich die
Felder gleichphasig: `P = (√P_A + √P_B)²/2`, also die Pegel 0, P/2, P/2, 2P und `CR = 10·log10(4) = 6 dB`.

**Directed Logic** (Bauplan 7.4, Prinzip 1). Zwei Add-Drop-Ringe als Weichen: in Resonanz (Bit 0)
koppelt ein Ring über, um die Abstimmphase π/2 verstimmt (Bit 1) lässt er durch. `A.through → B.in`,
`A.drop → B.add`; an `B.through` erscheint XNOR, an `B.drop` XOR. Der Kontrast ist endlich, weil ein
verstimmter Ring noch etwas überkoppelt (Lorentz-Ausläufer) und sich die Leckpfade kohärent addieren.

## Zeugnis: Messvorschriften

Die Kriterien folgen D. A. B. Miller, *Are optical transistors the logical next step?*, Nature
Photonics 4, 3 (2010). **Die Schwellen der Ampeln sind Festlegungen von PhotonLab, keine Physik.**

| Kriterium | Messung | grün | gelb |
|---|---|---|---|
| Logische Funktion | `P1_min > P0_max` für alle Ausgänge | erfüllt | Funktion nur erkannt und unbekannt |
| Kontrastverhältnis | schlechtestes CR aller Ausgänge | ≥ 20 dB | ≥ 10 dB |
| Extinktionsverhältnis | schlechtestes ER aller Ausgänge | ≥ 20 dB | ≥ 10 dB |
| Kaskadierbarkeit | Kaskaden-Test (unten) | Logik bleibt erhalten | – |
| Fan-out | `P1_min` / Quellleistung je optischem Eingang | ≥ 2 und kaskadierbar | ≥ 2, nicht kaskadierbar |
| Pegelregeneration | Kennlinie (unten) | S-förmig, Pegel wiederhergestellt, gleiche Codierung | rein elektrische Eingänge |
| Isolation | Quellleistung / in die Quellen zurücklaufende Leistung | ≥ 30 dB | ≥ 20 dB |
| Kein kritischer Bias | Toleranz gegen Phasenfehler und Verstimmung (unten) | ≥ 0,3 rad und ≥ 1 nm | ≥ 0,1 rad und ≥ 0,1 nm |
| Pegel unabhängig vom Verlust | Abfall des 1-Pegels pro dB weniger Quellleistung | ≤ 0,1 dB/dB | ≤ 0,5 dB/dB |
| Schaltenergie | – | nicht bewertet | |
| Latenz | Gruppenlaufzeit `dφ/dω` zum Ausgang im 1-Zustand | nicht bewertet | |

**Kaskaden-Test.** PhotonLab baut zwei Kopien der Schaltung, entfernt den Detektor der ersten und die
Quelle eines intensitätscodierten Eingangs der zweiten und verbindet die beiden Stellen. Die
zweistufige Schaltung muss `f(f(A1, B1), B2)` mit dem Entscheidungspegel der Einzelstufe liefern.
Bei phasencodierten oder elektrischen Eingängen ist der Test nicht möglich: der Ausgang liegt als
Lichtleistung vor, die Codierungen passen nicht.

**Kennlinie.** Ein Eingang wird stufenlos von Bit 0 (x = 0) nach Bit 1 (x = 1) gefahren, die übrigen
stehen fest. Die Steigung `|dP/dx|` wird auf den Hub `P1_min − P0_max` normiert. Regeneration verlangt
eine Steigung < 1 an beiden Pegeln (Störungen werden gedämpft) und > 1 dazwischen. Zusätzlich muss
der Ausgang in derselben Codierung vorliegen wie der Eingang, und der 1-Pegel darf der Quellleistung
nicht folgen. Ein lineares Netzwerk erfüllt das nie: seine Ausgangsfelder sind linear in den
Quellfeldern, der 1-Pegel fällt um genau 1 dB pro dB.

Beispiel XOR: `P(x) = 2P·sin²(πx/2)` ist bezüglich der Eingangsphase S-förmig (Steigung 0 an den
Pegeln, π/2 in der Mitte) – trotzdem keine Regeneration, weil der Ausgang keine Phase mehr trägt.
Beispiel AND: `P(x) ∝ (√x + 1)²` hat bei x = 0 unendliche Steigung; kleine Störungen eines dunklen
Eingangs werden verstärkt.

**Bias-Toleranz.** Alle Phasen-Parameter der Schaltung (Einheit rad, auch die der Eingänge) werden
gemeinsam um ±δ verschoben, mit den ungünstigsten Vorzeichen (alle 2^k Kombinationen, ab 7 Parametern
64 feste Zufalls-Kombinationen). Gesucht ist das größte δ eines geometrischen Rasters (Faktor 1,23,
bis 90°), bei dem jede Zeile noch richtig entschieden wird. Entsprechend die größte Verstimmung der
Wellenlänge (1 pm bis 20 nm). Zum Vergleich: ein Silizium-Ring driftet thermisch um etwa
`λ·(dn/dT)/n_g ≈ 1550 nm · 1,8·10⁻⁴ K⁻¹ / 4,2 ≈ 0,07 nm/K` (dn/dT nach Bauplan 4.4).

## Gatter auf Feld-Ebene: Messung und Stufen

* **Ausgangsleistung:** zeitliches Mittel über die letzten 4 Perioden, entweder Poynting-Fluss durch eine
  Linie oder Leistung der geführten Mode (Modenmonitor, Mittel von a²). Der Modenmonitor zählt nur Licht im
  Wellenleiter, wie ein Detektor hinter einem einmodigen Wellenleiter; Streulicht eines Y-Kombinierers im
  gegenphasigen Fall läuft sonst schräg durch die Flusslinie und füllt die logische 0 auf (gemessen beim
  Halbaddierer-Chip: etwa 30 % Untergrund mit Fluss, 0,5 % mit Mode).
* **Einmodig:** Der Kombinierer trennt gleich- und gegenphasige Anteile nur, wenn der Ausgang die
  antisymmetrische Mode nicht führt. Bei Hz (TM-Schicht) gilt das für den Kern mit n = 2,85 in SiO2 erst unter
  etwa 0,32 µm; der Halbaddierer-Chip nutzt 0,30 µm. Mit 0,45 µm addierten sich die Leistungen ohne Interferenz.
* **Stufen (Festlegung):** Passive Interferenz-Gatter lassen sich nicht direkt hintereinanderschalten (Phase
  und Pegel des Ausgangs hängen vom Eingang ab). Ein Feld-Gatter mit mehreren Szenen kann deshalb Verbindungen
  (`FieldGateLink`) haben: das Bit eines früheren Ausgangs, entschieden an dessen Schwelle (mittig zwischen
  schlechtester 0 und 1 dieses Ausgangs), schaltet eine Quelle einer späteren Szene ein. Das entspricht einer
  idealen Regeneration (Empfänger + neuer Sender); deren Kosten (Energie, Zeit) sind nicht bewertet.
* **Volladdierer:** Die Summe dreier Bits geht mit linearer Interferenz an einem Ausgang nicht: Gerade Fälle
  verlangen |u_i + u_j| klein für alle Paare, das erzwingt drei Zeiger im Abstand von 120° und damit
  |u_1 + u_2 + u_3| = 0 für den ungeraden Fall 111. Der Volladdierer besteht daher aus drei Halbaddierer-Chips
  mit Regeneration (S1, C1 -> Summe, C2 -> Übertrag = C1 ODER C2 am gleichphasigen Ausgang).

* **Kristall-Halbaddierer (ein Chip, `field_phc_halfadder`):** W1-Wellenleiter im Dreiecksgitter, nur
  60°-Richtungen; Teiler und Kombinierer sind 3-zählige Y-Verzweigungen, B biegt mit einem 60°-Knick in die
  Waagerechte. Phasenhebel: der untere Arm von A ist einen Gitterschritt länger (Bloch-Phase k·a der W1-Mode).
  Veränderte Lochradien als Phasenschieber änderten auch die Transmission stark (kleine Kavität) und wurden
  verworfen. Abgleich gemessen: B-Phase 0,25 rad, A-Amplitude 1,3 (A verliert auf seinem Weg mehr), 1580 nm,
  4,8 ps (nahe der Bandkante ist das Licht langsam; nach 2,4 ps waren die Pegel noch nicht stationär).
  Ergebnis: Summe 7,8 dB, Übertrag 5,5 dB. Die Auslöschung der Summe bleibt bei etwa 15 % der Einzelpegel,
  unabhängig von der Phase: die Felder von A und B kommen nicht in exakt derselben Verteilung an (nahe der
  Grenze zur zweiten Defektmode); bei 1560 nm interferierte der Übertrag gar nicht mehr. Der Streifen-Chip
  ist deutlich besser (22 dB).

## Was das Zeugnis nicht sagt

- Alles ist stationär: keine Schaltzeiten, keine Bitrate, keine Schaltenergie. Die Latenz ist nur die
  Gruppenlaufzeit des Lichts.
- Kein Rauschen, keine Bitfehlerrate; die Toleranzen sind Worst-Case-Grenzen, keine Statistik
  (dafür gibt es das Monte-Carlo-Fenster im Chip-Designer).
- Die Bauteilmodelle sind reflexionsfrei, die Isolation ist deshalb idealisiert.
- Elektrische Treiber und Detektor-Elektronik sind nicht modelliert; bei Directed Logic entscheidet
  die Elektronik, das optische Zeugnis bewertet nur den Lichtweg.
- Mehrere Laser gelten als perfekt kohärent zueinander. Reale phasencodierte Gatter brauchen eine
  gemeinsame Quelle oder Phasenregelung.

## Validierung

| Fall | Erwartung | Ergebnis |
|---|---|---|
| XOR, ideal | Pegel 0 / 2P, CR > 20 dB | Abweichung < 1e-15, CR > 100 dB |
| XOR, Koppler K = 0,4 | `CR = 20·log10((t+κ)/(t−κ))` | relativ 1e-10 |
| XOR, Bias 0,3 rad | `CR = 10·log10((1+sin β)/(1−sin β))` | relativ 1e-10 |
| XOR-Kennlinie | Steigung π/2 in der Mitte, flach an den Pegeln | relativ 1e-3 |
| AND | Pegel 0, P/2, P/2, 2P; CR = 6,02 dB | relativ 1e-10 |
| AND-Kaskade, Zeile 0 1 1 | `(√(P/2) + √P)²/2 = 1,457 mW` > Schwelle 1,25 mW: falsch | relativ 1e-12 |
| Wellenleiter 1 mm | Latenz `n_g·L/c₀` = 14,0 ps | relativ 1e-6 |
| Linearität | 1-Pegel fällt um 1,00 dB pro dB | relativ 1e-9 |
