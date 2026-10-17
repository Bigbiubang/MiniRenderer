#pragma once
#include "geometry.h"
#include <cmath>
#include <numbers>
#include <string>

struct SceneInstance {
    std::string path;
    vec3 translation{0, 0, 0};
    vec3 rotation{0, 0, 0}; // Euler angles in degrees: X, then Y, then Z
    vec3 scale{1, 1, 1};
    int mesh = -1;
    mat<4,4> model;
};

inline mat<4,4> make_model_matrix(const SceneInstance &instance) {
    const double radians = std::numbers::pi/180.;
    const double cx = std::cos(instance.rotation.x*radians), sx = std::sin(instance.rotation.x*radians);
    const double cy = std::cos(instance.rotation.y*radians), sy = std::sin(instance.rotation.y*radians);
    const double cz = std::cos(instance.rotation.z*radians), sz = std::sin(instance.rotation.z*radians);
    const mat<4,4> translation = {{{1,0,0,instance.translation.x}, {0,1,0,instance.translation.y},
                                   {0,0,1,instance.translation.z}, {0,0,0,1}}};
    const mat<4,4> rotate_x = {{{1,0,0,0}, {0,cx,-sx,0}, {0,sx,cx,0}, {0,0,0,1}}};
    const mat<4,4> rotate_y = {{{cy,0,sy,0}, {0,1,0,0}, {-sy,0,cy,0}, {0,0,0,1}}};
    const mat<4,4> rotate_z = {{{cz,-sz,0,0}, {sz,cz,0,0}, {0,0,1,0}, {0,0,0,1}}};
    const mat<4,4> scale = {{{instance.scale.x,0,0,0}, {0,instance.scale.y,0,0},
                             {0,0,instance.scale.z,0}, {0,0,0,1}}};
    return translation * rotate_z * rotate_y * rotate_x * scale;
}
