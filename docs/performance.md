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

- Zeitliches Blocken (mehrere Schritte je Speicherdurchgang) und einfache Genauigkeit (`float`): würden den
  Speicherverkehr weiter senken, ändern aber die Ergebnisse; offen als #112.

## Grafikkarte (OpenCL)

Das 2D-FDTD rechnet auf Wunsch auf der Grafikkarte (`libs/field/src/gpu.cpp`, Schalter Ansicht > Grafikkarte
bzw. `--gpu` in der Kommandozeile). Gebaut wird nur gegen die OpenCL-Header von Khronos (gepinnt in
`cmake/Dependencies.cmake`); die Bibliothek des Treibers (`OpenCL.dll`, `libOpenCL.so.1`) wird zur Laufzeit
geladen. Fehlt sie oder kann das Gerät keine doppelte Genauigkeit, rechnet alles auf dem Prozessor.

* Kerne: dieselben Formeln und dieselbe Reihenfolge wie `step_ez`/`step_hz`/`record`, doppelte Genauigkeit,
  `FP_CONTRACT OFF`. Quellen (einseitig, Regeneratoren) und Monitore laufen auf der Karte; Signale werden je
  Block (128 Schritte bzw. Entscheidungsabstand der Regeneratoren) vorab berechnet, Messreihen je Block
  zurückgeholt. Felder kommen nur auf Anfrage zurück (`field()`, `energy()`).
* Abnahme: auf OpenCL für Prozessoren (PoCL) **bitgleich** zum Prozessor-Kern für Ez, Hz, Randschichten,
  periodische Ränder, Punkt-/Linien-/Modenquellen, einseitige Quellen und Regeneratoren (`tests/test_gpu.cpp`,
  mit `PHOTONLAB_OPENCL_DEVICE=any`). Auf Grafikkarten gilt die Toleranz 1e-12 relativ (IEEE-Rundung von
  Addition und Multiplikation ist dort ebenfalls exakt; Division kommt in den Kernen nicht vor).
* Auf dem Prozessor bleiben: Kerr-Formen (Newton je Zelle), inverses Design (setzt Permittivitäten und
  Einspeisungen) und die S-Parameter-Extraktion (liest das Feld in jedem Schritt; ein Fourier-Monitor auf der
  Karte fehlt noch).
* **3D** (`Fdtd3d::use_gpu`, die 3D-Rechnung der Szene nutzt es automatisch): Kerne `h3`, `e3` je Zelle wie
  `update_h`/`update_e`, je Randschicht-Term ein Aufruf `pml3` nur über dessen Schicht, in der Reihenfolge von
  `pml_terms_` (eine Ecke bekommt Beiträge zweier Terme; die Reihenfolge der Additionen bleibt die des
  Prozessors). Quellen je Quelle (`src3`, Signal vom Prozessor), Monitore als „Proben“ (`run_probed`,
  `gather3`): je Schritt die Feldwerte der Flussflächen und Punkte in einen Block, der alle bis zu 256
  Schritte zurückkommt; die Flüsse rechnet der Prozessor daraus mit derselben Formel. Abnahme auf PoCL:
  alle sechs Komponenten, Proben und Flüsse der extrudierten Szene **bitgleich** (`tests/test_gpu.cpp`). Die
  3D-S-Parameter lesen die Ports in jedem Schritt und rechnen deshalb auf dem Prozessor.
* Geschwindigkeit: `photonlab_cli bench --gpu`. Das FDTD ist durch die Speicherbandbreite begrenzt: eine
  Grafikkarte mit 400 GB/s liegt etwa 5–10-mal über einem Desktop-Prozessor, auch in doppelter Genauigkeit.
  Gemessen wurde in der Entwicklungsumgebung nur PoCL auf dem Prozessor (keine Grafikkarte vorhanden).

## Profiling

Wo die Zeit hingeht, zeigt `perf` (Linux) bzw. der Visual-Studio-Profiler. Beobachtung: Der 2D-Schritt ist durch
den Speicher begrenzt (etwa 50 Byte je Zelle und Schritt nach der Änderung, vorher etwa 120); ein Thread
erreicht deshalb schon die Hälfte des Möglichen, mehr als 4 Threads bringen auf typischen Desktop-Rechnern wenig.
