#include "photonlab/layout/gds.hpp"

#include <cmath>
#include <fstream>
#include <iterator>

namespace photonlab::layout {
namespace {

// Satztypen und Datentypen (GDSII Stream Format Manual, Kap. 3)
enum : std::uint16_t {
  header = 0x0002,
  bgnlib = 0x0102,
  libname = 0x0206,
  units = 0x0305,
  endlib = 0x0400,
  bgnstr = 0x0502,
  strname = 0x0606,
  endstr = 0x0700,
  boundary = 0x0800,
  layer_rec = 0x0D02,
  datatype_rec = 0x0E02,
  xy = 0x1003,
  endel = 0x1100
};

class Writer {
 public:
  void record(std::uint16_t type, const std::vector<std::uint8_t>& data = {}) {
    const std::size_t length = 4 + data.size();
    if (length > 0xFFFF) throw LayoutError("GDSII-Datensatz zu lang");
    put16(static_cast<std::uint16_t>(length));
    put16(type);
    bytes.insert(bytes.end(), data.begin(), data.end());
  }
  static void append16(std::vector<std::uint8_t>& d, std::int32_t v) {
    const auto u = static_cast<std::uint16_t>(v);
    d.push_back(static_cast<std::uint8_t>(u >> 8));
    d.push_back(static_cast<std::uint8_t>(u & 0xFF));
  }
  static void append32(std::vector<std::uint8_t>& d, std::int32_t v) {
    const auto u = static_cast<std::uint32_t>(v);
    for (int s = 24; s >= 0; s -= 8) d.push_back(static_cast<std::uint8_t>((u >> s) & 0xFF));
  }
  static void append64(std::vector<std::uint8_t>& d, std::uint64_t u) {
    for (int s = 56; s >= 0; s -= 8) d.push_back(static_cast<std::uint8_t>((u >> s) & 0xFF));
  }
  static std::vector<std::uint8_t> text(const std::string& s) {
    std::vector<std::uint8_t> d(s.begin(), s.end());
    if (d.size() % 2) d.push_back(0);  // Zeichenketten auf gerade Länge auffüllen
    return d;
  }
  std::vector<std::uint8_t> bytes;

 private:
  void put16(std::uint16_t v) {
    bytes.push_back(static_cast<std::uint8_t>(v >> 8));
    bytes.push_back(static_cast<std::uint8_t>(v & 0xFF));
  }
};

// Feste Zeitangabe (2000-01-01 00:00:00), damit gleiche Layouts byte-gleiche Dateien ergeben
std::vector<std::uint8_t> timestamps() {
  std::vector<std::uint8_t> d;
  for (int twice = 0; twice < 2; ++twice)
    for (int v : {2000, 1, 1, 0, 0, 0}) Writer::append16(d, v);
  return d;
}

}  // namespace

std::uint64_t encode_real8(double value) {
  if (value == 0.0) return 0;
  std::uint64_t sign = value < 0.0 ? 1 : 0;
  double v = std::abs(value);
  int exponent = 64;
  while (v >= 1.0) v /= 16.0, ++exponent;
  while (v < 1.0 / 16.0) v *= 16.0, --exponent;
  auto mantissa = static_cast<std::uint64_t>(std::llround(std::ldexp(v, 56)));
  if (mantissa >= (std::uint64_t{1} << 56)) mantissa >>= 4, ++exponent;
  if (exponent < 0 || exponent > 127) throw LayoutError("Zahl außerhalb des GDSII-Gleitkommabereichs");
  return sign << 63 | static_cast<std::uint64_t>(exponent) << 56 | mantissa;
}

double decode_real8(std::uint64_t bits) {
  const double sign = (bits >> 63) ? -1.0 : 1.0;
  const int exponent = static_cast<int>((bits >> 56) & 0x7F) - 64;
  const std::uint64_t mantissa = bits & ((std::uint64_t{1} << 56) - 1);
  return sign * std::ldexp(static_cast<double>(mantissa), 4 * exponent - 56);
}

std::vector<std::uint8_t> write_gds(const Layout& layout) {
  Writer w;
  std::vector<std::uint8_t> d;
  Writer::append16(d, 600);
  w.record(header, d);
  w.record(bgnlib, timestamps());
  w.record(libname, Writer::text(layout.library));
  d.clear();
  Writer::append64(
      d, encode_real8(layout.database_unit / layout.user_unit));  // Benutzer-Einheiten je DB-Einheit
  Writer::append64(d, encode_real8(layout.database_unit));        // Meter je DB-Einheit
  w.record(units, d);
  for (const Cell& cell : layout.cells) {
    w.record(bgnstr, timestamps());
    w.record(strname, Writer::text(cell.name));
    for (const Polygon& polygon : cell.polygons) {
      if (polygon.points.size() < 3) throw LayoutError("Polygon mit weniger als 3 Ecken");
      if (polygon.points.size() > 8190) throw LayoutError("Polygon mit mehr als 8190 Ecken");
      w.record(boundary);
      d.clear();
      Writer::append16(d, polygon.layer);
      w.record(layer_rec, d);
      d.clear();
      Writer::append16(d, polygon.datatype);
      w.record(datatype_rec, d);
      d.clear();
      for (const Point& p : polygon.points) Writer::append32(d, p.x), Writer::append32(d, p.y);
      Writer::append32(d, polygon.points.front().x),
          Writer::append32(d, polygon.points.front().y);  // schließen
      w.record(xy, d);
      w.record(endel);
    }
    w.record(endstr);
  }
  w.record(endlib);
  return w.bytes;
}

void write_gds_file(const Layout& layout, const std::string& path) {
  const auto bytes = write_gds(layout);
  std::ofstream out(path, std::ios::binary);
  if (!out) throw LayoutError("Datei kann nicht geschrieben werden: " + path);
  out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

Layout read_gds(const std::vector<std::uint8_t>& bytes) {
  Layout layout;
  layout.cells.clear();
  std::size_t pos = 0;
  auto u16 = [&](std::size_t at) {
    return static_cast<std::uint16_t>(static_cast<unsigned>(bytes[at]) << 8 | bytes[at + 1]);
  };
  auto i32 = [&](std::size_t at) {
    std::uint32_t u = 0;
    for (int k = 0; k < 4; ++k) u = u << 8 | bytes[at + static_cast<std::size_t>(k)];
    return static_cast<std::int32_t>(u);
  };
  auto u64 = [&](std::size_t at) {
    std::uint64_t u = 0;
    for (int k = 0; k < 8; ++k) u = u << 8 | bytes[at + static_cast<std::size_t>(k)];
    return u;
  };
  bool ended = false, in_boundary = false;
  Polygon current;
  while (pos + 4 <= bytes.size() && !ended) {
    const std::size_t length = u16(pos);
    const std::uint16_t type = u16(pos + 2);
    if (length < 4 || length % 2 || pos + length > bytes.size())
      throw LayoutError("GDSII-Datei beschädigt (Satzlänge an Byte " + std::to_string(pos) + ")");
    const std::size_t data = pos + 4, size = length - 4;
    switch (type) {
      case libname:
      case strname: {
        std::string s(bytes.begin() + static_cast<std::ptrdiff_t>(data),
                      bytes.begin() + static_cast<std::ptrdiff_t>(data + size));
        while (!s.empty() && s.back() == '\0') s.pop_back();
        if (type == libname)
          layout.library = s;
        else if (!layout.cells.empty())
          layout.cells.back().name = s;
        break;
      }
      case units:
        if (size != 16) throw LayoutError("GDSII: UNITS hat falsche Länge");
        layout.database_unit = decode_real8(u64(data + 8));
        layout.user_unit = layout.database_unit / decode_real8(u64(data));
        break;
      case bgnstr:
        layout.cells.push_back({});
        break;
      case boundary:
        in_boundary = true;
        current = {};
        break;
      case layer_rec:
        if (in_boundary) current.layer = static_cast<std::int16_t>(u16(data));
        break;
      case datatype_rec:
        if (in_boundary) current.datatype = static_cast<std::int16_t>(u16(data));
        break;
      case xy:
        if (in_boundary) {
          for (std::size_t k = 0; k + 8 <= size; k += 8)
            current.points.push_back({i32(data + k), i32(data + k + 4)});
          if (current.points.size() >= 2 && current.points.front() == current.points.back())
            current.points.pop_back();
        }
        break;
      case endel:
        if (in_boundary) {
          if (layout.cells.empty()) throw LayoutError("GDSII: Polygon außerhalb einer Zelle");
          layout.cells.back().polygons.push_back(current);
        }
        in_boundary = false;
        break;
      case endlib:
        ended = true;
        break;
      default:  // Pfade, Texte, Referenzen usw.: ihre Datensätze werden übersprungen
        break;
    }
    pos += length;
  }
  if (!ended) throw LayoutError("GDSII-Datei unvollständig (ENDLIB fehlt)");
  return layout;
}

Layout read_gds_file(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) throw LayoutError("Datei kann nicht gelesen werden: " + path);
  std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  return read_gds(bytes);
}

}  // namespace photonlab::layout
