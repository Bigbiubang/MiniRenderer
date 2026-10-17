#include "scene_file.h"
#include <fstream>
#include <iomanip>
#include <sstream>

bool parse_scene(std::istream &input, const std::filesystem::path &directory,
                 SceneConfig &config, std::string &error) {
    std::string line;
    int line_number = 0;
    while (std::getline(input, line)) {
        line_number++;
        std::istringstream values(line);
        std::string key;
        if (!(values >> key) || key.starts_with('#')) continue;

        auto number = [&](double &value) { return static_cast<bool>(values >> value) && std::isfinite(value); };
        auto vector = [&](vec3 &value) {
            return number(value.x) && number(value.y) && number(value.z);
        };
        bool valid = true;
        if (key == "size") {
            valid = static_cast<bool>(values >> config.width >> config.height);
        } else if (key == "msaa") {
            valid = static_cast<bool>(values >> config.msaa_samples);
        } else if (key == "texture_filter") {
            std::string filter;
            valid = static_cast<bool>(values >> filter) &&
                    (filter == "nearest" || filter == "trilinear");
            if (valid) config.texture_filter = filter == "nearest" ? TextureFilter::Nearest : TextureFilter::Trilinear;
        } else if (key == "eye" || key == "center" || key == "up" || key == "light") {
            vec3 &value = key == "eye" ? config.eye : key == "center" ? config.center :
                          key == "up" ? config.up : config.light;
            valid = vector(value);
        } else if (key == "fov" || key == "near" || key == "far") {
            double &value = key == "fov" ? config.fov :
                            key == "near" ? config.near_plane : config.far_plane;
            valid = number(value);
        } else if (key == "instance") {
            std::string path;
            valid = static_cast<bool>(values >> std::quoted(path)) && !path.empty();
            if (valid) config.instances.push_back({.path = (directory/path).lexically_normal().string()});
        } else if (key == "translate" || key == "rotate" || key == "scale") {
            valid = !config.instances.empty();
            if (valid) {
                vec3 &value = key == "translate" ? config.instances.back().translation :
                              key == "rotate" ? config.instances.back().rotation : config.instances.back().scale;
                valid = vector(value);
            }
        } else {
            valid = false;
        }
        values >> std::ws;
        if (!valid || !values.eof()) {
            error = "Scene line " + std::to_string(line_number) + ": invalid " + key;
            return false;
        }
    }
    return true;
}

bool load_scene_file(const std::filesystem::path &path, SceneConfig &config, std::string &error) {
    std::ifstream input(path);
    if (!input) {
        error = "Cannot open scene file: " + path.string();
        return false;
    }
    return parse_scene(input, std::filesystem::absolute(path).parent_path(), config, error);
}
