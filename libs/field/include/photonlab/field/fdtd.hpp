#pragma once
// 2D-FDTD nach Yee: Leapfrog-Zeitschritt auf versetztem Gitter, CPML-Randschichten, weiche Quellen, Monitore.
// Gleichungen, Gitterlage und Annahmen: docs/physics/fdtd.md; Geschwindigkeit: docs/performance.md
// Ein Zeitschritt rechnet in Zeilenbändern auf mehreren Threads; das Ergebnis ist bitgleich für jede Zahl
// von Threads (jede Zelle wird unabhängig von der Aufteilung gleich gerechnet).
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "photonlab/core/job.hpp"
#include "photonlab/core/step_team.hpp"
#include "photonlab/field/scene.hpp"

namespace photonlab::field {

// Aufgezeichnete Messreihe eines Monitors.
struct MonitorData {
  std::string name;
  MonitorKind kind = MonitorKind::point;
  std::string unit;            // "V/m" bzw. "A/m" (Punkt, Mode), "W/m" (Fluss je Meter Bauteilhöhe)
  double mode_index = 0.0;     // Modenmonitor: effektiver Index der beobachteten Mode
  double start_time = 0.0;     // Zeit der ersten Probe [s]
  double sample_interval = 0;  // Abstand der Proben [s] (= Zeitschritt)
  std::vector<double> values;
};

class Fdtd {
 public:
  // Baut Gitter, Materialverteilung, Randschichten und Quellen. Wirft FieldError bei ungültiger Szene.
  explicit Fdtd(const FieldScene& scene);
  ~Fdtd();
  Fdtd(const Fdtd&) = delete;
  Fdtd& operator=(const Fdtd&) = delete;

  // Grafikkarte (OpenCL, siehe gpu.hpp): true schaltet um, wenn Gerät und Szene es erlauben (keine
  // Kerr-Formen, keine zusätzlichen Einspeisungen); Rückgabe: rechnet jetzt auf der Grafikkarte. false holt
  // den Zustand zurück auf den Prozessor. Ergebnisse sind gleich bis auf die Rundung (doppelte Genauigkeit).
  bool use_gpu(bool on);
  bool on_gpu() const { return gpu_ != nullptr; }

  void step();
  // Threads je Zeitschritt: 0 = automatisch (Kerne, aber mindestens etwa 32000 Zellen je Thread), 1 =
  // seriell. Läuft die Rechnung selbst schon parallel zu anderen (S-Parameter-Ports, Wahrheitstabellen), 1
  // setzen.
  void set_threads(int threads);
  int threads() const;
  // Faktor auf die Signale aller Quellen (Standard 1): erlaubt langsame Leistungsrampen, z. B. für Hysterese
  void set_source_scale(double scale) { source_scale_ = scale; }
  double source_scale() const { return source_scale_; }
  // Rechnet steps Zeitschritte; bricht ab, wenn control->cancel_requested gesetzt wird.
  void run(std::size_t steps, JobControl* control = nullptr);

  int nx() const { return nx_; }
  int ny() const { return ny_; }
  double cell_size() const { return dx_; }
  double time_step() const { return dt_; }
  double time() const { return static_cast<double>(steps_) * dt_; }  // Zeit des Skalarfeldes
  std::size_t step_count() const { return steps_; }
  Polarization polarization() const { return polarization_; }

  // Skalarfeld senkrecht zur Ebene: Ez [V/m] bzw. Hz [A/m], Zeile für Zeile (Index i + nx*j).
  // Ez liegt auf den Gitterknoten (i*dx, j*dx), Hz in den Zellmitten.
  const std::vector<double>& field() const {
    gpu_sync();
    return f_;
  }
  // Zum Setzen eines Anfangsfeldes; schaltet von der Grafikkarte zurück auf den Prozessor.
  std::vector<double>& mutable_field() {
    use_gpu(false);
    return f_;
  }
  // Felder in der Ebene, einen halben Zeitschritt versetzt: Hx, Hy [A/m] bzw. Ex, Ey [V/m].
  const std::vector<double>& in_plane_x() const {
    gpu_sync();
    return gx_;
  }
  const std::vector<double>& in_plane_y() const {
    gpu_sync();
    return gy_;
  }
  // Relative Permittivität an den Orten des Skalarfeldes (für Darstellung und Energie).
  const std::vector<double>& permittivity() const { return eps_f_; }
  // Ladungsträgerdichte [m^-3] am Knoten nahe (x, y); 0 ohne Ladungsträgermodell (Ez, Kerr-Zellen)
  double carrier_density_at(double x, double y) const;

  // Feldenergie je Meter Bauteilhöhe [J/m] in der Form, die das Leapfrog-Verfahren exakt erhält
  // (Skalarfeld als Produkt aufeinanderfolgender Zeitschritte).
  double energy() const;

  const std::vector<MonitorData>& monitors() const {
    gpu_sync();
    return monitors_;
  }

  // --- für inverses Design (Phase 13, nur Ez-Polarisation, ohne Kerr-Zellen) ---
  // Relative Permittivität am Knoten index ändern (vor oder während der Rechnung).
  void set_permittivity(std::size_t index, double eps);
  // Zellen und Gewichte eines Modenmonitors: Modenamplitude = Summe(Feld[cells[k]] * weights[k]).
  struct ModeWeights {
    std::vector<std::size_t> cells;
    std::vector<double> weights;
  };
  ModeWeights mode_weights(std::size_t monitor) const;
  // Zusätzliche Quelle mit beliebigem Profil: je Schritt Feld[cells[k]] += weights[k] * Signal(t).
  // Genutzt werden signal, wavelength, phase, ramp_periods, pulse_duration und amplitude (als Faktor).
  void add_injection(const std::vector<std::size_t>& cells, const std::vector<double>& weights,
                     const Source& signal);

  // Zeitsignal einer Quelle (ohne Amplitude), öffentlich für Tests und Darstellung.
  static double signal(const Source& source, double time);

 private:
  struct SourceCell {
    std::size_t index;
    double weight;  // Zuwachs des Skalarfeldes pro Zeitschritt für Signal = 1
  };
  struct ActiveSource {
    Source source;
    std::vector<SourceCell> cells;
    // einseitige Modenquelle: zweite Schicht, deren Signal um extra_phase verschoben ist
    std::vector<SourceCell> cells2;
    double extra_phase = 0.0;
    // Regenerator: Monitor, Fenster (Schritte je Periode), geplante Umschaltungen und laufende Rampe
    bool triggered = false;
    std::size_t monitor = 0;
    bool mode_power = false;  // Modenmonitor: a^2 mitteln
    std::size_t window = 1;
    bool target = false;                                    // zuletzt entschiedener Zustand
    std::vector<std::pair<double, bool>> pending;           // (Zeit, Zustand), nach Zeit geordnet
    double ramp_start = 0.0, env_from = 0.0, env_to = 0.0;  // laufende Rampe der Einhüllenden
    double envelope(double time) const;
  };
  void update_triggers();  // Entscheidungen der Regeneratoren nach record(), alle decision_interval_ Schritte
  // Signale aller Quellen zur Zeit t, zwei Plätze je Quelle (Schicht 1 und 2); übernimmt fällige
  // Umschaltungen der Regeneratoren (deshalb nicht const)
  void source_signals(double time, std::vector<double>& out);
  std::size_t decision_interval_ = 0;  // 0 = keine Regeneratoren
  std::vector<double> signals_;

  // Grafikkarte: Zustand in gpu.cpp; die Felder auf dem Prozessor sind dann nur Zwischenspeicher
  struct GpuState;
  struct GpuStateDeleter {
    void operator()(GpuState* state) const;
  };
  std::unique_ptr<GpuState, GpuStateDeleter> gpu_;
  void gpu_step();
  void gpu_sync() const;  // Felder und Messreihen holen, falls die Grafikkarte weiter ist
  struct FluxLine {
    std::size_t monitor;   // Index in monitors_
    bool vertical = true;  // senkrechte Linie: Fluss in +x, sonst in +y
    int fixed = 0;         // i (senkrecht) bzw. j (waagerecht)
    int from = 0, to = 0;  // Bereich der laufenden Koordinate, einschließlich
  };
  struct ModeProbe {
    std::size_t monitor;
    std::vector<std::size_t> cells;
    std::vector<double> weights;  // Profil * Gewicht / Norm: Summe(Feld * weights) = Modenamplitude
  };
  struct PointProbe {
    std::size_t monitor;
    std::size_t index;
  };

  template <class Body>
  void for_rows(const Body& body);
  void step_ez();
  void step_hz();
  void record();
  std::size_t at(int i, int j) const {
    return static_cast<std::size_t>(i) + static_cast<std::size_t>(nx_) * static_cast<std::size_t>(j);
  }

  Polarization polarization_;
  int nx_ = 0, ny_ = 0;
  double dx_ = 0.0, dt_ = 0.0;
  bool periodic_x_ = false, periodic_y_ = false;
  std::size_t steps_ = 0;

  mutable std::vector<double> f_, f_prev_, gx_, gy_;  // mutable: Zwischenspeicher beim Rechnen auf der GPU
  std::vector<double> zero_row_;                      // Nachbarzeile jenseits einer ideal leitenden Wand
  std::vector<double> eps_f_;                         // relative Permittivität am Skalarfeld
  // Kerr-Zellen (nur Ez): D/eps0 wird mitgeführt, E aus eps*E + chi3*E^3 = D/eps0 per Newton-Verfahren
  struct KerrCell {
    std::size_t index;
    double eps;       // lineare relative Permittivität
    double chi3;      // [m^2/V^2]
    double sigma2;    // TPA als nichtlineare Leitfähigkeit sigma = sigma2 E^2 [S m/V^2 ... S/m je (V/m)^2]
    double tau;       // Lebensdauer der Ladungsträger [s]; 0 = keine
    double sigma_fc;  // Leitfähigkeit je Ladungsträger [S/m pro m^-3]
    double eps_fc;    // d(eps)/dN [m^3]
    double d;         // D/eps0 [V/m]
    double n;         // Ladungsträgerdichte [m^-3]
    double e_old;     // Feld vor dem Schritt [V/m]
  };
  std::vector<KerrCell> kerr_cells_;    // Ez (Skalarfeld)
  std::vector<KerrCell> kerr_x_cells_;  // Hz-Polarisation: Ex
  std::vector<KerrCell> kerr_y_cells_;  // Hz-Polarisation: Ey
  // Nichtlinearer Schritt einer Zelle: e_lin = Ergebnis des linearen Yee-Schritts, other_sq = Quadrat der
  // anderen Komponente in der Ebene (Hz-Polarisation; Ez: 0). Rückgabe: neues E.
  double nonlinear_update(KerrCell& cell, double e_lin, double other_sq) const;
  double source_scale_ = 1.0;
  double hbar_omega_ = 1.28e-19;   // Photonenenergie der ersten Quelle [J] (für die Paarerzeugung)
  std::vector<double> cf_;         // Update-Koeffizient des Skalarfeldes (Ez: dt/(eps0*eps_r), Hz: dt/mu0)
  std::vector<double> cgx_, cgy_;  // Update-Koeffizienten der Felder in der Ebene
  // CPML: Koeffizienten je Achse an ganzen (e) und halben (h) Gitterplätzen, Hilfsfelder psi
  std::vector<double> bxe_, cxe_, bxh_, cxh_, bye_, cye_, byh_, cyh_;
  std::vector<double> psi_fx_, psi_fy_, psi_gx_, psi_gy_;
  bool pml_x_ = false, pml_y_ = false;
  int ix_lo_ = 0, ix_hi_ = 0;  // Spalten ohne Randschicht in x: [ix_lo_, ix_hi_)
  int requested_threads_ = 0;
  std::unique_ptr<StepTeam> team_;

  std::vector<ActiveSource> sources_;
  mutable std::vector<MonitorData> monitors_;
  std::vector<PointProbe> probes_;
  std::vector<FluxLine> flux_lines_;
  std::vector<ModeProbe> mode_probes_;
};

}  // namespace photonlab::field
