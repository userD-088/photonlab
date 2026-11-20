# 3D-FDTD (Phase 12)

Code: `libs/field/include/photonlab/field/fdtd3d.hpp`, `libs/field/src/fdtd3d.cpp`, Abnahme-Experiment
`libs/field/src/waveguide3d.cpp`. Geschwindigkeit: [`../performance.md`](../performance.md).

## Gitter

Würfelzellen der Kantenlänge dx, Zelle (i, j, k), Index i + nx·(j + ny·k). Lage der Komponenten (Yee 1966):

| Komponente | Ort |
|---|---|
| Ex | (i+½, j, k) |
| Ey | (i, j+½, k) |
| Ez | (i, j, k+½) |
| Hx | (i, j+½, k+½) |
| Hy | (i+½, j, k+½) |
| Hz | (i+½, j+½, k) |

E zu ganzen Zeitschritten n, H zu n+½. Außerhalb des Gitters gilt E = 0 (ideal leitende Wand); auf den
Wänden i = 0, j = 0, k = 0 bleiben die tangentialen E-Komponenten 0 (sie werden nicht gerechnet), auf den
gegenüberliegenden Wänden ist der fehlende Nachbar 0. Das ist dieselbe Konvention wie im 2D-FDTD.

## Gleichungen

Mit c_E = dt/(ε0 εr) am Ort der Komponente und Differenzen D_a über eine Zelle in Richtung a:

```
H von n-½ nach n+½ (Vorwärtsdifferenzen):
  Hx -= dt/μ0 · (D_y Ez − D_z Ey)
  Hy -= dt/μ0 · (D_z Ex − D_x Ez)
  Hz -= dt/μ0 · (D_x Ey − D_y Ex)
E von n nach n+1 (Rückwärtsdifferenzen):
  Ex += c_E · (D_y Hz − D_z Hy)
  Ey += c_E · (D_z Hx − D_x Hz)
  Ez += c_E · (D_x Hy − D_y Hx)
```

Stabilität (Courant 3D): dt ≤ dx / (c·√3). Gerechnet wird mit 0,99 dieser Grenze.

Numerische Dispersion: Eine ebene Welle exp(i(k·r − ωt)) löst das Differenzenschema genau dann, wenn

```
sin²(ω dt/2) = (c dt / (n dx))² · [sin²(kx dx/2) + sin²(ky dx/2) + sin²(kz dx/2)]
```

Test: Eigenmoden eines ideal leitenden, mit Dielektrikum gefüllten Quaders (je eine E-Komponente,
z. B. Ez ~ sin(π i/nx) sin(π j/ny)) schwingen exakt mit dieser Frequenz; geprüft wird
F(n+1) + F(n−1) = 2 cos(ω dt) F(n) auf 1e-10 (`tests/test_fdtd3d.cpp`).

## Material

Jede E-Komponente bekommt das Mittel von εr über 2 × 2 × 2 Punkte im Abstand dx/4 um ihren Ort. Liegen
Quaderflächen auf Zellkanten (Abmessungen als Vielfache von dx), sitzen nur Komponenten **tangential** zu
einer Grenzfläche auf ihr; für sie ist das arithmetische Mittel die richtige Näherung erster Ordnung
(Farjadpour u. a., Opt. Lett. 31, 2972 (2006)). Für schräge oder gekrümmte Flächen ist das nur eine
Treppenstufen-Näherung. Keine Dispersion, keine Verluste, keine Nichtlinearität im 3D (die gibt es im 2D-FDTD; #113).

## Randschicht (CPML)

Wie im 2D (Roden/Gedney 2000, κ = 1, α = 0, Profil Grad 3, σ_max = 0,8·(m+1)/(η0·dx·n)), jetzt an allen
sechs Seiten. Für jede Feldkomponente und jede ihrer beiden Ableitungsrichtungen gibt es ein Hilfsfeld ψ
(12 Stück), gespeichert **nur in der Schicht** dieser Richtung:

```
ψ = b·ψ + c·D_a(Quelle)      E += c_E · s · ψ      H -= dt/μ0 · s · ψ
```

s ist das Vorzeichen des Terms in der Update-Gleichung. Die Hauptschleife rechnet ohne ψ; eine zweite
Schleife je Ebene ergänzt die Korrektur nur in den Schichtzellen. Gemessen: Reflexion einer Punktquelle
−94 dB bei 10 Zellen (Gegenprobe ohne Schicht: −7 dB, der Test sieht das Echo also).

## Abnahme: 3D-Wellenleiter = Modenlöser

Experiment `run_waveguide_3d`: gerader Wellenleiter entlang x, Kern als Quader durch die Randschichten.
Die Quelle ist eine Ey-Stromschicht mit dem Profil der Grundmode des semi-vektoriellen Modenlösers auf
demselben Querschnittsgitter (quasi-TE), Dauerstrich mit sin²-Rampe. Nach dem Einschwingen (Rampe plus zwei
Laufzeiten mit dem Kernindex als obere Grenze des Gruppenindex) wird Ey entlang der Kernachse über ganze
Perioden Fourier-transformiert. n_eff = |Steigung der Phase| / k0 aus einer Ausgleichsgeraden im Messbereich
(0,4 µm hinter der Quelle bis 5 Zellen vor der Randschicht).

Zusätzlich: n_eff ohne Yee-Dispersion in Ausbreitungsrichtung, (2/dx)·sin(β dx/2) / ((2/(c dt))·sin(ω dt/2)),
Modenüberlapp am Messquerschnitt und die Welligkeit der Amplitude (Maß für Reflexionen). Die Korrektur nimmt
nur den Gitterfehler **längs** heraus; der Fehler der Diskretisierung quer hat oft das andere Vorzeichen
(Schicht: roh 2,8508, korrigiert 2,8447, exakt 2,8495). Sie dient zur Einordnung, nicht als bessere Zahl;
bewertet wird immer der rohe Wert.

| Fall (λ = 1550 nm, dx = 20 nm) | 3D-FDTD | Referenz | Abweichung |
|---|---|---|---|
| Si-Schicht 220 nm in SiO2 (TE0) | 2,8508 | analytisch 2,8495 | **0,05 %** |
| Si-Draht 500 × 220 nm (quasi-TE) | 2,4452 | Literatur vollvektoriell 2,44 | **0,2 %** |
| Si-Draht 500 × 220 nm | 2,4452 | semi-vektorieller Modenlöser 2,4943 | 2,0 % (Fehler der Näherung, siehe `modes.md`) |
| Si3N4 1000 × 400 nm | 1,6484 | semi-vektorieller Modenlöser 1,6408 | 0,46 % |

Die Abweichung beim Si-Draht liegt beim Modenlöser, nicht beim FDTD: Der semi-vektorielle Löser vernachlässigt
die Kopplung von Ex und Ey und liegt bei stark führenden Wellenleitern einige Prozent zu hoch; das 3D-FDTD
rechnet alle sechs Komponenten. Bei der Schicht (exakt lösbar) stimmen beide.

Konvergenz Si3N4: dx = 40 nm → 1,0 %, dx = 20 nm → 0,46 % (zweite Ordnung). Schicht dx = 10 nm: 0,04 %.
Literatur n_eff = 2,44: Chrostowski/Hochberg, *Silicon Photonics Design* (2015), Kap. 3 (aus dem Gedächtnis
zitiert, wie schon in `modes.md`).

## Feld-Szene in 3D (`extrude3d.*`, Bauteil-Labor: Umschalter 2D/3D)

Die 2D-Szene wird zu einem Schichtaufbau (Voreinstellung Standard-SOI): Substrat SiO2 unten, Bauteilschicht der
Dicke d (220 nm), Mantel SiO2 (oder Luft) oben, je 0,7 µm bis zur Randschicht. Brechzahlen aus der
Materialbibliothek bei der Wellenlänge der ersten Quelle.

- **Formen**: in 2D stehen meist effektive Indizes (2,85 für 220 nm Si). Festlegung: Formen und Hintergrund mit
  einer 2D-Brechzahl ab 2 werden in der Schicht zum Kernmaterial, Luft (n = 1) bleibt Luft, andere Brechzahlen
  bleiben unverändert; Stellen ohne Form bekommen das Material des oberen Mantels (geätzte Schicht). Kristall-
  Szenen (Hintergrund 2,85) werden so zu einer Si-Platte mit Luftlöchern.
- **Gitter**: Würfelzellen (Voreinstellung 40 nm, etwa 11 Zellen je Wellenlänge in Si: grob). Die Randschicht
  ist höchstens so dick wie die der 2D-Szene, damit Quellen und Monitore außerhalb liegen.
- **Modenquellen**: Stromfläche quer zur Linie über den ganzen Querschnitt außerhalb der Randschicht, Profil =
  dominante E-Komponente der **echten Querschnittsmode** aus dem eigenen Modenlöser (`fde.cpp`, quasi-TE bei
  Hz-Szenen, quasi-TM bei Ez-Szenen) auf dem 3D-Gitter; nur ohne geführte Mode das getrennte Profil
  (2D-Mode mal Schichtmode). Gewicht 2 n_eff c dt/(ε dx) · A wie in 2D, einseitig ebenfalls wie in 2D mit dem
  n_eff des Modenlösers. Linienquellen: gleichmäßig bzw. Gauß quer, Schichtmode senkrecht. Punktquellen:
  ein Dipol der Hauptkomponente in der Mitte der Schicht.
- **Flussmonitore**: Fläche quer über die Linie und den ganzen Querschnitt (ohne Randschicht);
  `S_x = ½(E_y[i] + E_y[i+1]) H_z − ½(E_z[i] + E_z[i+1]) H_y` am Ort von H (i + ½), entsprechend S_y;
  Mittel über die letzte Periode der ersten Quelle. Modenmonitore zählen in 3D den ganzen Fluss.
  Einheit W (Quelle mit Amplitude 1 V/m); aussagekräftig sind die Verhältnisse der Monitore.
- **Nicht in 3D**: Regeneratoren (bleiben aus), Kerr/TPA (linear gerechnet), periodische Ränder (Randschicht),
  Grafikkarte.

**Prüfung** (`tests/test_extrude3d.cpp`): Brechzahlen des Aufbaus an Kern, Mantel, Substrat; gerader
Si-Wellenleiter 500 × 220 nm (Gitter 50 nm): Fluss B/A = 0,98 (Toleranz 3 %) zwischen zwei Flächen 1,6 und
2,8 µm hinter der Quelle. Der Rest ist Streulicht der Quelle, das nach oben und unten abstrahlt: B/A steigt
von 0,93 auf 0,98, wenn A von 0,6 auf 1,6 µm Abstand rückt, unabhängig von Zelle (40/50 nm) und Laufzeit.
Monitore deshalb mindestens 1,5 µm hinter Quellen legen.

## Grenzen

- Nur Quader, nur reelle, konstante Brechzahlen; keine Szene im Editor, kein Dateiformat (#114).
- Quellen: Stromschichten mit beliebigem Profil über `Source3d`; Modenquelle nur im Experiment `waveguide3d`.
- Keine Monitore außer dem, was das Experiment selbst auswertet; keine S-Parameter in 3D (#115).
- Speicher: 9 Gleitkommazahlen je Zelle plus Randschicht, etwa 90 Byte; 60 Millionen Zellen sind die Grenze.
