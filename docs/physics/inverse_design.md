# Inverses Design mit der Adjoint-Methode (Phase 13)

Code: `libs/field/include/photonlab/field/inverse_design.hpp`, `libs/field/src/inverse_design.cpp`.
Tests: `tests/test_inverse_design.cpp`. CLI: `photonlab_cli inverse-design`. Oberfläche: Bauteil-Labor,
Reiter „Inverses Design".

## Aufgabe

Ein 90-Grad-Knick: Si-Wellenleiter (400 nm, Brechzahlen aus `libs/materials` bei der Mittenwellenlänge) in SiO2,
Licht kommt von links, soll unten in der Grundmode austreten. Dazwischen liegt ein quadratisches Designgebiet
(Standard 2,4 × 2,4 µm, Pixel = FDTD-Zelle 40 nm). Jede Designvariable ρ ∈ [0, 1] bestimmt die Permittivität
eines Knotens: ε = ε_Mantel + ρ̂ (ε_Kern − ε_Mantel). Ziel: mittlere Transmission T = |a|² / |a_Bezug|² bei
1530, 1550 und 1570 nm (a = Amplitude der Ausgangsmode, a_Bezug aus einem geraden Wellenleiter).

## Gradient aus dem diskreten Yee-Schema

Ez-Polarisation. Der Zeitschritt ist

```
E^{n+1} = E^n + dt/(ε0 εr) · (rot H)^{n+1/2} + q · s((n+1/2) dt)       (q: Einspeisung je Knoten)
H^{n+1/2} = H^{n-1/2} − dt/μ0 · (rot' E)^n
```

Mit der Fourier-Summe X̂(ω) = Σ_n x(t_n) e^{−iω t_n} (E zu ganzen, H und Quelle zu halben Zeitschritten) und
Feldern, die am Anfang und am Ende 0 sind (Puls vollständig abgeklungen), wird E^{n+1} − E^n zu i Ω dt Ê mit
Ω = 2 sin(ω dt/2)/dt. Eliminiert man H:

```
M(ε) Ê = i Ω μ0 ε0 Ŝ / dt · (εr q),        M(ε) = rot rot' − (Ω/c)² diag(εr)
```

M ist symmetrisch (rot' ist die Transponierte von rot auf dem Gitter; in der CPML nur näherungsweise).
Die Modenamplitude ist a = wᵀ Ê (w: Gewichte des Modenmonitors). Eine Änderung δεr im Designgebiet ändert
M um −(Ω/c)² δεr, also

```
δa = (Ω/c)² Σ_r (M^{-1} w)_r Ê_r δεr_r
```

M^{-1} w ist das Feld einer **adjungierten Quelle**: eine Einspeisung q_adj = w / εr an den Monitorknoten. Ihr
FDTD-Lauf liefert Ê_adj = M^{-1} w · i Ω μ0 ε0 Ŝ_adj / dt, und mit c² μ0 ε0 = 1:

```
da/dεr(r) = Ω dt · Ê_adj(r) Ê(r) / (i Ŝ_adj)
dT/dεr(r) = 2 Re( a* · da/dεr(r) ) / |a_Bezug|²
```

Ŝ_adj ist die Fourier-Summe des tatsächlich eingespeisten Signals (zu halben Zeitschritten). Je Iteration
laufen Vorwärts-, adjungierter und Bezugslauf gleichzeitig (Threads). Ein adjungierter Lauf liefert den
Gradienten für alle Wellenlängen, weil der Puls breitbandig ist.

**Prüfung** (Test und `validate`): dT/dεr an einzelnen Pixeln gegen zentrale Differenzen (δε = 0,01) an einem
ungleichmäßigen Startpunkt: Abweichung 0,01 % bis 0,4 %. Der Rest kommt vom nicht ganz abgeklungenen Feld und
der CPML.

## Filter, Projektion, Schrittweite

- Kegelfilter (Radius 80 nm = 2 Pixel), am Rand normiert; in der Kettenregel mit seiner Transponierten.
- Projektion ρ̂ = (tanh(βη) + tanh(β(ρ̃ − η))) / (tanh(βη) + tanh(β(1 − η))), η = 0,5, β = 1 → 4 → 16 → 64
  (35 %, 25 %, 25 %, 15 % der Iterationen). Am Ende Schwelle 0,5: binäres Design.
- Schritt: ρ += s · g / max|g| (g: Gradient nach ρ), s startet bei 0,15, halbiert sich, wenn das Ziel sinkt,
  wächst sonst um 20 % bis zum Startwert. Kein MMA, kein Adam: einfach und nachvollziehbar.

## Ergebnis (Standard, 40 Iterationen, etwa 30 s auf 4 Kernen)

| Design | mittlere Transmission (1530/1550/1570 nm) |
|---|---|
| Startpunkt ρ = 0,5 | 0,003 |
| Designgebiet nur Mantel | 0,025 |
| stetiges Enddesign | 0,995 |
| **binäres Enddesign (nur Si/SiO2)** | **0,995** |

Spektrum des binären Designs: 0,94 bei 1450 nm, über 0,98 von 1490 bis 1610 nm, 0,96 bei 1650 nm.
Mit 16 Iterationen (Test): binär 0,98.

## Grenzen

- **2D mit Ez** (E senkrecht zur Ebene). Ein Chip aus 220-nm-SOI führt TE-artig (Hz in 2D) und braucht für
  verlässliche Zahlen 3D; das Ergebnis ist ein Lehrbeispiel der Methode, kein fertiges Bauteil (#122).
- Keine garantierte Mindeststrukturgröße: Der Filter macht kleine Strukturen nur selten; einzelne Pixel
  (40 nm) können übrig bleiben. Eine Fertigung bräuchte Mindestbreiten-Nebenbedingungen (#121).
- Nur ein lokales Optimum; anderer Start oder andere Schrittweite geben ein anderes Design.
- Keine Materialdispersion im FDTD (Brechzahl bei der Mittenwellenlänge).
- Die Ableitung setzt einen symmetrischen Gitteroperator voraus; in der CPML stimmt das nur näherungsweise,
  das Designgebiet liegt aber weit davon entfernt.
