# Moden von Wellenleitern (Phase 5)

Code: `libs/modes` · Tests: `tests/test_modes.cpp` · Bericht: `docs/validation/modes.csv` ·
CLI: `photonlab_cli run <datei> --modes`

Eine Mode ist eine Feldverteilung, die sich entlang des Wellenleiters nur um einen Phasenfaktor
`e^{iβz}` ändert. `n_eff = β/k₀` ist ihr effektiver Index, `n_g = n_eff − λ·dn_eff/dλ` ihr
Gruppenindex. Geführt ist sie, wenn `n_eff` über dem Index des Mantels liegt.

## Schichtwellenleiter (analytisch)

Kern der Dicke d (Brechzahl n₁) zwischen zwei Halbräumen (n₂ unten, n₃ oben). Mit

    κ = k₀·√(n₁² − n_eff²),   γ₂ = k₀·√(n_eff² − n₂²),   γ₃ = k₀·√(n_eff² − n₃²)

lautet die Eigenwertgleichung der Mode m:

    κ·d = arctan(w₂·γ₂/κ) + arctan(w₃·γ₃/κ) + m·π

mit `w = 1` für TE (E parallel zu den Schichten) und `w = (n₁/n_Mantel)²` für TM. Die linke minus
die rechte Seite fällt monoton mit n_eff; die Nullstelle wird durch Intervallhalbierung bestimmt.
Symmetrische Schicht: Zahl der Moden `⌊V/π⌋ + 1` mit `V = k₀·d·√(n₁² − n₂²)`.

Feld im Kern `cos(κy − φ)` mit `tan φ = w₂γ₂/κ`, außen exponentiell abklingend.

**Effektiv-Index-Methode:** erst die senkrechte Schicht lösen, deren n_eff als Kernindex einer
waagerechten Schicht der Streifenbreite einsetzen (mit der jeweils anderen Polarisation). Grobe
Näherung; für den Si-Streifen 500 × 220 nm liefert sie 2,49.

## Eindimensionales Profil (finite Differenzen)

Für beliebige Brechzahlprofile entlang einer Linie, z. B. unter einer Modenquelle im 2D-FDTD:

    TE:  E″ + k₀²n²·E = β²·E
    TM:  n²·(H′/n²)′ + k₀²n²·H = β²·H

TE ergibt eine symmetrische Tridiagonalmatrix. TM wird mit `D = diag(n²)` symmetrisiert
(`D^{1/2}·S·D^{1/2}`). Die Moden sind orthogonal unter dem Gewicht 1 (TE) bzw. 1/n² (TM).

## Querschnitt: semi-vektorieller Modenlöser

Gerechnet wird nur die dominante Komponente des elektrischen Feldes (Stern, IEE Proc. J 135, 56
(1988)). Für quasi-TE (E waagerecht, entlang x):

    ∂/∂x[(1/ε)·∂(εE)/∂x] + ∂²E/∂y² + k₀²ε·E = β²·E

Entlang der Feldrichtung springt E an Materialgrenzen (εE ist stetig), quer dazu ist E stetig.
Für quasi-TM tauschen x und y die Rollen. Diskretisierung auf einem gleichmäßigen Gitter, an
Zellgrenzen `ε_Mitte = (ε₁ + ε₂)/2`. Zellen, die eine Materialgrenze schneiden, erhalten die
flächengewichtete Permittivität. Am Rand des Gitters ist das Feld null.

Die Matrix ist dünn besetzt und nicht symmetrisch. Gesucht sind die größten β². Verfahren:
Unterraum-Iteration mit Shift-Invert (LU-Zerlegung von `A − σ`) und Rayleigh-Ritz; σ liegt erst
sicher über allen Eigenwerten und rückt dann dicht über den größten. Gemeldet werden nur Moden mit
Residuum `|Ax − β²x|/|β²x| < 10⁻⁶`.

**Geführt?** Oben und unten zählt die größte Brechzahl am Rand. Seitlich zählt der effektive Index
der Schichtmode der Randspalte: Bei einem Rippenwellenleiter führt die durchgehende Kernschicht
selbst Licht, die Rippe führt nur Moden mit größerem n_eff.

**Kenngrößen:** Einschluss `Γ = ∫_Kern |E|² / ∫ |E|²`, Modenfläche `A_eff = (∫|E|²)² / ∫|E|⁴`,
Gruppenindex aus zwei Nachbarwellenlängen (±0,2 %) mit neu ausgewerteten Sellmeier-Gleichungen.

## Modenquelle und Modenmonitor im 2D-FDTD

Im 2D-FDTD ist jeder Wellenleiter eine Schicht in der Zeichenebene: Ez-Polarisation entspricht
deren TE-Moden, Hz-Polarisation den TM-Moden. Die Modenquelle ist eine Stromschicht mit dem
Modenprofil f (bzw. h):

    ΔEz = (2·n_eff·c₀·Δt / (n²·Δ))·A·f           ΔHz = (2·n_eff·c₀·Δt / (n²·Δ))·(A/η₀)·h

Herleitung: Die Stromschicht erzeugt den Sprung des tangentialen H (bzw. E), der zu zwei nach außen
laufenden Moden gehört: `Js = 2·n_eff·E/η₀` bzw. `Ms = 2·n_eff·η₀·H/n²`. A ist der Spitzenwert von
Ez bzw. von η₀·Hz. Leistung der Mode: `P = n_eff·A²/(2η₀)·∫ f²·w dy` mit w = 1 (Ez) bzw. 1/n² (Hz).
Die Quelle strahlt nach beiden Seiten.

Der Modenmonitor liefert die Amplitude `a = ⟨F, f⟩/⟨f, f⟩` mit demselben Gewicht w; er trennt die
beiden Laufrichtungen nicht.

## Grenzen des Verfahrens

- **Semi-vektoriell:** Die Kopplung zwischen Ex und Ey fehlt. Bei schwach führenden Wellenleitern
  (Glasfaser, Nitrid mit großem Querschnitt) ist das genau; bei stark führenden Silizium-Drähten
  liegt n_eff einige Prozent zu hoch und n_g einige Prozent zu tief. Hybride Moden und die
  Kreuzung von TE- und TM-artigen Moden gibt es nicht.
- Gruppenindex: Die Moden werden über ihre Reihenfolge zugeordnet; kreuzen sich zwei Moden zwischen
  den Nachbarwellenlängen, ist der Wert falsch.
- Kein Verlust: weder Absorption noch Abstrahlung in das Substrat noch Biegeverluste.
- Feld am Gitterrand null: Moden nahe der Grenzfrequenz hängen von der Mantelbreite ab.

## Validierung

| Fall | Erwartung | Ergebnis |
|---|---|---|
| Schichtmoden, TE und TM | erfüllen die Eigenwertgleichung, Anzahl nach V | Residuum < 10⁻⁹ |
| Si-Schicht 220 nm in SiO₂, TE0 | Literatur etwa 2,85 | 2,8495 |
| Profil-Löser gegen Schichtmoden | gleiche n_eff, gleiches Feld | 0,5 % bzw. 0,03 |
| Querschnitt-Löser im Grenzfall der Schicht | TE und TM der Schicht | 0,2 % |
| **Si-Streifen 500 × 220 nm, TE0** | Literatur n_eff ≈ 2,44, n_g ≈ 4,2 (vollvektoriell) | **2,494 (+2,2 %), 4,05 (−3,5 %)** |
| Gitterkonvergenz 40 nm → 20 nm | kleine Änderung | < 0,03 |
| Modenquelle, Ez und Hz | Amplitude A, Leistung nach Formel, höhere Moden klein | 3 %, 4 %, < 2 % |

Die Literaturwerte für den Streifen stammen aus Chrostowski/Hochberg, *Silicon Photonics Design*
(2015), Kap. 3. Ein Vergleich mit Meep/MPB (Bauplan, Abnahme Phase 5) steht noch aus.
