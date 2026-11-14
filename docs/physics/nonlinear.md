# Nichtlineare Optik (Phase 8)

Code: `libs/circuit/src/kerr_cavity.cpp` (Kompaktmodell), Tests: `tests/test_kerr_cavity.cpp`,
CLI: `photonlab_cli bistability`.

## Coupled-Mode-Modell eines Resonators (Kompaktmodell)

Amplitude a mit |a|² = U (Energie im Resonator, J), Eingangswelle s mit |s|² = P (W), im mit dem Laser
(ω_L) rotierenden Bezugssystem:

    da/dt = ( -i (ω_eff - ω_L) - γ/2 ) a + sqrt(κ_in) s
    dN/dt = g U² - N/τ
    ω_eff = ω0 - β U + ξ N          γ = κ_in + κ_0 + κ_T U + σ N

* β: Kerr-Verschiebung. Mit n2 > 0 sinkt die Resonanzfrequenz (Rotverschiebung):
  Δn = n2 I, I = c U /(n_g V_eff) (mittlere Intensität im Ring), Δω = -ω0 Δn/n_g
  ⇒ **β = ω0 n2 c /(n_g² V_eff)**, V_eff = A_eff·L.
* κ_T U: Zwei-Photonen-Absorption. Absorbierte Leistung P_TPA = β_TPA I² V ⇒
  **κ_T = β_TPA c² /(n_g² V_eff)**. Jedes absorbierte Photonenpaar erzeugt ein Elektron-Loch-Paar:
  g = κ_T /(2 ħ ω0 V_eff) (dN/dt = P_TPA /(2ħω V)).
* Ladungsträger: Blauverschiebung ξ = ω0 |dn/dN| /n_g, Absorption σ = α_FC c/n_g.
  Silizium: n2 = 4,5e-18 m²/W, β_TPA = 8,0e-12 m/W (Bristow u. a., APL 90, 191104 (2007), 1550 nm);
  Soref-Bennett (IEEE JQE 23, 123 (1987)) linearisiert um 1e17 cm⁻³ mit N_e = N_h:
  dn/dN = -3,6e-27 m³, Absorption 1,45e-21 m². **Die Linearisierung ist eine grobe Näherung** (das Soref-Bennett-Gesetz
  ist nichtlinear in N). Lebensdauer τ = 1 ns als Annahme.
* Durchgang eines All-Pass-Resonators: s_out = s - sqrt(κ_in) a, T = |1 - κ_in /(γ/2 + iδ)|², δ = ω_eff - ω_L.
  Bei Resonanz und linear: T = ((κ_0 - κ_in)/(κ_0 + κ_in))².

## Stationäre Zustände und Bistabilität

Im Gleichgewicht ist die Eingangsleistung eine **explizite** Funktion der Energie:

    P(U) = U [ (ω_eff(U) - ω_L)² + γ(U)²/4 ] / κ_in      (N = g τ U²)

Mehrdeutige Lösungen für P(U) = P_in sind die Bistabilität; stabil sind die Äste mit dP/dU > 0 (für Kerr/TPA
hinreichend und notwendig; mit Ladungsträgern nur notwendig, Selbstpulsieren schließt das nicht aus).
Für den reinen Kerr-Resonator (x = βU, Δ = ω0 - ω_L, h = γ/2): P = x[(Δ - x)² + h²]/(β κ_in).
dP/dx = 0 hat zwei Lösungen x = (2Δ ± sqrt(Δ² - 3h²))/3, also **Bistabilität ab Δ = sqrt(3) h = sqrt(3) γ/2**,
bei der kritischen Leistung **P_c = sqrt(3)/9 γ³ /(β κ_in)** (Wendepunkt bei U_c = 2Δ/(3β)).

## Prüfungen

* Linear: Lorentz-Kurve und T bei kritischer/Unterkopplung exakt (1e-9).
* Schwelle sqrt(3)γ/2: unterhalb genau ein Zustand bei jeder Leistung, oberhalb drei (stabil, instabil, stabil);
  P(U_c) = P_c exakt.
* Klappleistungen bei Δ = 2h gegen die geschlossene Lösung der Kubik.
* Zeitrechnung (RK4): konstante Leistung läuft in den stationären Zustand (1e-4); langsame Rampe klappt bei den
  analytischen Leistungen (5 % bzw. 8 %, Abweichung durch endliche Rampengeschwindigkeit).

## Ergebnisse für einen Si-Ring (`bistability`: Q = 2e5 kritisch gekoppelt, V_eff = 6,3 µm³, n_g = 4,2)

| Fall | Ergebnis |
|---|---|
| nur Kerr, Verstimmung 3 h | klappt bei 12,7 mW hinauf, 7,3 mW hinunter (Zeitrechnung: 12,9 / 7,1 mW) |
| mit TPA, Verstimmung 3 h | **keine Bistabilität**: TPA vergrößert γ mit U, die effektive Verstimmung fällt unter die Schwelle |
| mit TPA, Verstimmung 6 h | klappt bei 92 mW hinauf, 67 mW hinunter |

Das stimmt mit der Erfahrung überein, dass TPA im Silizium Bistabilität unterdrückt und erst bei starker
Verstimmung und hoher Leistung wieder zulässt.

## Grenzen

* Ein Resonator mit einer Mode; Dispersion, Mehrmoden-Kopplung und thermischer Effekt (Phase 8, folgt) fehlen noch.
* Der Gleichgewichtsansatz N = g τ U² nimmt schnell eingestellte Ladungsträger an; in der Zeitrechnung folgen sie
  dagegen der Ratengleichung.
* Die Zahlen sind für **Streifenwellenleiter-Ringe** gedacht; V_eff ist eine Eingabe (A_eff·L), keine
  aus dem Modenlöser gerechnete Größe (Kopplung an die Feld-Ebene: Issue #64).

## Nichtlinearität im FDTD (Ez- und Hz-Polarisation)

Eingeschaltet über Formen mit `n2` [m²/W], `beta_tpa` [m/W] und `carrier_lifetime` [s] (Szene, `.plab.json`; nur
wenn ≠ 0 gespeichert). In den Zellen dieser Formen wird nicht E, sondern D/ε0 =: d fortgeschrieben (Ez: die Zellen von
Ez; Hz: die Zellen von Ex und Ey getrennt, isotroper Kerr-Effekt P_NL = ε0 χ3 |E|² E mit |E|² = Ex² + Ey², die jeweils
andere Komponente gemittelt aus den vier Nachbarn des versetzten Gitters):

    d  ←  d + ε ΔE_lin  −  (Δt/ε0) σ E          (ΔE_lin: normaler Yee-Schritt inkl. CPML-Terme)
    ε_eff(N) E + χ3 (E² + E_andere²) E = d         (monotone Kubik, Newton ab dem alten E, ≤ 8 Schritte)

* **Kerr**: P_NL = ε0 χ3 E³, n2 = 3 χ3 /(4 n0² ε0 c) ⇒ χ3 = 4 n0² ε0 c n2/3. Eine ebene Welle der Amplitude E0 sieht
  Δε = (3/4) χ3 E0² (Dritte-Harmonische-Terme entstehen im Löser mit, werden aber hier nicht ausgewertet).
* **TPA** als nichtlineare Leitfähigkeit σ = σ2 E²: Zyklusmittel ⟨σ2 E⁴⟩ = (3/8) σ2 E0⁴ = β I², I = n0 c ε0 E0²/2
  ⇒ σ2 = (2/3) β n0² c² ε0².
* **Ladungsträger**: dN/dt = σ2 E⁴/(2ħω) − N/τ (ein Paar je zwei Photonen, ħω der ersten Quelle); ε_eff = ε + 2 n0 (dn/dN) N,
  Absorption σ_FC = α_FC n0 c ε0 · N, mit den linearisierten Silizium-Konstanten von oben.
* Quellen innerhalb nichtlinearer Zellen werden nicht in d eingerechnet (die Quelle würde zurückgenommen): Quelle in den
  linearen Bereich setzen. `Fdtd::set_source_scale` skaliert alle Quellen (langsame Leistungsrampen).

### Prüfungen im FDTD

| Prüfung | Ergebnis |
|---|---|
| Phase einer ebenen Welle im Kerr-Streifen gegen φ = (2π/λ) n2 I L (Ez und Hz) | Abweichung unter 2 % |
| TPA-Transmission gegen 1/(1 + β I0 L) (Ez und Hz) | 4 % Toleranz bestanden |
| Ladungsträgerdichte gegen β I² τ /(2ħω) | 10 % Toleranz bestanden |
| freie Ladungsträger senken die Phase (Blauverschiebung) | ja |
| **Abnahme Resonanzverschiebung**: Bragg-Resonator (Q ≈ 390) mit Kerr-Spacer, Verstimmung 0,5 γ, Verschiebung ≈ 0,55 γ | Energie im FDTD 0,01555, Vorhersage aus verschobener gemessener Linienform 0,01596 (2,6 %), Kompaktmodell mit Lorentz-Linie 0,01651 (6 %); kalt 0,00842 |
| **Abnahme Hysterese**: langsame Rampe hinauf/hinunter bei Verstimmung 1,5 γ | Hysterese im Feld sichtbar; Sprung hinunter bei 2,50e10 W/m² (Kompaktmodell mit derselben Rampe: 2,48e10), Sprung hinauf 1,56e11 (Modell 1,44e11) |

Die Kerr-Konstante des Kompaktmodells folgt aus dem **linearen** Modenprofil des FDTD (Störungsrechnung):
Δω = −ω0 (3/8) χ3 ∫|E|⁴ dx / ∫ ε|E|² dx, β' = Δω/U' mit U' = ½ ε0 ∫ ε|E|² dx (alles je Fläche, damit gleiche Normierung
wie die Einfallsleistung). Die Linienform wird nicht als Lorentz angenommen, sondern gemessen (U/P der linearen
Struktur); das Lorentz-Modell trifft trotzdem auf wenige Prozent.

**Wichtig – statisch ≠ dynamisch:** Die statischen Klappleistungen (Extrema von P(U)) 1,07e11 W/m² hinauf und 6,3e10 hinunter
liegen weit von den Sprungstellen der Rampe entfernt (1,5e11 und 2,5e10): in der Nähe der Klappstelle wird die
Antwort langsam (kritische Verlangsamung), die Schleife der endlichen Rampe ist breiter als die statische. Eine
Messung "Hysteresebreite" gilt immer für eine bestimmte Rampengeschwindigkeit; Kompaktmodell und FDTD stimmen bei
**gleicher** Rampe überein.

Hinweis zu den Tests: doctest-`Approx` rechnet mit `scale = 1` (Toleranz ε·(1 + |x|)); für kleine Zahlen ist das praktisch
eine absolute Toleranz. Die neuen Tests nutzen deshalb `.scale(0.0)`.

## Nichtlineare Bauteile in der Schaltung (Phase 8, #79/#81)

Code: `ring_kerr`, `soa` in `libs/circuit/src/builtin_components.cpp`; Löser `CompiledCircuit::solve(λ, state, …)`,
Rampen `wavelength_ramp` / `power_ramp` (`sweep.hpp`). Tests: `tests/test_circuit_nonlinear.cpp`.

**Löser.** Ein nichtlineares Bauteil hat einen inneren Zustand (Ring: umlaufende Leistung je Richtung; SOA: Verstärkung).
Fixpunkt-Iteration: lineares System mit den S-Matrizen beim aktuellen Zustand lösen → jedes nichtlineare Bauteil bestimmt
aus seinen einlaufenden Wellen den passenden Zustand (`settle_state`) → wiederholen, bis sich nichts mehr ändert
(relativ 1e-10). Ohne Rückkopplung zwischen nichtlinearen Bauteilen genügen 2 Durchläufe; bei Rückkopplung wird nach 20
Schritten gedämpft, nach 400 meldet der Löser Nichtkonvergenz. Gibt es mehrere Zustände (Bistabilität), wählt das Bauteil
den, der dem vorigen am nächsten liegt: eine **Rampe**, die den Zustand weiterreicht, folgt einem Ast und zeigt Hysterese.
Ein einzelnes `solve(λ)` startet beim Zustand null (unterster Ast).

**Kerr-Ring** (`ring_kerr`, All-Pass): Umlauf je Richtung

    H = e^{iθ} · H_lin · sqrt(1/(1 + β_TPA I L)) · exp(i (k0 n2 I L + k0 (dn/dT) R_th P_abs L)),   I = (P_selbst + 2 P_gegen)/A_eff
    P_selbst = κ² |a_ein|² / |1 - t H|²       (selbstkonsistent, Nullstellen per Abtastung + Bisektion)
    τ = (t - H)/(1 - t H)

P_abs = im Ring absorbierte Leistung (linearer Verlust + TPA), R_th = Wärmewiderstand. Die Gegenrichtung wirkt doppelt
(Kreuzphasenmodulation). Thermik ist stationär (keine thermische Zeitkonstante). Silizium-Werte: n2 = 4,5e-18 m²/W,
β_TPA = 8e-12 m/W (Bristow 2007), dn/dT aus `libs/materials`; A_eff = 0,1 µm² ist eine Annahme für Si-Streifen, R_th ist
aufbauabhängig (Größenordnung 1e3…1e4 K/W) und standardmäßig 0. Freie Ladungsträger fehlen im Ring-Bauteil noch
(im Kompaktmodell `kerr_cavity` und im FDTD vorhanden).

**SOA** (`soa`): stationäre Sättigung G = G0 exp(-(G-1) P_ein/P_sat) (Saleh 1989; Agrawal, *Fiber-Optic Communication
Systems*, Kap. 7), P_ein = Summe beider Richtungen, Phase -(α_H/2) ln(G/G0). Ohne Rauschen (ASE) und ohne Dynamik.
20 dB und 10 mW sind Größenordnungen, keine Datenblattwerte.

| Prüfung | Ergebnis |
|---|---|
| `ring_kerr` ohne Nichtlinearität = `ring_allpass` | Abweichung < 1e-12 |
| Klappleistungen der Leistungsrampe (600 Schritte) = Extrema der geschlossenen Ringgleichung | innerhalb 1,5 Schritten |
| Ring-Klappleistungen gegen das Coupled-Mode-Kompaktmodell (T_rt = n_g L/c, κ_in = κ²/T_rt, κ_0 = (1-a²)/T_rt, β = ω n2/(n_g A T_rt)) | 33,8 / 19,3 mW gegen 34,0 / 19,2 mW (< 1 %) |
| TPA senkt die umlaufende Leistung; Selbstkonsistenz P = κ²P_ein/|1 - tH|² | 1e-8 |
| Thermik-Dreieck (R_th = 5 K/mW, 5 mW): Minimum hin 1550,40 nm, zurück 1550,11 nm | Hin- und Rückweg verschieden |
| SOA: 3-dB-Ausgangsleistung P_sat G0 ln2/(G0 - 2) | 1e-6 |
| zwei Kerr-Ringe hintereinander = einzeln gerechnet | 1e-8 |

Beispiel: `examples/08_ring_nichtlinear.plab.json` (2 mW, R_th = 5 K/mW). `photonlab_cli run … --sweep` rechnet bei
nichtlinearen Schaltungen hin und zurück (Spalte `richtung`); der Chip-Designer zeigt im Spektrum und in der Kennlinie
beide Wege („(hin)“, „(zurück)“). Der Monte-Carlo-Lauf und die Resonanz-Auswertung nutzen den untersten Ast.
