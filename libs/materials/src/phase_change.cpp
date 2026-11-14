#include "photonlab/materials/phase_change.hpp"

namespace photonlab::materials {

const PhaseChangeMaterial& gst225() {
  static const PhaseChangeMaterial gst{
      "GST-225",
      {4.4, 0.098},
      {7.1, 0.78},
      430.0,
      890.0,
      "n, k: 'Reversible optical gate switching in Si wire waveguide integrated with Ge2Sb2Te5 thin film' "
      "(Simulationswerte, Literatur streut stark); Temperaturen: Wuttig und Yamada, Nature Materials 6, 824 "
      "(2007)"};
  return gst;
}

}  // namespace photonlab::materials
