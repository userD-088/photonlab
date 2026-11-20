#include "viewport3d.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

#include "photonlab/core/units.hpp"

// Hinweis: Alle Texte bleiben im Latin-1-Zeichenvorrat (eingebaute ImGui-Schrift).

namespace photonlab::gui {
namespace {

// Weltkoordinaten in µm: x wie in der Szene, Welt-y = -Szenen-y (von oben sieht die Szene aus wie im
// 2D-Bild), z nach oben (0 = Unterkante der Schicht), z mit der Überhöhung gestreckt.
struct V3 {
  float x = 0.0f, y = 0.0f, z = 0.0f;
};
V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 operator*(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
V3 normalize(V3 a) {
  const float n = std::sqrt(dot(a, a));
  return n > 0.0f ? a * (1.0f / n) : a;
}

constexpr float fov = 0.70f;  // senkrechter Blickwinkel [rad], etwa 40 Grad

struct View {
  V3 eye, f, r, u;
  ImVec2 center;
  float focal = 1.0f, near_z = 0.01f;
  bool project(V3 w, ImVec2* s, float* depth) const {
    const V3 d = w - eye;
    const float z = dot(d, f);
    if (z < near_z) return false;
    *s = {center.x + focal * dot(d, r) / z, center.y - focal * dot(d, u) / z};
    if (depth) *depth = z;
    return true;
  }
  V3 ray(ImVec2 s) const {
    return normalize(f + r * ((s.x - center.x) / focal) - u * ((s.y - center.y) / focal));
  }
};

struct Prim {
  float depth = 0.0f;
  int count = 3;
  ImVec2 p[4];
  ImVec2 uv[4];
  ImU32 color = 0;
  ImTextureID texture = 0;
};

ImVec4 mix(ImVec4 a, ImVec4 b, float t) {
  return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t};
}

class Builder {
 public:
  explicit Builder(const View& view) : view_(view) {}
  std::vector<Prim> prims;

  // Ebene Fläche mit 3 oder 4 Ecken; normal zeigt nach außen. cull: Rückseiten weglassen.
  void face(const V3* pts, int count, V3 normal, ImVec4 color, bool cull) {
    V3 c{};
    for (int k = 0; k < count; ++k) c = c + pts[k];
    c = c * (1.0f / static_cast<float>(count));
    const float facing = dot(normal, view_.eye - c);
    if (cull && facing <= 0.0f) return;
    Prim p;
    p.count = count;
    float depth = 0.0f;
    for (int k = 0; k < count; ++k) {
      float d = 0.0f;
      if (!view_.project(pts[k], &p.p[k], &d)) return;
      depth += d;
    }
    p.depth = depth / static_cast<float>(count);
    // Licht von schräg oben; doppelseitige Flächen werden von der Seite beleuchtet, die man sieht
    const V3 n = facing < 0.0f ? normal * -1.0f : normal;
    const float k = 0.55f + 0.45f * std::max(0.0f, dot(n, normalize({0.35f, 0.45f, 1.0f})));
    p.color = ImGui::ColorConvertFloat4ToU32({color.x * k, color.y * k, color.z * k, color.w});
    prims.push_back(p);
  }
  void quad(V3 a, V3 b, V3 c, V3 d, V3 normal, ImVec4 color, bool cull) {
    const V3 pts[4] = {a, b, c, d};
    face(pts, 4, normal, color, cull);
  }
  void triangle(V3 a, V3 b, V3 c, V3 normal, ImVec4 color, bool cull) {
    const V3 pts[3] = {a, b, c};
    face(pts, 3, normal, color, cull);
  }
  void textured(const V3 (&pts)[4], const ImVec2 (&uv)[4], ImTextureID texture) {
    Prim p;
    p.count = 4;
    p.texture = texture;
    float depth = 0.0f;
    for (int k = 0; k < 4; ++k) {
      float d = 0.0f;
      if (!view_.project(pts[k], &p.p[k], &d)) return;
      depth += d;
      p.uv[k] = uv[k];
    }
    p.depth = depth * 0.25f;
    p.color = IM_COL32_WHITE;
    prims.push_back(p);
  }

  // Senkrechter Körper über einem konvexen Umriss (Rechteck, Taper, Kreis)
  void prism(const std::vector<V3>& loop, float z0, float z1, ImVec4 color) {
    const std::size_t n = loop.size();
    if (n < 3) return;
    V3 c{};
    for (const V3& p : loop) c = c + p;
    c = c * (1.0f / static_cast<float>(n));
    auto at = [&](std::size_t k, float z) { return V3{loop[k % n].x, loop[k % n].y, z}; };
    for (std::size_t k = 1; k + 1 < n; ++k) {
      triangle(at(0, z1), at(k, z1), at(k + 1, z1), {0, 0, 1}, color, true);
      triangle(at(0, z0), at(k, z0), at(k + 1, z0), {0, 0, -1}, color, true);
    }
    for (std::size_t k = 0; k < n; ++k) {
      const V3 a = at(k, z0), b = at(k + 1, z0);
      V3 normal = normalize(cross(b - a, {0, 0, 1}));
      const V3 mid = (a + b) * 0.5f;
      if (dot(normal, V3{mid.x - c.x, mid.y - c.y, 0}) < 0.0f) normal = normal * -1.0f;
      quad(a, b, at(k + 1, z1), at(k, z1), normal, color, true);
    }
  }

  // Band zwischen zwei Bögen (Ring, Bogen): außen und innen gleich viele Punkte um den Mittelpunkt center
  void band(const std::vector<V3>& outer, const std::vector<V3>& inner, V3 center, float z0, float z1,
            ImVec4 color, bool closed) {
    const std::size_t n = outer.size();
    if (n < 2 || inner.size() != n) return;
    const std::size_t segments = closed ? n : n - 1;
    for (std::size_t k = 0; k < segments; ++k) {
      const std::size_t m = (k + 1) % n;
      auto lift = [](V3 p, float z) { return V3{p.x, p.y, z}; };
      quad(lift(outer[k], z1), lift(outer[m], z1), lift(inner[m], z1), lift(inner[k], z1), {0, 0, 1}, color,
           true);
      quad(lift(outer[k], z0), lift(outer[m], z0), lift(inner[m], z0), lift(inner[k], z0), {0, 0, -1}, color,
           true);
      for (int side = 0; side < 2; ++side) {
        const V3 a = side == 0 ? outer[k] : inner[k], b = side == 0 ? outer[m] : inner[m];
        V3 normal = normalize(cross(b - a, {0, 0, 1}));
        const V3 mid = (a + b) * 0.5f;
        const float away = dot(normal, V3{mid.x - center.x, mid.y - center.y, 0});
        if ((side == 0) != (away > 0.0f)) normal = normal * -1.0f;  // außen weg vom Mittelpunkt, innen hin
        quad(lift(a, z0), lift(b, z0), lift(b, z1), lift(a, z1), normal, color, true);
      }
    }
    if (!closed)
      for (std::size_t end : {std::size_t{0}, n - 1}) {
        const V3 a = outer[end], b = inner[end];
        V3 normal = normalize(cross(b - a, {0, 0, 1}));
        const V3 next = outer[end == 0 ? 1 : n - 2];
        if (dot(normal, next - a) > 0.0f) normal = normal * -1.0f;
        quad(V3{a.x, a.y, z0}, V3{b.x, b.y, z0}, V3{b.x, b.y, z1}, V3{a.x, a.y, z1}, normal, color, true);
      }
  }

  // Quader [x0, x1] x [y0, y1] x [z0, z1] (Weltkoordinaten)
  void box(float x0, float x1, float y0, float y1, float z0, float z1, ImVec4 color) {
    const std::vector<V3> loop = {{x0, y0, 0}, {x1, y0, 0}, {x1, y1, 0}, {x0, y1, 0}};
    prism(loop, z0, z1, color);
  }

 private:
  const View& view_;
};

V3 world(double x_m, double y_m, float z) {
  return {static_cast<float>(x_m / units::um), static_cast<float>(-y_m / units::um), z};
}

int segments_for(double radius_m) {
  return std::clamp(static_cast<int>(radius_m / units::um * 24.0), 12, 48);
}

}  // namespace

void Viewport3d::draw(const field::FieldScene& scene, const field::Stack3d& stack, const FieldSlices* slices,
                      const ViewportHooks& hooks, ImVec2 size, float ui_scale) {
  ImGuiIO& io = ImGui::GetIO();
  const ImVec2 area_min = ImGui::GetCursorScreenPos();
  const ImVec2 area = {std::max(size.x, 50.0f), std::max(size.y, 50.0f)};
  const ImVec2 area_max = {area_min.x + area.x, area_min.y + area.y};
  ImDrawList* draw = ImGui::GetWindowDrawList();
  draw->AddRectFilled(area_min, area_max, IM_COL32(12, 15, 22, 255));
  if (scene.empty()) {
    ImGui::Dummy(area);
    return;
  }

  // Aufbau in Weltkoordinaten (µm, z überhöht)
  const float ez = exaggeration_;
  const float width = static_cast<float>(scene.width / units::um),
              height = static_cast<float>(scene.height / units::um);
  const float t = static_cast<float>(stack.thickness / units::um) * ez;
  const float below = static_cast<float>(stack.below / units::um) * ez;
  const float above = static_cast<float>(stack.above / units::um) * ez;
  const float z_mid = 0.5f * t;
  if (fit_pending_ || fitted_width_ != scene.width || fitted_height_ != scene.height) {
    camera_.tx = 0.5f * width, camera_.ty = -0.5f * height, camera_.tz = z_mid;
    camera_.distance = 0.95f * std::hypot(width, height) + 1.5f;
    fit_pending_ = false;
    fitted_width_ = scene.width, fitted_height_ = scene.height;
  }

  View view;
  const V3 target{camera_.tx, camera_.ty, camera_.tz};
  const V3 offset{std::cos(camera_.pitch) * std::cos(camera_.yaw),
                  std::cos(camera_.pitch) * std::sin(camera_.yaw), std::sin(camera_.pitch)};
  view.eye = target + offset * camera_.distance;
  view.f = normalize(target - view.eye);
  view.r = normalize(cross(view.f, {0, 0, 1}));
  view.u = cross(view.r, view.f);
  view.center = {area_min.x + 0.5f * area.x, area_min.y + 0.5f * area.y};
  view.focal = 0.5f * area.y / std::tan(0.5f * fov);
  view.near_z = 0.02f * camera_.distance;

  // --- Bedienung: links klicken wählt (ziehen verschiebt bzw. dreht), rechts dreht, Mitte verschiebt ---
  ImGui::SetNextItemAllowOverlap();
  ImGui::InvisibleButton("##raum", area,
                         ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                             ImGuiButtonFlags_MouseButtonMiddle);
  const bool hovered = ImGui::IsItemHovered();
  const ImVec2 mouse = io.MousePos;
  auto on_plane = [&](float z, float* x, float* y) {
    const V3 d = view.ray(mouse);
    if (std::abs(d.z) < 1e-6f) return false;
    const float s = (z - view.eye.z) / d.z;
    if (s <= 0.0f) return false;
    *x = view.eye.x + d.x * s, *y = view.eye.y + d.y * s;
    return true;
  };
  // Objekt unter dem Mauszeiger: nächster Treffer entlang des Sehstrahls
  auto pick = [&]() {
    Pick3d kind = Pick3d::none;
    int index = -1;
    float best = std::numeric_limits<float>::max();
    const V3 d = view.ray(mouse);
    auto line_object = [&](Pick3d k, int i, double x1, double y1, double x2, double y2, bool point) {
      if (point) {
        ImVec2 s;
        float depth = 0.0f;
        if (view.project(world(x1, y1, z_mid), &s, &depth) &&
            std::hypot(s.x - mouse.x, s.y - mouse.y) < 8.0f * ui_scale && depth < best)
          best = depth, kind = k, index = i;
        return;
      }
      const V3 a = world(x1, y1, 0), b = world(x2, y2, 0);
      V3 normal = normalize(cross(b - a, {0, 0, 1}));
      const float denom = dot(d, normal);
      if (std::abs(denom) < 1e-6f) return;
      const float s = dot(a - view.eye, normal) / denom;
      if (s <= 0.0f || s >= best) return;
      const V3 h = view.eye + d * s;
      const V3 ab = b - a;
      const float along = dot(V3{h.x - a.x, h.y - a.y, 0}, ab) / std::max(dot(ab, ab), 1e-12f);
      if (along < -0.02f || along > 1.02f || h.z < -below || h.z > t + above) return;
      best = s, kind = k, index = i;
    };
    for (int i = 0; i < static_cast<int>(scene.sources.size()); ++i) {
      const auto& s = scene.sources[static_cast<std::size_t>(i)];
      line_object(Pick3d::source, i, s.x1, s.y1, s.x2, s.y2, s.kind == field::SourceKind::point);
    }
    for (int i = 0; i < static_cast<int>(scene.monitors.size()); ++i) {
      const auto& m = scene.monitors[static_cast<std::size_t>(i)];
      line_object(Pick3d::monitor, i, m.x1, m.y1, m.x2, m.y2, m.kind == field::MonitorKind::point);
    }
    for (int i = 0; i < static_cast<int>(scene.ports.size()); ++i) {
      const auto& p = scene.ports[static_cast<std::size_t>(i)];
      line_object(Pick3d::port, i, p.x1, p.y1, p.x2, p.y2, false);
    }
    for (const float z : {t, z_mid}) {
      if (std::abs(d.z) < 1e-6f) break;
      const float s = (z - view.eye.z) / d.z;
      if (s <= 0.0f || s >= best) continue;
      const V3 h = view.eye + d * s;
      const double x = h.x * units::um, y = -h.y * units::um;
      for (int i = static_cast<int>(scene.shapes.size()) - 1; i >= 0; --i)
        if (field::contains(scene.shapes[static_cast<std::size_t>(i)], x, y)) {
          best = s, kind = Pick3d::shape, index = i;
          break;
        }
    }
    return std::pair{kind, index};
  };

  if (ImGui::IsItemActivated() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
    const auto [kind, index] = pick();
    const bool additive = io.KeyCtrl || io.KeyShift;
    dragging_object_ = false;
    moved_ = false;
    if (kind == Pick3d::none) {
      if (!additive && hooks.select) hooks.select(Pick3d::none, -1, false);
    } else {
      if (hooks.select && !(hooks.is_selected && hooks.is_selected(kind, index) && !additive))
        hooks.select(kind, index, additive);
      dragging_object_ = !additive && on_plane(z_mid, &drag_x_, &drag_y_);
    }
  }
  if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f)) {
    float x = 0.0f, y = 0.0f;
    if (dragging_object_ && on_plane(z_mid, &x, &y)) {
      if (hooks.move && (x != drag_x_ || y != drag_y_)) {
        hooks.move((x - drag_x_) * units::um, -(y - drag_y_) * units::um);
        moved_ = true;
      }
      drag_x_ = x, drag_y_ = y;
    } else if (!dragging_object_) {
      camera_.yaw -= io.MouseDelta.x * 0.008f;
      camera_.pitch = std::clamp(camera_.pitch + io.MouseDelta.y * 0.008f, 0.05f, 1.55f);
    }
  }
  if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Right, 2.0f)) {
    camera_.yaw -= io.MouseDelta.x * 0.008f;
    camera_.pitch = std::clamp(camera_.pitch + io.MouseDelta.y * 0.008f, 0.05f, 1.55f);
  }
  if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f)) {
    const float k = camera_.distance / view.focal;
    const V3 shift = view.r * (-io.MouseDelta.x * k) + view.u * (io.MouseDelta.y * k);
    camera_.tx += shift.x, camera_.ty += shift.y, camera_.tz += shift.z;
  }
  if (ImGui::IsItemDeactivated()) {
    if (moved_ && hooks.move_done) hooks.move_done();
    dragging_object_ = moved_ = false;
  }
  if (hovered && io.MouseWheel != 0.0f)
    camera_.distance = std::clamp(camera_.distance * std::pow(0.88f, io.MouseWheel), 0.5f, 5000.0f);
  // Rechtsklick ohne Ziehen: Menü (mit Ziehen wird gedreht)
  const ImVec2 right_drag = ImGui::GetMouseDragDelta(ImGuiMouseButton_Right, 0.0f);
  if (hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right) &&
      std::hypot(right_drag.x, right_drag.y) < 4.0f) {
    const auto [kind, index] = pick();
    if (kind != Pick3d::none && hooks.select && !(hooks.is_selected && hooks.is_selected(kind, index)))
      hooks.select(kind, index, false);
    ImGui::OpenPopup("kontext3d");
  }
  if (ImGui::BeginPopup("kontext3d")) {
    if (hooks.context_menu) hooks.context_menu();
    ImGui::EndPopup();
  }

  // --- Szene aufbauen ---
  Builder builder(view);
  const bool field_shown = slices && show_field_ && (slices->top || slices->side);
  const float solid_alpha = field_shown ? 0.35f : 0.95f;
  const ImVec4 color_core{0.60f, 0.66f, 0.78f, solid_alpha};
  const ImVec4 color_other{0.75f, 0.86f, 1.0f, 0.40f};
  const ImVec4 color_hole{0.07f, 0.07f, 0.09f, field_shown ? 0.5f : 0.95f};
  const ImVec4 color_selected{1.0f, 0.80f, 0.30f, 0.95f};
  const bool slab = scene.background_index >= stack.core_threshold;
  auto is_selected = [&](Pick3d kind, int i) { return hooks.is_selected && hooks.is_selected(kind, i); };

  // Substrat und (bei Kristallen) die Schicht selbst als Platte
  builder.box(0.0f, width, -height, 0.0f, -below, 0.0f, {0.28f, 0.42f, 0.66f, 0.22f});
  if (slab)
    builder.box(0.0f, width, -height, 0.0f, 0.0f, t, {color_core.x, color_core.y, color_core.z, 0.45f});

  for (int i = 0; i < static_cast<int>(scene.shapes.size()); ++i) {
    const auto& s = scene.shapes[static_cast<std::size_t>(i)];
    if (!s.enabled) continue;
    ImVec4 color = s.index >= stack.core_threshold ? color_core : s.index < 1.05 ? color_hole : color_other;
    if (is_selected(Pick3d::shape, i)) color = mix(color, color_selected, 0.55f);
    const double c = std::cos(s.rotation), sn = std::sin(s.rotation);
    if (s.kind == field::ShapeKind::rectangle || s.kind == field::ShapeKind::taper) {
      const double end = s.kind == field::ShapeKind::taper ? s.width2 : s.height;
      const double hx[4] = {-0.5, 0.5, 0.5, -0.5};
      const double hv[4] = {-0.5 * s.height, -0.5 * end, 0.5 * end, 0.5 * s.height};
      std::vector<V3> loop;
      for (int k = 0; k < 4; ++k) {
        const double u = hx[k] * s.width, v = hv[k];
        loop.push_back(world(s.x + c * u - sn * v, s.y + sn * u + c * v, 0));
      }
      builder.prism(loop, 0.0f, t, color);
    } else if (s.kind == field::ShapeKind::circle) {
      std::vector<V3> loop;
      const int n = segments_for(s.radius);
      for (int k = 0; k < n; ++k) {
        const double a = 2.0 * 3.14159265358979 * k / n;
        loop.push_back(world(s.x + s.radius * std::cos(a), s.y + s.radius * std::sin(a), 0));
      }
      builder.prism(loop, 0.0f, t, color);
    } else {
      const bool closed = s.kind == field::ShapeKind::ring;
      const int n = segments_for(s.radius);
      std::vector<V3> outer, inner;
      for (int k = 0; k <= (closed ? n - 1 : n); ++k) {
        const double a = closed ? 2.0 * 3.14159265358979 * k / n : s.rotation + s.sweep * k / n;
        outer.push_back(world(s.x + s.radius * std::cos(a), s.y + s.radius * std::sin(a), 0));
        inner.push_back(world(s.x + s.inner_radius * std::cos(a), s.y + s.inner_radius * std::sin(a), 0));
      }
      builder.band(outer, inner, world(s.x, s.y, 0), 0.0f, t, color, closed);
    }
  }

  // Quellen, Monitore und Ports: Flächen quer durch den Aufbau (so speisen bzw. messen sie in 3D)
  auto plane = [&](double x1, double y1, double x2, double y2, ImVec4 color) {
    const V3 a = world(x1, y1, 0), b = world(x2, y2, 0);
    builder.quad({a.x, a.y, -below}, {b.x, b.y, -below}, {b.x, b.y, t + above}, {a.x, a.y, t + above},
                 normalize(cross(b - a, {0, 0, 1})), color, false);
  };
  const ImVec4 color_source{1.0f, 0.62f, 0.25f, 0.28f}, color_monitor{0.40f, 0.85f, 0.55f, 0.22f},
      color_port{0.75f, 0.50f, 1.0f, 0.20f};
  auto stronger = [](ImVec4 c, bool on) { return on ? ImVec4{c.x, c.y, c.z, 0.55f} : c; };
  for (int i = 0; i < static_cast<int>(scene.sources.size()); ++i) {
    const auto& s = scene.sources[static_cast<std::size_t>(i)];
    if (s.enabled && s.kind != field::SourceKind::point)
      plane(s.x1, s.y1, s.x2, s.y2, stronger(color_source, is_selected(Pick3d::source, i)));
  }
  for (int i = 0; i < static_cast<int>(scene.monitors.size()); ++i) {
    const auto& m = scene.monitors[static_cast<std::size_t>(i)];
    if (m.enabled && m.kind != field::MonitorKind::point)
      plane(m.x1, m.y1, m.x2, m.y2, stronger(color_monitor, is_selected(Pick3d::monitor, i)));
  }
  for (int i = 0; i < static_cast<int>(scene.ports.size()); ++i) {
    const auto& p = scene.ports[static_cast<std::size_t>(i)];
    plane(p.x1, p.y1, p.x2, p.y2, stronger(color_port, is_selected(Pick3d::port, i)));
  }

  // Feldschnitte als Kacheln (damit sie sich mit den Körpern sortieren lassen)
  if (field_shown) {
    const float gw = static_cast<float>(slices->grid_width / units::um),
                gh = static_cast<float>(slices->grid_height / units::um);
    if (slices->top) {
      const int tx = 24, ty = std::max(4, static_cast<int>(std::lround(24.0f * gh / std::max(gw, 1e-3f))));
      for (int j = 0; j < ty; ++j)
        for (int i = 0; i < tx; ++i) {
          const float u0 = static_cast<float>(i) / tx, u1 = static_cast<float>(i + 1) / tx;
          const float v0 = static_cast<float>(j) / static_cast<float>(ty),
                      v1 = static_cast<float>(j + 1) / static_cast<float>(ty);
          const V3 pts[4] = {{u0 * gw, -v0 * gh, z_mid},
                             {u1 * gw, -v0 * gh, z_mid},
                             {u1 * gw, -v1 * gh, z_mid},
                             {u0 * gw, -v1 * gh, z_mid}};
          const ImVec2 uv[4] = {{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}};
          builder.textured(pts, uv, slices->top);
        }
    }
    if (slices->side) {
      const float y = static_cast<float>(-slices->y_cut / units::um);
      const float depth = static_cast<float>(slices->grid_depth / units::um);
      const float bottom =
          static_cast<float>(-slices->layer_bottom / units::um);  // Gitterboden relativ zur Schicht
      const int tx = 24, tz = 6;
      for (int k = 0; k < tz; ++k)
        for (int i = 0; i < tx; ++i) {
          const float u0 = static_cast<float>(i) / tx, u1 = static_cast<float>(i + 1) / tx;
          const float s0 = static_cast<float>(k) / tz, s1 = static_cast<float>(k + 1) / tz;  // von unten
          const float z0 = (bottom + s0 * depth) * ez, z1 = (bottom + s1 * depth) * ez;
          const V3 pts[4] = {{u0 * gw, y, z0}, {u1 * gw, y, z0}, {u1 * gw, y, z1}, {u0 * gw, y, z1}};
          const ImVec2 uv[4] = {{u0, 1.0f - s0}, {u1, 1.0f - s0}, {u1, 1.0f - s1}, {u0, 1.0f - s1}};
          builder.textured(pts, uv, slices->side);
        }
    }
  }

  // --- Zeichnen: Raster am Boden, Mantel als Umriss, dann alle Flächen von hinten nach vorn ---
  draw->PushClipRect(area_min, area_max, true);
  auto line3 = [&](V3 a, V3 b, ImU32 color, float thickness) {
    ImVec2 sa, sb;
    if (view.project(a, &sa, nullptr) && view.project(b, &sb, nullptr))
      draw->AddLine(sa, sb, color, thickness);
  };
  {
    const double nice[] = {0.5, 1, 2, 5, 10, 20, 50};
    float step = 1.0f;
    for (double n : nice)
      if (std::max(width, height) / n <= 30.0) {
        step = static_cast<float>(n);
        break;
      }
    const ImU32 grid = IM_COL32(70, 80, 100, 90);
    for (float x = 0.0f; x <= width + 1e-3f; x += step)
      line3({x, 0, -below}, {x, -height, -below}, grid, 1.0f);
    for (float y = 0.0f; y <= height + 1e-3f; y += step)
      line3({0, -y, -below}, {width, -y, -below}, grid, 1.0f);
    char text[32];
    std::snprintf(text, sizeof text, "Raster %g µm", static_cast<double>(step));
    draw->AddText({area_min.x + 8.0f, area_max.y - ImGui::GetFontSize() - 6.0f}, IM_COL32(150, 160, 180, 200),
                  text);
  }
  if (show_cladding_ && stack.cladding != "Luft") {
    const ImU32 clad = IM_COL32(120, 160, 220, 70);
    const float z1 = t + above;
    const V3 corners[4] = {{0, 0, 0}, {width, 0, 0}, {width, -height, 0}, {0, -height, 0}};
    for (int k = 0; k < 4; ++k) {
      const V3 a = corners[k], b = corners[(k + 1) % 4];
      line3({a.x, a.y, z1}, {b.x, b.y, z1}, clad, 1.0f);
      line3({a.x, a.y, 0}, {a.x, a.y, z1}, clad, 1.0f);
    }
  }
  std::sort(builder.prims.begin(), builder.prims.end(),
            [](const Prim& a, const Prim& b) { return a.depth > b.depth; });
  const ImDrawListFlags flags = draw->Flags;
  draw->Flags &= ~ImDrawListFlags_AntiAliasedFill;  // keine hellen Nähte zwischen Nachbarflächen
  for (const Prim& p : builder.prims) {
    if (p.texture)
      draw->AddImageQuad(p.texture, p.p[0], p.p[1], p.p[2], p.p[3], p.uv[0], p.uv[1], p.uv[2], p.uv[3]);
    else if (p.count == 3)
      draw->AddTriangleFilled(p.p[0], p.p[1], p.p[2], p.color);
    else
      draw->AddQuadFilled(p.p[0], p.p[1], p.p[2], p.p[3], p.color);
  }
  draw->Flags = flags;

  // Linien der Quellen und Monitore in der Mitte der Schicht, Punkte, Namen (immer obenauf)
  auto marker = [&](double x1, double y1, double x2, double y2, bool point, ImU32 color,
                    const std::string& name, bool selected) {
    const V3 a = world(x1, y1, z_mid), b = world(x2, y2, z_mid);
    ImVec2 sa;
    if (!view.project(a, &sa, nullptr)) return;
    if (point)
      draw->AddCircleFilled(sa, (selected ? 6.0f : 4.5f) * ui_scale, color);
    else
      line3(a, b, color, selected ? 3.0f : 2.0f);
    if (show_labels_ || selected)
      draw->AddText({sa.x + 6.0f, sa.y - ImGui::GetFontSize() - 2.0f}, color, name.c_str());
  };
  for (int i = 0; i < static_cast<int>(scene.sources.size()); ++i) {
    const auto& s = scene.sources[static_cast<std::size_t>(i)];
    if (s.enabled)
      marker(s.x1, s.y1, s.x2, s.y2, s.kind == field::SourceKind::point, IM_COL32(255, 160, 70, 255), s.name,
             is_selected(Pick3d::source, i));
  }
  for (int i = 0; i < static_cast<int>(scene.monitors.size()); ++i) {
    const auto& m = scene.monitors[static_cast<std::size_t>(i)];
    if (m.enabled)
      marker(m.x1, m.y1, m.x2, m.y2, m.kind == field::MonitorKind::point, IM_COL32(110, 220, 140, 255),
             m.name, is_selected(Pick3d::monitor, i));
  }
  for (int i = 0; i < static_cast<int>(scene.ports.size()); ++i) {
    const auto& p = scene.ports[static_cast<std::size_t>(i)];
    marker(p.x1, p.y1, p.x2, p.y2, false, IM_COL32(200, 140, 255, 255), p.name, is_selected(Pick3d::port, i));
  }
  // Achsenkreuz unten rechts
  {
    const ImVec2 origin = {area_max.x - 46.0f * ui_scale, area_max.y - 46.0f * ui_scale};
    const float length = 30.0f * ui_scale;
    const struct {
      V3 axis;
      ImU32 color;
      const char* label;
    } axes[] = {{{1, 0, 0}, IM_COL32(240, 90, 90, 255), "x"},
                {{0, -1, 0}, IM_COL32(110, 220, 110, 255), "y"},
                {{0, 0, 1}, IM_COL32(110, 150, 255, 255), "z"}};
    for (const auto& a : axes) {
      const ImVec2 tip = {origin.x + length * dot(a.axis, view.r), origin.y - length * dot(a.axis, view.u)};
      draw->AddLine(origin, tip, a.color, 2.0f);
      draw->AddText({tip.x + 2.0f, tip.y - 7.0f}, a.color, a.label);
    }
  }
  draw->PopClipRect();

  // --- Ansicht: Kamera-Vorgaben und Schalter (oben links, über der Fläche) ---
  ImGui::SetCursorScreenPos({area_min.x + 6.0f, area_min.y + 6.0f});
  struct Preset {
    const char* label;
    float yaw, pitch;
  };
  const Preset presets[] = {{"Schräg", -1.05f, 0.62f},
                            {"Oben", -1.5708f, 1.55f},
                            {"Vorne", -1.5708f, 0.08f},
                            {"Seite", 0.0f, 0.08f}};
  for (const auto& p : presets) {
    if (ImGui::SmallButton(p.label)) camera_.yaw = p.yaw, camera_.pitch = p.pitch;
    ImGui::SameLine();
  }
  if (ImGui::SmallButton("Einpassen")) fit_pending_ = true;
  ImGui::SameLine();
  ImGui::Checkbox("Mantel", &show_cladding_);
  ImGui::SameLine();
  ImGui::Checkbox("Feld", &show_field_);
  ImGui::SameLine();
  ImGui::Checkbox("Namen", &show_labels_);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0f);
  ImGui::SliderFloat("##hoehe", &exaggeration_, 1.0f, 10.0f, "Höhe x%.0f");
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
    ImGui::SetTooltip(
        "Überhöhung in z: die Schicht (220 nm) ist sonst dünn wie Papier.\nGerechnet wird unverzerrt.");
  ImGui::SetCursorScreenPos({area_min.x + 6.0f, area_min.y + 6.0f + ImGui::GetFrameHeightWithSpacing()});
  ImGui::TextDisabled(
      "Links: wählen und ziehen (ins Leere: drehen)  Rechts: drehen, Menü  Mitte: verschieben  Rad: zoomen");
  ImGui::SetCursorScreenPos({area_min.x, area_max.y});
  ImGui::Dummy({0.0f, 0.0f});
}

}  // namespace photonlab::gui
