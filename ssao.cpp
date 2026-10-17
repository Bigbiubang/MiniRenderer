#include "ssao.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>

std::vector<double> compute_ssao(const std::vector<double> &depth, const int width, const int height,
                                 const mat<4,4> &projection, const mat<4,4> &viewport,
                                 const double radius, const int sample_count) {
    const int pixel_count = width*height;
    const mat<4,4> inverse_projection = projection.inverse();
    std::vector<vec3> position(pixel_count), normal(pixel_count, {0, 0, 1});
    std::vector<std::uint8_t> valid(pixel_count, 0);
#ifdef _OPENMP
    #pragma omp parallel for
#endif
    for (int y=0; y<height; y++) {
        for (int x=0; x<width; x++) {
            const int index = x+y*width;
            if (depth[index] <= -999.) continue;
            const vec4 ndc = {(x-viewport[0][3])/viewport[0][0],
                              (y-viewport[1][3])/viewport[1][1], depth[index], 1};
            const vec4 p = inverse_projection*ndc;
            position[index] = p.xyz()/p.w;
            valid[index] = 1;
        }
    }

#ifdef _OPENMP
    #pragma omp parallel for
#endif
    for (int y=0; y<height; y++) {
        for (int x=0; x<width; x++) {
            const int index = x+y*width;
            if (!valid[index]) continue;
            auto axis_difference = [&](int left, int right) {
                const bool has_left = left >= 0 && left < pixel_count && valid[left];
                const bool has_right = right >= 0 && right < pixel_count && valid[right];
                if (has_right && (!has_left ||
                    std::abs(position[right].z-position[index].z) <
                    std::abs(position[left].z-position[index].z)))
                    return position[right]-position[index];
                return has_left ? position[index]-position[left] : vec3{};
            };
            const vec3 dx = axis_difference(x > 0 ? index-1 : -1,
                                            x+1 < width ? index+1 : -1);
            const vec3 dy = axis_difference(y > 0 ? index-width : -1,
                                            y+1 < height ? index+width : -1);
            const vec3 n = cross(dx, dy);
            if (norm(n) > 1e-12) normal[index] = normalized(n);
        }
    }

    std::vector<double> raw(pixel_count, 1.);
    constexpr double golden_angle = 2.399963229728653;
#ifdef _OPENMP
    #pragma omp parallel for
#endif
    for (int y=0; y<height; y++) {
        for (int x=0; x<width; x++) {
            const int index = x+y*width;
            if (!valid[index]) continue;
            const vec3 p = position[index], n = normal[index];
            const vec3 reference = std::abs(n.z) < .9 ? vec3{0, 0, 1} : vec3{0, 1, 0};
            const vec3 tangent = normalized(cross(reference, n));
            const vec3 bitangent = cross(n, tangent);
            std::uint32_t seed = 42u + static_cast<std::uint32_t>(index);
            seed ^= seed >> 16;
            seed *= 0x7feb352du;
            seed ^= seed >> 15;
            seed *= 0x846ca68bu;
            seed ^= seed >> 16;
            const double rotation = 2.*std::numbers::pi*seed/4294967296.;
            double occlusion = 0;
            int used = 0;
            for (int i=0; i<sample_count; i++) {
                const double u = (i+.5)/sample_count;
                const double angle = i*golden_angle + rotation;
                const double radial = std::sqrt(u);
                const double scale = .15 + .85*u*u;
                const vec3 direction = tangent*(radial*std::cos(angle)) +
                                       bitangent*(radial*std::sin(angle)) +
                                       n*std::sqrt(1.-u);
                const vec3 sample = p + direction*(radius*scale) + n*(radius*.01);
                const vec4 clip = projection*vec4{sample.x, sample.y, sample.z, 1};
                if (clip.w <= 0) continue;
                const vec4 screen = viewport*(clip/clip.w);
                if (screen.x < -.5 || screen.x >= width-.5 ||
                    screen.y < -.5 || screen.y >= height-.5) continue;
                const int sx = static_cast<int>(std::round(screen.x));
                const int sy = static_cast<int>(std::round(screen.y));
                const int hit_index = sx+sy*width;
                if (!valid[hit_index]) continue;
                const double delta = position[hit_index].z-p.z;
                if (std::abs(delta) >= radius) continue;
                used++;
                if (position[hit_index].z > sample.z + radius*.02)
                    occlusion += 1.-std::abs(delta)/radius;
            }
            if (used) raw[index] = std::clamp(1.-1.5*occlusion/used, 0., 1.);
        }
    }

    // Separable depth- and normal-aware blur removes sampling noise without crossing silhouettes.
    std::vector<double> intermediate(pixel_count, 1.), result(pixel_count, 1.);
    auto blur = [&](const std::vector<double> &source, std::vector<double> &destination, bool horizontal) {
#ifdef _OPENMP
        #pragma omp parallel for
#endif
        for (int y=0; y<height; y++) {
            for (int x=0; x<width; x++) {
                const int index = x+y*width;
                if (!valid[index]) continue;
                double total = 0, weight_sum = 0;
                for (int offset=-2; offset<=2; offset++) {
                    const int nx = x+(horizontal ? offset : 0);
                    const int ny = y+(horizontal ? 0 : offset);
                    if (nx < 0 || nx >= width || ny < 0 || ny >= height) continue;
                    const int neighbor = nx+ny*width;
                    if (!valid[neighbor]) continue;
                    const double depth_difference = std::abs(position[neighbor].z-position[index].z);
                    const double alignment = std::max(0., normal[index]*normal[neighbor]);
                    const double weight = (3.-std::abs(offset))*
                                          std::exp(-depth_difference/(radius*.25))*
                                          std::pow(alignment, 8);
                    total += source[neighbor]*weight;
                    weight_sum += weight;
                }
                destination[index] = weight_sum ? total/weight_sum : source[index];
            }
        }
    };
    blur(raw, intermediate, true);
    blur(intermediate, result, false);
    return result;
}
