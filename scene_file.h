#pragma once
#include "scene.h"
#include "texture.h"
#include <filesystem>
#include <istream>
#include <numbers>
#include <string>
#include <vector>

struct SceneConfig {
    int width = 800, height = 800, msaa_samples = 1;
    TextureFilter texture_filter = TextureFilter::Trilinear;
    vec3 eye{-1, 0, 2}, center{0, 0, 0}, up{0, 1, 0};
    double fov = 2.*std::atan(1./std::sqrt(5.))*180./std::numbers::pi;
    double near_plane = .1, far_plane = 100.;
    vec3 light{1, 1, 1}; // direction toward the directional light
    std::vector<SceneInstance> instances;
};

bool parse_scene(std::istream &input, const std::filesystem::path &directory,
                 SceneConfig &config, std::string &error);
bool load_scene_file(const std::filesystem::path &path, SceneConfig &config, std::string &error);
