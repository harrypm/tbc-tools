/*
 * File:        testcompositevectorscope.cpp
 * Module:      tbc-analyse
 * Purpose:     Unit test for the composite (measurement) vectorscope
 *              acquisition engine (composite_vectorscope.cpp)
 *
 * Builds synthetic in-memory PAL / NTSC frames whose colour burst angle,
 * amplitude and V-switch state are known exactly, then checks what the
 * engine recovers: U/V samples, burst amplitude in IRE and percent, phase
 * jitter, PAL burst split, chroma/burst ratio, sample classification,
 * line phases, subsampling stride and the TBC-domain adaptation helpers.
 * Deterministic: no file I/O, no clock, no network.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "composite_vectorscope.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void check(bool condition, const std::string &what)
{
    if (!condition) {
        std::cerr << "FAIL: " << what << std::endl;
        ++g_failures;
    } else {
        std::cout << "ok: " << what << std::endl;
    }
}

void checkNear(double actual, double expected, double tolerance, const std::string &what)
{
    const bool pass = std::abs(actual - expected) <= tolerance;
    check(pass, what + " (actual " + std::to_string(actual) + ", expected "
                   + std::to_string(expected) + ")");
}

using tbc::cvbs::CompositeScopeParameters;
using tbc::cvbs::CompositeVectorscopeOptions;
using tbc::cvbs::VectorscopeData;
using tbc::cvbs::VectorscopeLinePhase;
using tbc::cvbs::VectorscopeMeasurements;
using tbc::cvbs::VectorscopeSampleClass;
using tbc::cvbs::VectorscopeSampleWindow;

// Synthesise one flat field-sequential CVBS_U10_4FSC frame in which every
// line carries a colour burst at a known angle relative to the engine's
// quadrature reference, and the active window carries a constant picture
// chroma vector.
//
// The chroma on a 4FSC line is placed exactly as the engine's product
// detector models it: x[i] = Y + U*sin(theta_i) + V*cos(theta_i), where
// theta_i is the reference phase at sample i (the subcarrier advance of the
// line start plus the sample's 4FSC phase step).  With the offset phi = 0
// the demodulator recovers (U, V) unrotated, so the burst lands on the
// angles the PAL / NTSC standards place it at.
struct SynthSpec {
    ::VideoSystem system;
    int32_t field_width;
    int32_t field1_height;
    int32_t field2_height;
    double burst_amplitude_ire;      // peak, measured blanking-to-white
    double picture_amplitude_ire;    // peak of the picture chroma vector
    bool pal_switched;               // alternate the burst angle per line
};

std::vector<int16_t> synthesise_frame(const SynthSpec &spec,
                                      const CompositeScopeParameters &parameters)
{
    const int32_t frame_lines = parameters.frame_lines();
    const double level_range = static_cast<double>(parameters.white_level
                                                    - parameters.blanking_level);
    const double cycles_per_line = tbc::cvbs::subcarrier_cycles_per_line(spec.system);
    const double burst_amplitude = spec.burst_amplitude_ire * level_range / 100.0;
    const double picture_amplitude = spec.picture_amplitude_ire * level_range / 100.0;
    const double swing = spec.pal_switched ? (M_PI / 4.0) : 0.0;

    std::vector<int16_t> frame(static_cast<size_t>(frame_lines) * parameters.field_width,
                               static_cast<int16_t>(parameters.blanking_level));
    for (int32_t line = 0; line < frame_lines; ++line) {
        // PAL swings the burst about the -U axis on alternate lines within a
        // field (ITU-R BT.470-6 Table 2 item 2.16); NTSC sits on the -U axis
        // (SMPTE 170M-2004 section 8.4).  The first burst line of each field
        // is swung +45 degrees: the reference unwrapping initialises its
        // running angle at (first angle - swing), so starting on the +swing
        // side lands that init on the -U axis itself and the unwrap is exact
        // from the first line.
        const int32_t field_line = (line < parameters.field1_height)
                                       ? line : (line - parameters.field1_height);
        const double burst_angle = M_PI + (((field_line % 2) == 0) ? swing : -swing);
        const double burst_u = burst_amplitude * std::cos(burst_angle);
        const double burst_v = burst_amplitude * std::sin(burst_angle);

        // Reference phase at the line start, as the engine computes it.
        const double base = cycles_per_line * static_cast<double>(line);
        const double frac = base - std::floor(base);

        int16_t *line_data = frame.data() + static_cast<size_t>(line) * parameters.field_width;
        for (int32_t i = 0; i < parameters.field_width; ++i) {
            const double theta = 2.0 * M_PI * (frac + (0.25 * (i & 3)));
            const double sine = std::sin(theta);
            const double cosine = std::cos(theta);

            double chroma_u = 0.0;
            double chroma_v = 0.0;
            if (i >= parameters.burst_start && i < parameters.burst_end) {
                chroma_u = burst_u;
                chroma_v = burst_v;
            } else if (i >= parameters.active_video_start && i < parameters.active_video_end) {
                // Constant picture chroma vector along the U axis.
                chroma_u = picture_amplitude;
                chroma_v = 0.0;
            }

            const double sample = static_cast<double>(parameters.blanking_level)
                                  + (chroma_u * sine) + (chroma_v * cosine);
            line_data[i] = static_cast<int16_t>(std::lround(sample));
        }
    }
    return frame;
}

void test_pal_nominal_burst()
{
    const SynthSpec spec{PAL, 1135, 313, 313, 21.4285714286, 42.8571428571, true};

    CompositeScopeParameters parameters;
    parameters.system = spec.system;
    parameters.field_width = spec.field_width;
    parameters.field1_height = spec.field1_height;
    parameters.field2_height = spec.field2_height;
    parameters.white_level = tbc::cvbs::cvbs_white(spec.system);
    parameters.blanking_level = tbc::cvbs::cvbs_blanking(spec.system);
    const std::pair<int32_t, int32_t> burst_range = tbc::cvbs::colour_burst_range(spec.system);
    parameters.burst_start = burst_range.first;
    parameters.burst_end = burst_range.second;
    parameters.active_video_start = tbc::cvbs::kPalActiveVideoStart;
    parameters.active_video_end = tbc::cvbs::kPalActiveVideoEnd;
    parameters.first_active_frame_line = tbc::cvbs::kPalFirstActiveFrameLine;
    parameters.last_active_frame_line = tbc::cvbs::kPalLastActiveFrameLine;

    const std::vector<int16_t> frame = synthesise_frame(spec, parameters);

    CompositeVectorscopeOptions options;
    options.window = VectorscopeSampleWindow::WholeLine;
    const std::optional<VectorscopeData> result = tbc::cvbs::extract_composite_vectorscope(
        frame.data(), frame.size(), parameters, 1, options);
    check(result.has_value(), "PAL: acquisition returns data");

    // Vertical averaging is display-only and phase-matched: on bars that do
    // not change down the frame it must leave every vector where it was (a
    // PAL average that mixed +V and -V lines would collapse V towards zero)
    // and must not touch the measurements.
    {
        CompositeVectorscopeOptions averaged_options = options;
        averaged_options.vertical_average = true;
        const std::optional<VectorscopeData> averaged = tbc::cvbs::extract_composite_vectorscope(
            frame.data(), frame.size(), parameters, 1, averaged_options);
        check(averaged.has_value() && averaged->samples.size() == result->samples.size(),
              "PAL: line averaging returns the same samples");
        double worst = 0.0;
        if (averaged.has_value() && averaged->samples.size() == result->samples.size()) {
            for (size_t i = 0; i < result->samples.size(); ++i) {
                if (result->samples[i].sample_class != tbc::cvbs::VectorscopeSampleClass::Picture) {
                    continue;
                }
                worst = std::max(worst, std::hypot(averaged->samples[i].u - result->samples[i].u,
                                                   averaged->samples[i].v - result->samples[i].v));
            }
            check(averaged->measurements.burst_phase_jitter_degrees
                          == result->measurements.burst_phase_jitter_degrees
                      && averaged->measurements.chroma_to_burst_ratio
                             == result->measurements.chroma_to_burst_ratio,
                  "PAL: line averaging leaves the measurements unchanged");
        }
        // 0.5% of the display range.
        check(worst < 164.0, "PAL: line averaging keeps static vectors in place (worst "
                                 + std::to_string(worst) + ")");
    }

    const VectorscopeMeasurements &m = result->measurements;
    check(m.valid, "PAL: burst reference recovered");
    checkNear(m.burst_amplitude_ire, 21.43, 0.5, "PAL: burst amplitude in IRE");
    checkNear(m.burst_amplitude_percent, 100.0, 2.0, "PAL: burst amplitude percent of nominal");
    check(m.burst_phase_jitter_degrees < 0.5, "PAL: burst phase jitter near zero (actual "
              + std::to_string(m.burst_phase_jitter_degrees) + " deg)");
    checkNear(m.burst_phase_split_error_degrees, 0.0, 0.5, "PAL: V-switch burst split error");
    // Picture chroma was synthesised at exactly twice the burst amplitude.
    checkNear(m.chroma_to_burst_ratio, 2.0, 0.1, "PAL: chroma/burst ratio");
    check(m.burst_line_count > 500, "PAL: burst lines contribute to the reference");

    // Whole-line PAL frame: 626 lines x 1135 samples = 709,610 < the 800,000
    // ceiling, so every sample is taken.
    check(result->sample_stride == 1, "PAL: whole frame under the sample ceiling (no stride)");
    check(result->samples.size() == static_cast<size_t>(626) * 1135,
          "PAL: every sample of every line emitted");

    // Sample classification: the burst window is 40 samples per line.
    size_t burst_samples = 0;
    size_t picture_samples = 0;
    size_t v_positive = 0;
    size_t v_negative = 0;
    for (const tbc::cvbs::UVSample &sample : result->samples) {
        if (sample.sample_class == VectorscopeSampleClass::Burst) {
            ++burst_samples;
        } else if (sample.sample_class == VectorscopeSampleClass::Picture) {
            ++picture_samples;
        }
        if (sample.line_phase == VectorscopeLinePhase::VPositive) {
            ++v_positive;
        } else if (sample.line_phase == VectorscopeLinePhase::VNegative) {
            ++v_negative;
        }
    }
    check(burst_samples == static_cast<size_t>(626) * 40, "PAL: burst window samples classified");
    check(picture_samples > 0, "PAL: active picture samples classified");
    check(v_positive > 0 && v_negative > 0, "PAL: both V-switch line phases classified");
    // 313-line fields hold 157 even-index and 156 odd-index lines, so the
    // frame classifies 314 lines on the -swing side and 312 on the +swing -
    // and with the whole line emitted at stride 1, every sample of a line
    // carries its line's phase.
    check(v_positive == static_cast<size_t>(312) * 1135
              && v_negative == static_cast<size_t>(314) * 1135,
          "PAL: V-switch phases classified per line parity");
}

void test_pal_selected_lines_and_stride()
{
    const SynthSpec spec{PAL, 1135, 313, 313, 21.4285714286, 42.8571428571, true};

    CompositeScopeParameters parameters;
    parameters.system = spec.system;
    parameters.field_width = spec.field_width;
    parameters.field1_height = spec.field1_height;
    parameters.field2_height = spec.field2_height;
    parameters.white_level = tbc::cvbs::cvbs_white(spec.system);
    parameters.blanking_level = tbc::cvbs::cvbs_blanking(spec.system);
    const std::pair<int32_t, int32_t> burst_range = tbc::cvbs::colour_burst_range(spec.system);
    parameters.burst_start = burst_range.first;
    parameters.burst_end = burst_range.second;
    parameters.active_video_start = tbc::cvbs::kPalActiveVideoStart;
    parameters.active_video_end = tbc::cvbs::kPalActiveVideoEnd;
    parameters.first_active_frame_line = tbc::cvbs::kPalFirstActiveFrameLine;
    parameters.last_active_frame_line = tbc::cvbs::kPalLastActiveFrameLine;

    const std::vector<int16_t> frame = synthesise_frame(spec, parameters);

    // Selected lines: interlaced frame lines 100..199 (0-based inclusive),
    // reported back in the same numbering.
    CompositeVectorscopeOptions options;
    options.window = VectorscopeSampleWindow::ActiveLine;
    options.active_lines_only = false;
    options.first_line = 100;
    options.last_line = 199;
    const std::optional<VectorscopeData> result = tbc::cvbs::extract_composite_vectorscope(
        frame.data(), frame.size(), parameters, 1, options);
    check(result.has_value(), "PAL: selected-lines acquisition returns data");
    check(result->first_line == 100 && result->last_line == 199,
          "PAL: selected line range reported in interlaced numbering");
    check(result->samples.size() == static_cast<size_t>(100) * (1105 - 157),
          "PAL: selected lines emit only the active-line window");

    // Sample ceiling: force subsampling and check the reported stride.
    CompositeVectorscopeOptions ceiling_options;
    ceiling_options.window = VectorscopeSampleWindow::WholeLine;
    ceiling_options.max_samples = 100000;
    const std::optional<VectorscopeData> strided = tbc::cvbs::extract_composite_vectorscope(
        frame.data(), frame.size(), parameters, 1, ceiling_options);
    check(strided.has_value() && strided->sample_stride == 8,
          "PAL: sample ceiling forces a stride of 8");
}

void test_ntsc_nominal_burst()
{
    const SynthSpec spec{NTSC, 910, 263, 263, 20.0, 40.0, false};

    CompositeScopeParameters parameters;
    parameters.system = spec.system;
    parameters.field_width = spec.field_width;
    parameters.field1_height = spec.field1_height;
    parameters.field2_height = spec.field2_height;
    parameters.white_level = tbc::cvbs::cvbs_white(spec.system);
    parameters.blanking_level = tbc::cvbs::cvbs_blanking(spec.system);
    const std::pair<int32_t, int32_t> burst_range = tbc::cvbs::colour_burst_range(spec.system);
    parameters.burst_start = burst_range.first;
    parameters.burst_end = burst_range.second;
    parameters.active_video_start = tbc::cvbs::kNtscActiveVideoStart;
    parameters.active_video_end = tbc::cvbs::kNtscActiveVideoEnd;
    parameters.first_active_frame_line = tbc::cvbs::kNtscFirstActiveFrameLine;
    parameters.last_active_frame_line = tbc::cvbs::kNtscLastActiveFrameLine;

    const std::vector<int16_t> frame = synthesise_frame(spec, parameters);

    CompositeVectorscopeOptions options;
    options.window = VectorscopeSampleWindow::WholeLine;
    const std::optional<VectorscopeData> result = tbc::cvbs::extract_composite_vectorscope(
        frame.data(), frame.size(), parameters, 1, options);
    check(result.has_value(), "NTSC: acquisition returns data");

    // Vertical averaging is display-only and phase-matched: on bars that do
    // not change down the frame it must leave every vector where it was (a
    // PAL average that mixed +V and -V lines would collapse V towards zero)
    // and must not touch the measurements.
    {
        CompositeVectorscopeOptions averaged_options = options;
        averaged_options.vertical_average = true;
        const std::optional<VectorscopeData> averaged = tbc::cvbs::extract_composite_vectorscope(
            frame.data(), frame.size(), parameters, 1, averaged_options);
        check(averaged.has_value() && averaged->samples.size() == result->samples.size(),
              "NTSC: line averaging returns the same samples");
        double worst = 0.0;
        if (averaged.has_value() && averaged->samples.size() == result->samples.size()) {
            for (size_t i = 0; i < result->samples.size(); ++i) {
                if (result->samples[i].sample_class != tbc::cvbs::VectorscopeSampleClass::Picture) {
                    continue;
                }
                worst = std::max(worst, std::hypot(averaged->samples[i].u - result->samples[i].u,
                                                   averaged->samples[i].v - result->samples[i].v));
            }
            check(averaged->measurements.burst_phase_jitter_degrees
                          == result->measurements.burst_phase_jitter_degrees
                      && averaged->measurements.chroma_to_burst_ratio
                             == result->measurements.chroma_to_burst_ratio,
                  "NTSC: line averaging leaves the measurements unchanged");
        }
        // 0.5% of the display range.
        check(worst < 164.0, "NTSC: line averaging keeps static vectors in place (worst "
                                 + std::to_string(worst) + ")");
    }

    const VectorscopeMeasurements &m = result->measurements;
    check(m.valid, "NTSC: burst reference recovered");
    checkNear(m.burst_amplitude_ire, 20.0, 0.5, "NTSC: burst amplitude in IRE");
    checkNear(m.burst_amplitude_percent, 100.0, 2.0, "NTSC: burst amplitude percent of nominal");
    check(m.burst_phase_jitter_degrees < 0.5, "NTSC: burst phase jitter near zero");
    checkNear(m.chroma_to_burst_ratio, 2.0, 0.1, "NTSC: chroma/burst ratio");

    // NTSC carries no V-switch: no line is classified.
    size_t phased = 0;
    for (const tbc::cvbs::UVSample &sample : result->samples) {
        if (sample.line_phase != VectorscopeLinePhase::NotApplicable) {
            ++phased;
        }
    }
    check(phased == 0, "NTSC: no V-switch line phases classified");
}

void test_secam_rejected()
{
    CompositeScopeParameters parameters;
    parameters.system = SECAM;
    parameters.field_width = 1135;
    parameters.field1_height = 313;
    parameters.field2_height = 313;
    parameters.white_level = tbc::cvbs::cvbs_white(SECAM);
    parameters.blanking_level = tbc::cvbs::cvbs_blanking(SECAM);
    parameters.burst_start = tbc::cvbs::kPalColourBurstStart;
    parameters.burst_end = tbc::cvbs::kPalColourBurstEnd;

    const std::vector<int16_t> frame(static_cast<size_t>(626) * 1135, 256);
    const std::optional<VectorscopeData> result = tbc::cvbs::extract_composite_vectorscope(
        frame.data(), frame.size(), parameters, 1, CompositeVectorscopeOptions{});
    check(!result.has_value(), "SECAM: engine rejects FM-chroma systems");
}

void test_parameter_resolution_and_assembly()
{
    // Metadata with no values set (-1) falls back to the PAL system
    // constants.
    TbcMetaData::VideoParameters video_parameters;
    video_parameters.system = PAL;
    video_parameters.numberOfSequentialFields = 2;
    video_parameters.colourBurstStart = -1;
    video_parameters.colourBurstEnd = -1;
    video_parameters.activeVideoStart = -1;
    video_parameters.activeVideoEnd = -1;
    video_parameters.white16bIre = -1;
    video_parameters.black16bIre = -1;
    video_parameters.blanking16bIre = -1;
    video_parameters.fieldWidth = -1;
    video_parameters.fieldHeight = -1;
    video_parameters.firstActiveFrameLine = -1;
    video_parameters.lastActiveFrameLine = -1;

    const CompositeScopeParameters parameters = tbc::cvbs::make_composite_scope_parameters(
        video_parameters, 0, 0);
    check(parameters.field_width == tbc::cvbs::kPalSamplesPerLineNominal,
          "Params: field width falls back to the PAL nominal");
    check(parameters.frame_lines() == 626, "Params: field heights fall back to PAL 313 + 313");
    check(parameters.burst_start == tbc::cvbs::kPalColourBurstStart
              && parameters.burst_end == tbc::cvbs::kPalColourBurstEnd,
          "Params: burst window falls back to the PAL constant");
    check(parameters.first_active_frame_line == tbc::cvbs::kPalFirstActiveFrameLine
              && parameters.last_active_frame_line == tbc::cvbs::kPalLastActiveFrameLine,
          "Params: active frame lines fall back to the PAL constants");
    check(parameters.white_level == tbc::cvbs::kPalWhite
              && parameters.blanking_level == tbc::cvbs::kPalBlanking,
          "Params: levels are the normative PAL U10 domain");

    // Metadata values are trusted where present.
    TbcMetaData::VideoParameters pal_metadata = video_parameters;
    pal_metadata.colourBurstStart = 100;
    pal_metadata.colourBurstEnd = 140;
    pal_metadata.fieldWidth = 1135;
    const CompositeScopeParameters trusted = tbc::cvbs::make_composite_scope_parameters(
        pal_metadata, 313, 313);
    check(trusted.burst_start == 100 && trusted.burst_end == 140,
          "Params: metadata burst window is trusted");

    // TBC 16-bit -> U10 assembly: PAL blanking 16384 -> 256, white 54016 ->
    // 844, and the degenerate-level case is refused.
    TbcMetaData::VideoParameters levels = video_parameters;
    levels.white16bIre = 54016;
    levels.black16bIre = 16384;
    const std::vector<uint16_t> field(4, 16384);
    const std::vector<uint16_t> second_field(4, 54016);
    const std::vector<int16_t> assembled = tbc::cvbs::assemble_composite_frame(
        field, second_field, levels);
    check(assembled.size() == 8, "Assembly: both fields concatenated");
    check(assembled[0] == 256, "Assembly: PAL blanking maps to U10 256");
    check(assembled[4] == 844, "Assembly: PAL white maps to U10 844");

    TbcMetaData::VideoParameters degenerate = levels;
    degenerate.white16bIre = 16384;
    const std::vector<int16_t> refused = tbc::cvbs::assemble_composite_frame(
        field, second_field, degenerate);
    check(refused.empty(), "Assembly: degenerate levels are refused");
}

}  // namespace

int main()
{
    test_pal_nominal_burst();
    test_pal_selected_lines_and_stride();
    test_ntsc_nominal_burst();
    test_secam_rejected();
    test_parameter_resolution_and_assembly();

    if (g_failures == 0) {
        std::cout << "testcompositevectorscope: all checks passed" << std::endl;
        return 0;
    }
    std::cerr << "testcompositevectorscope: " << g_failures << " check(s) failed" << std::endl;
    return 1;
}
