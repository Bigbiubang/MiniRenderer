#pragma once

#include "geometry.h"
#include <vector>

std::vector<double> compute_ssao(const std::vector<double> &depth, int width, int height,
                                 const mat<4,4> &projection, const mat<4,4> &viewport,
                                 double radius, int sample_count);
