# Physik-Notizen

Zu jedem Modul kommt hier eine kurze Notiz: Gleichungen, Annahmen, Quellen, Gültigkeitsgrenzen.
Das hilft Menschen und Claude gleichermaßen, den Code nachzuvollziehen.

## Konventionen
- Intern nur SI-Einheiten (`libs/core/include/photonlab/core/units.hpp`).
- Komplexe Amplituden a mit |a|² = Leistung in Watt.
- Richtkoppler: S = [[t, iκ], [iκ, t]], t = cos(kL), κ = sin(kL).
- Zeitkonvention: e^{-iωt} (für Wellenleiter: Phase φ = +2π·n_eff·L/λ).

## Geplante Notizen
- `circuit_solver.md` – (I − S·C)·b = S·a_ext (Phase 1)
- `fdtd.md` – Yee-Gitter, Courant, CPML (Phase 4)
- `modesolver.md` – FDE-Eigenwertproblem (Phase 5)
