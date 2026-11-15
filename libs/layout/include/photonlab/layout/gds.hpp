#pragma once
// GDSII-Stream (Calma), Format nach "GDSII Stream Format Manual" Release 6.0 (Cadence 1987): Datensätze mit
// 2 Byte Länge, 1 Byte Satztyp, 1 Byte Datentyp, Big-Endian; Gleitkommazahlen im 8-Byte-Format mit Exponent
// zur Basis 16 (Exzess 64). Geschrieben werden HEADER, BGNLIB, LIBNAME, UNITS, je Zelle BGNSTR/STRNAME, je
// Polygon BOUNDARY/LAYER/DATATYPE/XY/ENDEL, ENDSTR, ENDLIB.
#include <cstdint>
#include <string>
#include <vector>

#include "photonlab/layout/layout.hpp"

namespace photonlab::layout {

std::vector<std::uint8_t> write_gds(const Layout& layout);
void write_gds_file(const Layout& layout, const std::string& path);

// Liest die oben genannten Datensätze (Polygone, Einheiten, Zellnamen); andere Elemente (Pfade, Texte,
// Referenzen) werden übersprungen. Wirft LayoutError bei kaputten Dateien.
Layout read_gds(const std::vector<std::uint8_t>& bytes);
Layout read_gds_file(const std::string& path);

// 8-Byte-Gleitkommazahl des GDSII-Formats (öffentlich für Tests)
std::uint64_t encode_real8(double value);
double decode_real8(std::uint64_t bits);

}  // namespace photonlab::layout
