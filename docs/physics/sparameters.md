# S-Parameter aus dem FDTD (Brücke 1, Phase 6)

Code: `libs/field/src/sparameters.cpp`, `libs/circuit/src/sparameter_table.cpp` ·
Tests: `tests/test_sparameters.cpp` · Bericht: `docs/validation/sparameters.csv` ·
CLI: `photonlab_cli run <datei> --sparam --out <datei.sparam.json>`

## Ablauf

1. Jeder **Port** ist eine achsenparallele Linie quer über einen Wellenleiter. Die Richtung ins
   Bauteil folgt aus der Lage (senkrechte Linie links: +x, rechts: −x; waagerechte oben: +y).
2. Für jeden Port j eine eigene Rechnung: eine **Modenquelle** (siehe [`modes.md`](modes.md)) hinter
   der Portlinie sendet einen Gauß-Puls, dessen Spektrum den Wellenlängenbereich abdeckt
   (Träger in der Mitte des Frequenzbereichs, τ = 2/Δω_halb, Spektrum an den Rändern etwa 1/e).
3. An allen Ports laufende Fourier-Transformation des Skalarfeldes und des Querfeldes entlang der
   Linie, jeweils mit den richtigen Abtastzeiten (das Querfeld liegt einen halben Schritt früher).
4. Die Rechnung endet, wenn die Feldenergie nach dem Puls auf 10⁻⁵ ihres Höchstwerts gefallen ist
   (spätestens nach der Höchstdauer; dann gibt es einen Hinweis).
5. An jedem Port Zerlegung in die einlaufende und die auslaufende Mode; daraus
   `S_ij = b_i·√N_i / (a_j·√N_j)` mit der Leistung N je Amplitude: |S_ij|² ist ein Leistungsverhältnis.
   Die Phase ist auf die Portlinien bezogen.

## Zerlegung in Laufrichtungen auf dem Gitter

Eine Mode, die sich in +x ausbreitet, hat ein festes Verhältnis von Querfeld zu Skalarfeld. Damit die
Zerlegung auf dem Yee-Gitter exakt ist, wird die Portmode **mit der Gitterform** von k₀ und β gerechnet:

    k₀' = 2·sin(ω·Δt/2) / (c₀·Δt),        β' = 2·sin(β_g·Δ/2) / Δ

Die Eigenwertgleichung entlang der Linie ist genau die des Yee-Gitters für eine in Ausbreitungsrichtung
gleichförmige Struktur (Ez: zweite Differenz mit ε an den Knoten; Hz: ε des parallelen und des
senkrechten E-Feldes an ihren eigenen Gitterorten). Mit den gemittelten Feldern an der Linie gilt:

    Ez-Polarisation:  H_quer = s·Y·Ez,          Y  = β'·cos(β_g·Δ/2) / (μ₀·ω')
    Hz-Polarisation:  E_quer = s·(Z₀/ε)·Hz,     Z₀ = β' / (ω'·ε₀·cos(β_g·Δ/2))

mit s = −1 (Ez, senkrechte Linie), +1 (Ez, waagerecht), +1 (Hz, senkrecht), −1 (Hz, waagerecht). Die
Projektion auf das Modenprofil f (Gewicht 1 bei Ez, 1/ε bei Hz) liefert e und h; dann

    a₊ = (e + s·h̃)/2,  a₋ = (e − s·h̃)/2,   h̃ = h/Y bzw. h/Z₀

Leistung je Amplitude: N = Y·Σf²·Δ/2 (Ez) bzw. N = Z₀·Σ(f²/ε)·Δ/2 (Hz).

Ergebnis für einen geraden Wellenleiter: |S21|² = 1 auf 2·10⁻⁴, Reflexion unter −80 dB, Phase genau
2π·n_eff·L/λ mit dem Index auf dem Gitter.

## 3D-Messung (`sparameters3d.cpp`, Bauteil-Designer: Schalter „3D“, CLI `--sparam --3d`)

Die Szene wird im Schichtaufbau `scene.stack` gerechnet (siehe [`fdtd3d.md`](fdtd3d.md)). Je Port:

1. **Rechnung mit dem Bauteil**: Puls mit der Querschnittsmode des Ports (eigener Modenlöser auf dem
   3D-Gitter, quasi-TE bei Hz-Szenen), 0,3 µm hinter der Portlinie.
2. **Referenz mit der geraden Zuleitung**: dieselbe Quelle, die Brechzahlen unter der Portlinie über das ganze
   Gebiet fortgesetzt.
3. **Zerlegung mit zwei Flächen**: Projektion der dominanten E-Komponente auf das Modenprofil an der
   Portfläche (x₀) und an einer zweiten Fläche etwa λ/(4 n_eff) weiter innen;
   `E(x) = A₊·e^{iβ_g(x−x₀)} + A₋·e^{−iβ_g(x−x₀)}` mit `β_g = 2·asin(β·Δ/2)/Δ` und β aus dem Modenlöser
   (n_eff an drei Wellenlängen, quadratisch interpoliert). So wird kein H-Profil gebraucht: das Verhältnis
   H/E einer stark führenden Si-Mode weicht um bis zu 25 % von n_eff/Z₀ ab (erster Versuch: scheinbare
   Reflexion −19 dB).
4. **Normierung**: Nahe der Quelle ist das Feld noch keine reine Mode (die beiden Flächen unterscheiden sich
   um 3 %, die Projektion unterschätzt die Welle um etwa 4 %). Der **Betrag** der einlaufenden Welle kommt
   deshalb vom fernen Ende der verlustfreien Referenz-Zuleitung, die **Phase** von der Portfläche. Ports auf
   der Seite der Quelle (gleiche Lage und Richtung) bekommen den Anteil der Referenz abgezogen (Streulicht,
   scheinbare Reflexion); auf der Gegenseite trägt die Referenz das durchgehende Licht und wird nicht
   abgezogen.

Abstrahlung nach oben und unten fehlt in der Matrix wie ein Verlust - so verhält sich das Bauteil auf dem
Chip. Prüfung (`tests/test_extrude3d.cpp`, Gitter 50 nm): gerader Si-Draht 500 × 220 nm |S21|² = 1,001;
Spalt 150 nm: reziprok auf 10⁻³, S11 = S22, |S11|² = 7–9 %, |S21|² = 62–67 %, Rest abgestrahlt.
Fingerabdruck für den Zwischenspeicher: Szene plus Schichtaufbau (`sparameter_hash_3d`), getrennt von 2D.

## Bauteil in der Schaltung

Die Matrix liegt an den Stützwellenlängen vor. Dazwischen werden Betrag und Phase getrennt linear
interpoliert (Real- und Imaginärteil linear zu interpolieren ließe den Betrag einbrechen, wenn sich die
Phase zwischen zwei Stützstellen stark dreht). Außerhalb des Bereichs meldet das Bauteil einen Fehler,
statt zu extrapolieren. Der Bauteiltyp heißt `sparam:<name>` und hat keine Parameter.

## Cache

Der Fingerabdruck (FNV-1a, 64 Bit) wird über das kanonische JSON der Szene gebildet, ohne Name,
Laufzeit, Quellen, Monitore und Portnamen. Liegt eine Tabelle mit gleichem Fingerabdruck in der
Bibliothek, bietet die Oberfläche sie an, statt neu zu rechnen.

## Grenzen

- Alle Grenzen des 2D-FDTD gelten (siehe [`fdtd.md`](fdtd.md)): keine Abstrahlung nach oben und unten,
  keine Materialdispersion, Treppenstufen.
- Jeder Port bezieht sich auf **eine** Mode. Wandelt das Bauteil Licht in höhere Moden um, fehlt diese
  Leistung in der Matrix (sie erscheint wie Verlust). Für Konsistenz in Schaltungen einmodige Wellenleiter
  verwenden.
- Die Portlinien müssen im gleichförmigen Teil des Wellenleiters liegen und die Mode ganz enthalten;
  zu kurze Linien werden gemeldet (Feld am Linienende > 5 % des Maximums).
- Ports müssen weit genug auseinanderliegen, dass das Feld eines anderen Wellenleiters nicht in die Linie
  reicht.

## Validierung

| Fall | Erwartung | Ergebnis |
|---|---|---|
| Gerader Wellenleiter, Ez und Hz | \|S21\|² = 1, \|S11\|² = 0, Phase nach n_eff | 1e-3, < −60 dB, 5e-3 rad (im Test) |
| Spalt im Wellenleiter | reziprok, passiv | \|S12 − S21\| < 1e-3, Spaltensumme 0,97 |
| **Zwei Spalte: Schaltung aus zwei extrahierten Spalten gegen FDTD des Gesamtaufbaus** | **innerhalb 1 %** | **0,3 % in der Leistung** |
| Richtkoppler mit S-Kurven (vier Ports) | reziprok, passiv | 1e-3, Spaltensumme 0,93–0,97 (Abstrahlung der Kurven) |
