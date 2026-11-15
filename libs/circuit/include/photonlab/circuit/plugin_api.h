/* PhotonLab-Plugin-Schnittstelle (reines C, damit Plugins mit jedem Compiler gebaut werden können).
 *
 * Ein Plugin ist eine gemeinsame Bibliothek (.dll, .so, .dylib), die genau eine Funktion exportiert:
 *
 *     PHOTONLAB_PLUGIN_EXPORT const plab_plugin* photonlab_plugin(void);
 *
 * Sie liefert eine Beschreibung mit Bauteiltypen. Jeder Bauteiltyp hat Ports, Parameter (in SI) und eine
 * Funktion, die die Streumatrix S (b = S a, |a|^2 = Leistung) bei einer Vakuumwellenlänge berechnet. PhotonLab
 * meldet die Typen in der Bauteil-Registry an; Editor, Löser, Sweeps, Monte-Carlo, Logik-Labor und Dateiformat
 * behandeln sie wie eingebaute Bauteile. Alle Zeiger in der Beschreibung müssen gültig bleiben, solange das
 * Programm läuft (statische Daten). Die Funktion s_matrix muss threadsicher sein (sie wird parallel gerufen).
 * Beispiel: plugins/example/example_plugin.cpp, Beschreibung: docs/plugins.md
 */
#ifndef PHOTONLAB_PLUGIN_API_H
#define PHOTONLAB_PLUGIN_API_H

#ifdef __cplusplus
extern "C" {
#endif

#define PHOTONLAB_PLUGIN_API_VERSION 1

#if defined(_WIN32)
#define PHOTONLAB_PLUGIN_EXPORT __declspec(dllexport)
#else
#define PHOTONLAB_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

typedef struct plab_param {
  const char* name;          /* Schlüssel in der Projektdatei, z. B. "length" */
  const char* label;         /* Anzeigename (nur Latin-1-Zeichen, UTF-8 kodiert) */
  double default_value;      /* SI */
  double min_value;          /* SI */
  double max_value;          /* SI */
  const char* display_unit;  /* z. B. "um"; darf NULL sein */
  double display_scale;      /* SI-Wert = Anzeigewert * display_scale; 0 = 1 */
} plab_param;

/* Streumatrix bei wavelength [m]. params: Werte in der Reihenfolge der Parameterliste (SI). s_re, s_im:
 * port_count * port_count Werte, Zeile für Zeile (S[i][j] = s_re[i*n + j] + i*s_im[i*n + j]).
 * Rückgabe 0 = in Ordnung; sonst ist das Ergebnis ungültig. */
typedef int (*plab_s_matrix_fn)(double wavelength, const double* params, double* s_re, double* s_im);

typedef struct plab_component {
  const char* name;               /* eindeutiger Typname, z. B. "bragg_grating" */
  const char* label;              /* Anzeigename */
  const char* description;        /* Modell und Vereinfachungen in einem Satz */
  int port_count;                 /* 1 bis 64 */
  const char* const* port_names;  /* port_count Namen */
  int left_port_count;            /* so viele der ersten Ports links im Schaltplan; -1 = nach Namen ("in...") */
  int param_count;
  const plab_param* params;
  plab_s_matrix_fn s_matrix;
} plab_component;

typedef struct plab_plugin {
  int api_version; /* = PHOTONLAB_PLUGIN_API_VERSION */
  const char* name;
  const char* version;
  const char* description;
  int component_count;
  const plab_component* components;
} plab_plugin;

typedef const plab_plugin* (*plab_entry_fn)(void);

#ifdef __cplusplus
}
#endif

#endif /* PHOTONLAB_PLUGIN_API_H */
