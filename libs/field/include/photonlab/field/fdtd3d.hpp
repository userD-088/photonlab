#pragma once
// 3D-FDTD nach Yee mit CPML an allen sechs Seiten, linear und verlustfrei, Brechzahl je Quader.
// Gitter, Gleichungen und Grenzen: docs/physics/fdtd3d.md; Geschwindigkeit: docs/performance.md
//
// Lage der Komponenten (Zelle (i, j, k) mit Kantenlänge dx, Index i + nx*(j + ny*k)):
//   Ex (i+1/2, j, k)    Ey (i, j+1/2, k)    Ez (i, j, k+1/2)
//   Hx (i, j+1/2, k+1/2)  Hy (i+1/2, j, k+1/2)  Hz (i+1/2, j+1/2, k)
// Außerhalb des Gitters ist E = 0 (ideal leitende Wand); auf den Wänden i = 0, j = 0, k = 0 bleiben die
// tangentialen E-Komponenten 0. Die CPML davor dämpft auslaufende Wellen.
#include <array>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "photonlab/core/job.hpp"
#include "photonlab/core/step_team.hpp"
#include "photonlab/field/scene.hpp"

namespace photonlab::field {

// Quader [x0, x1] x [y0, y1] x [z0, z1] mit fester Brechzahl; spätere Quader überdecken frühere.
struct Block {
  std::string name;
  double x0 = 0.0, x1 = 0.0, y0 = 0.0, y1 = 0.0, z0 = 0.0, z1 = 0.0;  // [m]
  double index = 1.0;
};

struct Scene3d {
  std::string name;
  double size_x = 2e-6, size_y = 2e-6, size_z = 2e-6;  // Rechengebiet ab dem Ursprung [m]
  double cell_size = 20e-9;                            // Würfelzelle [m]
  double background_index = 1.0;
  int pml_cells = 10;     // CPML-Dicke an jeder Seite (0 = ideal leitende Box)
  double courant = 0.99;  // Anteil an der Stabilitätsgrenze dt = dx/(c*sqrt(3))
  std::vector<Block> blocks;
  // Optional: Brechzahl an einem Punkt (x, y, z) statt Hintergrund und Quadern, z. B. eine extrudierte
  // Feld-Szene (extrude3d.hpp). Wird nur im Konstruktor aufgerufen.
  std::function<double(double x, double y, double z)> index_function;
};

enum class Component3d { ex, ey, ez, hx, hy, hz };

// Weiche Quelle: pro Zeitschritt wächst die E-Komponente in jeder Zelle um weight * Signal(t). Das Signal
// (Dauerstrich mit Rampe oder Gauß-Puls) ist dasselbe wie im 2D-FDTD (Fdtd::signal).
struct Source3d {
  Component3d component = Component3d::ey;
  std::vector<std::size_t> cells;
  std::vector<double> weights;
  Source signal;  // genutzt: signal, wavelength, phase, ramp_periods, pulse_duration
};

// Prüft die Szene; wirft FieldError mit Klartext.
void validate(const Scene3d& scene);

// Wert einer Feldkomponente in einer Zelle, nach jedem Schritt mitgeschrieben (Monitore)
struct Probe3d {
  Component3d component = Component3d::ey;
  std::size_t cell = 0;
};

class Fdtd3d {
 public:
  explicit Fdtd3d(const Scene3d& scene);
  ~Fdtd3d();
  Fdtd3d(const Fdtd3d&) = delete;
  Fdtd3d& operator=(const Fdtd3d&) = delete;

  void add_source(Source3d source);
  void step();
  // Rechnet steps Zeitschritte; bricht ab, wenn control->cancel_requested gesetzt wird.
  void run(std::size_t steps, JobControl* control = nullptr);
  // Wie run, aber ohne Fortschritt; values bekommt nach jedem Schritt die Werte aller Proben angehängt
  // (Schritt für Schritt hintereinander). Auf der Grafikkarte kommen nur diese Werte zurück, nicht die
  // Felder.
  void run_probed(std::size_t steps, const std::vector<Probe3d>& probes, std::vector<double>* values,
                  JobControl* control = nullptr);

  // Grafikkarte (gpu.hpp, OpenCL): gleiche Formeln in gleicher Reihenfolge wie auf dem Prozessor, bitgleich.
  // Gibt zurück, ob jetzt dort gerechnet wird. Felder kommen beim Lesen (component, energy) zurück; Schreiben
  // über component() schaltet auf den Prozessor zurück.
  bool use_gpu(bool on);
  bool on_gpu() const { return static_cast<bool>(gpu_); }
  // 0 = automatisch (Kerne, aber mindestens etwa 32000 Zellen je Thread), 1 = seriell
  void set_threads(int threads);
  int threads() const;

  int nx() const { return nx_; }
  int ny() const { return ny_; }
  int nz() const { return nz_; }
  std::size_t cell_count() const { return f_[0].size(); }
  double cell_size() const { return dx_; }
  double time_step() const { return dt_; }
  double time() const { return static_cast<double>(steps_) * dt_; }  // Zeit der E-Felder
  std::size_t step_count() const { return steps_; }
  std::size_t at(int i, int j, int k) const {
    return static_cast<std::size_t>(i) +
           static_cast<std::size_t>(nx_) *
               (static_cast<std::size_t>(j) + static_cast<std::size_t>(ny_) * static_cast<std::size_t>(k));
  }

  // Feldkomponente [V/m bzw. A/m]; schreibbar für Anfangsbedingungen in Tests
  const std::vector<double>& component(Component3d c) const {
    gpu_sync();
    return f_[static_cast<std::size_t>(c)];
  }
  std::vector<double>& component(Component3d c) {
    use_gpu(false);
    return f_[static_cast<std::size_t>(c)];
  }
  // Relative Permittivität am Ort einer E-Komponente (gemittelt über die Zelle, siehe fdtd3d.md)
  double permittivity(Component3d c, std::size_t index) const;
  // Feldenergie [J] (E und H zu leicht versetzten Zeiten, für Plausibilität und Stabilität)
  double energy() const;

 private:
  template <class Body>
  void for_planes(const Body& body);
  void update_h(int k);
  void update_e(int k);
  void pml_plane(int k, bool e_phase);
  struct GpuState;
  struct GpuStateDeleter {
    void operator()(GpuState* state) const;
  };
  void gpu_step();
  void gpu_sync() const;  // Felder von der Grafikkarte holen, falls veraltet
  void gpu_run_probed(std::size_t steps, const std::vector<Probe3d>& probes, std::vector<double>* values,
                      JobControl* control);
  std::unique_ptr<GpuState, GpuStateDeleter> gpu_;

  int nx_ = 0, ny_ = 0, nz_ = 0, pml_ = 0;
  double dx_ = 0.0, dt_ = 0.0;
  std::size_t steps_ = 0;
  std::vector<double> zero_row_;  // Nachbarzeile jenseits der Wand
  mutable std::array<std::vector<double>, 6>
      f_;  // Ex, Ey, Ez, Hx, Hy, Hz (Kopie, solange die Grafikkarte rechnet)
  std::array<std::vector<double>, 3> coef_;  // dt/(eps0*eps_r) an Ex, Ey, Ez
  // CPML-Profile je Achse an ganzen (e) und halben (h) Gitterplätzen
  std::array<std::vector<double>, 3> be_, ce_, bh_, ch_;
  // Hilfsfelder psi nur in den Randschichten: je Feldkomponente und Ableitungsrichtung (12 Stück)
  struct PmlTerm {
    int target;  // Index in f_
    int source;  // Index in f_
    int axis;    // Ableitungsrichtung 0 = x, 1 = y, 2 = z
    double sign;
    std::vector<double> psi;
  };
  std::vector<PmlTerm> pml_terms_;
  std::vector<Source3d> sources_;
  int requested_threads_ = 0;
  std::unique_ptr<StepTeam> team_;
};

}  // namespace photonlab::field
