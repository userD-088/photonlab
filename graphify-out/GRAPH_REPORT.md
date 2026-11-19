# Graph Report - photonlab  (2026-11-19)

## Corpus Check
- 226 files · ~202,045 words
- Verdict: corpus is large enough that graph structure adds value.
- Unclassified: 8 file(s) not represented in the graph (top: .csv 4, (none) 2, .cmake 2)

## Summary
- 3344 nodes · 6621 edges · 193 communities (187 shown, 6 thin omitted)
- Extraction: 93% EXTRACTED · 7% INFERRED · 0% AMBIGUOUS · INFERRED: 431 edges (avg confidence: 0.85)
- Token cost: 0 input · 0 output

## Community Hubs (Navigation)
- chip_designer
- project
- fdtd
- field_lab
- test_fdtd3d
- ?
- main
- quantum_view
- sparam_view
- chip_designer
- kerr_cavity
- report
- photonic_crystal
- scene
- test_field_fdtd
- logic_sim
- test_quantum
- fdtd3d
- test_pwe
- link
- field_lab
- pwe
- reference_scenes
- step_team
- builtin_components
- material
- inverse_view
- link
- test_circuit_resonators
- scene
- waveguide3d
- gate
- file_dialog
- fdtd
- reference_designs
- fdtd
- fock
- monte_carlo
- gate
- report
- logic_sim
- builtin_components
- pcm
- band_view
- logic_sim_view
- logic_sim
- ?
- sweep
- mode_view
- solver
- test_circuit_solver
- logic_lab
- registry
- builtin_components
- scene
- mesh
- thread_pool
- builtin_components
- parallel_for
- fdtd3d
- inverse_design
- inverse_design
- inverse_design
- chip_designer
- builtin_components
- registry
- builtin_components
- gds
- truth_table
- cross_section
- registry
- logic_lab
- logic_sim
- eye_view
- ci
- netlist
- inverse_design
- logic_sim
- silicon_carriers
- waveguide3d
- field_lab
- plugin
- scene
- logic_sim
- slab
- analysis
- scene
- sparameters
- layout
- logic_sim
- logic_sim
- cross_section
- test_logic
- mode_view
- band_view
- wave3d_view
- layout
- sparameters
- gds
- experiments
- link
- chip_designer
- field_gate_view
- active_devices
- component
- plugin_api
- test_layout
- field_gate
- fde
- mesh
- pwe
- truth_table
- fde
- pwe
- pwe
- chip_designer
- field_gate_view
- builtin_components
- units
- fdtd3d
- fdtd3d
- field_gate
- logic_sim
- phase_change
- experiments
- test_circuit_nonlinear
- wave3d_view
- test_active
- solver
- builtin_components
- sparameters
- layout
- experiments
- inverse_view
- netlist
- plugin
- builtin_components
- monte_carlo
- fdtd3d
- sparameters
- test_circuit_components
- field_gate_view
- wave3d_view
- plugin_api
- builtin_components
- builtin_components
- fdtd3d
- fdtd
- scene
- scene
- field_gate
- profile
- pwe
- experiments
- example_plugin
- plugin_api
- builtin_components
- builtin_components
- sparameters
- field_gate
- optimize
- pwe
- mesh
- mesh
- chip_designer
- chip_designer
- field_lab
- monte_carlo
- plugin
- step_team
- fdtd
- waveguide3d
- report
- cross_section
- slab
- pwe
- pwe
- link
- wave3d_view
- reference_designs
- solver
- inverse_design
- inverse_design
- test_nonlinear_field
- inverse_view
- layout
- netlist
- ?
- thread_pool
- pwe
- link
- ?

## God Nodes (most connected - your core abstractions)
1. `ChipDesigner` - 180 edges
2. `Fdtd` - 103 edges
3. `FieldLab` - 83 edges
4. `FieldScene` - 77 edges
5. `Circuit` - 71 edges
6. `Fdtd3d` - 55 edges
7. `QuantumView` - 50 edges
8. `ComponentRegistry` - 47 edges
9. `ThreadPool` - 45 edges
10. `ComponentType` - 43 edges

## Surprising Connections (you probably didn't know these)
- `cmd_info()` --calls--> `expi()`  [INFERRED]
  apps/photonlab_cli/main.cpp → libs/core/include/photonlab/core/complex.hpp
- `ChipDesigner::update_solution()` --references--> `CompiledCircuit`  [INFERRED]
  apps/photonlab_gui/chip_designer.cpp → libs/circuit/include/photonlab/circuit/solver.hpp
- `ChipDesigner::update_solution()` --references--> `Solution`  [INFERRED]
  apps/photonlab_gui/chip_designer.cpp → libs/circuit/include/photonlab/circuit/solver.hpp
- `ChipDesigner::update_curve()` --references--> `Circuit`  [INFERRED]
  apps/photonlab_gui/chip_designer.cpp → libs/circuit/include/photonlab/circuit/netlist.hpp
- `ChipDesigner::draw_properties()` --references--> `Circuit`  [INFERRED]
  apps/photonlab_gui/chip_designer.cpp → libs/circuit/include/photonlab/circuit/netlist.hpp

## Import Cycles
- None detected.

## Communities (193 total, 6 thin omitted)

### Community 0 - "chip_designer"
Cohesion: 0.03
Nodes (65): ChipDesigner, auto_sweep_, balance_, cell_, compiled_, curve_, curve_max_, curve_min_ (+57 more)

### Community 1 - "project"
Cohesion: 0.05
Nodes (55): SParameterTable, description, label, left_port_count, name, ports, s, source_hash (+47 more)

### Community 2 - "fdtd"
Cohesion: 0.03
Nodes (49): Fdtd, bxe_, bxh_, bye_, byh_, cf_, cgx_, cgy_ (+41 more)

### Community 3 - "field_lab"
Cohesion: 0.04
Nodes (47): FieldLab, band_view_, boost_weak_, display_mode_, DisplayMode, field, intensity, dragging_ (+39 more)

### Community 4 - "test_fdtd3d"
Cohesion: 0.04
Nodes (36): cplx, power(), "Abnahme Phase 12: Si-Draht 500 x 220 nm im 3D-FDTD", "Abnahme Phase 12: Si-Schicht im 3D-FDTD = analytischer Schichtwellenleiter", "Phase 12: 2D-FDTD rechnet mit jeder Zahl von Threads bitgleich", "Phase 12: 3D-Energie bleibt im geschlossenen Hohlraum beschränkt, Threads ändern nichts", "Phase 12: 3D-Hohlraum schwingt mit der Frequenz der Yee-Dispersionsrelation", "Phase 12: Reflexion der 3D-Randschicht unter -40 dB" (+28 more)

### Community 6 - "main"
Cohesion: 0.10
Nodes (43): cmd_bands(), cmd_bench(), cmd_bistability(), cmd_components(), cmd_example(), cmd_eye(), cmd_fdtd3d(), cmd_field() (+35 more)

### Community 7 - "quantum_view"
Cohesion: 0.06
Nodes (35): pattern_text(), QuantumView, bs_, bs_dirty_, bs_modes_, bs_photons_, bs_samples_, bs_seed_ (+27 more)

### Community 8 - "sparam_view"
Cohesion: 0.07
Nodes (25): SDL_Window, SParamView::SParamView(), instances_using(), SParamView, control_, error_, fit_plot_, future_ (+17 more)

### Community 9 - "chip_designer"
Cohesion: 0.11
Nodes (24): ChipDesigner::ChipDesigner(), gds_writer_, on_open_sparam, on_save_gate, on_save_sparam, collect_powers(), ChipDesigner::draw_confirm_popup(), ChipDesigner::draw_curve() (+16 more)

### Community 10 - "kerr_cavity"
Cohesion: 0.08
Nodes (34): HysteresisLoop, down, up, KerrCavity, carrier_gen, carrier_loss, carrier_shift, carrier_tau (+26 more)

### Community 11 - "report"
Cohesion: 0.05
Nodes (37): Criterion, key, name, rating, remark, value, GateReport, cascade (+29 more)

### Community 12 - "photonic_crystal"
Cohesion: 0.07
Nodes (35): CrystalLayout, a, kind, origin_x, origin_y, radius, radius_override, removed (+27 more)

### Community 13 - "scene"
Cohesion: 0.06
Nodes (34): FieldScene, background_index, boundary_x, boundary_y, cell_size, courant, duration, height (+26 more)

### Community 14 - "test_field_fdtd"
Cohesion: 0.07
Nodes (24): "Abnahme Phase 4: Energieerhaltung im verlustfreien Gitter mit Dielektrikum", "Abnahme Phase 4: numerische Dispersionsrelation des Yee-Schemas", "Abnahme Phase 4: Reflexion der CPML-Randschicht unter -40 dB", "Courant: dt = courant*dx/(c*sqrt(2)); ungültige Szenen werden abgelehnt", "Geometrie: Formen, Überdeckung und Hinweise zur Auflösung", "Grenzfläche zweier Medien: Fresnel-Reflexion und -Transmission bei senkrechtem Einfall", "Kerr-Effekt im FDTD: Phasenverschiebung entspricht n2*I*L (Ez und Hz)", "Linienquelle strahlt eine ebene Welle der eingestellten Amplitude ab" (+16 more)

### Community 15 - "logic_sim"
Cohesion: 0.13
Nodes (28): CircuitError, BlockPtr, Family, ideal, passive, regenerated, LogicCircuit, inputs (+20 more)

### Community 16 - "test_quantum"
Cohesion: 0.10
Nodes (13): "Doppelte Anmeldung wird abgelehnt", "Neues Bauteil funktioniert ohne Änderung am Löser", "Registry kennt die Bauteile der Phasen 1 und 2", "Registry lehnt unbekannte Typen, unbekannte und ungültige Parameter ab", "Boson Sampling: Stichprobe folgt den Permanenten, unterscheidbare Photonen nicht", "Clements-Zerlegung: N(N-1)/2 MZIs, Rekonstruktion auf Maschinengenauigkeit (Abnahme)", "Fock-Verteilung: Anzahl der Besetzungen und Normierung (ununterscheidbar und unterscheidbar)", "Hong-Ou-Mandel: Koinzidenz 0 bei idealen Photonen, Sichtbarkeit = Ununterscheidbarkeit (Abnahme)" (+5 more)

### Community 17 - "fdtd3d"
Cohesion: 0.07
Nodes (18): Fdtd3d, be_, bh_, ce_, ch_, coef_, dt_, dx_ (+10 more)

### Community 18 - "test_pwe"
Cohesion: 0.08
Nodes (21): amplitude_spectrum(), average_over_periods(), "Kerr-Resonator: Bistabilität beginnt bei Verstimmung sqrt(3)*gamma/2 und kritischer Leistung P_c", "Kerr-Resonator: Klapp-Leistungen bei delta = 2 g/2 folgen aus der Kubik", "Kerr-Resonator: linear stimmt mit der Lorentz-Kurve und dem kritisch gekoppelten Minimum", "Kerr-Resonator: Zeitverlauf läuft in den stationären Zustand und zeigt die Hysterese", plain(), "Silizium-Resonator: Parameter aus Material und Güte, TPA begrenzt die Energie, Ladungsträger " (+13 more)

### Community 19 - "link"
Cohesion: 0.06
Nodes (32): LinkSettings, bit_rate, bits, dark_current, driver_bandwidth, junction_rc, laser_power, linewidth (+24 more)

### Community 20 - "field_lab"
Cohesion: 0.16
Nodes (10): FieldLab::FieldLab(), distance_to_segment(), diverging(), drag_index(), drag_um(), inferno(), Rgb, b (+2 more)

### Community 21 - "pwe"
Cohesion: 0.11
Nodes (30): PweError, UnitCell, a1x, a1y, a2x, a2y, circles, epsilon_background (+22 more)

### Community 22 - "reference_scenes"
Cohesion: 0.12
Nodes (23): PathLeg, count, direction, crystal_shapes(), lattice_step(), remove_line(), remove_path(), add_s_bend() (+15 more)

### Community 23 - "step_team"
Cohesion: 0.08
Nodes (16): StepTeam, body_, done_cv_, error_, generation_, mutex_, pending_, start_cv_ (+8 more)

### Community 24 - "builtin_components"
Cohesion: 0.10
Nodes (16): ParamMap, DirectionalCoupler, excess_loss_db_, power_coupling_, make(), pcm_cell(), PcmCellComponent, cell_ (+8 more)

### Community 25 - "material"
Cohesion: 0.10
Nodes (19): Material, label, name, source, terms, thermo_optic, thermo_optic_source, valid_max (+11 more)

### Community 26 - "inverse_view"
Cohesion: 0.10
Nodes (15): drag_nm(), InverseView, control_, future_, has_result_, history_, pixels_, renderer_ (+7 more)

### Community 27 - "link"
Cohesion: 0.07
Nodes (25): Result, error, key, link, LinkResult, ber, bits, current (+17 more)

### Community 28 - "test_circuit_resonators"
Cohesion: 0.08
Nodes (20): "Monte-Carlo am MZI: Mittelwert folgt der analytischen Erwartung", "Monte-Carlo: korrelierte Streuung trifft alle Instanzen gleich", "Monte-Carlo: reproduzierbar, unabhängig von der Threadanzahl, abhängig vom Startwert", "Monte-Carlo: Werte bleiben im Parameterbereich, fehlerhafte Einstellungen werden abgelehnt", "Statistik und Ausbeute ignorieren bzw. zählen ungültige Stichproben richtig", "Zufallszahlen: standardnormalverteilt und reine Funktion der Argumente", "Abnahme Phase 2: 10.000-Punkte-Sweep in unter 1 s", "Abnahme Phase 2: Ring-FSR, Güte und Extinktion entsprechen den Formeln" (+12 more)

### Community 29 - "scene"
Cohesion: 0.14
Nodes (24): Boundary, periodic, pml, ShapeKind, circle, rectangle, ring, SignalKind (+16 more)

### Community 30 - "waveguide3d"
Cohesion: 0.07
Nodes (25): Waveguide3dResult, amplitude, amplitude_ripple, axis_x, cell_size, cells_per_second, completed, cross_mode (+17 more)

### Community 31 - "gate"
Cohesion: 0.09
Nodes (19): print_report(), GateDefinition, inputs, name, outputs, LogicInput, encoding, instance (+11 more)

### Community 32 - "file_dialog"
Cohesion: 0.09
Nodes (16): ChipDesigner::draw_file_menu(), ChipDesigner::save_or_ask(), DialogKind, open_project, open_sparam, save_gate, save_gds, save_project (+8 more)

### Community 33 - "fdtd"
Cohesion: 0.09
Nodes (13): MonitorData, kind, mode_index, name, sample_interval, start_time, unit, values (+5 more)

### Community 34 - "reference_designs"
Cohesion: 0.15
Nodes (17): Circuit, connections, instances, name, reference_and_interference(), reference_design(), reference_mzi(), reference_mzi_heater() (+9 more)

### Community 35 - "fdtd"
Cohesion: 0.08
Nodes (25): FluxLine, fixed, from, monitor, to, vertical, KerrCell, chi3 (+17 more)

### Community 36 - "fock"
Cohesion: 0.17
Nodes (16): g, Pattern, boson_sampling(), coincidence(), amplitude(), detect(), distribution(), distribution_mixed() (+8 more)

### Community 37 - "monte_carlo"
Cohesion: 0.09
Nodes (19): MonteCarloResult, cancelled, failed, names, units, values, MonteCarloSettings, samples (+11 more)

### Community 38 - "gate"
Cohesion: 0.16
Nodes (19): Encoding, electrical, intensity, phase, adjective(), apply_inputs(), encoding_from_string(), function_bits() (+11 more)

### Community 39 - "report"
Cohesion: 0.16
Nodes (19): build_cascade(), characterize(), format_db(), holds(), Nominal, bits, threshold, output_power_at() (+11 more)

### Community 40 - "logic_sim"
Cohesion: 0.09
Nodes (21): Issue, Severity, error, info, warning, text, where, SimSettings (+13 more)

### Community 41 - "builtin_components"
Cohesion: 0.09
Nodes (10): Component, Detector, dark_current_, responsivity_, GratingCoupler, bandwidth_, center_, insertion_loss_db_ (+2 more)

### Community 42 - "pcm"
Cohesion: 0.12
Nodes (21): PcmCell, ambient, confinement, crystallization_time, fraction, length, thermal_resistance, thermal_time (+13 more)

### Community 43 - "band_view"
Cohesion: 0.15
Nodes (11): BandView, a_nm_, control_, defect_pattern_, fit_, future_, has_output_, presets_ready_ (+3 more)

### Community 44 - "logic_sim_view"
Cohesion: 0.13
Nodes (11): bits_text(), LogicSimView::LogicSimView(), LogicSimView, control_, design_, family_, first_vector_, fit_ (+3 more)

### Community 45 - "logic_sim"
Cohesion: 0.09
Nodes (21): Result, circuit, design, error, family, sim, NetTrace, bit (+13 more)

### Community 46 - "?"
Cohesion: 0.10
Nodes (9): apply_style(), build_default_layout(), draw_plugins_window(), load_font(), main(), example_plugin(), "Plugin-Bauteil in Schaltung und Projektdatei wie ein eingebautes", "Plugin Bragg-Gitter: Spitzenreflexion tanh²(kappa L), Nullstellen, Energieerhaltung" (+1 more)

### Community 47 - "sweep"
Cohesion: 0.15
Nodes (18): CircuitState, RampResult, solutions, states, SweepSettings, points, wavelength_start, wavelength_stop (+10 more)

### Community 48 - "mode_view"
Cohesion: 0.09
Nodes (16): ModeView, auto_started_, control_, future_, has_result_, pixels_, renderer_, selected_mode_ (+8 more)

### Community 49 - "solver"
Cohesion: 0.13
Nodes (9): CompiledCircuit, components_, port_labels_, slots_, CompiledCircuit::measurements(), CompiledCircuit::port_index(), CompiledCircuit::power_balance(), CompiledCircuit::solve() (+1 more)

### Community 50 - "test_circuit_solver"
Cohesion: 0.09
Nodes (14): Mirror, r_, EmittingMirror, emission_, "Externe Anregung an offenen Ports: (I - S*C) b = S a_ext", "Fehlerhafte Netzlisten werden mit Klartext abgelehnt", "MZI folgt cos²(dphi/2) auf 1e-12 und erhält die Energie", "MZI mit ungleichen Kopplern: Extinktion nach Formel" (+6 more)

### Community 51 - "logic_lab"
Cohesion: 0.10
Nodes (19): LogicLab, field_gate_view_, fit_optimizer_, future_, logic_sim_view_, optimize_error_, optimize_instance_, optimize_max_ (+11 more)

### Community 52 - "registry"
Cohesion: 0.10
Nodes (13): ComponentAction, apply, description, label, ComponentType, actions, create, description (+5 more)

### Community 53 - "builtin_components"
Cohesion: 0.10
Nodes (16): GuidedPath, loss_db_per_cm, n_eff, n_group, wavelength_ref, PnPhaseShifter, length_, path_ (+8 more)

### Community 54 - "scene"
Cohesion: 0.10
Nodes (18): Monitor, kind, mode_order, name, wavelength, x1, x2, y1 (+10 more)

### Community 55 - "mesh"
Cohesion: 0.27
Nodes (17): cplx, Matrix, apply_left(), apply_right(), calibrate(), clements_decompose(), coupler(), fidelity() (+9 more)

### Community 56 - "thread_pool"
Cohesion: 0.10
Nodes (9): BandView::BandView(), InverseView::InverseView(), Wave3dView::Wave3dView(), ThreadPool, cv_, mutex_, queue_, stopping_ (+1 more)

### Community 57 - "builtin_components"
Cohesion: 0.19
Nodes (5): CVector, Soa, alpha_, g0_, p_sat_

### Community 58 - "parallel_for"
Cohesion: 0.11
Nodes (14): LoopState, body, control, count, cv, done, error, in_flight (+6 more)

### Community 59 - "fdtd3d"
Cohesion: 0.11
Nodes (19): Block, index, name, x0, x1, y0, y1, z0 (+11 more)

### Community 60 - "inverse_design"
Cohesion: 0.11
Nodes (10): BendProblem, design_nx_, design_ny_, eps_clad_, i0_, j0_, reference_scene_, settings_ (+2 more)

### Community 61 - "inverse_design"
Cohesion: 0.18
Nodes (17): BendProblem::BendProblem(), BendProblem::evaluate(), BendProblem::pixel_x0(), BendProblem::pixel_y0(), BendProblem::reference_amplitudes(), cone_filter(), cone_filter_transpose(), cplx (+9 more)

### Community 62 - "inverse_design"
Cohesion: 0.10
Nodes (20): InverseDesignResult, beta, binary, completed, density, eps_cladding, eps_core, history (+12 more)

### Community 63 - "chip_designer"
Cohesion: 0.12
Nodes (11): body_rows(), PortGeometry, left, position, ChipDesigner::draw_canvas(), operator+(), Solution, a (+3 more)

### Community 64 - "builtin_components"
Cohesion: 0.25
Nodes (4): CMatrix, amplitude_from_loss_db(), two_port(), expi()

### Community 65 - "registry"
Cohesion: 0.15
Nodes (13): Component::measure(), Component::measure_at(), Component::s_matrix_at(), Component::settle_state(), Component::source(), ComponentRegistry::add(), ComponentRegistry::builtin_instance(), ComponentRegistry::create() (+5 more)

### Community 66 - "builtin_components"
Cohesion: 0.13
Nodes (12): nearest_root(), RingKerr, area_, beta_, dn_dt_, k2_, length_, n2_ (+4 more)

### Community 67 - "gds"
Cohesion: 0.25
Nodes (9): decode_real8(), encode_real8(), read_gds(), read_gds_file(), timestamps(), write_gds(), write_gds_file(), Writer (+1 more)

### Community 68 - "truth_table"
Cohesion: 0.16
Nodes (11): TruthRow, bits, power, TruthTable, rows, optimize_parameter(), worst_contrast_db(), analyze_output() (+3 more)

### Community 69 - "cross_section"
Cohesion: 0.12
Nodes (16): print_modes(), Result, error, result, section, CrossSectionMode, confinement, effective_area (+8 more)

### Community 70 - "registry"
Cohesion: 0.16
Nodes (11): active_registry(), active_registry_ptr(), ComponentRegistry, ComponentRegistry::builtin(), ComponentRegistry::extend_builtin(), reg(), reg(), reg() (+3 more)

### Community 71 - "logic_lab"
Cohesion: 0.27
Nodes (5): find_spec(), instance_combo(), rating_color(), registry(), traffic_light()

### Community 72 - "logic_sim"
Cohesion: 0.17
Nodes (11): all_vectors(), CircuitBlock, characterized_correct_, circuit_, expected_, gate_, output_, design_names() (+3 more)

### Community 73 - "eye_view"
Cohesion: 0.15
Nodes (7): EyeView::EyeView(), drag(), EyeView, control_, fit_, future_, window_name

### Community 74 - "ci"
Cohesion: 0.12
Nodes (16): runs-on, steps, strategy, jobs, build, os, name, on (+8 more)

### Community 75 - "netlist"
Cohesion: 0.19
Nodes (12): PortRef, instance, port, Circuit::connect(), Circuit::disconnect(), Circuit::find_instance(), Circuit::is_connected(), Circuit::remove_instance() (+4 more)

### Community 76 - "inverse_design"
Cohesion: 0.12
Nodes (16): InverseDesignSettings, cell_size, cladding, core, design_size, filter_radius, initial, iterations (+8 more)

### Community 77 - "logic_sim"
Cohesion: 0.12
Nodes (17): Built, blocks, consumers, driver, input_net, issues, level, names (+9 more)

### Community 78 - "silicon_carriers"
Cohesion: 0.18
Nodes (9): PnJunction, acceptors, donors, temperature, PnJunction::built_in_voltage(), PnJunction::capacitance_per_area(), PnJunction::depletion_width(), PnJunction::width_n() (+1 more)

### Community 79 - "waveguide3d"
Cohesion: 0.12
Nodes (15): print_waveguide3d(), Waveguide3dSettings, cell_size, cladding, clearance, core, height, length (+7 more)

### Community 80 - "field_lab"
Cohesion: 0.12
Nodes (14): Control, finished, full_speed, steps_per_frame, stop, stop_at, Snapshot, energy (+6 more)

### Community 81 - "plugin"
Cohesion: 0.29
Nodes (9): load_plugin(), load_plugins_from(), loaded_plugins(), make_type(), open_library(), path_of(), plugin_list(), text() (+1 more)

### Community 82 - "scene"
Cohesion: 0.12
Nodes (15): Source, amplitude, beam_width, kind, mode_order, name, phase, pulse_duration (+7 more)

### Community 83 - "logic_sim"
Cohesion: 0.12
Nodes (15): LogicInstance, name, type, LogicWire, from, to, VectorResult, checked (+7 more)

### Community 84 - "slab"
Cohesion: 0.21
Nodes (13): ModeError, SlabMode, n_eff, order, SlabPolarization, te, tm, overlap_weight() (+5 more)

### Community 85 - "analysis"
Cohesion: 0.19
Nodes (13): Resonance, baseline, extinction_db, fwhm, position, quality, value, ResonanceKind (+5 more)

### Community 86 - "scene"
Cohesion: 0.13
Nodes (14): Shape, beta_tpa, carrier_lifetime, height, index, inner_radius, kind, n2 (+6 more)

### Community 87 - "sparameters"
Cohesion: 0.15
Nodes (10): SParameterResult, cancelled, left_port_count, n_eff, ports, s, simulated_time, steps (+2 more)

### Community 88 - "layout"
Cohesion: 0.16
Nodes (13): Cell, name, polygons, LayerInfo, index, layer, name, Layout (+5 more)

### Community 89 - "logic_sim"
Cohesion: 0.14
Nodes (13): BlockPort, encoding, name, one_level, threshold, worst_one, worst_zero, zero_level (+5 more)

### Community 90 - "logic_sim"
Cohesion: 0.13
Nodes (11): LogicBlock, delay, family, inputs, note, outputs, regenerating, static_power (+3 more)

### Community 91 - "cross_section"
Cohesion: 0.13
Nodes (13): CrossSection, cell_size, core, height, lower, margin_x, margin_y, modes (+5 more)

### Community 92 - "test_logic"
Cohesion: 0.13
Nodes (14): "Abnahme Phase 3: das Zeugnis markiert fehlende Regeneration und Kaskadierbarkeit des XOR", "Abnahme Phase 3: Interferenz-XOR mit CR > 20 dB im Idealfall", "Directed Logic: XOR/XNOR richtig, aber schmalbandig und nur über Elektronik kaskadierbar", "Fehlerhafte Gatter-Definitionen werden mit Klartext abgelehnt", "Funktionen erkennen und Bits zuordnen", "Kontrastverhältnis CR = 10*log10(P1_min / P0_max)", "Latenz ist die Gruppenlaufzeit n_g*L/c0; ein Wellenleiter ist kein regenerierendes Gatter", "Lineares AND: 6 dB Kontrast, Kaskade wird gebaut und scheitert" (+6 more)

### Community 93 - "mode_view"
Cohesion: 0.21
Nodes (4): validate_phc_bend(), ModeView::ModeView(), drag_nm(), material_combo()

### Community 94 - "band_view"
Cohesion: 0.14
Nodes (13): Output, curves, error, settings, Settings, cutoff, eps_background, eps_inclusion (+5 more)

### Community 95 - "wave3d_view"
Cohesion: 0.14
Nodes (10): SDL_Renderer, Wave3dView, control_, cross_, future_, has_result_, mode_, renderer_ (+2 more)

### Community 96 - "layout"
Cohesion: 0.29
Nodes (9): LayoutError, circle_segments(), inverse_design_layout(), make_ccw(), pixels_to_polygons(), sanitize(), scene_to_layout(), signed_area() (+1 more)

### Community 97 - "sparameters"
Cohesion: 0.15
Nodes (12): PortGrid, eps_between, eps_point, fixed, from, inward, plane_a, plane_b (+4 more)

### Community 98 - "gds"
Cohesion: 0.14
Nodes (14): std::uint16_t, bgnlib, bgnstr, boundary, datatype_rec, endel, endlib, endstr (+6 more)

### Community 99 - "experiments"
Cohesion: 0.14
Nodes (12): CircuitTransfer, inputs, outputs, u, HomResult, coincidence, coincidence_far, curve (+4 more)

### Community 100 - "link"
Cohesion: 0.23
Nodes (11): RingCmt, detuning, kappa_0, kappa_in, amplitude_from_db_per_m(), ber_from_q(), cplx, prbs() (+3 more)

### Community 101 - "chip_designer"
Cohesion: 0.15
Nodes (10): MonteCarloData, error, key, result, SeriesData, error, key, names (+2 more)

### Community 102 - "field_gate_view"
Cohesion: 0.15
Nodes (8): FieldGateView, control_, future_, gate_, gate_for_, has_result_, names_, selected_

### Community 103 - "active_devices"
Cohesion: 0.18
Nodes (9): PnSection, confinement, height, junction, width, heater_phase(), PnSection::absorption(), PnSection::capacitance_per_length() (+1 more)

### Community 104 - "component"
Cohesion: 0.15
Nodes (12): Measurement, name, unit, value, ParamSpec, default_value, display_scale, display_unit (+4 more)

### Community 105 - "plugin_api"
Cohesion: 0.17
Nodes (12): plab_component, description, label, left_port_count, name, param_count, params, port_count (+4 more)

### Community 106 - "test_layout"
Cohesion: 0.19
Nodes (8): "Abnahme Phase 13: Adjoint-Gradient = finite Differenzen", "Abnahme Phase 13: inverses Design eines 90-Grad-Knicks, binär und als GDSII", "Filter und Projektion: Grenzwerte, Ableitung, Mittelwert erhalten", "GDSII: 8-Byte-Gleitkommazahlen wie in der Formatbeschreibung", "GDSII: Datei beginnt mit HEADER 600, endet mit ENDLIB, Rücklesen ergibt dieselben Polygone", "Layout: Flächen der Formen (Rechteck exakt, Kreis und Ring innerhalb der Sehnenabweichung)", "Layout: Kristall-Szene (W1) – je Loch ein Polygon, Löcher auf eigener Lage", "Layout: Pixelbild als Rechtecke, Fläche = Zahl der Pixel"

### Community 107 - "field_gate"
Cohesion: 0.17
Nodes (11): FieldGate, bias_sources, description, inputs, name, outputs, scenes, FieldGateInput (+3 more)

### Community 108 - "fde"
Cohesion: 0.17
Nodes (11): FdeGrid, dx, dy, index, nx, ny, FdeMode, field (+3 more)

### Community 109 - "mesh"
Cohesion: 0.17
Nodes (12): MeshDecomposition, mzis, output_phases, size, Mzi, mode, phi, theta (+4 more)

### Community 110 - "pwe"
Cohesion: 0.20
Nodes (9): Curve, bands, bulk_gaps, polarization, Gap, band, lower, upper (+1 more)

### Community 111 - "truth_table"
Cohesion: 0.17
Nodes (11): OutputAnalysis, bits, contrast_db, extinction_db, has_expected, p0_max, p1_min, p_max (+3 more)

### Community 112 - "fde"
Cohesion: 0.17
Nodes (12): EdgeCondition, symmetric, zero, FdePolarization, quasi_te, quasi_tm, FdeSettings, modes (+4 more)

### Community 113 - "pwe"
Cohesion: 0.18
Nodes (9): BandPath, distance, marks, points, BandResult, frequency, gaps, path (+1 more)

### Community 114 - "pwe"
Cohesion: 0.21
Nodes (9): KPoint, kx, ky, Prepared, eps, eps_inv, solve_k(), solve_prepared() (+1 more)

### Community 115 - "chip_designer"
Cohesion: 0.20
Nodes (6): ChipDesigner::draw_monte_carlo(), ChipDesigner::start_monte_carlo(), LogicLab::LogicLab(), format_param(), format_si(), ui_path_from_utf8()

### Community 116 - "field_gate_view"
Cohesion: 0.31
Nodes (4): FieldGateView::FieldGateView(), rating_color(), scene_key(), traffic_light()

### Community 117 - "builtin_components"
Cohesion: 0.18
Nodes (8): RingAddDrop, half_tuning_, k1_, k2_, length_, path_, t1_, t2_

### Community 118 - "units"
Cohesion: 0.18
Nodes (5): omega_from_wavelength(), "1550 nm entsprechen etwa 193,4 THz", "Dezibel-Umrechnung", "Einheiten werden in SI umgerechnet", "Vakuum: c0 = 1/sqrt(eps0*mu0)"

### Community 119 - "fdtd3d"
Cohesion: 0.18
Nodes (9): Component3d, ex, ey, ez, hx, hy, hz, Fdtd3d::permittivity() (+1 more)

### Community 120 - "fdtd3d"
Cohesion: 0.29
Nodes (9): cells_along(), Fdtd3d::energy(), Fdtd3d::Fdtd3d(), Fdtd3d::pml_plane(), Fdtd3d::set_threads(), Fdtd3d::update_e(), Fdtd3d::update_h(), index_at() (+1 more)

### Community 121 - "field_gate"
Cohesion: 0.31
Nodes (6): characterize_field_gate(), field_gate_names(), has_source(), run_field_truth_table(), scenes_for_row(), validate()

### Community 122 - "logic_sim"
Cohesion: 0.24
Nodes (5): KerrLatch, circuit_, hold_, p_down_, p_up_

### Community 123 - "phase_change"
Cohesion: 0.18
Nodes (8): PhaseChangeMaterial, crystallization_temperature, index_amorphous, index_crystalline, melting_temperature, name, source, gst225()

### Community 124 - "experiments"
Cohesion: 0.20
Nodes (10): CnotResult, bell_fidelity, fidelity, success, truth, unitary, cnot_unitary(), linear_optical_cnot() (+2 more)

### Community 125 - "test_circuit_nonlinear"
Cohesion: 0.20
Nodes (9): "Kerr-Ring: Klappleistungen der Rampe = Extrema der geschlossenen Ringgleichung = Kompaktmodell", "Kerr-Ring mit Erwärmung: Wellenlängen-Rampe zeigt das Thermik-Dreieck (hin anders als zurück)", "Kerr-Ring: ohne Nichtlinearität identisch mit dem linearen All-Pass-Ring", "Kerr-Ring: TPA begrenzt die umlaufende Leistung, Gegenrichtung wirkt doppelt", linear_phase(), "Nichtlineare Schaltung mit Rückkopplung: zwei Kerr-Ringe hintereinander konvergieren", ring_circuit(), "SOA: Verstärkung sättigt nach G = G0 exp(-(G-1)P/P_sat); 3-dB-Ausgangsleistung analytisch" (+1 more)

### Community 127 - "test_active"
Cohesion: 0.22
Nodes (6): "Heizer: P_pi = 24,8 mW für 61,6 µm (Harris 2014) und lineare Phase", "Photodiode: Photostrom, Schrot- und thermisches Rauschen, Signal-Rausch-Verhältnis", "PN-Phasenschieber: V_pi*L im Bereich gemessener Verarmungs-Modulatoren (1 bis 4 V cm)", "PN-Übergang: Diffusionsspannung, Verarmungszone und Kapazität nach der Verarmungsnäherung", "Ring-Modulator: Sperrspannung verschiebt die Resonanz um lambda * dn_eff * Anteil / n_g", "Soref-Bennett: Brechzahl- und Absorptionsänderung bei 1e18 cm^-3"

### Community 128 - "solver"
Cohesion: 0.20
Nodes (7): Slot, first_port, name, port_count, InstanceMeasurement, instance, measurement

### Community 129 - "builtin_components"
Cohesion: 0.20
Nodes (8): RingPn, coverage_, length_, path_, pn_, t_, tuning_, voltage_

### Community 130 - "sparameters"
Cohesion: 0.20
Nodes (8): PortMode, beta_numeric, impedance, n_eff, norm, profile, weight_sum, solve_port_mode()

### Community 131 - "layout"
Cohesion: 0.20
Nodes (7): Point, x, y, Polygon, datatype, layer, points

### Community 132 - "experiments"
Cohesion: 0.20
Nodes (10): HomSettings, coherence_time, detectors, indistinguishability, multiphoton, reflectivity, Detectors, dark_count (+2 more)

### Community 133 - "inverse_view"
Cohesion: 0.22
Nodes (7): Live, density, fresh, history, mutex, nx, ny

### Community 134 - "netlist"
Cohesion: 0.22
Nodes (7): Instance, grid_x, grid_y, name, params, type, Circuit::add()

### Community 135 - "plugin"
Cohesion: 0.22
Nodes (7): PluginInfo, components, description, name, path, version, warnings

### Community 136 - "builtin_components"
Cohesion: 0.22
Nodes (6): Photodiode, bandwidth_, dark_, load_, responsivity_, temperature_

### Community 137 - "monte_carlo"
Cohesion: 0.42
Nodes (6): perturbed_circuit(), splitmix64(), standard_normal(), statistics(), unit_interval(), yield_fraction()

### Community 138 - "fdtd3d"
Cohesion: 0.25
Nodes (6): Source3d, cells, component, signal, weights, Fdtd3d::add_source()

### Community 139 - "sparameters"
Cohesion: 0.22
Nodes (8): PortDft, plane, scalar, RunResult, incoming, outgoing, steps, time

### Community 140 - "test_circuit_components"
Cohesion: 0.22
Nodes (8): "Laser und Detektor: Leistung, Phase, Photostrom", "Phasenschieber: e^{i*phi} und Einfügeverlust", "Richtkoppler: S = [[t, i*kappa], [i*kappa, t]] mit t² + kappa² = 1", unitarity_error(), "Verlustfreie Bauteile sind unitär, alle Bauteile reziprok", "Wellenleiter: Gruppenlaufzeit entspricht dem Gruppenindex", "Wellenleiter: Phase 2*pi*n_eff*L/lambda bei der Bezugswellenlänge", "Wellenleiter: Verlust in dB/cm"

### Community 141 - "field_gate_view"
Cohesion: 0.25
Nodes (7): Result, cancelled, error, gate, gate_name, report, seconds

### Community 142 - "wave3d_view"
Cohesion: 0.25
Nodes (6): SDL_Texture, Picture, nx, ny, pixels, texture

### Community 143 - "plugin_api"
Cohesion: 0.25
Nodes (8): plab_param, default_value, display_scale, display_unit, label, max_value, min_value, name

### Community 144 - "builtin_components"
Cohesion: 0.25
Nodes (6): Heater, dn_dt_, length_, path_, power_, r_th_

### Community 145 - "builtin_components"
Cohesion: 0.25
Nodes (6): Waveguide, length_, loss_db_per_cm_, n_eff_, n_group_, wavelength_ref_

### Community 146 - "fdtd3d"
Cohesion: 0.25
Nodes (6): PmlTerm, axis, psi, sign, source, target

### Community 147 - "fdtd"
Cohesion: 0.25
Nodes (6): ModeWeights, cells, weights, Fdtd::add_injection(), Fdtd::mode_weights(), Fdtd::set_permittivity()

### Community 148 - "scene"
Cohesion: 0.25
Nodes (7): Port, mode_order, name, x1, x2, y1, y2

### Community 149 - "scene"
Cohesion: 0.25
Nodes (7): SParameterSettings, decay, max_time, points, source_offset, wavelength_start, wavelength_stop

### Community 150 - "field_gate"
Cohesion: 0.25
Nodes (8): FieldGateReport, criteria, function_correct, gate_name, outputs, table, wavelength, worst_contrast_db

### Community 151 - "profile"
Cohesion: 0.25
Nodes (6): ProfileMode, field, n_eff, order, profile_modes(), scale

### Community 152 - "pwe"
Cohesion: 0.25
Nodes (7): BandSettings, bands, cutoff, polarization, Polarization, ez, hz

### Community 153 - "experiments"
Cohesion: 0.25
Nodes (8): BosonSamplingResult, distinguishable, exact, outcomes, sampled, samples, tvd_distinguishable, tvd_sampled

### Community 154 - "example_plugin"
Cohesion: 0.46
Nodes (6): bragg(), cplx, mmi(), plab_plugin* photonlab_plugin(void)(), put(), sinhc()

### Community 155 - "plugin_api"
Cohesion: 0.29
Nodes (7): plab_plugin, api_version, component_count, components, description, name, version

### Community 156 - "builtin_components"
Cohesion: 0.29
Nodes (5): BandpassFilter, bandwidth_, center_, insertion_loss_db_, order_

### Community 157 - "builtin_components"
Cohesion: 0.29
Nodes (3): Laser, phase_, power_

### Community 158 - "sparameters"
Cohesion: 0.76
Nodes (6): build_port_grid(), cplx, extract_sparameters(), port_inward_sign(), port_is_vertical(), validate_sparameter_setup()

### Community 159 - "field_gate"
Cohesion: 0.29
Nodes (5): FieldGateOutput, expected, monitor, name, scene

### Community 160 - "optimize"
Cohesion: 0.29
Nodes (6): OptimizeResult, best_contrast_db, best_x, contrast_db, found, x

### Community 161 - "pwe"
Cohesion: 0.29
Nodes (6): Rectangle, epsilon, height, width, x, y

### Community 162 - "mesh"
Cohesion: 0.29
Nodes (6): MeshErrors, coupler_sigma, crosstalk, loss_db, phase_sigma, seed

### Community 163 - "mesh"
Cohesion: 0.29
Nodes (7): NeuralDemoResult, accuracy_calibrated, accuracy_fabricated, accuracy_ideal, fidelity_calibrated, fidelity_fabricated, samples

### Community 164 - "chip_designer"
Cohesion: 0.33
Nodes (3): ChipDesigner::draw_spectrum(), ChipDesigner::update_sweep(), to_dbm()

### Community 165 - "chip_designer"
Cohesion: 0.33
Nodes (6): Pending, load_example, new_project, none, open_project, quit

### Community 166 - "field_lab"
Cohesion: 0.33
Nodes (6): ObjectKind, monitor, none, port, shape, source

### Community 167 - "monte_carlo"
Cohesion: 0.33
Nodes (6): Statistics, count, max, mean, min, stddev

### Community 168 - "plugin"
Cohesion: 0.40
Nodes (3): PluginComponent, params_, type_

### Community 169 - "step_team"
Cohesion: 0.40
Nodes (3): split_range(), Fdtd3d::for_planes(), Fdtd::for_rows()

### Community 170 - "fdtd"
Cohesion: 0.33
Nodes (6): ActiveSource, cells, source, SourceCell, index, weight

### Community 171 - "waveguide3d"
Cohesion: 0.40
Nodes (5): FieldError, design_scene(), set_radius(), run_waveguide_3d(), whole_cells()

### Community 172 - "report"
Cohesion: 0.33
Nodes (6): CascadeResult, gate, logic_preserved, possible, reason, table

### Community 173 - "cross_section"
Cohesion: 0.53
Nodes (4): analyze(), build_grid(), overlap(), validate()

### Community 174 - "slab"
Cohesion: 0.33
Nodes (6): Transverse, gamma_lower, gamma_upper, kappa, weight_lower, weight_upper

### Community 175 - "pwe"
Cohesion: 0.33
Nodes (5): Circle, epsilon, radius, x, y

### Community 176 - "pwe"
Cohesion: 0.40
Nodes (4): EpsilonTable, mspan, nspan, values

### Community 177 - "link"
Cohesion: 0.33
Nodes (4): Pole, active, k, y

### Community 178 - "wave3d_view"
Cohesion: 0.40
Nodes (4): Result, error, result, settings

### Community 179 - "reference_designs"
Cohesion: 0.40
Nodes (5): ReferenceSetup, circuit, monte_carlo, sweep, wavelength

### Community 180 - "solver"
Cohesion: 0.40
Nodes (5): PowerBalance, absorbed, emitted, external_in, leaving_open

### Community 181 - "inverse_design"
Cohesion: 0.40
Nodes (5): GradientSample, adjoint, finite_difference, pixel, check_gradient()

### Community 182 - "inverse_design"
Cohesion: 0.40
Nodes (4): InverseDesignProgress, density, history, iteration

### Community 183 - "test_nonlinear_field"
Cohesion: 0.40
Nodes (5): build_cavity(), Cavity, scene, spacer_x0, spacer_x1

### Community 184 - "inverse_view"
Cohesion: 0.50
Nodes (3): Outcome, error, result

### Community 185 - "layout"
Cohesion: 0.50
Nodes (4): LayoutSettings, cell_name, max_deviation, min_segments

### Community 186 - "netlist"
Cohesion: 0.67
Nodes (3): Connection, a, b

### Community 190 - "pwe"
Cohesion: 0.67
Nodes (3): PathKind, square, triangular

### Community 191 - "link"
Cohesion: 0.67
Nodes (3): ModulatorKind, mzm, ring

## Knowledge Gaps
- **1383 isolated node(s):** `lattice`, `radius_rel`, `eps_background`, `eps_inclusion`, `polarization` (+1378 more)
  These have ≤1 connection - possible missing edges. (Counts symbols only; 1909 node(s) total have ≤1 connection when file, concept and rationale nodes are included.)
- **6 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `ChipDesigner` connect `chip_designer` to `solver`, `project`, `field_lab`, `?`, `quantum_view`, `sparam_view`, `chip_designer`, `field_lab`, `inverse_view`, `file_dialog`, `chip_designer`, `chip_designer`, `band_view`, `sweep`, `mode_view`, `solver`, `logic_lab`, `solver`, `thread_pool`, `parallel_for`, `chip_designer`, `eye_view`, `netlist`, `analysis`, `mode_view`, `chip_designer`, `field_gate_view`, `chip_designer`, `field_gate_view`?**
  _High betweenness centrality (0.136) - this node is a cross-community bridge._
- **Are the 2 inferred relationships involving `ChipDesigner` (e.g. with `.draw_library()` and `.add_to_library()`) actually correct?**
  _`ChipDesigner` has 2 INFERRED edges - model-reasoned connections that need verification._
- **What connects `lattice`, `radius_rel`, `eps_background` to the rest of the system?**
  _1383 weakly-connected nodes found - possible missing edges._
- **Should `chip_designer` be split into smaller, more focused modules?**
  _Cohesion score 0.02638453500522466 - nodes in this community are weakly interconnected._
- **Why does `FieldLab` connect `field_lab` to `chip_designer`, `fdtd`, `fdtd`, `?`, `field_lab`, `sparam_view`, `band_view`, `field_lab`, `mode_view`, `field_lab`, `thread_pool`, `inverse_view`, `wave3d_view`?**
  _High betweenness centrality (0.091) - this node is a cross-community bridge._
- **Are the 2 inferred relationships involving `Fdtd` (e.g. with `.snapshot_from_sim()` and `run_cavity()`) actually correct?**
  _`Fdtd` has 2 INFERRED edges - model-reasoned connections that need verification._
- **Should `project` be split into smaller, more focused modules?**
  _Cohesion score 0.05477477477477478 - nodes in this community are weakly interconnected._