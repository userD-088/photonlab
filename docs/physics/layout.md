# Maskenlayout und GDSII (Phase 13)

Code: `libs/layout` (`layout.hpp`, `gds.hpp`). Tests: `tests/test_layout.cpp`. CLI: `photonlab_cli gds`,
`photonlab_cli inverse-design --out-gds`. Oberfläche: Datei → „Feld-Szene als GDSII exportieren ...",
Bauteil-Labor → Inverses Design → „Als GDSII exportieren ...".

## Von der Szene zum Polygon

- Datenbank-Einheit 1 nm, Benutzer-Einheit 1 µm (übliche Werte für Photonik-Layouts). Koordinaten werden auf
  das Raster gerundet.
- y wird gespiegelt: Die Szene zählt y nach unten (wie der Bildschirm), Layout-Werkzeuge nach oben.
- Rechteck: vier Ecken, Drehung um den Mittelpunkt wie in `contains()`.
- Kreis: n Ecken auf dem Kreis, so dass die Sehne höchstens 1 nm vom Bogen abweicht (mindestens 16 Ecken);
  die Fläche liegt um höchstens Umfang × 1 nm unter π r².
- Ring: ein Polygon mit Schnittkante bei Winkel 0 (außen gegen, innen im Uhrzeigersinn).
- Alle Polygone gegen den Uhrzeigersinn, höchstens 8190 Ecken (Grenze des XY-Satzes).
- Lagen: je Brechzahl eine, größte Brechzahl = Lage 1. Die Reihenfolge der Formen (spätere überdecken
  frühere) gibt es in GDSII nicht; Formen mit der Brechzahl des Hintergrunds (Löcher) landen auf einer eigenen
  Lage und müssen beim Maskenentwurf abgezogen werden. Überlappende Formen werden nicht verschmolzen.
- Pixelbilder (inverses Design): Läufe gleicher Pixel je Zeile als Rechteck, gleiche Läufe in
  aufeinanderfolgenden Zeilen zusammengefasst.

## GDSII-Stream

Nach „GDSII Stream Format Manual" (Calma/Cadence, Release 6.0): Datensätze mit 2 Byte Länge, Satztyp,
Datentyp, Big-Endian. Geschrieben: HEADER (600), BGNLIB, LIBNAME, UNITS, BGNSTR, STRNAME, je Polygon BOUNDARY,
LAYER, DATATYPE, XY (geschlossen), ENDEL, dann ENDSTR, ENDLIB. Die Zeitangaben sind fest (2000-01-01), damit
gleiche Layouts byte-gleiche Dateien ergeben.

Gleitkommazahlen: 8 Byte, Vorzeichen, Exponent zur Basis 16 mit Exzess 64, 56 Bit Mantisse. Die Mantisse
hält einen double exakt; geprüft: 1,0 = 4110000000000000, 0,5 = 4080000000000000, −2 = C120000000000000.
Viele Werkzeuge schreiben für 1e-3 und 1e-9 die Bytes 3E4189374BC6A7EF und 3944B82FA09B5A51; PhotonLab
weicht davon in der letzten Stelle ab (1 bzw. 3 Einheiten von 2^56, relativ 1e-16), weil es genauer rundet.
Beim Lesen spielt das keine Rolle.

Der Leser (`read_gds`) versteht die geschriebenen Sätze und überspringt alles andere (Pfade, Texte,
Referenzen). Er dient den Tests (Hin und Zurück) und ist kein allgemeiner GDSII-Importer.

## Grenzen

- Kein Gegentest mit KLayout oder gdstk im Projekt (keine zusätzliche Abhängigkeit); geprüft sind Satzaufbau,
  Zahlenformat und Hin und Zurück.
- Nur Polygone (BOUNDARY), eine Zelle je Layout, keine Hierarchie, keine Texte (#120).
- Die Schaltung des Chip-Designers hat keine Geometrie; exportiert werden Feld-Szenen und inverse Designs.
