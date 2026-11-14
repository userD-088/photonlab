# Bandstruktur photonischer Kristalle (PWE)

Code: `libs/pwe`. Tests: `tests/test_pwe.cpp`, Validierung: `photonlab_cli validate`.

## Gleichungen

Unendlich ausgedehnte, in z homogene 2D-Struktur mit periodischer Permittivität ε(r) (Gitter a1, a2). Die
Maxwell-Gleichungen zerfallen in zwei Sätze (Bloch-Ansatz, Wellenvektor k in der Ebene):

| Polarisation | Feld | Gleichung |
|---|---|---|
| `ez` ("TM" bei Joannopoulos) | E = E_z(r) ẑ | −∇²E = (ω/c)² ε E |
| `hz` ("TE" bei Joannopoulos) | H = H_z(r) ẑ | ∇·(ε⁻¹ ∇H) = −(ω/c)² H |

Entwicklung nach ebenen Wellen exp(i(k+G)·r) mit reziproken Gittervektoren G, |G| ≤ Abschneidegrenze:

* `ez`: |k+G|² E_G = ν² Σ_G' ε(G−G') E_G' (verallgemeinertes hermitesches Problem, ε-Matrix positiv definit)
* `hz`: Σ_G' (k+G)·(k+G') η(G−G') H_G' = ν² H_G, mit η = **Inverse der ε-Matrix** (Laurent-/Inverse-Regel nach
  Ho, Chan, Soukoulis 1990). Die Fourier-Reihe von 1/ε würde bei Sprüngen langsamer konvergieren.

Einheiten: Längen in a, k und G in 2π/a, Frequenz ν = ωa/(2πc) = a/λ. Die Frequenz gilt damit für jede
Gitterkonstante; die Betriebswellenlänge folgt aus a = λ·ν_Mitte der Lücke.

## Fourier-Koeffizienten

Kreis (Mittelpunkt r_c, Radius R, Differenz Δε zum Hintergrund), q = 2π|G|:
ε(G) = [ε_b A δ_G0 + Δε · 2πR² J₁(qR)/(qR) · e^(−i q·r_c)] / A   (A = Zellfläche; q→0: πR²).
Rechteck (achsenparallel, w × h): Δε · w h · sinc(q_x w/2) sinc(q_y h/2) · e^(−i q·r_c) / A.
Formen dürfen sich nicht überlappen (Beiträge werden addiert).

## Brillouin-Zonen-Pfad

Dreiecksgitter (a2 unter 60°): Γ = 0, M = (b1+b2)/2 (|M| = 1/(√3 a)), K = (2b1+b2)/3 (|K| = 2/(3a)).
Quadratgitter: Γ, X = b1/2, M = (b1+b2)/2. Lücken werden am Pfad bestimmt (Band n < Band n+1 an allen
Punkten). Das ist für die üblichen Gitter exakt die vollständige Lücke, weil die Extrema der Bänder auf dem
Rand der irreduziblen Zone liegen; bei Superzellen mit niedriger Symmetrie ist es nur eine Schätzung.

## Prüfung

* Leeres Gitter (ε uniform): ν = |k+G|/√ε, exakt (1e-9) für beide Polarisationen und beide Gitter.
* 1D-Bragg-Stapel (Rechtecke, schmale Zelle): Frequenzen erfüllen cos(2πk) = cos p1 cos p2 − ½(n1/n2+n2/n1) sin p1 sin p2.
* Konvergenz mit der Abschneidegrenze (Dreiecksgitter, hz): Unterschied 6 → 9 unter 0,5 % bzw. 1 %.
* Literatur (nach Joannopoulos u. a., *Photonic Crystals*, Kap. 5; Zahlen **aus dem Gedächtnis**, Quelle war hier
  nicht abrufbar, daher grobe Toleranz 3-4 %): Quadratgitter Stäbe ε = 8,9, r = 0,2a: TM-Lücke ≈ 0,32 … 0,44;
  Dreiecksgitter Luftlöcher r = 0,48a in ε = 13: vollständige Lücke ≈ 0,43 … 0,52 (≈ 19 %).
  Eigenes Ergebnis (Abschneidegrenze 7–8): 0,430 … 0,521; Quadratgitter 0,322 … 0,443.
  **Offen:** Vergleich gegen MPB mit exakt gleichen Parametern (Issue #52) und gegen den eigenen FDTD
  (Transmission durch einen endlichen Kristall).

## Grenzen

* 2D, unendlich hoch: keine Abstrahlung nach oben/unten. Echte Platten-Kristalle (Si 220 nm) haben Lichtkegel
  und etwas andere Lücken; die Brechzahl für 2D-Rechnungen ist deshalb eine effektive Brechzahl (siehe `modes.md`).
* Keine Materialdispersion, ε ist ein fester Wert.
* Dichte Matrizen: bis ca. 3000 ebene Wellen praktikabel (Elementarzelle: Sekunden; Superzelle für Liniendefekte
  braucht eine kleine Abschneidegrenze).
