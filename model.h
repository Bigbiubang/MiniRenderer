#include "geometry.h"
#include "texture.h"

class Model {
    std::vector<vec4> verts = {};    // array of vertices        ┐ generally speaking, these arrays
    std::vector<vec4> norms = {};    // array of normal vectors  │ do not have the same size
    std::vector<vec2> tex = {};      // array of tex coords      ┘ check the logs of the Model() constructor
    std::vector<int> facet_vrt = {}; //  ┐ per-triangle indices in the above arrays,
    std::vector<int> facet_nrm = {}; //  │ the size is supposed to be
    std::vector<int> facet_tex = {}; //  ┘ nfaces()*3

    Texture2D normalmap;
    Texture2D diffusemap;
    Texture2D specularmap;

    public:
        Model(const std::string filename);
        int nverts() const; // number of vertices
        int nfaces() const; // number of triangles
        vec4 vert(const int i) const;                          // 0 <= i < nverts()
        vec4 vert(const int iface, const int nthvert) const;   // 0 <= iface <= nfaces(), 0 <= nthvert < 3
        vec4 normal(const int iface, const int nthvert) const; // normal coming from the "vn x y z" entries in the .obj file
        vec4 normal(const vec2 &uv, const vec2 &uv_dx = {}, const vec2 &uv_dy = {},
                    TextureFilter filter = TextureFilter::Trilinear) const;
        bool has_normalmap() const;
        vec4 diffuse(const vec2 &uv, const vec2 &uv_dx = {}, const vec2 &uv_dy = {},
                     TextureFilter filter = TextureFilter::Trilinear) const;
        double specular(const vec2 &uv, const vec2 &uv_dx = {}, const vec2 &uv_dy = {},
                        TextureFilter filter = TextureFilter::Trilinear) const;
        vec2 uv(const int iface, const int nthvert) const;     // uv coordinates of triangle corners
};
