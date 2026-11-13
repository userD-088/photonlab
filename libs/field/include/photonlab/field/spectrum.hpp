#pragma once
// Spektrum einer Zeitreihe per direkter Fourier-Summe (DFT-Monitor).
#include <vector>

namespace photonlab::field {

// Amplitude |X(f)| = (2/N) * |Summe x_k * exp(-2*pi*i*f*t_k)| für die angegebenen Frequenzen [Hz].
// Für einen Dauerstrich A*sin(2*pi*f0*t) über ganze Perioden liefert das A bei f0.
std::vector<double> amplitude_spectrum(const std::vector<double>& samples, double sample_interval,
                                       const std::vector<double>& frequencies);

// Mittelwert der letzten Proben über eine ganze Zahl von Perioden (für Dauerstrich-Flüsse).
double average_over_periods(const std::vector<double>& samples, double sample_interval, double period,
                            int periods = 4);

}  // namespace photonlab::field
