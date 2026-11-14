/************************************************************************

    secambleed.h

    tbc-chroma-decoder - Colourisation filter for the tbc format.

    Copyright (C) 2026 Benoit Vuillemin

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

#ifndef SECAMBLEED_H
#define SECAMBLEED_H

#include <string>
#include <vector>

// Colour-bleed reconstruction for SECAM colour-under recordings.
//
// A tape copy smears each colour difference horizontally with a response h
// (measured per tape and per component from the colour ramps at clean luma
// edges; it may be long and asymmetric). The luma is sharp. Reconstruction:
//
//  1. Per line, at the colour's own resolution (1/3 of the samples), find c
//     minimising |h*c - o|^2 + lambda*sum w_i (c_i+1 - c_i)^2 + mu*|c - o_s|^2,
//     w_i = exp(-(dY_i/sigma)^2): the colour may only step where the luma
//     steps. The correction c - o is brought back to full width linearly.
//  2. Over the frame, every point takes the colour of its own luma region:
//     a 2D geodesic mean (domain-transform recursive filter, Gastal &
//     Oliveira 2011) that does not cross luma edges, weighted by how stable
//     the colour is at each source point (a point inside a colour ramp is
//     contaminated by bleed; one in a colour plateau is correct).
//
// The response h is measured on the tape itself, without any reference: at a
// sharp luma edge with calm luma on both sides and a real colour change, the
// true colour steps exactly at the luma edge, so the decoded colour profile
// across it is the step response of the colour chain, and its derivative is
// h (collectSteps + responseFromSteps, median over many edges).
//
// Units: luma 0..1 (black..white); colour differences as normalised Cb / Cr
// (+-0.5 full scale). Lengths in samples / frame lines.
namespace SecamBleed {

struct Params {
    std::vector<double> kernelDb;   // measured response for D'B (odd length, centred, sum 1)
    std::vector<double> kernelDr;   // measured response for D'R
    double lambda = 1.0;            // step 1: smoothness weight
    double sigma = 0.04;            // step 1: luma step scale of the smoothness weights
    double mu = 0.02;               // step 1: pull toward the (lightly smoothed) observation
    int decimation = 3;             // step 1: colour resolution factor
    double spatial = 60.0;          // step 2: geodesic reach (samples)
    double range = 0.06;            // step 2: luma step that stops the reach
    double rampScale = 0.003;       // step 2: colour slope (per sample) of an unreliable point
    double maxChange = 0.02;        // largest change of a sample's colour (trust region; 0 = none)

    bool enabled() const { return !kernelDb.empty() && !kernelDr.empty(); }
};

// Appends to `steps` the normalised colour step profiles (pre + post samples
// each, 0 before the luma edge, 1 after; each end level is the mean of
// `plateau` samples) found at the clean luma edges of a w x h frame.
// c: one colour difference. responseFromSteps expects the defaults.
void collectSteps(const std::vector<double> &Y, const std::vector<double> &c, int w, int h,
                  std::vector<float> &steps, int pre = 80, int post = 80, int plateau = 15);

// Ratio of the extra LF shelf (1 + jf/(ratio*break)) / (1 + jf/break) that
// best turns the median of the step profiles (collected without the shelf,
// with the given pre / post) into a clean step: no overshoot, no lead before
// the luma edge, settled on both sides. 0 if there are too few profiles.
// Reorders `steps`.
double lfShelfRatio(std::vector<float> &steps, int pre, int post, double sampleRate, double breakHz);

// Median of the step profiles -> response (derivative, clipped at 0, trimmed
// to its useful support, sum 1). Empty if there are too few profiles.
// Reorders `steps`.
std::vector<double> responseFromSteps(std::vector<float> &steps);

// Writes a response as one tap per line (the format readKernel reads).
bool writeKernel(const std::string &path, const std::vector<double> &kernel, std::string &error);

// Reads a response: whitespace-separated numbers, odd count, normalised to sum 1.
// Returns an empty vector (and sets error) on failure.
std::vector<double> readKernel(const std::string &path, std::string &error);

// In place on a w x h frame (row-major). Y: luma; cb, cr: colour differences.
void reconstruct(const std::vector<double> &Y, std::vector<double> &cb, std::vector<double> &cr,
                 int w, int h, const Params &p);

}

#endif
