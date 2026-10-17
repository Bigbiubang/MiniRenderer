#pragma once
#include <iostream>
#include <cassert>
#include <cmath>

template<int n> struct vec {
    double data[n] = {0};
    double& operator[](const int i)       {assert(i>=0 && i<n); return data[i];}
    double  operator[](const int i) const {assert(i>=0 && i<n); return data[i];}
};

template<int n> std::ostream& operator<<(std::ostream& out, const vec<n>& v) {
    for(int i=0;i<n;i++) out<<v[i]<<" ";
    return out;
};

template<> struct vec<2> {
    double x = 0, y = 0;
    double& operator[](const int i)       {assert(i>=0 && i<2); return i ? y : x;}
    double  operator[](const int i) const {assert(i>=0 && i<2); return i ? y : x;}
};

template<> struct vec<3> {
    double x = 0, y = 0, z = 0;
    double& operator[](const int i)       {assert(i>=0 && i<3); return i ? (1==i ? y : z) : x;}
    double  operator[](const int i) const {assert(i>=0 && i<3); return i ? (1==i ? y : z) : x;}
};

template<> struct vec<4> {
    double x = 0, y = 0, z = 0, w = 0;
    double& operator[](const int i)       {assert(i>=0 && i<4); return i ? (1==i ? y : i==2?z:w) : x;}
    double  operator[](const int i) const {assert(i>=0 && i<4); return i ? (1==i ? y : i==2?z:w) : x;}
    
    vec<2> xy() const {
        return {x, y};
    }

    vec<3> xyz() const {
        return {x, y, z};
    }

};

typedef vec<2> vec2;
typedef vec<3> vec3;
typedef vec<4> vec4;



template<int n> vec<n> operator+(const vec<n>&a, const vec<n>&b) {
    vec<n> c;
    for(int i=0; i<n; i++) c[i] = a[i] + b[i];
    return c;
};

template<int n> vec<n> operator/(const vec<n>&a, double num) {
    vec<n> res;
    for(int i=0; i<n; i++) res[i] = a[i] / num;
    return res;
}

template<int n> vec<n> operator-(const vec<n>&a, const vec<n>&b) {
    vec<n> c;
    for(int i=0; i<n; i++) c[i] = a[i] - b[i];
    return c;
};

template<int n> double operator*(const vec<n>&a, const vec<n>&b) {
    double res=0;
    for(int i=0; i<n; i++) res += a[i] * b[i];
    return res;
};

template<int n> vec<n> operator*(const vec<n>&v, const double num) {
    vec<n> res;
    for(int i=0; i<n; i++) res[i] = v[i] * num;
    return res;
}

template<int n> vec<n> operator*(const double num, const vec<n>&v) {
    vec<n> res;
    for(int i=0; i<n; i++) res[i] = v[i] * num;
    return res;
}

template<int n> vec<n> hadamard(const vec<n>& a, const vec<n>& b) {
    vec<n> res;
    for(int i=0; i<n; i++) res[i] = a[i] * b[i];
    return res;
}

template<int n> vec<n> normalized(const vec<n>& v) {
    vec<n> res;
    double sum = 0;
    for(int i=0; i<n; i++) sum += v[i] * v[i];
    assert(sum > 1e-12);
    sum = sqrt(sum);
    for(int i=0; i<n; i++) res[i] = v[i] / sum;
    return res;
}

//计算向量长度
template<int n> double norm(const vec<n>& v) {
    double res=0;
    for(int i=0; i<n; i++) res += pow(v[i], 2);
    return sqrt(res);
}


inline double cross(const vec<2>&a, const vec<2>&b) {
    return a[0] * b[1] - a[1] * b[0];
};

inline vec<3> cross(const vec<3>&a, const vec<3>&b) {
    vec<3> res;
    res[0] = a[1] * b[2] - a[2] * b[1];
    res[1] = a[2] * b[0] - a[0] * b[2];
    res[2] = a[0] * b[1] - a[1] * b[0];
    return res;
};

template<int row, int col> struct mat{
    double data[row][col] = {0};
    double* operator[](const int i) {assert(i>=0 && i<row); return data[i];};
    const double* operator[](const int i) const {assert(i>=0 && i<row); return data[i];};

    mat<col, row> transpose() const {
        mat<col, row> result;
        for (int i=0; i<row; i++)
            for (int j=0; j<col; j++) result[j][i] = data[i][j];
        return result;
    }

    mat<row, col> inverse() const {
        static_assert(row == col, "Matrix must be square");
        return invert_transpose().transpose();
    }

    double det() const {
        static_assert (row==3 && col==3, "det() currently only supports 3x3 matrices");
        return
            data[0][0] * (data[1][1] * data[2][2] - data[1][2] * data[2][1])
          - data[0][1] * (data[1][0] * data[2][2] - data[1][2] * data[2][0])
          + data[0][2] * (data[1][0] * data[2][1] - data[1][1] * data[2][0]);
    }

    mat<row, col> invert_transpose() const {

        static_assert(row == col, "Matrix must be square");

        constexpr int N = row;

        // 构造增广矩阵 [A | I]
        double aug[N][2 * N] = {};

        for (int i = 0; i < N; i++) {
            for (int j = 0; j < N; j++) {
                aug[i][j] = data[i][j];
            }

            aug[i][N + i] = 1.0;
        }

        // Gauss-Jordan 消元
        for (int i = 0; i < N; i++) {

            // 选择绝对值最大的主元
            int pivot = i;

            for (int r = i + 1; r < N; r++) {
                if (std::abs(aug[r][i]) >
                    std::abs(aug[pivot][i])) {
                    pivot = r;
                }
            }

            // 检查是否可逆
            assert(std::abs(aug[pivot][i]) > 1e-12);

            // 交换行
            for (int j = 0; j < 2 * N; j++) {
                std::swap(aug[i][j], aug[pivot][j]);
            }

            // 将主元归一化为 1
            double div = aug[i][i];

            for (int j = 0; j < 2 * N; j++) {
                aug[i][j] /= div;
            }

            // 消去其他行第 i 列
            for (int r = 0; r < N; r++) {

                if (r == i) continue;

                double factor = aug[r][i];

                for (int j = 0; j < 2 * N; j++) {
                    aug[r][j] -= factor * aug[i][j];
                }
            }
        }

        // 此时右半部分为 A^-1
        // 转置后得到 A^-T
        mat<row, col> result = {};

        for (int i = 0; i < N; i++) {
            for (int j = 0; j < N; j++) {
                result[i][j] = aug[j][N + i];
            }
        }

        return result;
    }

};




template<int row, int col> vec<row> operator*(const mat<row,col>& A, const vec<col>& v) {
    vec<row> res;
    for(int i=0; i<row; i++){
        for(int j=0; j<col; j++) {
            res[i] += A[i][j] * v[j];
        }
    }
    return res;
};

template<int row1, int col1, int row2, int col2> mat<row1, col2> operator*(const mat<row1,col1>& A, const mat<row2, col2>& B) {
    static_assert(col1 == row2);
    
    mat<row1, col2> res;

    for(int i=0; i<row1; i++) {
        for(int j=0; j<col2; j++) {

            for(int t=0; t<col1; t++){
                res[i][j] += A[i][t] * B[t][j];
            }

        }
    }

    return res;
}

