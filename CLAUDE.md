# PhotonLab – Kontext für Claude

## Projekt
Photonik-Simulator in C++20 mit zwei Ebenen:
- Feld (Bauteil-Labor): 2D-FDTD (linear + nichtlinear, GPU), 3D-Rechnung der Szene, Modenlöser, PWE-Bandstruktur, inverses Design
- Schaltung (Chip-Designer): S-Matrizen, aktive und nichtlineare Kompaktmodelle, Fenster „Logik“ (Bits schalten, Wahrheitstabelle, Zeugnis)
Brücke: S-Parameter-Extraktion (Feld → Schaltung, Reiter „Bauteil für den Chip-Designer“).
Ziel: physikalisch realistisch, ehrlich bewertet, praktisch nutzbar; Schaltungen sollen als echter Chip baubar sein.

Der alte Bauplan (PDF) ist abgelöst; Quanten-Modus, Augendiagramm und Logik-Event-Simulator wurden entfernt.
Interaktiver Prototyp (JS, gleiche Physik): `docs/prototypes/photonische_gatter.html`.
Offene Aufgaben: `docs/ISSUES.md`.

## Build
```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```
Ohne GUI (schneller, z. B. für reine Physik-Arbeit): `-DPHOTONLAB_BUILD_GUI=OFF`

## Struktur
- `libs/core` – Einheiten, Konstanten, komplexe Zahlen, Threadpool, JobControl, `parallel_for`, `StepTeam` (feste Thread-Gruppe für Zeitschritte)
- `libs/circuit` – Component-Interface und -Registry, Plugin-Lader (`plugin.*`, C-Schnittstelle `plugin_api.h`), S-Parameter-Bauteil (`sparam:<name>`), Netzliste, linearer Löser, Sweep, Resonanz-Auswertung, Monte-Carlo, Referenz-Designs
- `libs/materials` – Sellmeier-Gleichungen (Si, SiO2, Si3N4), Gruppenindex, dn/dT; jede Zahl mit Quelle
- `libs/modes` – Schichtwellenleiter analytisch, 1D-Profil-Löser, semi-vektorieller Modenlöser für Querschnitte
- `libs/field` – Feld-Szene (Formen, Gruppen, Quellen mit Regenerator und einseitiger Abstrahlung, Monitore, Ports), Bausteine (`add_building_block`), Grafikkarte (`gpu.*`, OpenCL zur Laufzeit geladen), 2D-FDTD nach Yee mit CPML (Threads je Zeitschritt), 3D-FDTD (`fdtd3d.*`, Quader) mit Abnahme-Experiment `waveguide3d.*`, inverses Design (`inverse_design.*`: Adjoint, Filter, Projektion), Moden- und Linienquellen, Spektrum, S-Parameter-Extraktion, Kristall-Generatoren (`photonic_crystal.*`: Gitter, Liniendefekte, Punktdefekte)
- `libs/pwe` – PWE-Bandstruktur photonischer Kristalle (2D, ez/hz), Lücken, Pfad Gamma-M-K bzw. Gamma-X-M
- `libs/logic` – Gatter-Definition (ein Eingang darf mehrere Instanzen schalten, Ausgänge mit fester Schwelle), Wahrheitstabellen-Läufer, Schwellwert-Analyse, Gatter-Zeugnis, Referenz-Gatter (Addierer), Gatter auf Feld-Ebene (`field_gate.*`: Eingänge = Quellen einer FDTD-Szene)
- `libs/layout` – Polygone aus Feld-Szenen und Pixelbildern, GDSII schreiben/lesen
- `plugins/example` – Beispiel-Plugin (Bragg-Gitter, MMI), gebaut nach `build/plugins/`
- `libs/io` – Projektdatei `.plab.json` (mit Bauteilbibliothek), `.sparam.json`, `.gate.json` (alle Zahlen in SI); `make_registry(project)` liefert die Registry eines Projekts
- `apps/photonlab_gui` – SDL3 + Dear ImGui (Docking) + ImPlot; Bauteil-Labor in `field_lab.cpp` (Reiter Feld, Bauteil für den Chip-Designer = `sparam_view.cpp`, Logik prüfen = `field_gate_view.cpp`, Moden und Bänder = `mode_view.cpp`/`band_view.cpp`/`wave3d_view.cpp`, Inverses Design = `inverse_view.cpp`), Chip-Designer in `chip_designer.cpp` mit dem Fenster Logik in `logic_view.cpp`; Plugins werden in `main.cpp` beim Start geladen
- `apps/photonlab_cli` – Kommandozeile: `components`, `example`, `materials`, `bands`, `field-gate`, `pcm`, `bistability`, `fdtd3d`, `bench`, `gds`, `inverse-design`, `plugins` (Option `--plugin`), `run [--sweep | --resonances | --monte-carlo | --logic | --field | --modes | --sparam] [--csv | --out]`, `validate`
- `examples` – Referenz-Designs als `.plab.json` (erzeugt mit `photonlab_cli example <name>`)
- `tests` – doctest; Physik-Validierung gegen analytische Lösungen
- `cmake/Dependencies.cmake` – alle externen Abhängigkeiten, Versionen gepinnt

## Regeln
- `libs/` bindet NIE SDL oder ImGui ein. Grafik nur in `apps/`.
- Intern nur SI. Nicht-SI-Variablen mit Einheit im Namen (`length_um`).
- Komplexe Zahlen: `photonlab::cplx` (= `std::complex<double>`), |a|² = Leistung in Watt.
- Neue Physik nur mit Test gegen eine bekannte Lösung.
- Materialkonstanten mit Quellenangabe im Kommentar.
- Bauteile über eine Component-Registry, nie Sonderfälle im Löser.
- Keine neuen Abhängigkeiten ohne Rückfrage; Versionen in `cmake/Dependencies.cmake` pinnen.
- Kleine Änderungen; ein Issue pro Branch.
- Nach jeder Änderung: bauen, alle Tests ausführen, Ergebnis berichten.
- Physik-Notiz in `docs/physics/` ergänzen, wenn Gleichungen dazukommen.
- Texte der Oberfläche und Anzeigenamen in der Registry nur mit Latin-1-Zeichen (kein Griechisch, keine Pfeile): die eingebaute ImGui-Schrift kennt nichts anderes.
- Neues Bauteil = neue Klasse in `libs/circuit/src/builtin_components.cpp` + Eintrag in `register_builtin_components`; Editor, CLI und Dateiformat übernehmen es automatisch.
- Lange Rechnungen als Job: `parallel_for` mit `JobControl` (Fortschritt, Abbruch); Zufall nur über `standard_normal` (plattformunabhängig reproduzierbar), nie `<random>`-Verteilungen.
- Bewertungen (Ampeln im Zeugnis) immer messen, nie annehmen; Schwellen und Messvorschrift in `docs/physics/logic.md` dokumentieren und als Festlegung kennzeichnen.
- Feld-Ebene: Polarisationen heißen nach ihrer Feldkomponente (`ez`, `hz`), nie TE/TM ohne Erklärung; Orte bei Hz in Zellmitten angeben (Rundung an Zellkanten).
- GUI-Jobs, die laufend Bilder liefern (FDTD): Löser, Steuerung und Schnappschuss per `shared_ptr` an den Job geben; der Job greift nie auf Objekte der Oberfläche zu.
- Eigenwertprobleme: Ergebnisse immer über das Residuum prüfen und nur geprüfte Moden melden (eigener Löser in `libs/modes/src/fde.cpp`; Spectra lieferte ungenaue Werte und ist nicht eingebunden).
- Brechzahlen nie fest eintippen: aus `libs/materials` bzw. aus dem Schichtlöser berechnen.
- Registry: nie `ComponentRegistry::builtin()` für geladene Projekte verwenden, sondern `io::make_registry(project)` (GUI: `active_registry()`, in Jobs `active_registry_ptr()` mitnehmen), sonst fehlen die Bauteile der Bibliothek.
- Referenz-Szenen/-Designs: berechnete Koordinaten (cos, atan2 ...) runden (`round_pico`), sonst unterscheiden sich die Beispieldateien zwischen Compilern im letzten Bit und der Beispiel-Test schlägt in der CI fehl.
- Tests: `doctest::Approx(x).epsilon(e)` rechnet mit `scale = 1` (Toleranz e*(1+|x|)) und ist für kleine Zahlen praktisch absolut: immer `.scale(0.0)` setzen, wenn relativ verglichen werden soll.
- Grafikkarte: Kerne in `libs/field/src/gpu.cpp` rechnen dieselben Formeln in derselben Reihenfolge wie `fdtd.cpp` (bitgleich, `tests/test_gpu.cpp`); jede Änderung am Prozessor-Kern dort nachziehen. Nur OpenCL-Header als Abhängigkeit, die Bibliothek wird zur Laufzeit geladen. Bibliothek und Tests rechnen ohne `gpu::set_enabled(true)` auf dem Prozessor.
- Feld-Szene: deaktivierte Objekte (`enabled = false`) wirken nirgends; Löser nur über `field::active_scene` bzw. `Fdtd(scene)` füttern, nie die Listen direkt filtern.
- Nichtlinearität im FDTD (Ez und Hz): Quellen nicht in nichtlinearen Formen platzieren (siehe `docs/physics/nonlinear.md`).
- Nichtlineare Bauteile: `state_size() > 0`, `s_matrix_at`, `settle_state`; der Löser iteriert. Für Hysterese Rampen (`wavelength_ramp`, `power_ramp`) mit weitergereichtem Zustand verwenden, nie parallel unabhängig.
- Bauteile mit gespeichertem Zustand (PCM): Zustand als Parameter, Änderungen über `ComponentType::actions` (Knöpfe im Eigenschaften-Fenster), nie Sonderfälle in der Oberfläche.
- FDTD-Kerne: Schleifen verzweigungsfrei und zusammenhängend über i halten (Vektorisierung), Randschicht-Terme nur in den Schichtzellen; Ergebnis muss für jede Thread-Zahl bitgleich bleiben (Test in `tests/test_fdtd3d.cpp`). Mehrere FDTD-Läufe parallel: `set_threads(StepTeam::share_for(...))`. Geschwindigkeit mit `photonlab_cli bench` messen (`docs/performance.md`).
- Plugins: nur beim Programmstart laden (`load_plugin`, `load_plugins_from`), nie während Jobs laufen; die Schnittstelle bleibt reines C und versioniert (`PHOTONLAB_PLUGIN_API_VERSION` erhöhen statt Strukturen ändern). Plugin-Bauteile sind normale Registry-Typen, keine Sonderfälle.
- Inverses Design: Gradienten immer gegen finite Differenzen prüfen (`check_gradient`), bevor eine neue Zielfunktion oder Parametrisierung benutzt wird.
- Code-Stil: `.clang-format`; Funktionen/Variablen snake_case, Klassen PascalCase; Kommentare auf Deutsch.

## Physik-Referenzen
- Courant 2D: dt <= dx / (c * sqrt(2)); 3D: dt <= dx / (c * sqrt(3)) (`docs/physics/fdtd3d.md`); Yee-Dispersion: sin²(w*dt/2) = S²*(sin²(kx*dx/2) + sin²(ky*dx/2)) (Details: `docs/physics/fdtd.md`)
- Richtkoppler: S = [[t, iκ], [iκ, t]]
- Schaltungslöser: (I - S*C) b = S a_ext + b_src (Details: `docs/physics/circuit_solver.md`)
- Wellenleiter: n_eff(lambda) = n_eff0 + (n_eff0 - n_g) * (lambda - lambda0) / lambda0
- Ring (All-Pass): tau = (t - H)/(1 - t*H), H = a*exp(i*phi); FSR = lambda²/(n_g*L) (Details: `docs/physics/resonators.md`)
- Schichtwellenleiter: kappa*d = atan(w2*g2/kappa) + atan(w3*g3/kappa) + m*pi, w = 1 (TE) bzw. (n1/n_Mantel)² (TM) (Details: `docs/physics/modes.md`)
- Kerr: n = n0 + n2 * I
- Kontrastverhältnis: CR = 10*log10(P1_min / P0_max)
