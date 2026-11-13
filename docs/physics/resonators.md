# Ringresonatoren, Filter und Spektrum-Auswertung (Phase 2)

Code: `libs/circuit/src/builtin_components.cpp`, `libs/circuit/src/analysis.cpp` ·
Tests: `tests/test_circuit_resonators.cpp` · Bericht: `docs/validation/ring.csv`

Konventionen wie in [`circuit_solver.md`](circuit_solver.md): Koppler `[[t, iκ], [iκ, t]]` mit
`t = √(1−K)`, `κ = √K`; Wellenleiter-Phase `φ = 2π·n_eff(λ)·L/λ` mit linearer Dispersion.

## Umlauf im Ring

Umfang `L = 2πR`, Umlauf-Amplitude `a = 10^(−α·L/20)` (α in dB pro Länge), Umlaufphase `φ`.
Abkürzung `H = a·e^{iφ}` für einen ganzen und `h = √a·e^{iφ/2}` für einen halben Umlauf.

## All-Pass-Ring (ein Bus)

    τ = (t − H) / (1 − t·H)
    T = |τ|² = (a² − 2ta·cos φ + t²) / (1 − 2ta·cos φ + t²a²)

- Resonanz bei `φ = 2π·m`. Dort `T_min = (a − t)² / (1 − ta)²`.
- Kritische Kopplung `t = a`: `T_min = 0`. Für `t < a` ist der Ring überkoppelt, für `t > a` unterkoppelt.
- Freier Spektralbereich: `FSR = λ₁·λ₂/(n_g·L) ≈ λ²/(n_g·L)` (exakt im Modell, weil φ linear in 1/λ ist).
- Halbwertsbreite (Lorentz-Näherung für hohe Finesse): `Δλ = λ²·(1 − ta) / (π·n_g·L·√(ta))`.
- Güte `Q = λ/Δλ`, Finesse `F = FSR/Δλ`.

Beide Ringe haben eine **Abstimmphase** θ: eine Zusatzphase pro Umlauf, `H = a·e^{i(φ+θ)}`. Sie steht
für einen Heizer oder Plasma-Modulator am Ring; ein Treibermodell (Spannung, Leistung, Bandbreite)
gibt es noch nicht.

## Add-Drop-Ring (zwei Busse)

Koppler 1 am Eingangsbus (`t₁, κ₁`), Koppler 2 am zweiten Bus (`t₂, κ₂`), dazwischen je ein halber Umlauf:

    in → through:  (t₁ − t₂·H) / (1 − t₁t₂·H)
    in → drop:     −κ₁κ₂·h    / (1 − t₁t₂·H)
    add → drop:    (t₂ − t₁·H) / (1 − t₁t₂·H)
    add → through: −κ₁κ₂·h    / (1 − t₁t₂·H)

Die Matrix ist reziprok und ohne Verlust unitär. Bei gleichen Kopplern und ohne Verlust geht in
Resonanz die gesamte Leistung in den Drop-Port. Für die Halbwertsbreite gilt die Formel des
All-Pass-Rings mit `t → t₁t₂`.

Beide Kompaktmodelle sind identisch (Abweichung < 1e-11) mit der ausgeschriebenen Schaltung aus
Richtkopplern und Wellenleiter-Schleifen, die der Schaltungslöser über die Rückkopplung löst.

## Weitere Bauteile

| Bauteil | Modell |
|---|---|
| Y-Splitter (1x2-MMI) | `S(out1,in) = S(out2,in) = 1/√2`, reziprok. Als Vereiniger kommt nur der gleichphasige Anteil an, der Rest strahlt ab: nicht unitär. |
| Bandpass | Butterworth-Prototyp der Ordnung n bei `x = 2(λ − λ_c)/B`: `H = Π (−p_k)/(ix − p_k)`, `p_k = e^{iπ(2k+n−1)/(2n)}`; `|H|² = 1/(1 + x^{2n})`. Ordnung 1 ist die Lorentz-Form. |
| Gitterkoppler | `T = T_max·exp(−4·ln2·((λ − λ_c)/B)²)`, B = volle 3-dB-Bandbreite. |

## Vereinfachungen

- Koppler der Ringe sind wellenlängenunabhängig; keine Rückstreuung (kein Aufspalten der Resonanz),
  keine Biegeverluste außer dem eingestellten Verlust, keine thermischen oder nichtlinearen Effekte.
- Der Bandpass absorbiert gesperrtes Licht; ein realer Filter reflektiert es oder leitet es in einen
  anderen Port.
- Der Gitterkoppler hat keine Phase, keine Rückreflexion und keine Winkelabhängigkeit. Die
  Standardwerte (4 dB, 60 nm) geben nur die Größenordnung üblicher SOI-Gitterkoppler wieder
  (Chrostowski/Hochberg, *Silicon Photonics Design*, Kap. 5).

## Auswertung von Spektren

`find_resonances` sucht lokale Extrema, die mindestens 5 % der Gesamtspanne tief sind, verfeinert das
Extremum parabolisch durch drei Punkte und bestimmt die Halbwertsbreite aus den linear interpolierten
Schnittpunkten mit dem halben Pegel zwischen Extremum und Bezugspegel (dem höheren der beiden
Nachbarmaxima). Extinktion = `10·log10(Bezugspegel/Extremwert)`.

Die Genauigkeit hängt von der Abtastung ab: unter etwa 10 Punkten pro Halbwertsbreite werden Breite
und Extinktion spürbar ungenau; GUI und CLI weisen darauf hin.

## Validierung

| Fall | Erwartung | Ergebnis |
|---|---|---|
| All-Pass-Ring gegen Koppler + Schleife und gegen `T(φ)` | identisch | < 1e-11 |
| Add-Drop-Ring gegen zwei Koppler + zwei Halbringe | identisch | < 1e-11 |
| Ring-FSR (0,5 pm Abtastung) | `λ₁λ₂/(n_g·L)` | relativ 3e-9 |
| Ring-Halbwertsbreite und Güte | Lorentz-Näherung | relativ 5e-5 |
| Ring-Extinktion | `(1 − ta)²/(a − t)²` | 3e-4 dB |
| Kritische Kopplung | `T_min = 0` | < 1e-12 |
| Bandpass | `1/(1 + x^{2n})`, 3 dB an den Bandkanten | relativ 1e-10 |
| 10.000-Punkte-Sweep | < 1 s | etwa 0,01 s (ein Thread) |
