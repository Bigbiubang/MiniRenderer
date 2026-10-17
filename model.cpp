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
    auto load_texture = [&filename](const std::string suffix, TGAImage &img) {
        size_t dot = filename.find_last_of(".");
        if (dot==std::string::npos) return;
        std::string texfile = filename.substr(0,dot) + suffix;
        std::cerr << "texture file " << texfile << " loading " << (img.read_tga_file(texfile.c_str()) ? "ok" : "failed") << std::endl;
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

vec4 Model::normal(const vec2 &uv) const {
    int x = std::clamp(static_cast<int>(uv[0]*normalmap.width()), 0, normalmap.width()-1);
    int y = std::clamp(static_cast<int>(uv[1]*normalmap.height()), 0, normalmap.height()-1);
    TGAColor c = normalmap.get(x, y);
    return normalized(vec4{(double)c[2],(double)c[1],(double)c[0],0}*2./255. - vec4{1,1,1,0});
}

bool Model::has_normalmap() const {
    return normalmap.width() > 0 && normalmap.height() > 0;
}

vec4 Model::diffuse(const vec2 &uv) const {
    if (!diffusemap.width() || !diffusemap.height()) return {1, 1, 1, 1};
    int x = std::clamp(static_cast<int>(uv[0]*diffusemap.width()), 0, diffusemap.width()-1);
    int y = std::clamp(static_cast<int>(uv[1]*diffusemap.height()), 0, diffusemap.height()-1);
    TGAColor c = diffusemap.get(x, y);
    return vec4{(double)c[2], (double)c[1], (double)c[0], c.bytespp == 4 ? (double)c[3] : 255.} / 255.;
}

double Model::specular(const vec2 &uv) const {
    if (!specularmap.width() || !specularmap.height()) return 1.;
    int x = std::clamp(static_cast<int>(uv[0]*specularmap.width()), 0, specularmap.width()-1);
    int y = std::clamp(static_cast<int>(uv[1]*specularmap.height()), 0, specularmap.height()-1);
    return specularmap.get(x, y)[0] / 255.;
}

vec2 Model::uv(const int iface, const int nthvert) const {
    return tex[facet_tex[iface*3+nthvert]];
}
