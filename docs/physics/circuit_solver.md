# Schaltungslöser (Schaltungs-Ebene, Phase 1)

Code: `libs/circuit` · Tests: `tests/test_circuit_*.cpp` · Bericht: `docs/validation/mzi.csv`

## Größen und Konventionen

- Jeder Port trägt zwei komplexe Wellen: `a` läuft in das Bauteil hinein, `b` heraus.
  `|a|²` und `|b|²` sind Leistungen in Watt, die Amplituden haben also die Einheit √W.
- Zeitkonvention `e^{-iωt}`: eine Welle sammelt über die Länge L die Phase `φ = +2π·n_eff·L/λ`.
- Ein Bauteil mit N Ports liefert pro Wellenlänge eine N×N-Streumatrix `S(λ)` mit `b = S·a`.
- Alle Rechnungen sind monochromatisch und stationär (eine Wellenlänge, eingeschwungener Zustand),
  eine Polarisation und eine Mode pro Port.

## Gleichungssystem

Alle Ports der Schaltung werden durchnummeriert. `S` ist die blockdiagonale Matrix aller Bauteile,
`C` die Verbindungsmatrix: Sind die Ports i und j verbunden, gilt `a_i = b_j` und `a_j = b_i`,
also `C_ij = C_ji = 1`. Mit von außen eingespeisten Wellen `a_ext` (an offenen Ports) und den von
Quellen-Bauteilen abgegebenen Wellen `b_src` gilt

    a = C·b + a_ext
    b = S·a + b_src
    ⇒ (I − S·C)·b = S·a_ext + b_src

Ohne Quellen-Bauteile ist das genau die Form `(I − S·C)·b = S·a_ext` aus dem Bauplan (Abschnitt 5.2).
Rückkopplungen (Ringe, Schleifen) stecken in `S·C` und werden ohne Sonderbehandlung exakt gelöst.

Das System ist dicht und wird per LU-Zerlegung mit Teilpivotisierung gelöst (Eigen). Danach wird das
relative Residuum geprüft; ist es größer als 1e-9 oder die Lösung nicht endlich, meldet der Löser
einen Fehler statt ein Ergebnis zu liefern. Das passiert nur bei einer getriebenen, verlustfreien
Schleife ohne Ausgang – dort wächst die Leistung physikalisch unbeschränkt.

## Kompaktmodelle

| Bauteil | Ports | Modell |
|---|---|---|
| Wellenleiter | in, out | `S21 = S12 = 10^(−α·L/20)·e^{iφ}`, `φ = 2π·n_eff(λ)·L/λ`, α in dB pro Länge |
| Richtkoppler | in1, in2, out1, out2 | `[[t, iκ], [iκ, t]]` von (in1, in2) nach (out1, out2), `t = √(1−K)`, `κ = √K` |
| Phasenschieber | in, out | `S21 = S12 = e^{iφ}` (mal Einfügeverlust) |
| Laser | out | `S = 0`, `b_src = √P·e^{iφ}` |
| Detektor | in | `S = 0`, Photostrom `I = R·|a|² + I_dunkel` |

Dispersion des Wellenleiters, linear um die Bezugswellenlänge λ₀:

    n_eff(λ) = n_eff0 + (n_eff0 − n_g)·(λ − λ₀)/λ₀

Das folgt aus der Definition des Gruppenindex `n_g = n_eff − λ·dn_eff/dλ`. Eingesetzt ergibt sich
`φ(λ) = 2π·L·[n_g/λ + (n_eff0 − n_g)/λ₀]`: Die Phase ist exakt periodisch in 1/λ, der freie
Spektralbereich eines Interferometers mit Längendifferenz ΔL ist `FSR = λ₁·λ₂/(n_g·ΔL) ≈ λ²/(n_g·ΔL)`.

Der Richtkoppler entspricht `t = cos(kL)`, `κ = sin(kL)` mit `K = sin²(kL)`; im Modell ist K direkt der
Parameter. Alle Zweitore und der Koppler sind reziprok (`S = Sᵀ`) und ohne Verlust unitär (`S†S = I`).

Standardwerte (Silizium-Streifenwellenleiter 500 nm × 220 nm, TE0, 1550 nm: `n_eff ≈ 2,44`,
`n_g ≈ 4,2`, Verlust ≈ 2 dB/cm; Ge-Detektor ≈ 1 A/W) nach Chrostowski/Hochberg,
*Silicon Photonics Design*, Cambridge University Press 2015.

## Vereinfachungen (Stand Phase 1)

- Koppler und Phasenschieber sind wellenlängenunabhängig; kein Bauteil reflektiert.
- Offene Ports sind ideal abgeschlossen: Licht verlässt dort die Schaltung, nichts kommt zurück.
- Der Laser ist ideal monochromatisch (keine Linienbreite, kein RIN) und hat einen idealen Isolator.
  Im Wellenlängen-Sweep verhält er sich wie ein durchstimmbarer Laser konstanter Leistung.
- Der Detektor rauscht nicht und hat keine Bandbreite.
- Mehrere Laser sind zueinander vollständig kohärent (gleiche Wellenlänge, feste Phase).

## Validierung

| Fall | Erwartung | Ergebnis |
|---|---|---|
| MZI, Phase 0 … 2π | Ausgänge `sin²(Δφ/2)` und `cos²(Δφ/2)`, Summe = 1 | Abweichung < 1e-15 (Kriterium 1e-12) |
| MZI mit ungleichen Kopplern | Minimum `(t₁κ₂ − κ₁t₂)²` | relativ 1e-12 |
| Unbalanciertes MZI | Spektrum `cos²(Δφ/2)`, `FSR = λ₁λ₂/(n_g·ΔL)` | < 1e-9 |
| All-Pass-Ring aus Koppler und Schleife | `T = (a² − 2ra·cos φ + r²)/(1 − 2ar·cos φ + r²a²)` | < 1e-11 |
| Wellenleiter | Gruppenlaufzeit `dφ/dω = n_g·L/c₀` | relativ 1e-6 (numerische Ableitung) |
| Bauteile | Unitarität ohne Verlust, Reziprozität | 1e-14 |

`photonlab_cli validate --out docs/validation` erzeugt den Bericht `mzi.csv` neu.
