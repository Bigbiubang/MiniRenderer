#include <algorithm>
#include <fstream>
#include <sstream>
#include "model.h"

Model::Model(const std::string filename) {
    std::ifstream in;
    in.open(filename, std::ifstream::in);
    if (in.fail()) return;
    std::string line;
    while (!in.eof()) {
        std::getline(in, line);
        std::istringstream iss(line.c_str());
        char trash;
        if (!line.compare(0, 2, "v ")) {
            iss >> trash;
            vec4 v = {0,0,0,1};
            for (int i : {0,1,2}) iss >> v[i];
            verts.push_back(v);
        } else if (!line.compare(0, 3, "vn ")) {
            iss >> trash >> trash;
            vec4 n;
            for (int i : {0,1,2}) iss >> n[i];
            norms.push_back(normalized(n));
        } else if (!line.compare(0, 3, "vt ")) {
            iss >> trash >> trash;
            vec2 uv;
            for (int i : {0,1}) iss >> uv[i];
            tex.push_back({uv.x, 1-uv.y});
        } else if (!line.compare(0, 2, "f ")) {
            int f,t,n, cnt = 0;
            iss >> trash;
            while (iss >> f >> trash >> t >> trash >> n) {
                facet_vrt.push_back(--f);
                facet_tex.push_back(--t);
                facet_nrm.push_back(--n);
                cnt++;
            }
        }
    }
    std::cerr << "# v# " << nverts() << " f# "  << nfaces() << std::endl;
    auto load_texture = [&filename](const std::string suffix, Texture2D &texture) {
        size_t dot = filename.find_last_of(".");
        if (dot==std::string::npos) return;
        std::string texfile = filename.substr(0,dot) + suffix;
        std::cerr << "texture file " << texfile << " loading " << (texture.load(texfile) ? "ok" : "failed") << std::endl;
    };
    // load_texture("_nm.tga", normalmap);
    load_texture("_nm_tangent.tga", normalmap);

    load_texture("_diffuse.tga", diffusemap);
    load_texture("_spec.tga", specularmap);

}

int Model::nverts() const { return verts.size(); }
int Model::nfaces() const { return facet_vrt.size()/3; }

vec4 Model::vert(const int i) const {
    return verts[i];
}

vec4 Model::vert(const int iface, const int nthvert) const {
    return verts[facet_vrt[iface*3+nthvert]];
}

vec4 Model::normal(const int iface, const int nthvert) const {
    return norms[facet_nrm[iface*3+nthvert]];
}

vec4 Model::normal(const vec2 &uv, const vec2 &uv_dx, const vec2 &uv_dy, const TextureFilter filter) const {
    const vec4 c = normalmap.sample(uv, uv_dx, uv_dy, filter);
    const vec4 n = {2*c.x-1, 2*c.y-1, 2*c.z-1, 0};
    return norm(n) > 1e-12 ? normalized(n) : vec4{0, 0, 1, 0};
}

bool Model::has_normalmap() const {
    return !normalmap.empty();
}

vec4 Model::diffuse(const vec2 &uv, const vec2 &uv_dx, const vec2 &uv_dy, const TextureFilter filter) const {
    return diffusemap.empty() ? vec4{1, 1, 1, 1} : diffusemap.sample(uv, uv_dx, uv_dy, filter);
}

double Model::specular(const vec2 &uv, const vec2 &uv_dx, const vec2 &uv_dy, const TextureFilter filter) const {
    return specularmap.empty() ? 1. : specularmap.sample(uv, uv_dx, uv_dy, filter).x;
}

vec2 Model::uv(const int iface, const int nthvert) const {
    return tex[facet_tex[iface*3+nthvert]];
}
