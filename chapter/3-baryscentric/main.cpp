#include <cmath>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <array>
#include "tgaimage.h"
#include "model.h"

constexpr int width  = 64;
constexpr int height = 64;

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
void triangle(int ax, int ay, int az, int bx, int by, int bz, int cx, int cy, int cz, TGAImage &framebuffer) {
    int bbminx = std::min(std::min(ax, bx), cx); // bounding box for the triangle
    int bbminy = std::min(std::min(ay, by), cy); // defined by its top left and bottom right corners
    int bbmaxx = std::max(std::max(ax, bx), cx);
    int bbmaxy = std::max(std::max(ay, by), cy);
    double total_area = signed_triangle_area(ax, ay, bx, by, cx, cy);
    if (total_area<1) return; // backface culling + discarding triangles that cover less than a pixel

#pragma omp parallel for
    for (int x=bbminx; x<=bbmaxx; x++) {
        for (int y=bbminy; y<=bbmaxy; y++) {
            double alpha = signed_triangle_area(x, y, bx, by, cx, cy) / total_area;
            double beta  = signed_triangle_area(x, y, cx, cy, ax, ay) / total_area;
            double gamma = signed_triangle_area(x, y, ax, ay, bx, by) / total_area;
            if (alpha<0 || beta<0 || gamma<0) continue; // negative barycentric coordinate => the pixel is outside the triangle

            float threshold = 0.11;
            if (alpha > threshold &&
                beta  > threshold &&
                gamma > threshold) {
                continue;
            }
     
        
            TGAColor color, color_a = red, color_b = green, color_c = blue;
            color[0] = alpha * color_a[0] + beta * color_b[0] + gamma * color_c[0];
            color[1] = alpha * color_a[1] + beta * color_b[1] + gamma * color_c[1];
            color[2] = alpha * color_a[2] + beta * color_b[2] + gamma * color_c[2];
         

            color[3] = 255;


            framebuffer.set(x, y, color);
        }
    }
}



int main(int argc, char** argv) {
    constexpr int width  = 64;
    constexpr int height = 64;
    TGAImage framebuffer(width, height, TGAImage::RGB);

    int ax = 17, ay =  4, az =  13;
    int bx = 55, by = 39, bz = 128;
    int cx = 23, cy = 59, cz = 255;

    triangle(ax, ay, az, bx, by, bz, cx, cy, cz, framebuffer);

    framebuffer.write_tga_file("framebuffer.tga");
    return 0;
}