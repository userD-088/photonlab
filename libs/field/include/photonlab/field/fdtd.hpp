#pragma once
// 2D-FDTD nach Yee: Leapfrog-Zeitschritt auf versetztem Gitter, CPML-Randschichten, weiche Quellen, Monitore.
// Gleichungen, Gitterlage und Annahmen: docs/physics/fdtd.md
#include <cstddef>
#include <string>
#include <vector>

#include "photonlab/core/job.hpp"
#include "photonlab/field/scene.hpp"

namespace photonlab::field {

// Aufgezeichnete Messreihe eines Monitors.
struct MonitorData {
  std::string name;
  MonitorKind kind = MonitorKind::point;
  std::string unit;            // "V/m" bzw. "A/m" (Punkt), "W/m" (Fluss je Meter Bauteilhöhe)
  double start_time = 0.0;     // Zeit der ersten Probe [s]
  double sample_interval = 0;  // Abstand der Proben [s] (= Zeitschritt)
  std::vector<double> values;
};

class Fdtd {
 public:
  // Baut Gitter, Materialverteilung, Randschichten und Quellen. Wirft FieldError bei ungültiger Szene.
  explicit Fdtd(const FieldScene& scene);

  void step();
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
  const std::vector<double>& field() const { return f_; }
  std::vector<double>& field() { return f_; }
  // Felder in der Ebene, einen halben Zeitschritt versetzt: Hx, Hy [A/m] bzw. Ex, Ey [V/m].
  const std::vector<double>& in_plane_x() const { return gx_; }
  const std::vector<double>& in_plane_y() const { return gy_; }
  // Relative Permittivität an den Orten des Skalarfeldes (für Darstellung und Energie).
  const std::vector<double>& permittivity() const { return eps_f_; }

  // Feldenergie je Meter Bauteilhöhe [J/m] in der Form, die das Leapfrog-Verfahren exakt erhält
  // (Skalarfeld als Produkt aufeinanderfolgender Zeitschritte).
  double energy() const;

  const std::vector<MonitorData>& monitors() const { return monitors_; }

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
  };
  struct FluxLine {
    std::size_t monitor;   // Index in monitors_
    bool vertical = true;  // senkrechte Linie: Fluss in +x, sonst in +y
    int fixed = 0;         // i (senkrecht) bzw. j (waagerecht)
    int from = 0, to = 0;  // Bereich der laufenden Koordinate, einschließlich
  };
  struct PointProbe {
    std::size_t monitor;
    std::size_t index;
  };

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

  std::vector<double> f_, f_prev_, gx_, gy_;
  std::vector<double> zero_row_;   // Nachbarzeile jenseits einer ideal leitenden Wand
  std::vector<double> eps_f_;      // relative Permittivität am Skalarfeld
  std::vector<double> cf_;         // Update-Koeffizient des Skalarfeldes (Ez: dt/(eps0*eps_r), Hz: dt/mu0)
  std::vector<double> cgx_, cgy_;  // Update-Koeffizienten der Felder in der Ebene
  // CPML: Koeffizienten je Achse an ganzen (e) und halben (h) Gitterplätzen, Hilfsfelder psi
  std::vector<double> bxe_, cxe_, bxh_, cxh_, bye_, cye_, byh_, cyh_;
  std::vector<double> psi_fx_, psi_fy_, psi_gx_, psi_gy_;
  bool pml_x_ = false, pml_y_ = false;

  std::vector<ActiveSource> sources_;
  std::vector<MonitorData> monitors_;
  std::vector<PointProbe> probes_;
  std::vector<FluxLine> flux_lines_;
};

}  // namespace photonlab::field
