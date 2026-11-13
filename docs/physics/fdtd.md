# 2D-FDTD nach Yee (Feld-Ebene, Phase 4)

Code: `libs/field` · Tests: `tests/test_field_fdtd.cpp` · CLI: `photonlab_cli run <datei> --field`

## Gleichungen

Maxwell-Gleichungen ohne Ströme in nichtmagnetischen, verlustfreien, dispersionsfreien Medien
(`ε = ε₀·n²`, `μ = μ₀`), unabhängig von z. Koordinaten: x nach rechts, y nach unten, z = x × y.
In 2D zerfallen sie in zwei unabhängige Sätze:

    Ez-Polarisation (E senkrecht zur Ebene)      Hz-Polarisation (E in der Ebene)
    ∂Hx/∂t = −(1/μ)·∂Ez/∂y                       ∂Ex/∂t =  (1/ε)·∂Hz/∂y
    ∂Hy/∂t =  (1/μ)·∂Ez/∂x                       ∂Ey/∂t = −(1/ε)·∂Hz/∂x
    ∂Ez/∂t =  (1/ε)·(∂Hy/∂x − ∂Hx/∂y)            ∂Hz/∂t =  (1/μ)·(∂Ex/∂y − ∂Ey/∂x)

In der Draufsicht auf einen Chip entspricht die Hz-Polarisation der TE-artigen Mode (E in der
Chip-Ebene), die Ez-Polarisation der TM-artigen. PhotonLab benennt die Sätze nach ihrer
Feldkomponente, weil „TE“ und „TM“ in der Literatur gegensätzlich verwendet werden.

## Gitter und Zeitschritt

Quadratische Zellen `Δ = dx = dy`. Die Komponenten sitzen versetzt (Yee 1966):

| | Skalarfeld (ganze Zeitschritte) | Felder in der Ebene (halbe Zeitschritte) |
|---|---|---|
| Ez-Satz | Ez auf Knoten (i, j) | Hx bei (i, j+½), Hy bei (i+½, j) |
| Hz-Satz | Hz in Zellmitten (i+½, j+½) | Ex bei (i+½, j), Ey bei (i, j+½) |

Zentrale Differenzen in Raum und Zeit (Leapfrog), zweite Ordnung genau. Stabil für

    Δt ≤ Δ / (c₀·√2)          (Courant-Bedingung in 2D)

PhotonLab setzt `Δt = courant · Δ/(c₀·√2)` mit `courant = 0,99`. Die Brechzahl wird an jedem
Komponentenort einzeln abgetastet; es gibt keine Glättung an Materialgrenzen (Treppenstufen).

## Numerische Dispersion

Eine ebene Welle `e^{i(kx·x + ky·y − ωt)}` erfüllt auf dem Gitter nicht `ω = c·|k|`, sondern

    sin²(ω·Δt/2) = S²·[sin²(kx·Δ/2) + sin²(ky·Δ/2)],     S = c·Δt/Δ

Die Phasengeschwindigkeit ist etwas zu klein und richtungsabhängig. Bei 20 Zellen pro Wellenlänge
beträgt der Fehler entlang der Achsen etwa 0,2 %, bei 10 Zellen knapp 1 %. Deshalb warnt die Oberfläche unter 15 Zellen
pro Wellenlänge im optisch dichtesten Material.

## Energie

Das Leapfrog-Verfahren erhält im abgeschlossenen, verlustfreien Gitter exakt

    U = ½·Σ ε·Ez(n)·Ez(n+1)·Δ² + ½·Σ μ·|H(n+½)|²·Δ²            (Ez-Satz; Hz-Satz entsprechend)

Das Skalarfeld geht als Produkt aufeinanderfolgender Zeitschritte ein. `Fdtd::energy()` liefert
diese Größe in J/m (je Meter Bauteilhöhe).

## Ränder

- **CPML** (Roden/Gedney 2000): absorbierende Schicht mit Hilfsfeldern ψ,
  `ψ ← b·ψ + c·∂F`, `b = e^{−σΔt/ε₀}`, `c = b − 1` (κ = 1, α = 0). Leitfähigkeit polynomiell,
  `σ(d) = σ_max·d³` mit `σ_max = 0,8·4/(η₀·Δ·n)` (Taflove/Hagness, Kap. 7), n = Brechzahl des
  Hintergrunds. Hinter der Schicht steht eine ideal leitende Wand.
- **Periodisch**: was an einer Seite hinausläuft, kommt gegenüber wieder herein.

Die Schicht ist auf den Hintergrund abgestimmt. Strukturen mit anderer Brechzahl, die bis in die
Schicht reichen (z. B. ein Wellenleiter), werden etwas schlechter absorbiert.

## Quellen

Weiche Quellen addieren pro Zeitschritt einen Beitrag zum Skalarfeld. Eine Linienquelle ist eine
Stromschicht: Eine elektrische Flächenstromdichte `Js` strahlt nach beiden Seiten eine ebene Welle
mit `E₀ = η·Js/2` ab, eine magnetische `Ms` eine Welle mit `E₀ = Ms/2`. Daraus folgt der Zuwachs

    ΔEz = (2·c₀·Δt/(n·Δ))·A·s(t)             ΔHz = (2·c₀·Δt/(η₀·Δ))·A·s(t)

für eine abgestrahlte Welle mit der **E-Feld-Amplitude A** (H-Amplitude `A·n/η₀`). Das gilt für
achsenparallele Linien; bei schrägen Linien und bei Punktquellen ist A nur ein relatives Maß.
Jede Linienquelle strahlt nach beiden Seiten.

Eine **Modenquelle** trägt statt eines gleichmäßigen oder Gauß-Profils das Profil einer geführten
Mode des Brechzahlprofils unter der Linie und regt diese Mode fast rein an; Herleitung und
Kalibrierung in [`modes.md`](modes.md).

Zeitsignale: Dauerstrich `sin(ωt + φ)` mit sin²-Rampe über 4 Perioden; Gauß-Puls
`e^{−((t−t₀)/τ)²}·sin(ω(t−t₀) + φ)` mit `t₀ = 4τ`.

## Monitore

- **Punkt**: Zeitverlauf des Skalarfeldes; Spektrum per direkter Fourier-Summe.
- **Fluss**: Poynting-Vektor `S = E × H` über eine achsenparallele Linie integriert, in W/m
  (je Meter Bauteilhöhe). E und H werden auf denselben Ort und dieselbe Zeit gemittelt.
  Für eine ebene Welle: `⟨S⟩ = A²·n/(2·η₀)`.

## Vereinfachungen

- **2D statt 3D**: Abstrahlung nach oben und unten fehlt; Verluste von Biegungen, Ringen und
  Kristallen sind deshalb zu klein, Güten zu groß. Für die Draufsicht auf einen Chip setzt man den
  effektiven Index der Schicht ein (Effektiv-Index-Methode, z. B. etwa 2,85 für 220 nm Silizium).
- Brechzahlen reell und ohne Dispersion; keine Absorption, keine Nichtlinearität (kommt in Phase 8).
- Treppenstufen an schrägen und runden Kanten: Resonanzen von Ringen liegen nicht dort, wo ein
  gefertigtes Bauteil sie hätte.
- Jede Quelle strahlt in beide Richtungen (auch die Modenquelle). Ein Gauß-Strahl regt neben der
  geführten Mode auch Strahlung an; die Modenquelle vermeidet das.

## Validierung

| Fall | Erwartung | Ergebnis |
|---|---|---|
| Dispersionsrelation, 4 Richtungen, beide Sätze | `cos(ωΔt) = 1 − 2S²(sin² + sin²)` | relativ 1e-11 |
| Energie im geschlossenen Gitter mit Dielektrikum, 3000 Schritte | konstant | Drift < 1e-11 |
| CPML, 12 Zellen, Punktquelle mit Puls | Reflexion < −40 dB | −90 dB (beide Sätze) |
| Linienquelle, n = 1 und 1,5 | Amplitude A, Fluss `A²n/(2η₀)` | 1 % bzw. 2 % |
| Grenzfläche n = 1 → 1,5 | `r = 0,2`, `t = 0,8` | 1 % |
| Wellenleiter, 9 µm | geführte Leistung kommt an | Verhältnis hinten/vorn zwischen 0,5 und 1,02 |
