#pragma once
// Zeitbereich einer optischen Übertragungsstrecke (Phase 9, Bauplan 5.3): Pseudozufallsdaten -> Treiber ->
// Modulator (Mach-Zehnder mit PN-Phasenschiebern oder Ring-Modulator als Coupled-Mode-Modell) -> Strecke ->
// Photodiode mit Bandbreite und Rauschen -> Augendiagramm, Q-Faktor, Bitfehlerrate.
// Gleichungen, Annahmen und Grenzen: docs/physics/active.md
#include <cstdint>
#include <vector>

#include "photonlab/circuit/active_devices.hpp"
#include "photonlab/core/job.hpp"

namespace photonlab::signal {

enum class ModulatorKind { mzm, ring };

struct LinkSettings {
  // Daten
  double bit_rate = 10e9;  // [bit/s]
  int prbs_order = 7;      // PRBS 2^n - 1 (7, 9, 11, 15)
  int bits = 508;          // Anzahl der simulierten Bits (die ersten 8 laufen ein und zählen nicht)
  int samples_per_bit = 32;
  std::uint64_t seed = 1;  // Zufall des Rauschens (reproduzierbar)

  // Sender
  double wavelength = 1550e-9;
  double laser_power = 1e-3;     // [W]
  double rin_db_per_hz = -1000;  // relatives Intensitätsrauschen [dB/Hz]; sehr klein = aus
  double linewidth = 0.0;        // Laser-Linienbreite [Hz] (Phasenrauschen als Zufallsweg)

  // Treiber und Modulator
  double v_low = 0.0, v_high = 2.0;  // Sperrspannung für Bit 0 und Bit 1 [V]
  double driver_bandwidth = 0.0;     // 3-dB-Bandbreite des Treibers (ein Pol) [Hz]; 0 = ideal
  bool junction_rc = true;           // RC der Sperrschicht (Serienwiderstand * Kapazität) als weiterer Pol
  double series_resistance = 50.0;   // [Ohm]
  ModulatorKind modulator = ModulatorKind::ring;
  circuit::PnSection pn;  // PN-Übergang (Standard: 5e17 cm^-3, 500 x 220 nm, Gamma 0,8)
  double n_group = 4.2;
  double loss_db_per_cm = 2.0;  // Wellenleiterverlust ohne Dotierung
  // Mach-Zehnder (Gegentakt: Arm 1 bei V, Arm 2 bei v_low + v_high - V)
  double mzm_length = 3e-3;
  double mzm_bias_phase = 1.5707963267948966;  // Arbeitspunkt (pi/2 = Quadratur)
  // Ring
  double ring_radius = 10e-6;
  double ring_coupling = 0.02;  // Leistungskopplung kappa^2 (nahe kritischer Kopplung mit dem PN-Verlust)
  double ring_coverage = 0.8;   // Anteil des Umfangs mit PN-Übergang
  double ring_detuning =
      20e-12;  // Laser minus Resonanz bei v_low [m] (> 0: langwellige Seite; 2 V schieben ~21 pm)

  // Strecke und Empfänger
  double link_loss_db = 0.0;
  double responsivity = 1.0;        // [A/W]
  double dark_current = 10e-9;      // [A]
  double receiver_bandwidth = 0.0;  // 3-dB-Bandbreite (zwei gleiche Pole) [Hz]; 0 = 0,7 * Bitrate
  double load_resistance = 50.0;    // thermisches Rauschen 4kT/R_L [Ohm]
  double temperature = 300.0;
  bool noise = true;  // Schrot-, thermisches und RIN-Rauschen
};

struct LinkResult {
  double sample_interval = 0.0;       // [s]
  std::vector<int> bits;              // gesendete Bits
  std::vector<double> drive;          // Spannung am Übergang [V]
  std::vector<double> optical;        // Leistung am Empfänger [W]
  std::vector<double> current;        // Strom nach dem Empfangsfilter, mit Rauschen [A]
  std::vector<double> current_clean;  // ohne Rauschen [A]
  // Augendiagramm: Ausschnitte von zwei Bitdauern um den besten Abtastzeitpunkt (Mitte = Abtastung)
  std::vector<std::vector<double>> eye;
  int sample_phase = 0;               // bester Abtastpunkt innerhalb des Bits
  int latency_bits = 0;               // Laufzeit von der Quelle bis zur Entscheidung [Bits]
  double mean1 = 0.0, mean0 = 0.0;    // mittlere Ströme der Bits 1 und 0 an der Abtastung [A]
  double sigma1 = 0.0, sigma0 = 0.0;  // Standardabweichungen [A]
  double q_factor = 0.0;              // |mean1 - mean0| / (sigma1 + sigma0)
  double ber = 0.0;                   // 1/2 erfc(Q / sqrt 2)
  double extinction_db = 0.0;         // 10 log10(P_hoch / P_tief) der optischen Pegel an der Abtastung
  double eye_opening = 0.0;  // ohne Rauschen: (kleinste 1 - größte 0) / (mean1 - mean0); 1 = keine ISI
  double noise_sigma_expected0 = 0.0;  // erwartete Rauschamplitude aus den Formeln bei Bit 0 [A]
  double noise_sigma_expected1 = 0.0;
  double noise_bandwidth = 0.0;  // Rauschbandbreite des Empfängers [Hz]
};

// PRBS-Folge der Ordnung n (Fibonacci-LFSR, Polynome x^7+x^6+1, x^9+x^5+1, x^11+x^9+1, x^15+x^14+1), Länge
// count.
std::vector<int> prbs(int order, int count);

// Bitfehlerrate bei Gaußschem Rauschen: BER = 1/2 erfc(Q / sqrt 2)
double ber_from_q(double q);

// Ring-Modulator als Coupled-Mode-Modell: Umlaufzeit T = n_g L / c, kappa_in = kappa^2 / T, kappa_0 = (1 -
// a^2)/T mit dem Umlaufverlust a^2 (Wellenleiter + Restabsorption des PN-Übergangs bei der Spannung).
// Stationärer Durchgang bei Verstimmung delta = w_res - w_L: |1 - kappa_in / (gamma/2 + i delta)|^2.
struct RingCmt {
  double kappa_in = 0.0;
  double kappa_0 = 0.0;
  double detuning = 0.0;  // w_res - w_L [rad/s]
  double transmission() const;
};
RingCmt ring_cmt(const LinkSettings& settings, double voltage);

// Zeitbereich der Strecke. control (optional): Fortschritt und Abbruch (leeres Ergebnis).
LinkResult simulate_link(const LinkSettings& settings, JobControl* control = nullptr);

}  // namespace photonlab::signal
