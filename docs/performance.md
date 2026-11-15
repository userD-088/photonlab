# Geschwindigkeit (Phase 12)

Messen auf dem eigenen Rechner:

```
photonlab_cli bench [--size 1000] [--threads N]
```

`bench` rechnet einen **Referenzkern** (der Ez-Zeitschritt so, wie er bis Phase 11 gerechnet wurde), dann den
heutigen 2D-FDTD (Ez und Hz) und den 3D-FDTD mit 1, 2, 4 ... Threads und gibt Millionen Zellen je Sekunde aus.

## Ergebnis (Abnahme: 2D-FDTD mindestens 5-mal schneller)

Gemessen im Entwicklungs-Container (4 logische Kerne, GCC, Release, ohne `PHOTONLAB_NATIVE`), Gitter 1000 × 1000
mit 20 Zellen Randschicht, 200 Schritte:

| Rechnung | Mio. Zellen/s | gegenüber Phase 11 |
|---|---|---|
| Phase 11, Ez (alter Kern; auch als Referenz in `bench`) | 150–166 | 1 |
| Ez, 1 Thread | 300–330 | 2,0 |
| Ez, 4 Threads | 840–960 | **5,5** |
| Hz, 1 Thread | 326–331 | 2,0 |
| Hz, 4 Threads | 1100–1160 | **7,0** |
| 3D 120³ mit Randschicht, 1 Thread | 63 | – |
| 3D 120³, 4 Threads | 280–320 | – |
| 3D 120³, 4 Threads, `PHOTONLAB_NATIVE` (AVX2) | 400 | – |

Die Testsuite läuft dadurch in 92 statt rund 150 Sekunden. Auf Rechnern mit weniger Kernen oder weniger
Speicherbandbreite fällt der Gewinn kleiner aus; `bench` zeigt den eigenen Wert.

## Was geändert wurde

1. **Randschicht nur am Rand.** Bis Phase 11 rechnete jede Zelle die vier Hilfsfelder ψ der CPML mit, obwohl
   sie außerhalb der Schicht immer 0 sind. Jetzt laufen die Schleifen in drei Abschnitten (Schicht links,
   Inneres ohne ψ, Schicht rechts), Zeilen außerhalb der Schicht in y ganz ohne ψ. Das spart den größten Teil
   des Speicherverkehrs. Das Ergebnis ist **bitgleich** (d + 0 = d); geprüft durch Vergleich der Felder,
   Energie und Fluss-Monitore mit dem alten Kern (Ez/Hz, mit Kerr-Material und periodischem Rand).
2. **Puffer tauschen statt kopieren.** Das alte Skalarfeld (für Energie, Fluss und Kerr) wurde je Schritt
   kopiert; jetzt schreibt der Schritt in den zweiten Puffer und tauscht die Zeiger.
3. **Threads je Zeitschritt** (`StepTeam` in `libs/core`): Zeilenbänder (2D) bzw. Ebenen (3D) werden auf eine
   feste Gruppe von Threads verteilt. Die Threads warten zwischen den Halbschritten kurz aktiv und legen sich
   erst dann schlafen, damit das Aufwecken nicht die Rechenzeit eines Halbschritts übersteigt. Jede Zelle wird
   unabhängig von der Aufteilung gleich gerechnet: **bitgleich für jede Zahl von Threads** (Test).
   `Fdtd::set_threads(0)` = automatisch (mindestens etwa 32 000 Zellen je Thread). Laufen mehrere FDTD-Rechnungen
   ohnehin parallel (S-Parameter-Ports, Wahrheitstabellen von Kristall-Gattern), teilen sie sich die Kerne
   (`StepTeam::share_for`).
4. **SIMD**: Die Schleifen sind verzweigungsfrei und mit `PHOTONLAB_RESTRICT` markiert; GCC vektorisiert sie
   (SSE2, im Standard-Build ohne besondere Flags). Mit `-DPHOTONLAB_NATIVE=ON` (AVX2 bzw. `-march=native`)
   gewinnt der 3D-FDTD etwa ein Drittel, der 2D-FDTD nichts: Er ist durch die Speicherbandbreite begrenzt.
   Das Programm läuft dann nur auf Prozessoren mit denselben Befehlen, deshalb ist die Option aus.

## Nicht umgesetzt

- **GPU-Backend.** Der Bauplan nennt SDL3-GPU-Compute, CUDA oder OpenCL. CUDA/OpenCL wären neue
  Abhängigkeiten (Rückfrage nötig); SDL3-GPU-Compute braucht Shader in drei Formaten (SPIR-V, DXIL, MSL) und
  darf nach den Projektregeln nicht in `libs/` stehen. Offen als #111.
- Zeitliches Blocken (mehrere Schritte je Speicherdurchgang) und einfache Genauigkeit (`float`): würden den
  Speicherverkehr weiter senken, ändern aber die Ergebnisse; offen als #112.

## Profiling

Wo die Zeit hingeht, zeigt `perf` (Linux) bzw. der Visual-Studio-Profiler. Beobachtung: Der 2D-Schritt ist durch
den Speicher begrenzt (etwa 50 Byte je Zelle und Schritt nach der Änderung, vorher etwa 120); ein Thread
erreicht deshalb schon die Hälfte des Möglichen, mehr als 4 Threads bringen auf typischen Desktop-Rechnern wenig.
