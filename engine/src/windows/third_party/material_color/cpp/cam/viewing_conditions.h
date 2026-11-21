/*
 * Copyright 2022 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Modified for ii-windows (see README.md next to cpp/): default viewing conditions at
 * full double precision; CreateViewingConditions (viewing_conditions.cc) not vendored.
 */

#ifndef CPP_CAM_VIEWING_CONDITIONS_H_
#define CPP_CAM_VIEWING_CONDITIONS_H_

namespace material_color_utilities {

struct ViewingConditions {
  double adapting_luminance = 0.0;
  double background_lstar = 0.0;
  double surround = 0.0;
  bool discounting_illuminant = false;
  double background_y_to_white_point_y = 0.0;
  double aw = 0.0;
  double nbb = 0.0;
  double ncb = 0.0;
  double c = 0.0;
  double n_c = 0.0;
  double fl = 0.0;
  double fl_root = 0.0;
  double z = 0.0;

  double white_point[3] = {0.0, 0.0, 0.0};
  double rgb_d[3] = {0.0, 0.0, 0.0};
};

// ii-windows: upstream rounds these to 9 decimals (and derives aw with a differently
// grouped but equivalent formula). These are the exact doubles materialyoucolor-python's
// ViewingConditions.make() computes (sRGB, D65, L* 50 background, average surround), so
// HCT here is bit-identical to the Python ii runs on Linux; with the rounded set, chroma
// was off by up to 1e-3.
static const ViewingConditions kDefaultViewingConditions = ViewingConditions{
    11.725677948856951,  // adapting_luminance
    50.0,  // background_lstar
    2.0,  // surround
    false,  // discounting_illuminant
    0.18418651851244416,  // background_y_to_white_point_y (n)
    29.980997194447333,  // aw
    1.0169191804458755,  // nbb
    1.0169191804458755,  // ncb
    0.69,  // c
    1.0,  // n_c
    0.3884814537800353,  // fl
    0.7894826179304937,  // fl_root
    1.909169568483652,  // z
    {95.047, 100.0, 108.883},
    {1.02117770275752, 0.9863077294280124, 0.9339605082802299},
};

}  // namespace material_color_utilities
#endif  // CPP_CAM_VIEWING_CONDITIONS_H_
