#pragma once
// PCM-Speicherzelle (Phase 9): GST-Flecken auf einem Wellenleiter. Der kristalline Anteil x (0 = amorph, 1 =
// kristallin) ist der Speicherinhalt und bleibt ohne Energiezufuhr erhalten. Lesen: Transmission bei kleiner
// Leistung. Schreiben: optische Pulse erwärmen den Fleck über seine Absorption; im Fenster T_c <= T < T_m
// kristallisiert er (SET), über T_m schmilzt er und erstarrt nach dem Puls amorph (RESET). Modell und
// Annahmen: docs/physics/active.md
#include <complex>
#include <vector>

namespace photonlab::circuit {

struct PcmCell {
  double length = 2e-6;             // Länge des GST-Flecks [m]
  double confinement = 0.03;        // Anteil der Modenleistung im GST (Annahme, abhängig von Dicke)
  double fraction = 0.0;            // kristalliner Anteil 0..1 (Speicherinhalt)
  double thermal_resistance = 1e5;  // Erwärmung je absorbierter Leistung [K/W] (Annahme)
  double thermal_time = 5e-9;       // thermische Zeitkonstante [s] (Annahme)
  double crystallization_time =
      50e-9;               // Zeitkonstante der Kristallisation im Temperaturfenster [s] (Annahme)
  double ambient = 300.0;  // [K]
};

// Komplexer effektiver Index des Flecks relativ zum Wellenleiter: Gamma * (n(x) - n_amorph), n(x) aus der
// linear gemischten Permittivität eps(x) = (1 - x) eps_a + x eps_c.
std::complex<double> pcm_index_change(const PcmCell& cell);
// Feld-Transmission und Leistungs-Transmission des Flecks bei der Wellenlänge
std::complex<double> pcm_field_transmission(const PcmCell& cell, double wavelength);
double pcm_transmission(const PcmCell& cell, double wavelength);

struct PulseResult {
  double peak_temperature = 0.0;  // [K]
  bool melted = false;
  double fraction_before = 0.0, fraction_after = 0.0;
};
// Ein Rechteckpuls der Leistung power [W] und Dauer duration [s] im Wellenleiter, danach Abkühlen (5
// thermische Zeitkonstanten). Erwärmung dT/dt = (R_th P_abs - (T - T0))/tau_th mit P_abs = P (1 - T_opt(x));
// im Fenster kristallisiert x mit dx/dt = (1 - x)/tau_c; Schmelzen setzt x am Ende auf 0 (schnelles Erstarren
// angenommen).
PulseResult apply_pcm_pulse(PcmCell& cell, double power, double duration, double wavelength);

// Programmieren mit Nachlesen (program and verify): amorphes GST absorbiert etwa siebenmal schwächer als
// kristallines. Ein einzelner Puls, der amorphes Material ins Kristallisationsfenster heizt, überhitzt es,
// sobald es kristallisiert, und schmilzt es wieder. Deshalb: Transmission lesen -> Pulsleistung so wählen,
// dass die stationäre Temperatur im unteren Drittel des Fensters liegt -> kurzer Puls (Zuwachs höchstens
// max_step) -> wiederholen bis zum Ziel. Liegt das Ziel unter dem aktuellen Anteil, wird zuerst mit
// reset_power/reset_duration amorphisiert.
struct ProgramResult {
  std::vector<PulseResult> pulses;
  bool reached = false;
};
ProgramResult program_pcm(PcmCell& cell, double target_fraction, double wavelength,
                          double reset_power = 50e-3, double reset_duration = 20e-9, double tolerance = 0.02,
                          double max_step = 0.08, int max_pulses = 200);

}  // namespace photonlab::circuit
