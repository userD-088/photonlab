# Aktive Photonik (Phase 9): Heizer, MZI-Schalter, PN-Modulator, Photodiode, PCM

Code: `libs/materials/src/silicon_carriers.cpp`, `libs/materials/src/phase_change.cpp`,
`libs/circuit/src/active_devices.cpp`, `libs/circuit/src/pcm.cpp`, Bauteile in `builtin_components.cpp`
(`heater`, `mzi_switch`, `pn_phase_shifter`, `ring_pn`, `photodiode`, `pcm_cell`).
Tests: `tests/test_active.cpp`, `tests/test_circuit_components.cpp`, `tests/test_pcm.cpp`; Validierung
`photonlab_cli validate`.

## Heizer-Phasenschieber

    Δφ = (2π/λ) · dn/dT · R_th · P_el · L        P_π = λ / (2 L dn/dT R_th)

Stationär, gleichmäßige Erwärmung, ohne Übersprechen. Voreinstellung: L = 61,6 µm und R_th so, dass P_π = 24,8 mW
wie gemessen bei Harris u. a., *Opt. Express* 22, 10487 (2014) (Si-Heizer ohne Unterätzung; typisch 20–25 mW,
unterätzt bis unter 1 mW). Temperaturerhöhung bei P_π ≈ 70 K. Das ist eine **Kalibrierung**, keine Vorhersage aus
der Geometrie (R_th hängt vom Aufbau ab).

## MZI-Schalter (`mzi_switch`)

Koppler C, zwei gleich lange Arme mit je einem Heizer (Formel oben, P_a und P_b), Koppler C:

    M = C · diag(e^{iφ_a}, e^{iφ_b}) · C · T_Arm,   C = [[t, iκ], [iκ, t]]

Für den 50:50-Koppler: `|M(out1, in1)|² = sin²(Δφ/2)`, `|M(out2, in1)|² = cos²(Δφ/2)` mit Δφ = φ_a − φ_b.
Gleiche Heizleistung (beide 0 oder beide P_π) koppelt über (Kreuz), ein Unterschied von P_π schaltet auf
Durchgang. Steuert Bit A den einen und Bit B den anderen Heizer, ist der Zustand A xor B – die Grundlage der
Addierer im Chip-Designer (`logic.md`). Reziprok, verlustfrei bis auf Koppler-Zusatzverlust und
Wellenleiterverlust; die Phase π gilt bei 1550 nm und skaliert mit 1/λ (breitbandig, keine Resonanz). Nicht
modelliert: thermisches Übersprechen zwischen den Armen, Zeitkonstante (einige µs), Erwärmung des Nachbarn.
Test: Kreuz/Durchgang, Zwischenwert sin²(0,15π), Unitarität (`test_circuit_components.cpp`).

**Übersprechen und Bauform.** Thermisches Übersprechen χ: jeder Arm erhält den Anteil χ der Phase des
Nachbarn, φ_a' = φ_a + χ φ_b; damit Δφ = (1 − χ)(φ_a − φ_b) und P_π,eff = P_π / (1 − χ) (Messwert „P_pi“).
Voreinstellung χ = 0; einige Prozent sind ohne Gräben zwischen den Armen eine realistische Größenordnung
(Annahme). Die thermische Zeitkonstante τ geht nur in den Messwert „Schaltzeit (10-90 %)“ = 2,2 τ ein (das
Modell bleibt stationär); Voreinstellung 4 µs (Größenordnung, Annahme). Zwei Knöpfe stellen den
Wärmewiderstand auf eine Bauform: **Standard** P_π = 24,8 mW, τ = 4 µs (Harris u. a. 2014) und
**unterätzt** (frei hängende Arme) P_π = 0,49 mW, τ = 100 µs (Größenordnung nach Fang u. a., IEEE Photon.
Technol. Lett. 23, 525 (2011)). Test: Δφ mit χ = 0,2, nach „Unterätzt“ ergibt 0,49 mW genau π.
Heizer und MZI-Schalter melden ihre Heizleistung; das Fenster Logik summiert sie und prüft, ob Bit 1 der
Heizer-Eingänge zu P_π passt (Knopf „Bit-1-Werte auf P_pi stellen“).

## PN-Phasenschieber (Plasma-Dispersion, Verarmung)

Soref-Bennett bei 1550 nm (Soref und Bennett, *IEEE JQE* 23, 123 (1987)), N in cm⁻³:

    Δn = −(8,8·10⁻²² ΔN_e + 8,5·10⁻¹⁸ ΔN_h^0,8)        Δα = 8,5·10⁻¹⁸ ΔN_e + 6,0·10⁻¹⁸ ΔN_h  [1/cm]

Abrupter PN-Übergang (Verarmungsnäherung, Sze): V_bi = kT/q ln(N_A N_D/n_i²),
w(V) = √(2ε(V_bi + V)(N_A+N_D)/(q N_A N_D)), w_n = w N_A/(N_A+N_D), w_p = w N_D/(N_A+N_D), C' = ε h / w.
Si: ε_r = 11,7, n_i = 1,0·10¹⁰ cm⁻³ (Sze, *Physics of Semiconductor Devices*, 3. Aufl.).
Überlapp **vereinfacht**: Mode gleichmäßig über die Breite W, Anteil Γ im Silizium, Übergang in der Mitte:

    Δn_eff(V) = Γ [Δn_e w_n(V) + Δn_h w_p(V)] / W          α(V) = Γ [α_e (W/2 − w_n) + α_h (W/2 − w_p)] / W

Voreinstellung 5·10¹⁷ cm⁻³ beidseitig, W = 500 nm, h = 220 nm, Γ = 0,8: **V_π L = 2,57 V cm** (bei 4 V); gemessen z. B.
2,7 V cm (Wanderwellen-PN-MZM). Ein Vollvektor-Überlapp mit dem Modenlöser fehlt (das Modell ist eine Abschätzung).
Grenzfrequenz RC: f = 1/(2π R_s C) mit der Sperrschichtkapazität.

**Ring-Modulator** (`ring_pn`): All-Pass-Ring, Anteil `coverage` des Umfangs mit PN-Übergang; Resonanzverschiebung
Δλ = λ Δn_eff · Anteil / n_g (Test: 21,2 pm bei 0 → 2 V, Formel 21,24 pm).

## Photodiode mit Rauschen

    I = R P + I_d,   σ_Schrot² = 2 q I B,   σ_therm² = 4 k T B / R_L,   SNR = (R P)² / (σ_Schrot² + σ_therm²)

Voreinstellung 1 A/W, 10 nA, 30 GHz, 50 Ω, 300 K (Größenordnungen für Ge-auf-Si nach Chrostowski/Hochberg, Kap. 7).

## PCM-Speicherzelle (GST-225)

GST-Fleck (Länge L, Anteil Γ der Modenleistung) auf dem Wellenleiter; kristalliner Anteil x ist der Speicherinhalt.
Permittivität linear gemischt: ε(x) = (1−x) ε_a + x ε_c, Feld-Transmission exp(i k0 Γ (n(x) − n_a) L).
GST bei 1550 nm: 4,4 + 0,098i (amorph), 7,1 + 0,78i (kristallin) – Simulationswerte aus „Reversible optical gate
switching in Si wire waveguide integrated with Ge2Sb2Te5 thin film“; **die Literatur streut stark** (Δn von 0,1 bis 3
je nach Schicht). Kristallisation ab ≈ 430 K, Schmelzen ≈ 890 K (Größenordnung nach Wuttig und Yamada,
*Nature Materials* 6, 824 (2007)). Fleck 2 µm, Γ = 0,03, R_th = 100 K/mW, τ_th = 5 ns, τ_c = 50 ns sind **Annahmen**.

Pulsmodell: dT/dt = (R_th P (1 − T_opt(x)) − (T − T0))/τ_th; im Fenster T_c ≤ T < T_m: dx/dt = (1 − x)/τ_c;
über T_m: geschmolzen, nach dem Puls amorph (schnelles Erstarren angenommen).

**Wichtig:** amorphes GST absorbiert etwa siebenmal schwächer als kristallines. Ein einzelner Puls, der amorphes
Material ins Fenster heizt, überhitzt es, sobald es kristallisiert, und schmilzt es (Test). Schreiben deshalb mit
**Programmieren und Nachlesen**: Transmission lesen → Leistung so, dass die stationäre Temperatur im unteren Drittel
des Fensters liegt → kurzer Puls (≤ 0,08 Zuwachs, Aufheiz- und Abkühlzeit berücksichtigt) → wiederholen. Löschen:
ein kurzer starker Puls (50 mW, 20 ns) schmilzt.

Ergebnis (`photonlab_cli pcm`): Transmission 0,953 (amorph) / 0,685 (kristallin); Schreiben in 18 Pulsen ohne Schmelzen,
Zwischenstufen 0,33 / 0,66 unterscheidbar, 1000 Lesepulse (0,1 mW) ändern den Zustand nicht. Im Chip-Designer hat die
PCM-Zelle die Handlungen „Schreiben“, „Löschen“, „Kristallisieren“ (Eigenschaften-Fenster).

## Grenzen

* Thermik stationär (Heizer) bzw. eine Zeitkonstante (PCM); kein thermisches Übersprechen.
* PN-Überlapp vereinfacht; Treiber und Sperrschicht als einzelne Pole; keine Wanderwellen-Elektrode.
* Rauschen weiß und gaußförmig nach dem Filter; BER aus der Gauß-Näherung (nicht gezählt).
* Zeitbereich nur für die feste Kette Sender → Modulator → Empfänger, nicht für beliebige Schaltungen des Chip-Designers
  (dafür fehlt Vector Fitting der S-Parameter).
