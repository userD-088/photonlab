# Monte-Carlo über Parameterstreuung (Phase 2)

Code: `libs/circuit/src/monte_carlo.cpp` · Tests: `tests/test_circuit_monte_carlo.cpp`

## Modell

Jede Stichprobe ist eine Kopie der Schaltung mit gestörten Parametern, gelöst bei der
Arbeitswellenlänge. Eine Streuung gilt für einen Parameter aller Instanzen eines Bauteiltyps:

    p = p_nenn + σ·z            (absolut)
    p = p_nenn·(1 + σ·z)        (relativ)

mit standardnormalverteiltem z. Unabhängig: jede Instanz erhält ein eigenes z (lokale Rauigkeit,
Breitenschwankung). Gemeinsam (korreliert): alle Instanzen erhalten dasselbe z (Schichtdicke,
Temperatur). Werte außerhalb des zulässigen Parameterbereichs werden auf den Rand begrenzt.

Ausbeute = Anteil der Stichproben, deren Messwert eine Grenze einhält; nicht lösbare Stichproben
zählen als Ausfall.

## Zufallszahlen

z ist eine reine Funktion von (Startwert, Stichprobe, Streuung, Instanz): SplitMix64 als Hash,
daraus zwei gleichverteilte Zahlen und die Box-Muller-Transformation. Deshalb ist das Ergebnis
unabhängig von Threadanzahl und Reihenfolge und auf allen Plattformen gleich
(`std::normal_distribution` wäre es nicht).

## Vereinfachungen

- Fertigungsgrößen (Breite, Dicke) werden nicht selbst modelliert; man streut direkt die
  Kompaktmodell-Parameter (z. B. `n_eff`). Der Zusammenhang Breite → `n_eff` kommt erst mit dem
  Modenlöser (Phase 5).
- Nur vollständig unabhängig oder vollständig korreliert, keine räumliche Korrelationslänge.
- Die Streuungen der Beispieldateien sind Beispielwerte, keine Daten eines Fertigungsprozesses.
- Der statistische Fehler eines Mittelwerts aus N Stichproben ist σ/√N.

## Validierung

MZI mit normalverteilter Phase `φ ~ N(φ₀, σ²)` am Kreuz-Ausgang `cos²(φ/2)`:

    E[P]   = (1 + cos φ₀·e^{−σ²/2}) / 2
    Var[P] = ((1 + cos 2φ₀·e^{−2σ²})/2 − cos²φ₀·e^{−σ²}) / 4

Der Test prüft mit 20.000 Stichproben den Mittelwert auf 4 Standardfehler und die Streuung auf 3 %,
außerdem Reproduzierbarkeit, Unabhängigkeit von der Threadanzahl und die Wirkung korrelierter Streuung.
