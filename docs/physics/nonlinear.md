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
