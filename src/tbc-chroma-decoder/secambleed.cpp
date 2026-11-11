/************************************************************************

    secambleed.cpp

    tbc-chroma-decoder - Colourisation filter for the tbc format.

    This file is part of tbc-tools.

    tbc-chroma-decoder is free software: you can redistribute it and/or
    modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation, either version 3 of the
    License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

************************************************************************/

#include "secambleed.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>

namespace SecamBleed {

namespace {

// Mirror index for "reflect" boundaries (d c b a | a b c d | d c b a).
inline int mirror(int i, int n)
{
    if (n == 1) return 0;
    while (i < 0 || i >= n) {
        if (i < 0) i = -i - 1;
        if (i >= n) i = 2 * n - i - 1;
    }
    return i;
}

// Centred moving average of odd `size` along a strided 1D signal.
void boxLine(const double *src, double *dst, int n, int size, int stride = 1)
{
    const int r = size / 2;
    double s = 0.0;
    for (int k = -r; k <= r; k++) s += src[mirror(k, n) * stride];
    for (int i = 0; i < n; i++) {
        dst[i * stride] = s / size;
        s += src[mirror(i + r + 1, n) * stride] - src[mirror(i - r, n) * stride];
    }
}

// Separable Gaussian blur, sigma in samples, radius 4 sigma (rounded).
void gaussian2d(const std::vector<double> &src, std::vector<double> &dst, int w, int h, double sigma)
{
    const int r = static_cast<int>(4.0 * sigma + 0.5);
    std::vector<double> k(2 * r + 1);
    double s = 0.0;
    for (int i = -r; i <= r; i++) s += (k[i + r] = std::exp(-0.5 * i * i / (sigma * sigma)));
    for (double &v : k) v /= s;
    std::vector<double> tmp(src.size());
    for (int y = 0; y < h; y++) {
        const double *row = &src[static_cast<size_t>(y) * w];
        for (int x = 0; x < w; x++) {
            double a = 0.0;
            for (int i = -r; i <= r; i++) a += k[i + r] * row[mirror(x + i, w)];
            tmp[static_cast<size_t>(y) * w + x] = a;
        }
    }
    dst.assign(src.size(), 0.0);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            double a = 0.0;
            for (int i = -r; i <= r; i++) a += k[i + r] * tmp[static_cast<size_t>(mirror(y + i, h)) * w + x];
            dst[static_cast<size_t>(y) * w + x] = a;
        }
}

// Decimated response: taps at offset k land on the coarse offset round(k/f).
std::vector<double> decimateKernel(const std::vector<double> &h, int f)
{
    const int half = static_cast<int>(h.size()) / 2;
    const int base = (half + f - 1) / f;
    std::vector<double> hd;
    for (int i = 0; i < static_cast<int>(h.size()); i++) {
        const int idx = static_cast<int>(std::lround(static_cast<double>(i - half) / f)) + base;
        if (idx >= static_cast<int>(hd.size())) hd.resize(idx + 1, 0.0);
        hd[idx] += h[i];
    }
    if (hd.size() % 2 == 0) hd.push_back(0.0);
    double s = 0.0;
    for (double v : hd) s += v;
    for (double &v : hd) v /= s;
    return hd;
}

// Normal matrix H^T H of the "same" convolution by hd on n samples, lower band stored by
// rows: A[j*(bw+1) + t] = A(j, j - bw + t), t = 0..bw (t = bw is the diagonal).
std::vector<double> normalBand(const std::vector<double> &hd, int n)
{
    const int K = static_cast<int>(hd.size()), hh = K / 2, bw = K - 1, B = bw + 1;
    auto H = [&](int i, int j) { const int k = i - j + hh; return (k >= 0 && k < K) ? hd[k] : 0.0; };
    std::vector<double> band(static_cast<size_t>(n) * B, 0.0);
    for (int j = 0; j < n; j++)
        for (int t = 0; t < B; t++) {
            const int k = j - bw + t;
            if (k < 0) continue;
            double a = 0.0;
            const int i0 = std::max(0, j - hh), i1 = std::min(n - 1, k + hh);
            for (int i = i0; i <= i1; i++) a += H(i, j) * H(i, k);
            band[static_cast<size_t>(j) * B + t] = a;
        }
    return band;
}

// Solves the banded SPD system in place on b; L (same layout as normalBand) is overwritten
// by its Cholesky factor. Row-oriented so the inner products run over contiguous memory.
void solveBanded(std::vector<double> &L, int bw, std::vector<double> &b)
{
    const int n = static_cast<int>(b.size()), B = bw + 1;
    for (int j = 0; j < n; j++) {
        double *Lj = &L[static_cast<size_t>(j) * B];        // Lj[t] = L(j, j-bw+t)
        for (int t = std::max(0, bw - j); t < B; t++) {
            const int k = j - bw + t;
            const double *Lk = &L[static_cast<size_t>(k) * B]; // Lk[t'] = L(k, k-bw+t')
            const int i0 = std::max(j - bw, k - bw);
            double s = Lj[t];
            const double *pj = Lj + (i0 - (j - bw)), *pk = Lk + (i0 - (k - bw));
            for (int i = 0, c = k - i0; i < c; i++) s -= pj[i] * pk[i];
            if (k < j) Lj[t] = s / Lk[bw];
            else Lj[t] = std::sqrt(std::max(s, 1e-300));
        }
    }
    for (int j = 0; j < n; j++) {                            // L z = b
        const double *Lj = &L[static_cast<size_t>(j) * B];
        double s = b[j];
        for (int k = std::max(0, j - bw); k < j; k++) s -= Lj[k - (j - bw)] * b[k];
        b[j] = s / Lj[bw];
    }
    for (int j = n - 1; j >= 0; j--) {                       // L^T x = z
        double s = b[j];
        for (int i = j + 1; i <= std::min(n - 1, j + bw); i++) s -= L[static_cast<size_t>(i) * B + (j - (i - bw))] * b[i];
        b[j] = s / L[static_cast<size_t>(j) * B + bw];
    }
}

// Step 1 on one component, all rows.
void deconvolve(const std::vector<double> &Y, std::vector<double> &c, int w, int h,
                const std::vector<double> &kernel, const Params &p)
{
    const int f = std::max(1, p.decimation), m = w / f;
    if (m < 4) return;
    const std::vector<double> hd = decimateKernel(kernel, f);
    const int K = static_cast<int>(hd.size()), hh = K / 2, bw = K - 1;
    const int B = bw + 1;
    const std::vector<double> band0 = normalBand(hd, m);
    std::vector<double> A(band0.size());

    std::vector<double> cd(m), ys(w), dyf(w - 1), rhs(m), olis(m), delta(m);
    for (int y = 0; y < h; y++) {
        double *row = &c[static_cast<size_t>(y) * w];
        const double *lum = &Y[static_cast<size_t>(y) * w];
        for (int j = 0; j < m; j++) {
            double s = 0.0;
            for (int t = 0; t < f; t++) s += row[j * f + t];
            cd[j] = s / f;
        }
        // Smoothness weights between coarse samples: the largest luma step inside each gap.
        boxLine(lum, ys.data(), w, 3);
        for (int x = 0; x + 1 < w; x++) dyf[x] = std::abs(ys[x + 1] - ys[x]);
        std::copy(band0.begin(), band0.end(), A.begin());
        for (int j = 0; j + 1 < m; j++) {
            const int x = f / 2 + j * f;
            double d = 0.0;
            for (int t = -1; t <= 1; t++) d = std::max(d, dyf[mirror(x + t, w - 1)]);
            d *= f / 3.0;
            const double wt = p.lambda * std::exp(-(d / p.sigma) * (d / p.sigma));
            A[static_cast<size_t>(j) * B + bw] += wt;
            A[static_cast<size_t>(j + 1) * B + bw] += wt;
            A[static_cast<size_t>(j + 1) * B + bw - 1] -= wt;  // A(j+1, j)
        }
        for (int j = 0; j < m; j++) A[static_cast<size_t>(j) * B + bw] += p.mu;
        boxLine(cd.data(), olis.data(), m, 3);
        for (int j = 0; j < m; j++) {
            double s = 0.0;
            const int i0 = std::max(0, j - hh), i1 = std::min(m - 1, j + hh);
            for (int i = i0; i <= i1; i++) s += cd[i] * hd[i - j + hh];
            rhs[j] = s + p.mu * olis[j];
        }
        solveBanded(A, bw, rhs);
        for (int j = 0; j < m; j++) delta[j] = rhs[j] - cd[j];
        // Back to full width (linear, clamped at the ends) as a correction of the observation.
        for (int x = 0; x < w; x++) {
            const double t = (x - (f - 1) / 2.0) / f;
            double v;
            if (t <= 0.0) v = delta[0];
            else if (t >= m - 1) v = delta[m - 1];
            else { const int j = static_cast<int>(t); const double a = t - j; v = (1 - a) * delta[j] + a * delta[j + 1]; }
            row[x] += v;
        }
    }
}

// Recursive edge-aware smoothing along rows (forward then backward); V(x) couples x and x+1.
// The three planes (cb, cr, weight) run together.
void recursiveRows(std::vector<double> *F, const std::vector<double> &V, int w, int h)
{
    for (int y = 0; y < h; y++) {
        const double *v = &V[static_cast<size_t>(y) * (w - 1)];
        double *a = &F[0][static_cast<size_t>(y) * w], *b = &F[1][static_cast<size_t>(y) * w], *c = &F[2][static_cast<size_t>(y) * w];
        for (int x = 1; x < w; x++) {
            const double k = v[x - 1];
            a[x] += k * (a[x - 1] - a[x]); b[x] += k * (b[x - 1] - b[x]); c[x] += k * (c[x - 1] - c[x]);
        }
        for (int x = w - 2; x >= 0; x--) {
            const double k = v[x];
            a[x] += k * (a[x + 1] - a[x]); b[x] += k * (b[x + 1] - b[x]); c[x] += k * (c[x + 1] - c[x]);
        }
    }
}

// Same down the columns; V(y, x) couples lines y and y+1.
void recursiveCols(std::vector<double> *F, int nCh, const std::vector<double> &V, int w, int h)
{
    for (int ch = 0; ch < nCh; ch++) {
        double *f = F[ch].data();
        for (int y = 1; y < h; y++) {
            double *r = f + static_cast<size_t>(y) * w; const double *q = r - w, *v = &V[static_cast<size_t>(y - 1) * w];
            for (int x = 0; x < w; x++) r[x] += v[x] * (q[x] - r[x]);
        }
        for (int y = h - 2; y >= 0; y--) {
            double *r = f + static_cast<size_t>(y) * w; const double *q = r + w, *v = &V[static_cast<size_t>(y) * w];
            for (int x = 0; x < w; x++) r[x] += v[x] * (q[x] - r[x]);
        }
    }
}

// Step 2.
void recolour(const std::vector<double> &Y, std::vector<double> &cb, std::vector<double> &cr,
              int w, int h, const Params &p)
{
    const size_t N = static_cast<size_t>(w) * h;
    std::vector<double> J;
    gaussian2d(Y, J, w, h, 0.8);

    // Reliability: exp(-(slope/rampScale)^2) of the colour's horizontal slope, the slope
    // taken on a 5-sample mean and averaged over 3 lines x 9 samples.
    std::vector<double> sb(N), sr(N), g(N), tmp(N), rel(N);
    for (int y = 0; y < h; y++) {
        boxLine(&cb[static_cast<size_t>(y) * w], &sb[static_cast<size_t>(y) * w], w, 5);
        boxLine(&cr[static_cast<size_t>(y) * w], &sr[static_cast<size_t>(y) * w], w, 5);
    }
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            const size_t i = static_cast<size_t>(y) * w + x;
            auto grad = [&](const std::vector<double> &s) {
                if (w < 2) return 0.0;
                if (x == 0) return s[i + 1] - s[i];
                if (x == w - 1) return s[i] - s[i - 1];
                return (s[i + 1] - s[i - 1]) / 2.0;
            };
            const double gb = grad(sb), gr = grad(sr);
            g[i] = std::sqrt(gb * gb + gr * gr);
        }
    for (int y = 0; y < h; y++) boxLine(&g[static_cast<size_t>(y) * w], &tmp[static_cast<size_t>(y) * w], w, 9);
    for (int x = 0; x < w; x++) boxLine(&tmp[x], &g[x], h, 3, w);
    for (size_t i = 0; i < N; i++) rel[i] = std::exp(-(g[i] / p.rampScale) * (g[i] / p.rampScale)) + 1e-4;

    // Domain distances.
    const double k = p.spatial / p.range;
    std::vector<double> Dx(static_cast<size_t>(h) * (w - 1)), Dy(static_cast<size_t>(h - 1) * w);
    for (int y = 0; y < h; y++)
        for (int x = 0; x + 1 < w; x++)
            Dx[static_cast<size_t>(y) * (w - 1) + x] = 1.0 + k * std::abs(J[static_cast<size_t>(y) * w + x + 1] - J[static_cast<size_t>(y) * w + x]);
    for (int y = 0; y + 1 < h; y++)
        for (int x = 0; x < w; x++)
            Dy[static_cast<size_t>(y) * w + x] = 1.0 + k * std::abs(J[static_cast<size_t>(y + 1) * w + x] - J[static_cast<size_t>(y) * w + x]);

    std::vector<double> F[3];
    F[0].resize(N); F[1].resize(N); F[2] = rel;
    for (size_t i = 0; i < N; i++) { F[0][i] = rel[i] * cb[i]; F[1][i] = rel[i] * cr[i]; }
    // Iteration i uses sigma_i = sigma_0 / 2^i, so its feedback a_i^D = (a_{i-1}^D)^2:
    // one exponential per coefficient, then squares.
    const int it = 3;
    const double s0 = p.spatial * std::sqrt(3.0) * std::pow(2.0, it - 1) / std::sqrt(std::pow(4.0, it) - 1.0);
    const double la = -std::sqrt(2.0) / s0;
    for (double &d : Dx) d = std::exp(la * d);
    for (double &d : Dy) d = std::exp(la * d);
    for (int i = 0; i < it; i++) {
        if (i > 0) { for (double &v : Dx) v *= v; for (double &v : Dy) v *= v; }
        recursiveRows(F, Dx, w, h);
        recursiveCols(F, 3, Dy, w, h);
    }
    for (size_t i = 0; i < N; i++) { cb[i] = F[0][i] / F[2][i]; cr[i] = F[1][i] / F[2][i]; }
}

// Step-profile geometry and selection (see collectSteps).
constexpr int kStepPre = 80;           // samples before the luma edge
constexpr int kStepPost = 80;          // samples after it
constexpr int kStepPlateau = 15;       // samples averaged for each colour plateau
constexpr double kStepLumaEdge = 0.05; // minimum luma step across 3 samples (0..1 scale)
constexpr double kStepLumaCalm = 0.025;// maximum luma std on each side of the edge
constexpr double kStepColour = 0.04;   // minimum colour change across the edge (Cb/Cr units)
constexpr int kResponseHalf = 45;      // response length 2 * 45 + 1 before trimming
constexpr double kResponseTrim = 0.133;// trimmed to the taps above this fraction of the peak

double sampleStd(const double *v, int n)
{
    double s = 0.0, s2 = 0.0;
    for (int i = 0; i < n; i++) { s += v[i]; s2 += v[i] * v[i]; }
    const double m = s / n;
    return std::sqrt(std::max(0.0, s2 / n - m * m));
}

double sampleMean(const double *v, int n)
{
    double s = 0.0;
    for (int i = 0; i < n; i++) s += v[i];
    return s / n;
}

} // namespace

void collectSteps(const std::vector<double> &Y, const std::vector<double> &c, int w, int h,
                  std::vector<float> &steps)
{
    const int length = kStepPre + kStepPost;
    std::vector<double> ys(w), g(w, 0.0);
    for (int y = 0; y < h; y++) {
        const double *lum = &Y[static_cast<size_t>(y) * w];
        const double *col = &c[static_cast<size_t>(y) * w];
        boxLine(lum, ys.data(), w, 3);
        for (int x = 1; x + 1 < w; x++) g[x] = std::abs(ys[x + 1] - ys[x - 1]) / 2.0;
        for (int x = kStepPre + 20; x < w - kStepPost - 20; x++) {
            // one point per edge: a local maximum of the luma gradient over the threshold
            if (g[x] <= kStepLumaEdge || g[x] < g[x - 1] || g[x] < g[x + 1]) continue;
            if (sampleStd(lum + x - kStepPre, kStepPre - 4) > kStepLumaCalm
                || sampleStd(lum + x + 5, kStepPost - 5) > kStepLumaCalm) continue;
            const double before = sampleMean(col + x - kStepPre, kStepPlateau);
            const double after = sampleMean(col + x + kStepPost - kStepPlateau, kStepPlateau);
            const double amplitude = after - before;
            if (std::abs(amplitude) < kStepColour) continue;
            for (int i = 0; i < length; i++)
                steps.push_back(static_cast<float>((col[x - kStepPre + i] - before) / amplitude));
        }
    }
}

std::vector<double> responseFromSteps(std::vector<float> &steps)
{
    const int length = kStepPre + kStepPost;
    const size_t count = steps.size() / length;
    if (count < 20) return {};
    // Median step response, sample by sample
    std::vector<double> s(length);
    std::vector<float> column(count);
    for (int i = 0; i < length; i++) {
        for (size_t k = 0; k < count; k++) column[k] = steps[k * length + i];
        const size_t mid = count / 2;
        std::nth_element(column.begin(), column.begin() + mid, column.end());
        double v = column[mid];
        if (count % 2 == 0) v = 0.5 * (v + *std::max_element(column.begin(), column.begin() + mid));
        s[i] = v;
    }
    // Light smoothing (Gaussian, sigma 1 sample), then the derivative: the response
    std::vector<double> smooth(length);
    for (int i = 0; i < length; i++) {
        double a = 0.0, ws = 0.0;
        for (int t = -4; t <= 4; t++) {
            const double wt = std::exp(-0.5 * t * t);
            a += wt * s[mirror(i + t, length)];
            ws += wt;
        }
        smooth[i] = a / ws;
    }
    std::vector<double> response(2 * kResponseHalf + 1);
    double sum = 0.0;
    for (int k = 0; k < static_cast<int>(response.size()); k++) {
        const int i = kStepPre - 1 - kResponseHalf + k;   // the edge lies between kStepPre - 1 and kStepPre
        response[k] = std::max(0.0, smooth[i + 1] - smooth[i]);
        sum += response[k];
    }
    if (!(sum > 0.0)) return {};
    // Trim symmetrically to the useful support
    const double peak = *std::max_element(response.begin(), response.end());
    int support = 3;
    for (int k = 0; k < static_cast<int>(response.size()); k++)
        if (response[k] > kResponseTrim * peak) support = std::max(support, std::abs(k - kResponseHalf));
    std::vector<double> trimmed(response.begin() + kResponseHalf - support, response.begin() + kResponseHalf + support + 1);
    sum = 0.0;
    for (double v : trimmed) sum += v;
    for (double &v : trimmed) v /= sum;
    return trimmed;
}

bool writeKernel(const std::string &path, const std::vector<double> &kernel, std::string &error)
{
    FILE *f = std::fopen(path.c_str(), "w");
    if (!f) { error = "cannot write " + path; return false; }
    for (double v : kernel) std::fprintf(f, "%.8f\n", v);
    std::fclose(f);
    return true;
}

std::vector<double> readKernel(const std::string &path, std::string &error)
{
    std::ifstream in(path);
    std::vector<double> h;
    if (!in) { error = "cannot open " + path; return {}; }
    double v;
    while (in >> v) h.push_back(v);
    if (h.size() < 3 || h.size() % 2 == 0) { error = path + ": need an odd number (>= 3) of taps"; return {}; }
    double s = 0.0;
    for (double x : h) s += x;
    if (!(s > 0.0)) { error = path + ": taps must sum to a positive value"; return {}; }
    for (double &x : h) x /= s;
    return h;
}

void reconstruct(const std::vector<double> &Y, std::vector<double> &cb, std::vector<double> &cr,
                 int w, int h, const Params &p)
{
    if (!p.enabled() || w < 8 || h < 2) return;
    deconvolve(Y, cb, w, h, p.kernelDb, p);
    deconvolve(Y, cr, w, h, p.kernelDr, p);
    recolour(Y, cb, cr, w, h, p);
}

} // namespace SecamBleed
