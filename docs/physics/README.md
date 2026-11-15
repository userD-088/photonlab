# Physik-Notizen

Zu jedem Modul kommt hier eine kurze Notiz: Gleichungen, Annahmen, Quellen, Gültigkeitsgrenzen.
Das hilft Menschen und Claude gleichermaßen, den Code nachzuvollziehen.

## Konventionen
- Intern nur SI-Einheiten (`libs/core/include/photonlab/core/units.hpp`).
- Komplexe Amplituden a mit |a|² = Leistung in Watt.
- Richtkoppler: S = [[t, iκ], [iκ, t]], t = cos(kL), κ = sin(kL).
- Zeitkonvention: e^{-iωt} (für Wellenleiter: Phase φ = +2π·n_eff·L/λ).

## Notizen
- [`circuit_solver.md`](circuit_solver.md) – Schaltungslöser (I − S·C)·b = S·a_ext + b_src und Kompaktmodelle (Phase 1)
- [`resonators.md`](resonators.md) – Ringresonatoren, Y-Splitter, Bandpass, Gitterkoppler, Resonanz-Auswertung (Phase 2)
- [`monte_carlo.md`](monte_carlo.md) – Monte-Carlo über Parameterstreuung (Phase 2)
- [`logic.md`](logic.md) – Logik-Labor: Wahrheitstabelle, Kontrast, Gatter-Zeugnis und seine Messvorschriften (Phase 3)
- [`fdtd.md`](fdtd.md) – 2D-FDTD nach Yee: Gitter, Courant, Dispersion, CPML, Quellen, Monitore (Phase 4)
- [`materials.md`](materials.md) – Sellmeier-Gleichungen, Gruppenindex, Quellen und Grenzen (Phase 5)
- [`modes.md`](modes.md) – Schichtwellenleiter, Modenlöser für Querschnitte, Modenquelle im FDTD (Phase 5)
- [`sparameters.md`](sparameters.md) – S-Parameter aus dem FDTD: Zerlegung in Laufrichtungen auf dem Gitter, Bauteil in der Schaltung (Phase 6)
- [`pwe.md`](pwe.md) – Bandstruktur photonischer Kristalle mit ebenen Wellen (Phase 7)
- [`photonic_crystals.md`](photonic_crystals.md) – Kristall-Generatoren, Liniendefekte, Kristall-Gatter (Phase 7)
- [`nonlinear.md`](nonlinear.md) – Kerr, TPA, freie Ladungsträger: Kompaktmodell, FDTD, Schaltung (Phase 8)
- [`active.md`](active.md) – Heizer, PN-Modulator, Photodiode, Zeitbereich, PCM (Phase 9)
- [`logic_level.md`](logic_level.md) – Logik-Ebene: Logikblöcke und Event-Simulator (Phase 10)
- [`quantum.md`](quantum.md) – Quanten-Modus und MZI-Gitter (Phase 11)
- [`fdtd3d.md`](fdtd3d.md) – 3D-FDTD: Yee-Gitter, CPML in den Schichten, Material-Mittelung, Abnahme 3D-Wellenleiter = Modenlöser (Phase 12)

## Geplante Notizen
- GDSII, inverses Design (Phase 13)
