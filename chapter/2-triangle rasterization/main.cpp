#include <cmath>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <array>
#include "tgaimage.h"
#include "model.h"

constexpr int width  = 1280;
constexpr int height = 1280;

constexpr TGAColor white   = {255, 255, 255, 255}; // attention, BGRA order
constexpr TGAColor green   = {  0, 255,   0, 255};
constexpr TGAColor red     = {  0,   0, 255, 255};
constexpr TGAColor blue    = {255, 128,  64, 255};
constexpr TGAColor yellow  = {  0, 200, 255, 255};

void line(int x0, int y0, int x1, int y1, TGAImage &framebuffer, TGAColor color)
{
    bool steep = std::abs(x0 - x1) < std::abs(y0 - y1);    //是否陡峭
    if(steep){
        std::swap(x0, y0);
        std::swap(x1, y1);
    } 
    if(x0 > x1){
        std::swap(x0, x1);
        std::swap(y0, y1);
    }
    // float yplus = (y1 - y0) / static_cast<float>(x1-x0);
    int y = y0;
    int ierror = 0;
    for(int x=x0; x<=x1; x++){
        // float t = (x-x0) / static_cast<float>(x1-x0);
        // int y = std::round(y0 + (y1-y0)*t);
        if(steep){
            framebuffer.set(y, x, color);
        }
        else{
            framebuffer.set(x, y, color);
        }
        ierror += 2 * std::abs(y1-y0);
        y += (y1 > y0 ? 1 : -1) * (ierror > x1 - x0);
        ierror -= 2 * (x1-x0)   * (ierror > x1 - x0);
    }
}

double signed_triangle_area(int ax, int ay, int bx, int by, int cx, int cy){
    return 0.5 * ( (bx-ax) * (cy-ay) - (by-ay) * (cx-ax) );
}

void triangle(int ax, int ay, int bx, int by, int cx, int cy, TGAImage &frambuffer, TGAColor color){
    int bbminx = std::min(std::min(ax, bx), cx);
    int bbminy = std::min(std::min(ay, by), cy);
    int bbmaxx = std::max(std::max(ax, bx), cx);
    int bbmaxy = std::max(std::max(ay, by), cy);

    double total_area = signed_triangle_area(ax, ay, bx, by, cx, cy);
    if(total_area<1) return;

#pragma omp parallel for
    for(int x=bbminx; x<=bbmaxx; x++){
        for(int y=bbminy; y<=bbmaxy; y++){
            double alpha = signed_triangle_area(x, y, bx, by, cx, cy) / total_area;
            double beta  = signed_triangle_area(x, y, cx, cy, ax, ay) / total_area;
            double gamma = signed_triangle_area(x, y, ax, ay, bx, by) / total_area;

            if(alpha < 0 || beta < 0 || gamma < 0) continue;
           
            frambuffer.set(x, y, color);
            
        }
    }

}



int main(int argc, char** argv) {

    TGAImage framebuffer(width, height, TGAImage::RGB);
    // triangle(  7, 45, 35, 100, 45,  60, framebuffer, red);
    // triangle(120, 35, 90,   5, 45, 110, framebuffer, white);
    // triangle(115, 83, 80,  90, 85, 120, framebuffer, green);
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " obj/model.obj" << std::endl;
        return 1;
    }

    Model model(argv[1]);

    int nf = model.nfaces();

    for(int i=0;i<nf;i++){
        std::array<int,3> idxs = model.getface(i);

        std::array<float,3> v1 = model.getvert(idxs[0]);
        std::array<float,3> v2 = model.getvert(idxs[1]);
        std::array<float,3> v3 = model.getvert(idxs[2]);

        int ax=std::round((v1[0]+1) * width / 2),ay=std::round((v1[1]+1) * height / 2);
        int bx=std::round((v2[0]+1) * width / 2),by=std::round((v2[1]+1) * height / 2);
        int cx=std::round((v3[0]+1) * width / 2),cy=std::round((v3[1]+1) * height / 2);

        TGAColor rnd;
        for(int c=0; c<3; c++) rnd[c] = std::rand()%255;
        triangle(ax, ay, bx, by, cx, cy, framebuffer, rnd);
    }





    framebuffer.write_tga_file("framebuffer.tga");
    return 0;
}