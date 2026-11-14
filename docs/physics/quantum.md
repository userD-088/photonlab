# Quanten-Modus und MZI-Gitter (Phase 11)

Code: `libs/quantum` (`fock.*`, `experiments.*`, `mesh.*`), Tests `tests/test_quantum.cpp`, CLI `quantum`, `mesh`,
`run <datei> --unitary`, Oberfläche: Arbeitsbereich „Quanten-Modus“.

## Modell

Ein lineares Interferometer mit m Moden ist eine Matrix U (Spalte = Eingang, Zeile = Ausgang; unitär ohne Verlust,
sonst sub-unitär). Fock-Zustand = Besetzung je Mode. Für n Photonen (Eingang s, Ausgang t, Listen der Moden je Photon)

    ⟨t|U|s⟩ = per(U[t, s]) / sqrt(∏ s_i! ∏ t_j!)        P = |⟨t|U|s⟩|²   (ununterscheidbar)
    P_dist = per(|U[t, s]|²) / (∏ s_i! ∏ t_j!)                         (unterscheidbar: klassische Wege)

Permanente nach Ryser mit Gray-Code (2ⁿ n Schritte). Teilweise Unterscheidbarkeit V: P = V P + (1−V) P_dist –
**für zwei Photonen exakt** (V = |⟨ψ1|ψ2⟩|²), für mehr Photonen eine Näherung mit gleichem paarweisem Überlapp.
Verlust: sub-unitäres U, die Summe der Wahrscheinlichkeiten ist dann die Wahrscheinlichkeit, dass kein Photon fehlt.

Detektoren: Effizienz η (jedes Photon unabhängig, binomial), Dunkelzählung p_dark je Detektor und Fenster,
Schwellwert- oder photonenzahlauflösend. Quellen: ideale Einzelphotonen; Mehrphotonen-Anteil p2 je Quelle als
Mischung mit |2⟩ (angekündigte Quellen aus spontaner Vierwellenmischung). Zufallsmatrizen Haar-verteilt (QR einer
komplexen Gauß-Matrix mit Phasenkorrektur, Mezzadri 2007), Zufall über `standard_normal`.

## Referenz-Experimente

| Experiment | Erwartung | Ergebnis |
|---|---|---|
| Hong-Ou-Mandel, 50:50 | Koinzidenz 0, Sichtbarkeit 1 | 0 / 1 (exakt) |
| HOM, Überlapp V | Sichtbarkeit V | V (exakt); Strahlteiler R: Koinzidenz (1−2R)², bei R = 1/3: 1/9, Sichtbarkeit 0,8 |
| HOM mit Effizienz η | Koinzidenzen × η², Sichtbarkeit gleich | ja; Mehrphotonen und Dunkelzählungen senken die Sichtbarkeit |
| Linear-optisches CNOT (Ralph u. a. 2002, O'Brien u. a. 2003) | Erfolg 1/9 je Eingang, CNOT-Tabelle | 1/9 und Tabelle exakt; logische Amplituden = −1/3 · CNOT |
| Bell-Zustand (Kontrolle in Überlagerung) | Φ+ | Güte 1 |
| Boson Sampling 3 Photonen / 6 Moden | Stichprobe folgt den Permanenten | Abstand (totale Variation) 0,015 bei 40 000 Stichproben; unterscheidbare Photonen 0,40 |
| Schaltung des Chip-Designers als U (Laser → Detektoren) | Koppler = Strahlteiler | HOM-Bündelung am 50:50-Koppler; das Interferenz-XOR-Beispiel bündelt zwei Photonen |

**CNOT-Aufbau** (Moden c0, c1, t0, t1, zwei leere Hilfsmoden): Hadamard (50:50) auf das Ziel; c1 und t+ treffen auf
R = 1/3; c0 und t− werden mit R = 1/3 gegen leere Moden abgeschwächt (t− mit gleichem Vorzeichen wie t+); Hadamard
auf das Ziel; fester Phasenschieber π auf c1 (ohne ihn ist es CNOT · Z auf der Kontrolle). Nachselektion: je ein
Photon in (c0, c1) und (t0, t1). Mit Ununterscheidbarkeit 0,9 sinkt die Wahrheitstabellen-Güte auf 0,92.

## MZI-Gitter (Clements)

MZI auf (m, m+1): T(θ, φ) = [[e^{iφ} cos θ, −sin θ], [e^{iφ} sin θ, cos θ]]. Zerlegung nach Clements u. a., *Optica* 3,
1460 (2016): abwechselnd Elemente von rechts (T⁻¹ auf Spalten) und von links (T auf Zeilen) nullen, bis eine
Diagonale D bleibt; die linken T⁻¹ werden durch D geschoben (T⁻¹ D = D′ T′). Ergebnis: N(N−1)/2 MZIs in einem
rechteckigen Gitter der Tiefe N plus Ausgangsphasen. Rekonstruktion bis 8 × 8: Fehler < 10⁻¹¹ (Haar, Fourier,
Permutation).

**Physikalisches MZI** (für das Fehlermodell): fester Phasenschieber π auf dem unteren Eingang, äußere Phase φ,
Koppler, innere Phase ±θ′ symmetrisch, Koppler, feste −π/2 auf beiden Ausgängen; mit 50:50-Kopplern und
θ′ = π/2 − θ ist das genau T(θ, φ) (Test: Abweichung < 10⁻¹¹). Fehler (reproduzierbar gezogen): Teilungsverhältnis
0,5 + σ_k·z je Koppler, fester Phasenversatz σ_φ·z, Verlust je MZI, thermisches Übersprechen (Anteil der inneren
Phase der Nachbarn derselben Spalte). Güte F = |Tr(U†V)|² / (N Tr(V†V)) (Verlust herausgerechnet; die Wege laufen
durch verschieden viele MZIs, daher senkt gleicher Verlust je MZI die Güte leicht).

**Kalibrierung:** Koordinatensuche über alle θ, φ und Ausgangsphasen mit kleiner werdender Schrittweite, unter der
Annahme, dass das gefertigte Gitter bekannt (gemessen) ist. Ergebnis 6 × 6: Phasenfehler 0,1 rad: 0,926 → 0,99999;
Kopplerfehler 3 %: 0,983 → 0,9996 (ungleiche Koppler begrenzen die erreichbare Güte).

**Beliebige Matrizen** über die Singulärwertzerlegung W = U Σ Vᵀ: zwei Gitter und Dämpfungsglieder σ_i/σ_max,
Skalierung σ_max elektronisch. **Netz-Demo:** 3 Klassen in 4 Merkmalen (Gauß-Wolken, 180 Proben), erste Schicht
h = |W1 x|² optisch (Photodioden lesen die Intensität des komplexen Feldes), zweite Schicht elektronisch, Training mit
Gradientenabstieg. Mit Phasenfehlern 0,15 rad und Kopplerfehlern 2 %: Genauigkeit ideal 100 %, gefertigt 76 %,
kalibriert 100 % (Standardwerte im CLI: 96 % → 100 %). Der Datensatz ist synthetisch und leicht; die Demo zeigt den
Einfluss der Fehler, nicht die Leistungsfähigkeit eines echten Beschleunigers.

## Grenzen

* Mehr als zwei teilweise unterscheidbare Photonen nur genähert; keine spektralen Freiheitsgrade, keine Zeitauflösung.
* Exakt nur für kleine Photonenzahlen (Permanenten wachsen mit 2ⁿ n); bei 30 Photonen bricht die Rechnung ab.
* Kalibrierung setzt ein bekanntes Fehlermodell voraus (in der Praxis: gemessene Matrix); keine Messstatistik.
* Hexagonale, programmierbare Gitter („photonisches FPGA“) und Reck-Zerlegung fehlen noch.
