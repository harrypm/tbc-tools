/************************************************************************

    secamdecoder.cpp

    tbc-chroma-decoder - Colourisation filter for the tbc format.

    Copyright (C) 2026 Harry Munday

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

#include "secamdecoder.h"

#include "decoderpool.h"

#include <algorithm>
#include <cmath>
#include <cstdio> // TEMP diagnostic
#include <atomic> // TEMP diagnostic

// The demodulation pipeline, its parameters and its comments are ported from
// libchromadec v0.1.0 (the SECAM decoder vapoursynth-analog uses),
// src/decoders/secam/secam_decoder.cpp -- the reference the user asked to
// copy. tbc-tools-specific stages (colour killer, V-interval neutralization,
// one-line hold to a full U/V raster, hue rotation, OutputWriter scaling,
// diagnostics) are carried over from the previous tbc-tools decoder; see the
// pre-port restore point in development-logs/ for that version.

namespace {

// BT.1700 Part C Table 4 (identical in BT.470-6 Table 2).
constexpr double kNominalFob = 4250000.0;   // Db undeviated subcarrier, 272*fH
constexpr double kNominalFor = 4406250.0;   // Dr undeviated subcarrier, 282*fH
constexpr double kDeviationDb = 230000.0;   // nominal deviation for D'B = 1
constexpr double kDeviationDr = 280000.0;   // nominal deviation for D'R = 1
constexpr double kBellCentre = 4286000.0;   // HF pre-correction f0
constexpr double kLfBreak = 85000.0;        // LF pre-correction f1
constexpr double kDbScale = 1.505;          // D'B = +1.505 (E'B - E'Y)
constexpr double kDrScale = -1.902;         // D'R = -1.902 (E'R - E'Y)

// Chroma band limits (Hz): flat over the FM block's nominal occupancy,
// raised-cosine transitions. Start point is the JVC BPF-1 takeoff curve
// (3.38-5.25 MHz, -3 dB at 3.9/4.75). This is the SPLIT-CHROMA input band
// (libchromadec's): it assumes the TBC carries no luma, so nothing out-of-
// block can leak. A COMBINED luma+chroma input needs the narrow band below
// instead (see kCombinedBandFlatHz).
constexpr double kBandStopLo = 2900000.0;
constexpr double kBandPassLo = 3500000.0;
constexpr double kBandPassHi = 5100000.0;
constexpr double kBandStopHi = 5700000.0;

// Combined-input band: flat over the legal +-428 kHz subcarrier excursion
// (BT.470) with a small margin, raised-cosine transitions out to +-580 kHz,
// centred on the FM block centre (the pre-port decoder's 8c7d00e tuning:
// 600 kHz let sync/V-interval noise through as coloured garbage, 450 kHz
// cut the combined-source luma dots with the block still covered; the
// libchromadec wide band on a combined source lets luma HF dominate the
// discriminator wherever the restored carrier is weak). Out-of-block luma
// cannot be removed by any filter -- luma INSIDE the band (3.9-4.756 MHz)
// is spectrally identical to chroma -- but the narrow band removes the rest.
constexpr double kCombinedBandFlatHz = 450000.0;
constexpr double kCombinedBandStopHz = 580000.0;

double bandGain(double f, double stopLo, double passLo, double passHi, double stopHi)
{
    const double af = std::abs(f);
    if (af <= stopLo || af >= stopHi) return 0.0;
    if (af < passLo) {
        const double t = (af - stopLo) / (passLo - stopLo);
        return 0.5 - 0.5 * std::cos(M_PI * t);
    }
    if (af > passHi) {
        const double t = (stopHi - af) / (stopHi - passHi);
        return 0.5 - 0.5 * std::cos(M_PI * t);
    }
    return 1.0;
}

// The receiver's HF "bell" (cloche) network: the closed-form inverse of
// the encoder's HF pre-correction (Table 4 item 10d, the "anti-bell"),
// A_HFP(f) = (1 + j16F) / (1 + j1.26F), F = f/f0 - f0/f. First-order
// networks invert exactly; evaluated at true analog frequencies. `centre`
// is where the encoder's f0 sits in this capture: heterodyne converters
// (VHS colour-under) translate the whole FM block, anti-bell shaping
// included, so the bell must follow the measured carriers, not nominal.
std::complex<double> bellNetwork(double f, double centre)
{
    if (f <= 0.0) return {0.0, 0.0};
    const double F = f / centre - centre / f;
    const std::complex<double> num(1.0, 1.26 * F);
    const std::complex<double> den(1.0, 16.0 * F);
    return num / den;
}

// Exact inverse of the LF pre-correction (Table 4 item 8):
// A_LFP(f) = (1 + jf/f1) / (1 + jf/3f1)  ->  inverse swaps the factors.
// Hermitian by construction when evaluated at signed frequencies.
std::complex<double> inverseLf(double f)
{
    const std::complex<double> num(1.0, f / (3.0 * kLfBreak));
    const std::complex<double> den(1.0, f / kLfBreak);
    return num / den;
}

// Minimum valid rows for a carrier-pair measurement, and the acceptable
// separation between the fitted rest-carrier cluster medians (Hz): the
// nominal pair is 156.25 kHz apart, so anything much tighter is one smeared
// cluster and anything much wider is noise/bottle carriers, not a pair.
constexpr qint32 kCalibrationMinRows = 8;
constexpr double kCalibrationMinSeparationHz = 90000.0;
constexpr double kCalibrationMaxSeparationHz = 230000.0;

// Recentre the band/bell masks when the measured block centre sits this far
// from the centre the masks were built for (Hz).
constexpr double kRecentreThresholdHz = 10000.0;

// Carrier-pair sanity gate: a real converter/colour-under offset is small
// next to the carrier spacing (ME-SECAM LO arithmetic lands ~+105 kHz; the
// block centre never moves more than about one full deviation, 350 kHz).
// A "measured" pair further from nominal than that is not a measurement --
// the etienne fixture's second field has no usable porch carrier at all, and
// its noise-only rows cluster ~1.6 MHz low while happening to pass the
// separation gate. (The pre-port decoder carried the same idea as its
// 200 kHz MAX_REF_DEV_HZ clamp; libchromadec has no equivalent because its
// sources always measure a real porch pair.)
constexpr double kCalibrationMaxCarrierOffsetHz = 350000.0;

// Maximum frequency deviation per unit of colour difference, in D'B/D'R
// units (the transmitter clips the pre-corrected signal to these, so
// demodulated excursions beyond them are never legitimate signal).
constexpr double kDevDbLo = -350000.0 / kDeviationDb;
constexpr double kDevDbHi =  506000.0 / kDeviationDb;
constexpr double kDevDrLo = -506000.0 / kDeviationDr;
constexpr double kDevDrHi =  350000.0 / kDeviationDr;

// Colour killer: below this fraction of the field's median carrier envelope
// there is no carrier to read a frequency from, so the reading is noise.
constexpr double kColourKillerRatio = 0.25;

// OutputWriter's RGB48 path (outputwriter.cpp) converts Y'UV with the fixed
// matrix R = Y + 1.139883*V, G = Y - 0.394642*U - 0.580622*V,
// B = Y + 2.032062*U -- the standard PAL/NTSC matrix, where U/V are the
// *normalized* (B-Y)/(R-Y). SECAM's D'R/D'B demodulate straight to (R'-Y')
// and (B'-Y'), so storing them pre-divided by these same constants makes
// the existing matrix reproduce the correct SECAM inverse (R = Y+Dr, B = Y+Db,
// G = Y-(kr.Dr+kb.Db)/kg) without touching OutputWriter at all -- verified by
// direct substitution.
constexpr double DR_TO_V_SCALE = 1.139883;
constexpr double DB_TO_U_SCALE = 2.032062;

/// Median of the values in [first, last), reordering them in the process
double medianInPlace(std::vector<double> &values)
{
    if (values.empty()) return 0.0;
    const size_t mid = values.size() / 2;
    std::nth_element(values.begin(), values.begin() + mid, values.end());
    return values[mid];
}

/// Bidirectional nearest-row fill (one-line hold): rows not marked valid in
/// `has` are replaced by the average of the nearest valid rows above and
/// below, or by whichever side exists.
void fillChannel(std::vector<double> &buffer, const std::vector<bool> &has,
                 qint32 fieldHeight, qint32 fieldWidth)
{
    std::vector<qint32> prevValid(fieldHeight, -1);
    qint32 last = -1;
    for (qint32 row = 0; row < fieldHeight; row++) {
        if (has[row]) last = row;
        prevValid[row] = last;
    }

    std::vector<qint32> nextValid(fieldHeight, -1);
    last = -1;
    for (qint32 row = fieldHeight - 1; row >= 0; row--) {
        if (has[row]) last = row;
        nextValid[row] = last;
    }

    for (qint32 row = 0; row < fieldHeight; row++) {
        if (has[row]) continue;

        double *dst = &buffer[row * fieldWidth];
        const qint32 p = prevValid[row];
        const qint32 n = nextValid[row];

        if (p >= 0 && n >= 0) {
            const double *pRow = &buffer[p * fieldWidth];
            const double *nRow = &buffer[n * fieldWidth];
            for (qint32 x = 0; x < fieldWidth; x++) dst[x] = 0.5 * (pRow[x] + nRow[x]);
        } else if (p >= 0) {
            const double *pRow = &buffer[p * fieldWidth];
            for (qint32 x = 0; x < fieldWidth; x++) dst[x] = pRow[x];
        } else if (n >= 0) {
            const double *nRow = &buffer[n * fieldWidth];
            for (qint32 x = 0; x < fieldWidth; x++) dst[x] = nRow[x];
        } else {
            for (qint32 x = 0; x < fieldWidth; x++) dst[x] = 0.0;
        }
    }
}

} // namespace

void SecamDecoder::FieldWork::init(qint32 fftBlockSize, qint32 fieldHeight, qint32 fieldWidth)
{
    const size_t size = static_cast<size_t>(fieldHeight) * fieldWidth;
    fftIn = fftw_alloc_complex(fftBlockSize);
    fftFreq = fftw_alloc_complex(fftBlockSize);
    fftWork = fftw_alloc_complex(fftBlockSize);
    fftOut = fftw_alloc_complex(fftBlockSize);
    analytic.assign(size, {0.0, 0.0});
    analyticRef.assign(size, {0.0, 0.0});
    fInst.assign(size, 0.0);
    fInstRef.assign(size, 0.0);
    demod.assign(size, 0.0);
    deemph.assign(size, 0.0);
    lumaFiltered.assign(size, 0.0);
    dr.assign(size, 0.0);
    db.assign(size, 0.0);
    lineIsRed.assign(fieldHeight, false);
    maskChroma.assign(fftBlockSize, {0.0, 0.0});
    maskBand.assign(fftBlockSize, 0.0);
    maskLuma.assign(fftBlockSize, 0.0);
    porchFreq.assign(fieldHeight, 0.0);
    porchEnvelope.assign(fieldHeight, 0.0);
    rowValid.assign(fieldHeight, false);
    carrierOffset = 0.0;
    mixFrequency = 0.0;
    fob = kNominalFob;
    for_ = kNominalFor;
    calibrated = false;
    lastGoodFob = 0.0;
    lastGoodFor = 0.0;
    haveLastGoodCalibration = false;
    combinedInput = false;
    inputChecked = false;
}

SecamDecoder::FieldWork::~FieldWork()
{
    if (fftIn != nullptr) fftw_free(fftIn);
    if (fftFreq != nullptr) fftw_free(fftFreq);
    if (fftWork != nullptr) fftw_free(fftWork);
    if (fftOut != nullptr) fftw_free(fftOut);
}

SecamDecoder::SecamDecoder()
{
}

SecamDecoder::SecamDecoder(const SecamDecoder::SecamConfiguration &config)
{
    // Only the settings are taken here; DecoderPool calls configure() with the
    // real video parameters before any decoding happens.
    secamConfig = config;
    secamConfig.videoParameters = TbcMetaData::VideoParameters();
}

SecamDecoder::~SecamDecoder()
{
    freeFftwState();
}

void SecamDecoder::freeFftwState()
{
    if (forwardPlan != nullptr) { fftw_destroy_plan(forwardPlan); forwardPlan = nullptr; }
    if (inversePlan != nullptr) { fftw_destroy_plan(inversePlan); inversePlan = nullptr; }
    if (planScratchIn != nullptr) { fftw_free(planScratchIn); planScratchIn = nullptr; }
    if (planScratchOut != nullptr) { fftw_free(planScratchOut); planScratchOut = nullptr; }
    blockSize = 0;
    fftMargin = 0;
}

bool SecamDecoder::updateConfiguration(const TbcMetaData::VideoParameters &videoParameters,
                                       const SecamDecoder::SecamConfiguration &configuration)
{
    if (!configure(videoParameters)) return false;
    secamConfig.chromaGain = configuration.chromaGain;
    secamConfig.chromaPhase = configuration.chromaPhase;
    secamConfig.clickNrLevel = configuration.clickNrLevel;
    secamConfig.nominalFirstActiveFieldLine = configuration.nominalFirstActiveFieldLine;

    return true;
}

bool SecamDecoder::configure(const TbcMetaData::VideoParameters &videoParameters)
{
    // SECAM/ME-SECAM sources carry the PAL line structure (625-line), and
    // vhs-decode reports them as SECAM, MESECAM, or PAL. All three are valid
    // here -- the decoder works on any 625-line PAL-geometry source.
    if (videoParameters.system != PAL && videoParameters.system != PAL_M
        && videoParameters.system != SECAM && videoParameters.system != MESECAM) {
        qCritical() << "This decoder is for PAL-line (SECAM/MESECAM) video only";
        return false;
    }

    secamConfig.videoParameters = videoParameters;
    const qint32 width = videoParameters.fieldWidth;
    sampleRate = videoParameters.sampleRate;

    // Block geometry: power of two with at least 256 samples of context on
    // each side of a row (the longest filter memory here is the bell
    // network's ~16/(2*pi*f0) tail, well under a microsecond).
    freeFftwState();
    blockSize = 1;
    while (blockSize < width + 512) blockSize <<= 1;
    fftMargin = (blockSize - width) / 2;

    // Plans are created here, once, before any decoder thread exists; the
    // per-FieldWork buffers are passed to the thread-safe new-array execute
    // (fftw_execute_dft) instead of the plan's own arrays.
    planScratchIn = fftw_alloc_complex(blockSize);
    planScratchOut = fftw_alloc_complex(blockSize);
    forwardPlan = fftw_plan_dft_1d(blockSize, planScratchIn, planScratchOut,
                                   FFTW_FORWARD, FFTW_ESTIMATE);
    inversePlan = fftw_plan_dft_1d(blockSize, planScratchIn, planScratchOut,
                                   FFTW_BACKWARD, FFTW_ESTIMATE);

    // Exact LF pre-correction inverse, offset-independent: built once here
    // and shared read-only by all decoder threads.
    maskDeemphasis.assign(blockSize, {0.0, 0.0});
    for (qint32 k = 0; k < blockSize; k++) {
        const double f = (k <= blockSize / 2 ? k : k - blockSize) * sampleRate / blockSize;
        maskDeemphasis[k] = inverseLf(f);
    }

    // Differentiating FIR, applied after the analytic signal is mixed down
    // to near-DC: any differentiator's fractional gain error is multiplied
    // by the absolute frequency it operates at, so discriminating at the
    // carrier would leak carrier-scale error into the small deviations.
    // At baseband a maximally-flat central difference (error O(w^9)) is
    // exact to sub-Hz across the +-1 MHz the mixed-down FM block spans.
    constexpr qint32 K = 4;
    constexpr double kMaxFlat[K] = {4.0 / 5.0, -1.0 / 5.0, 4.0 / 105.0, -1.0 / 280.0};
    diffTaps.assign(2 * K + 1, 0.0);
    for (qint32 k = 1; k <= K; k++) {
        diffTaps[K - k] = kMaxFlat[k - 1];
        diffTaps[K + k] = -kMaxFlat[k - 1];
    }

    // Porch measurement window: the tail of the back porch, directly before
    // active video. The reference carrier runs across the whole porch, but
    // the early porch still carries the sweep from the previous line's
    // carrier switch, so measure where the carrier has had the longest to
    // settle (libchromadec). The tail stops 10 samples short of the declared
    // active start (libchromadec uses -5): on combined luma+chroma captures
    // the picture turn-on transient begins a few samples before
    // activeVideoStart (measured on the etienne fixture: edge ringing from
    // ~activeVideoStart-10 at an amplitude that swamps the weak rest
    // carrier), and a poisoned median reads a false carrier pair. Split
    // chroma TBCs carry carrier-level signal across the whole blanking, so
    // the shorter tail costs nothing there. When activeVideoStart has been
    // widened into the horizontal margin (tbc-analyse full-frame/hybrid
    // decode bounds), fall back to a back-porch window just past
    // colourBurstEnd -- the burst bounds describe the input signal and are
    // untouched by the widening -- and only then to libchromadec's
    // fraction-of-line last resort.
    if (videoParameters.activeVideoStart > 70 && videoParameters.activeVideoStart < width) {
        porchEnd = videoParameters.activeVideoStart - 10;
        porchStart = porchEnd - 60;
    } else if (videoParameters.colourBurstEnd > videoParameters.colourBurstStart
               && videoParameters.colourBurstEnd <= width) {
        const qint32 burstWidth = videoParameters.colourBurstEnd - videoParameters.colourBurstStart;
        porchStart = videoParameters.colourBurstStart;
        porchEnd = std::min(videoParameters.colourBurstEnd + burstWidth / 2, width);
    } else {
        porchStart = static_cast<qint32>(width * 0.085);
        porchEnd = static_cast<qint32>(width * 0.155);
    }
    if (porchStart < 0 || porchEnd <= porchStart || porchEnd > width || width < 2) {
        qCritical() << "SECAM decoder: no usable porch measurement window in these video parameters";
        return false;
    }

    // TEMP diagnostic: confirm window bounds vs blanking layout.
    std::fprintf(stderr, "SECAMCFG sampleRate=%.1f fieldWidth=%d colourBurstStart=%d colourBurstEnd=%d activeVideoStart=%d activeVideoEnd=%d firstActiveFieldLine=%d -> blockSize=%d margin=%d porchStart=%d porchEnd=%d (len=%d)\n",
                 videoParameters.sampleRate, videoParameters.fieldWidth,
                 videoParameters.colourBurstStart, videoParameters.colourBurstEnd,
                 videoParameters.activeVideoStart, videoParameters.activeVideoEnd,
                 videoParameters.firstActiveFieldLine, blockSize, fftMargin,
                 porchStart, porchEnd, porchEnd - porchStart);

    return true;
}

QThread *SecamDecoder::makeThread(QAtomicInt& abort, DecoderPool& decoderPool) {
    return new SecamThread(abort, decoderPool, secamConfig);
}

// (Re)build the carrier-relative frequency masks with the FM block centred
// at nominal + work.carrierOffset. Heterodyne converters translate the whole
// block (anti-bell shaping included), so the band, the bell network, and the
// discriminator's mix-down all follow the measured carrier pair.
void SecamDecoder::buildChromaMasks(FieldWork &work) const
{
    work.maskChroma.assign(blockSize, {0.0, 0.0});
    work.maskBand.assign(blockSize, 0.0);
    // Band limits: libchromadec's wide band for a split-chroma input; the
    // narrow block-centred band for a combined luma+chroma input (detected
    // in decodeField; see the constants above). Both are evaluated on the
    // offset-shifted frequency, so the band follows the measured block.
    double stopLo = kBandStopLo, passLo = kBandPassLo, passHi = kBandPassHi, stopHi = kBandStopHi;
    if (work.combinedInput) {
        const double centre = 0.5 * (kNominalFob + kNominalFor);
        stopLo = centre - kCombinedBandStopHz;
        passLo = centre - kCombinedBandFlatHz;
        passHi = centre + kCombinedBandFlatHz;
        stopHi = centre + kCombinedBandStopHz;
    }
    for (qint32 k = 0; k < blockSize; k++) {
        const double f = (k <= blockSize / 2 ? k : k - blockSize) * sampleRate / blockSize;
        // Analytic-signal factor: keep positive frequencies doubled, drop
        // negative ones (exact Hilbert via the block FFT).
        double analyticFactor = 0.0;
        if (k == 0 || k == blockSize / 2) analyticFactor = 1.0;
        else if (k < blockSize / 2)       analyticFactor = 2.0;
        const double fShifted = std::abs(f) - work.carrierOffset;
        work.maskChroma[k] = analyticFactor * bandGain(fShifted, stopLo, passLo, passHi, stopHi)
                             * bellNetwork(std::abs(f), kBellCentre + work.carrierOffset);
        // Combined-input luma path: a STEEP low-pass below the FM block.
        // A notch around the steady block cannot catch the transient
        // sidebands a colour edge sweeps across the band skirts (dot crawl
        // on colour transitions), and luma inside/above the block band is
        // unrecoverable in a CVBS source. Corner + steepness match the
        // separated-mode decode the user validated (3.2 MHz order-12 luma
        // LPF: pass to ~3.1 MHz, stopped by ~3.35 MHz), anchored on the
        // measured block centre + offset like the chroma masks. Real
        // symmetric gain: applied to the raw composite.
        {
            const double blockCentre = 0.5 * (kNominalFob + kNominalFor) + work.carrierOffset;
            const double lumaPass = blockCentre - 1230000.0;
            const double lumaStop = blockCentre - 980000.0;
            if (std::abs(f) <= lumaPass) work.maskLuma[k] = 1.0;
            else if (std::abs(f) < lumaStop) {
                const double t = (std::abs(f) - lumaPass) / (lumaStop - lumaPass);
                work.maskLuma[k] = 0.5 - 0.5 * std::cos(M_PI * t);
            } else {
                work.maskLuma[k] = 0.0;
            }
        }
        // Bell-free analytic band: its real part is the plain band-filtered
        // signal (the analytic factor restores the Hermitian half exactly),
        // and the complex signal is the calibration reference. Keeping the
        // bell out of this path matters: the bell network shapes noise and
        // switch transients asymmetrically around its centre, which biases
        // the discriminated porch medians and ties them to the mask state.
        work.maskBand[k] = analyticFactor * bandGain(fShifted, stopLo, passLo, passHi, stopHi);
    }
    work.mixFrequency = 0.5 * (kNominalFob + kNominalFor) + work.carrierOffset;
}

// FFT one row into the bell+band and bell-free mixed-down analytic signals.
// Each TBC line's carrier phase is independent of its neighbours', but the
// rows are contiguous in time, so the block window reads context samples
// from the adjacent rows (clamped at the field edges) and the mix-down
// phasor is phase-continuous across the whole field via the global sample
// index -- the discriminator then sees a continuous phase track.
void SecamDecoder::filterRowAnalytic(const quint16 *fieldData, qint32 numSamples, qint32 row,
                                     FieldWork &work) const
{
    const qint32 width = secamConfig.videoParameters.fieldWidth;
    const qint32 base = row * width;

    for (qint32 i = 0; i < blockSize; i++) {
        qint32 s = base - fftMargin + i;
        s = std::clamp(s, 0, numSamples - 1);
        work.fftIn[i][0] = static_cast<double>(fieldData[s]);
        work.fftIn[i][1] = 0.0;
    }
    fftw_execute_dft(forwardPlan, work.fftIn, work.fftFreq);

    const double scale = 1.0 / blockSize;
    for (qint32 k = 0; k < blockSize; k++) {
        const std::complex<double> s(work.fftFreq[k][0], work.fftFreq[k][1]);
        const std::complex<double> v = s * work.maskChroma[k];
        work.fftWork[k][0] = v.real();
        work.fftWork[k][1] = v.imag();
    }
    fftw_execute_dft(inversePlan, work.fftWork, work.fftOut);
    // Mix the analytic chroma down to near-DC (phase-continuous across the
    // field: rows are contiguous in time, so the global sample index is the
    // time base).
    const double wMix = 2.0 * M_PI * work.mixFrequency / sampleRate;
    const std::complex<double> phasor0 =
        std::polar(1.0, -std::fmod(wMix * base, 2.0 * M_PI));
    const std::complex<double> step = std::polar(1.0, -wMix);
    std::complex<double> phasor = phasor0;
    for (qint32 x = 0; x < width; x++) {
        work.analytic[static_cast<size_t>(base) + x] =
            std::complex<double>(work.fftOut[fftMargin + x][0], work.fftOut[fftMargin + x][1])
            * scale * phasor;
        phasor *= step;
    }

    // Bell-free band pass: the mixed-down complex signal is the reference the
    // carrier calibration discriminates.
    for (qint32 k = 0; k < blockSize; k++) {
        work.fftWork[k][0] = work.fftFreq[k][0] * work.maskBand[k];
        work.fftWork[k][1] = work.fftFreq[k][1] * work.maskBand[k];
    }
    fftw_execute_dft(inversePlan, work.fftWork, work.fftOut);
    phasor = phasor0;
    for (qint32 x = 0; x < width; x++) {
        const std::complex<double> v(work.fftOut[fftMargin + x][0], work.fftOut[fftMargin + x][1]);
        work.analyticRef[static_cast<size_t>(base) + x] = v * scale * phasor;
        phasor *= step;
    }
}

// FFT one row and notch the FM block band out of it: the combined-input luma
// (the composite with the subcarrier block removed) that replaces the raw
// passthrough in the Y output. Same block-FFT context scheme as the analytic
// filter; maskLuma is a real symmetric gain (1 - band), so the result is the
// zero-phase notched composite. Only used when work.combinedInput.
void SecamDecoder::filterRowLuma(const quint16 *fieldData, qint32 numSamples, qint32 row,
                                 FieldWork &work) const
{
    const qint32 width = secamConfig.videoParameters.fieldWidth;
    const qint32 base = row * width;

    for (qint32 i = 0; i < blockSize; i++) {
        qint32 s = base - fftMargin + i;
        s = std::clamp(s, 0, numSamples - 1);
        work.fftIn[i][0] = static_cast<double>(fieldData[s]);
        work.fftIn[i][1] = 0.0;
    }
    fftw_execute_dft(forwardPlan, work.fftIn, work.fftFreq);

    const double scale = 1.0 / blockSize;
    for (qint32 k = 0; k < blockSize; k++) {
        work.fftWork[k][0] = work.fftFreq[k][0] * work.maskLuma[k];
        work.fftWork[k][1] = work.fftFreq[k][1] * work.maskLuma[k];
    }
    fftw_execute_dft(inversePlan, work.fftWork, work.fftOut);
    for (qint32 x = 0; x < width; x++) {
        work.lumaFiltered[static_cast<size_t>(base) + x] = work.fftOut[fftMargin + x][0] * scale;
    }
}

// Instantaneous frequency (Hz) over one row via the differentiating FIR on
// the mixed-down analytic signal. Im(conj(a) * a') / |a|^2 is the exact
// phase derivative: the envelope's own derivative lands in the real part,
// so a separate limiter stage is unnecessary and per-line AGC scaling cancels.
void SecamDecoder::discriminateRow(const std::complex<double> *analyticData, qint32 numSamples,
                                    qint32 row, double *fInstRow, FieldWork &work) const
{
    const qint32 width = secamConfig.videoParameters.fieldWidth;
    const qint32 K = (static_cast<qint32>(diffTaps.size()) - 1) / 2;
    const qint32 base = row * width;
    const double freqScale = sampleRate / (2.0 * M_PI);

    for (qint32 x = 0; x < width; x++) {
        const std::complex<double> a = analyticData[static_cast<size_t>(base) + x];
        const double mag2 = std::norm(a);
        if (mag2 < 1e-9) {
            fInstRow[x] = 0.0;
            continue;
        }
        std::complex<double> d(0.0, 0.0);
        for (qint32 m = -K; m <= K; m++) {
            if (m == 0) continue;
            const qint32 s = std::clamp(base + x - m, 0, numSamples - 1);
            d += diffTaps[m + K] * analyticData[s];
        }
        fInstRow[x] = work.mixFrequency
                      + freqScale * (a.real() * d.imag() - a.imag() * d.real()) / mag2;
    }
}

// Apply the exact LF de-emphasis mask to one row of the deviation signal
// (overlap-save over the flat per-field buffer, like the analytic filter).
void SecamDecoder::filterRowDeemphasis(FieldWork &work, qint32 numSamples, qint32 row) const
{
    const qint32 width = secamConfig.videoParameters.fieldWidth;
    const qint32 base = row * width;

    for (qint32 i = 0; i < blockSize; i++) {
        qint32 s = base - fftMargin + i;
        s = std::clamp(s, 0, numSamples - 1);
        work.fftIn[i][0] = work.demod[static_cast<size_t>(s)];
        work.fftIn[i][1] = 0.0;
    }
    fftw_execute_dft(forwardPlan, work.fftIn, work.fftFreq);

    const double scale = 1.0 / blockSize;
    for (qint32 k = 0; k < blockSize; k++) {
        const std::complex<double> s(work.fftFreq[k][0], work.fftFreq[k][1]);
        const std::complex<double> v = s * maskDeemphasis[k];
        work.fftWork[k][0] = v.real();
        work.fftWork[k][1] = v.imag();
    }
    fftw_execute_dft(inversePlan, work.fftWork, work.fftOut);
    for (qint32 x = 0; x < width; x++) {
        work.deemph[static_cast<size_t>(base) + x] = work.fftOut[fftMargin + x][0] * scale;
    }
}

void SecamDecoder::decodeField(const SourceVideo::Data &data, FieldWork &work) const
{
    const TbcMetaData::VideoParameters &videoParameters = secamConfig.videoParameters;
    const qint32 fieldWidth = videoParameters.fieldWidth;
    const qint32 fieldHeight = videoParameters.fieldHeight;
    const qint32 firstLine = videoParameters.firstActiveFieldLine;
    // The V-interval cutoff for chroma output: the original first active
    // field line before any full-frame widening. Rows below this are
    // V-interval (bottles/test-signal lines) that carry real carrier but are
    // not picture, so their chroma must render neutral even in full-frame
    // mode. They are still demodulated and read for carrier calibration.
    const qint32 chromaFirstLine = (secamConfig.nominalFirstActiveFieldLine >= 0)
                                    ? secamConfig.nominalFirstActiveFieldLine
                                    : firstLine;
    const qint32 numSamples = fieldHeight * fieldWidth;
    const quint16 *fieldData = data.data();

    // Detect a combined luma+chroma input once per work (per decodeFrames
    // call). A chroma TBC carries only the restored FM block riding around
    // the midpoint -- nothing below the block floor (the fixtures measure
    // >= 20k on the 16-bit scale); a combined source carries the full
    // composite, with sync tips far below black on every line (the etienne
    // combined fixture clips at 0). Sync-tip presence selects the narrow
    // combined band; without it the wide libchromadec band lets luma HF
    // own the discriminator wherever the restored carrier is weak (the
    // etienne combined fixture's +deviation carriers sit below the
    // in-band luma noise and decoded as a false ~4.14 MHz cluster).
    if (!work.inputChecked) {
        const double syncLevel = videoParameters.black16bIre
            - 0.25 * static_cast<double>(videoParameters.white16bIre - videoParameters.black16bIre);
        qint32 below = 0, total = 0;
        for (qint32 i = 0; i < numSamples; i += 7) {
            if (static_cast<double>(fieldData[i]) < syncLevel) below++;
            total++;
        }
        work.combinedInput = (total > 0 && below * 100 > total); // >1% of samples below the sync level
        work.inputChecked = true;
        buildChromaMasks(work); // rebuild at the selected band
        std::fprintf(stderr, "SECAMINPUT combined=%d (below-sync %.2f%%)\n",
                     work.combinedInput ? 1 : 0, total > 0 ? 100.0 * below / total : 0.0); // TEMP diagnostic
    }

    // Central-window per-row stats over the picture region: the reference
    // envelope scale for validity gating, and the frequency inputs the
    // content-statistics ident fallback reads (libchromadec's centreStats;
    // the bell-free reference path).
    const qint32 centreStart = std::max<qint32>(0, videoParameters.activeVideoStart + 30);
    const qint32 centreEnd = std::max(centreStart + 64,
                                      std::min<qint32>(fieldWidth, videoParameters.activeVideoEnd - 30));
    const auto centreStats = [&](qint32 row, double &freqOut, double &envOut) {
        double freqSum = 0.0, envSum = 0.0;
        qint32 n = 0;
        for (qint32 x = centreStart; x < centreEnd; x++) {
            const size_t i = static_cast<size_t>(row) * fieldWidth + x;
            freqSum += work.fInstRef[i];
            envSum += std::abs(work.analyticRef[i]);
            n++;
        }
        freqOut = (n > 0) ? freqSum / n : 0.0;
        envOut = (n > 0) ? envSum / n : 0.0;
    };
    const auto centreEnvelope = [&](qint32 row) -> double {
        double f, e;
        centreStats(row, f, e);
        return e;
    };

    double activeEnvMedian = 0.0;

    // Filter, discriminate, and calibrate; when the measured carrier pair
    // shows the FM block sits away from where the masks were built (VHS
    // colour-under converters translate the whole block, anti-bell included),
    // recentre the masks on the measurement and redo the field once. The
    // porch measurement runs on the bell-free band path, where a steady
    // in-band carrier is untouched by mask recentring (only the distant band
    // edges move), so one recentre settles it.
    for (qint32 pass = 0; pass < 2; pass++) {
        for (qint32 row = 0; row < fieldHeight; row++) {
            filterRowAnalytic(fieldData, numSamples, row, work);
        }
        // Combined-input luma: notch the FM block band out of the composite
        // (maskLuma follows the measured block and is rebuilt with the masks
        // on recentre). A split chroma TBC has no luma to filter.
        if (work.combinedInput) {
            for (qint32 row = 0; row < fieldHeight; row++) {
                filterRowLuma(fieldData, numSamples, row, work);
            }
        }
        for (qint32 row = 0; row < fieldHeight; row++) {
            discriminateRow(work.analytic.data(), numSamples, row,
                            &work.fInst[static_cast<size_t>(row) * fieldWidth], work);
            discriminateRow(work.analyticRef.data(), numSamples, row,
                            &work.fInstRef[static_cast<size_t>(row) * fieldWidth], work);
        }

        // TEMP diagnostic: one-shot per-sample frequency profile across the
        // blanking/front-porch of one picture line, to locate the rest carrier.
        static std::atomic<bool> dumped{false};
        if (!dumped.exchange(true)) {
            const qint32 probeRow = 30; // a picture line in both trimmed and full-frame
            if (probeRow < fieldHeight) {
                const double *fl = &work.fInst[static_cast<size_t>(probeRow) * fieldWidth];
                std::fprintf(stderr, "SECAMPROBE row=%d firstActiveFieldLine=%d porchStart=%d porchEnd=%d\n",
                             probeRow, firstLine, porchStart, porchEnd);
                for (qint32 x = 0; x <= 210; x += 2) {
                    std::fprintf(stderr, "PROBE x=%d freq=%.1f\n", x, fl[x]);
                }
            }
        }

        // Back-porch reference measurement: per row, the median discriminated
        // frequency and mean analytic envelope over the porch window, both
        // from the bell-free reference path. The median rides out FM clicks
        // and window-edge transients on noisy tape; the flat band response
        // keeps the noise around each carrier symmetric, so the median sits
        // on the carrier instead of being dragged toward the bell centre.
        for (qint32 row = 0; row < fieldHeight; row++) {
            work.porchFreq[row] = 0.0;
            work.porchEnvelope[row] = 0.0;
            std::vector<double> windowFreqs;
            windowFreqs.reserve(static_cast<size_t>(porchEnd - porchStart));
            double envSum = 0.0;
            qint32 n = 0;
            for (qint32 x = porchStart; x < porchEnd; x++) {
                const size_t i = static_cast<size_t>(row) * fieldWidth + x;
                windowFreqs.push_back(work.fInstRef[i]);
                envSum += std::abs(work.analyticRef[i]);
                n++;
            }
            if (n > 0) {
                std::sort(windowFreqs.begin(), windowFreqs.end());
                work.porchFreq[row] = windowFreqs[windowFreqs.size() / 2];
                work.porchEnvelope[row] = envSum / n;
            }
        }

        {
            std::vector<double> activeEnv;
            for (qint32 row = firstLine; row < fieldHeight; row++) {
                activeEnv.push_back(centreEnvelope(row));
            }
            activeEnvMedian = 0.0;
            if (!activeEnv.empty()) activeEnvMedian = medianInPlace(activeEnv);
        }

        // Rows with a usable porch carrier: the reference carrier has the
        // same order of amplitude as the picture FM block, so gate against
        // the picture envelope. A blanked porch leaves only filter leakage,
        // which this rejects.
        const double envThreshold = kColourKillerRatio * activeEnvMedian;
        work.rowValid.assign(fieldHeight, false);
        std::vector<double> validFreqs;
        for (qint32 row = 0; row < fieldHeight; row++) {
            if (envThreshold > 0.0 && work.porchEnvelope[row] > envThreshold) {
                work.rowValid[row] = true;
                validFreqs.push_back(work.porchFreq[row]);
            }
        }

        // Carrier calibration: split the per-line porch frequencies into the
        // two undeviated carriers. The measured pair absorbs converter
        // offsets (ME-SECAM LO arithmetic), so absolute positions are never
        // assumed. A field whose pair fails the separation gate reuses the
        // last good field's pair: the block shift is a property of the
        // capture, not the field, so a stale measurement stays close while
        // the nominal carriers can sit a full deviation away.
        work.fob = work.haveLastGoodCalibration ? work.lastGoodFob : kNominalFob;
        work.for_ = work.haveLastGoodCalibration ? work.lastGoodFor : kNominalFor;
        work.calibrated = false;
        if (static_cast<qint32>(validFreqs.size()) >= kCalibrationMinRows) {
            std::sort(validFreqs.begin(), validFreqs.end());
            const size_t half = validFreqs.size() / 2;
            // Per-cluster medians: FM clicks put long tails on the porch
            // frequency distributions, and a tail-dragged carrier estimate
            // becomes a constant colour-difference offset downstream.
            const double lowMedian = validFreqs[half / 2];
            const double highMedian = validFreqs[half + (validFreqs.size() - half) / 2];
            const double separation = highMedian - lowMedian;
            if (separation > kCalibrationMinSeparationHz && separation < kCalibrationMaxSeparationHz
                && std::abs(lowMedian - kNominalFob) <= kCalibrationMaxCarrierOffsetHz
                && std::abs(highMedian - kNominalFor) <= kCalibrationMaxCarrierOffsetHz) {
                work.fob = lowMedian;
                work.for_ = highMedian;
                work.calibrated = true;
            }
        }

        if (pass == 0 && work.calibrated) {
            const double measuredOffset =
                0.5 * (work.fob + work.for_) - 0.5 * (kNominalFob + kNominalFor);
            if (std::abs(measuredOffset - work.carrierOffset) > kRecentreThresholdHz) {
                work.carrierOffset = measuredOffset;
                buildChromaMasks(work);
                continue;
            }
        }
        break;
    }

    if (work.calibrated) {
        work.lastGoodFob = work.fob;
        work.lastGoodFor = work.for_;
        work.haveLastGoodCalibration = true;
    } else if (work.haveLastGoodCalibration) {
        qInfo() << "SecamDecoder: porch carrier pair not measurable;"
                << "reusing the last measured subcarriers";
    } else {
        qWarning() << "SecamDecoder: porch carrier pair not measurable;"
                   << "using nominal subcarriers";
    }

    const double split = 0.5 * (work.fob + work.for_);

    // D'R sits on the upper rest carrier, D'B on the lower. The sequence
    // alternates strictly at line rate (BT.470), so rather than trusting each
    // line's own measurement, fit the better of the two possible parities
    // over the measurable picture rows: a noisy line can land on the wrong
    // side, the majority never does. The measured pair (not the fixed block
    // centre) splits the clusters, so a converter offset that moves both
    // carriers cannot break the vote.
    qint32 agreeEvenIsRed = 0;
    qint32 picture = 0;
    for (qint32 row = firstLine; row < fieldHeight; row++) {
        if (!work.rowValid[row]) continue;
        const bool measuredRed = work.porchFreq[row] > split;
        if (measuredRed == ((row % 2) == 0)) agreeEvenIsRed++;
        picture++;
    }
    // A porch fit is only trusted when it measured enough rows decisively
    // (libchromadec Auto's gate: >= 8 rows at >= 0.6 confidence). A combined
    // luma+chroma source whose restored rest carrier is weak (the etienne
    // fixture's porch carrier sits ~5 dB above the in-band noise) votes at
    // coin-flip confidence -- trusting it flickers the D'R/D'B lattice
    // field to field.
    qint32 identSource = 0;
    work.identSource = 0; // TEMP diagnostic: 0 = porch, 1 = content, 2 = bottles
    const double porchConfidence = (picture > 0)
        ? static_cast<double>(std::max(agreeEvenIsRed, picture - agreeEvenIsRed)) / picture
        : 0.0;
    // libchromadec Auto accepts a porch fit at >= 0.6 confidence; a marginal
    // fit on a weak-porch source (the etienne fixture's field 2 voted 0.62
    // and landed on the wrong side of the BR.469 four-field cycle) is also
    // cross-checked here against a decisive bottles fit, which wins from
    // 0.75 upward (the same bar libchromadec requires of the bottles fit
    // itself). A decisive porch fit (>= 0.75) is never replaced.
    if (picture < 8 || porchConfidence < 0.75) {
        // Field-ident bottles (BT.470-6 option B, libchromadec Auto's second
        // mechanism): trapezoids on the early vertical-interval lines, Dr
        // lines deviated far positive and Db lines far negative, so the
        // carrier split separates them trivially. The etienne capture
        // carries them (rows 6-14 read the Db bottle at ~3.90 MHz, ~0.35 MHz
        // below foB) while its weak restored porch carrier cannot vote.
        qint32 bottleRows = 0;
        qint32 bottleAgreeEvenIsRed = 0;
        const qint32 bottleLast = std::min<qint32>(15, fieldHeight - 1);
        // Bottle plausibility: the BT.470-6 option-B trapezoids deviate the
        // subcarrier to the full-scale extremes, fob-350 kHz (Db) / for+350 kHz
        // (Dr), following the measured carrier pair. libchromadec's fitBottles
        // trusts the envelope gate alone (its sources carry level bottles on
        // every ident line); on captures whose one polarity of bottles is
        // barely above the noise (the etienne capture's Dr bottles sit ~20 dB
        // under its Db bottles), a noise-floor row can pass the envelope gate
        // yet read a band-edge frequency, voting garbage. Only rows that read
        // an actual bottle plateau vote.
        const double bottleDb = work.fob - 350000.0;
        const double bottleDr = work.for_ + 350000.0;
        constexpr double kBottleToleranceHz = 200000.0;
        for (qint32 row = 5; row <= bottleLast; row++) {
            double f, e;
            centreStats(row, f, e);
            if (activeEnvMedian <= 0.0 || e < 0.5 * activeEnvMedian) continue;
            if (std::abs(f - bottleDb) > kBottleToleranceHz
                && std::abs(f - bottleDr) > kBottleToleranceHz) continue;
            const bool measuredRed = f > split;
            if (measuredRed == ((row % 2) == 0)) bottleAgreeEvenIsRed++;
            bottleRows++;
        }
        const double bottleConfidence = (bottleRows > 0)
            ? static_cast<double>(std::max(bottleAgreeEvenIsRed, bottleRows - bottleAgreeEvenIsRed)) / bottleRows
            : 0.0;
        // TEMP diagnostic: dump the per-row bottle inputs on the first field
        // whose bottles fit fails to win, to see why.
        static std::atomic<bool> bottleDumped{false};
        if (!(bottleRows >= 4 && bottleConfidence >= 0.75) && !bottleDumped.exchange(true)) {
            for (qint32 row = 5; row <= bottleLast && row < fieldHeight; row++) {
                double f, e;
                centreStats(row, f, e);
                std::fprintf(stderr, "BOTTLEDIAG row=%d freq=%.1f env=%.1f gate=%.1f %s\n",
                             row, f, e, 0.5 * activeEnvMedian,
                             (activeEnvMedian > 0.0 && e >= 0.5 * activeEnvMedian) ? "PASS" : "fail");
            }
            std::fprintf(stderr, "BOTTLEDIAG summary rows=%d agree=%d conf=%.2f\n",
                         bottleRows, bottleAgreeEvenIsRed, bottleConfidence);
        }
        if (bottleRows >= 4 && bottleConfidence >= 0.75) {
            agreeEvenIsRed = bottleAgreeEvenIsRed;
            picture = bottleRows;
            identSource = 2;
            work.identSource = 2;
        } else {
            // Content-statistics fallback (libchromadec Auto's last resort):
            // a line's mean discriminated frequency sits near its own
            // carrier (real content averages toward low saturation), so a
            // median split over the active rows recovers the alternation
            // without trusting absolute carrier positions.
            std::vector<qint32> contentRows;
            std::vector<double> contentFreqs;
            for (qint32 row = firstLine; row < fieldHeight; row++) {
                double f, e;
                centreStats(row, f, e);
                if (activeEnvMedian <= 0.0 || e < 0.3 * activeEnvMedian) continue;
                contentRows.push_back(row);
                contentFreqs.push_back(f);
            }
            if (static_cast<qint32>(contentRows.size()) >= 16) {
                std::vector<double> sorted = contentFreqs;
                const double median = medianInPlace(sorted);
                agreeEvenIsRed = 0;
                picture = 0;
                for (size_t i = 0; i < contentRows.size(); i++) {
                    const bool measuredRed = contentFreqs[i] > median;
                    if (measuredRed == ((contentRows[i] % 2) == 0)) agreeEvenIsRed++;
                    picture++;
                }
                identSource = 1;
                work.identSource = 1;
            }
        }
    }
    const bool drOnEven = (picture == 0) ? false : (agreeEvenIsRed * 2) >= picture;

    for (qint32 row = 0; row < fieldHeight; row++) {
        work.lineIsRed[row] = ((row % 2) == 0) == drOnEven;
    }

    // TEMP diagnostic: capture vote tally + calibration. Confidence is the
    // majority share whichever parity won.
    work.parityEvenIsRed = agreeEvenIsRed;
    work.parityPicture = picture;
    work.parityDrOnEven = drOnEven;
    work.parityConfidence = (picture > 0)
        ? static_cast<double>(std::max(agreeEvenIsRed, picture - agreeEvenIsRed)) / picture
        : 0.0;

    std::vector<bool> hasDr(fieldHeight, false);
    std::vector<bool> hasDb(fieldHeight, false);

    // Chroma noise floor from the same porch windows used for calibration:
    // median absolute deviation of the per-line porch frequencies about their
    // component's carrier (feeds the click concealment threshold below).
    // Only meaningful when the porch pair measured well -- on a source whose
    // porch readings are noise (weak combined-source rest carrier), the MAD
    // is garbage and its concealment-overshoot term would disable the stage,
    // so it is zeroed and the level-based threshold stands alone.
    if (identSource == 0) {
        std::vector<double> porchDeviations;
        for (qint32 row = firstLine; row < fieldHeight; row++) {
            if (!work.rowValid[row]) continue;
            const double centre = work.porchFreq[row] > split ? work.for_ : work.fob;
            porchDeviations.push_back(std::abs(work.porchFreq[row] - centre));
        }
        work.noiseMadHz = medianInPlace(porchDeviations);
    } else {
        work.noiseMadHz = 0.0;
    }

    // Phase A: raw per-sample deviation in D'B/D'R units. One component per
    // line, matching the SECAM line sequence; the other component's slot is
    // left for the one-line hold. No de-emphasis yet -- click concealment and
    // the deviation rail run on the raw deviation so a spike is bounded
    // before the LF boost can amplify it. The colour killer zeroes samples
    // with no carrier (sync/V-interval, dropouts) so the deviation rail does
    // not clamp that noise to a coloured value, and rows below the nominal
    // first active line (V-interval bottles/test lines) are zeroed too: they
    // carry carrier but are not picture, so their demodulated deviation would
    // render as coloured garbage in full-frame.
    const double killerLevel = kColourKillerRatio * activeEnvMedian;
    for (qint32 row = 0; row < fieldHeight; row++) {
        double *demodLine = &work.demod[static_cast<size_t>(row) * fieldWidth];
        if (row < chromaFirstLine) {
            for (qint32 x = 0; x < fieldWidth; x++) demodLine[x] = 0.0;
            hasDr[row] = work.lineIsRed[row];
            hasDb[row] = !work.lineIsRed[row];
            continue;
        }
        const double *fInstLine = &work.fInst[static_cast<size_t>(row) * fieldWidth];
        const std::complex<double> *envRefLine = &work.analyticRef[static_cast<size_t>(row) * fieldWidth];
        const bool isRed = work.lineIsRed[row];
        const double zero = isRed ? work.for_ : work.fob;
        const double deviation = isRed ? kDeviationDr : kDeviationDb;
        for (qint32 x = 0; x < fieldWidth; x++) {
            demodLine[x] = (killerLevel > 0.0 && std::abs(envRefLine[x]) < killerLevel)
                               ? 0.0
                               : (fInstLine[x] - zero) / deviation;
        }
        hasDr[row] = isRed;
        hasDb[row] = !isRed;

        // TEMP diagnostic: mean fInst/demod per 1/8th of the active area on
        // one Db row and one Dr row (row 30 and 31: the active region only).
        // One-shot, like SECAMPROBE.
        static std::atomic<bool> barDumped{false};
        if ((row == 30 || row == 31) && !barDumped.load()) {
            const qint32 lo = videoParameters.activeVideoStart;
            const qint32 hi = videoParameters.activeVideoEnd;
            for (qint32 b = 0; b < 8; b++) {
                const qint32 blo = lo + (hi - lo) * b / 8;
                const qint32 bhi = lo + (hi - lo) * (b + 1) / 8;
                double fs = 0.0, ds = 0.0, es = 0.0;
                for (qint32 x = blo; x < bhi; x++) {
                    fs += fInstLine[x];
                    ds += demodLine[x];
                    es += std::abs(envRefLine[x]);
                }
                const qint32 n = bhi - blo;
                std::fprintf(stderr, "BARDIAG row=%d comp=%s band%d x[%d:%d] freq=%.0f demod=%+.3f env=%.0f\n",
                             row, isRed ? "Dr" : "Db", b, blo, bhi, fs / n, ds / n, es / n);
            }
            if (row == 31) barDumped.store(true);
        }
    }

    // Phase B: FM click concealment ("SECAM fire" suppression). On a combined
    // luma+chroma source a luma transition inside the chroma block makes the
    // discriminator spike (deviation overshoot) or the carrier envelope
    // collapse, and that sample reads as a bright dot. Detect those samples
    // and conceal them: short spans are linearly interpolated across, wider
    // spans borrow the previous same-component line (row-2, already concealed
    // since rows run top-down). Ported from libchromadec's SecamDecoder,
    // including the noise-adaptive overshoot term (a real sample never
    // exceeds the Table 4 maxima, so anything beyond them by the overshoot
    // factor -- which grows with the measured porch noise floor -- is a
    // click). Level 0 bypasses the stage entirely.
    if (secamConfig.clickNrLevel > 0.0) {
        const double level = qBound(0.0, secamConfig.clickNrLevel, 2.0);
        // Envelope dip threshold: the carrier envelope collapses to this
        // fraction of the row's median over the active region.
        const double dipDb = 12.0 - 6.0 * level;
        const double envRatio = std::pow(10.0, -dipDb / 20.0);
        // Deviation overshoot in Table 4 max-deviation multiples.
        const double overshoot = std::max(2.6 - 1.6 * level, 1.15)
                                 + 6.0 * work.noiseMadHz / 506000.0;
        const qint32 detectStart = std::max<qint32>(0, videoParameters.activeVideoStart - 8);
        const qint32 detectEnd = std::min<qint32>(fieldWidth, videoParameters.activeVideoEnd + 8);

        std::vector<double> rowEnv;
        std::vector<char> flagged(fieldWidth, 0);
        for (qint32 row = firstLine; row < fieldHeight; row++) {
            double *demodLine = &work.demod[static_cast<size_t>(row) * fieldWidth];
            const std::complex<double> *analyticLine = &work.analytic[static_cast<size_t>(row) * fieldWidth];
            const bool isRed = work.lineIsRed[row];
            const double loBound = isRed ? kDevDrLo : kDevDbLo;
            const double hiBound = isRed ? kDevDrHi : kDevDbHi;
            const double flagLo = loBound * overshoot;
            const double flagHi = hiBound * overshoot;

            // Row median envelope over the detect region is the reference
            // for the dip test; the band filter keeps even a dead carrier's
            // leakage near a quarter of nominal, so a deeper dip is needed
            // to catch real holes.
            rowEnv.clear();
            for (qint32 x = detectStart; x < detectEnd; x++) rowEnv.push_back(std::abs(analyticLine[x]));
            if (rowEnv.empty()) continue;
            std::sort(rowEnv.begin(), rowEnv.end());
            const double envRef = rowEnv[rowEnv.size() / 2];
            if (envRef <= 0.0) continue;
            const double envFloor = envRef * envRatio;

            std::fill(flagged.begin(), flagged.end(), 0);
            bool any = false;
            for (qint32 x = detectStart; x < detectEnd; x++) {
                if (demodLine[x] < flagLo || demodLine[x] > flagHi || std::abs(analyticLine[x]) < envFloor) {
                    flagged[x] = 1;
                    any = true;
                }
            }
            if (!any) continue;

            qint32 x = detectStart;
            while (x < detectEnd) {
                if (!flagged[x]) { x++; continue; }
                qint32 runEnd = x;
                while (runEnd < detectEnd && flagged[runEnd]) runEnd++;
                const qint32 spanStart = std::max<qint32>(0, x - 3);
                const qint32 spanEnd = std::min<qint32>(fieldWidth, runEnd + 3);

                if (spanEnd - spanStart <= 48 && spanStart > 0 && spanEnd < fieldWidth) {
                    // Short span: linearly interpolate across the click.
                    const double before = demodLine[spanStart - 1];
                    const double after = demodLine[spanEnd];
                    const qint32 n = spanEnd - spanStart + 1;
                    for (qint32 i = spanStart; i < spanEnd; i++) {
                        demodLine[i] = before + (after - before) * (i - spanStart + 1) / n;
                    }
                } else if (row >= 2) {
                    // Wide span: borrow the previous same-component line
                    // (row-2). It was already concealed if it needed to be.
                    const double *prev = &work.demod[static_cast<size_t>(row - 2) * fieldWidth];
                    for (qint32 i = spanStart; i < spanEnd; i++) demodLine[i] = prev[i];
                } else {
                    for (qint32 i = spanStart; i < spanEnd; i++) demodLine[i] = 0.0;
                }
                x = spanEnd;
            }
        }
    }

    // Phase C: deviation rail. Clamp every sample to the BT.470 Table 4 maxima
    // (in D'B/D'R units) before de-emphasis. Whatever clicks the concealment
    // missed (or all of them when it is bypassed) is bounded here so the LF
    // boost cannot blow a residual spike up into a visible streak.
    for (qint32 row = 0; row < fieldHeight; row++) {
        if (row < chromaFirstLine) continue;
        double *demodLine = &work.demod[static_cast<size_t>(row) * fieldWidth];
        const bool isRed = work.lineIsRed[row];
        const double loBound = isRed ? kDevDrLo : kDevDbLo;
        const double hiBound = isRed ? kDevDrHi : kDevDbHi;
        for (qint32 x = 0; x < fieldWidth; x++) {
            demodLine[x] = std::clamp(demodLine[x], loBound, hiBound);
        }
    }

    // Phase D: exact LF de-emphasis (frequency-domain inverse of the encoder
    // pre-correction) + scale from D'B/D'R to the colour-difference units the
    // output stage expects. V-interval rows stay zeroed.
    for (qint32 row = 0; row < fieldHeight; row++) {
        const bool isRed = work.lineIsRed[row];
        double *dst = isRed ? &work.dr[static_cast<size_t>(row) * fieldWidth]
                            : &work.db[static_cast<size_t>(row) * fieldWidth];
        if (row < chromaFirstLine) {
            for (qint32 x = 0; x < fieldWidth; x++) dst[x] = 0.0;
            continue;
        }
        filterRowDeemphasis(work, numSamples, row);
        const double unit = isRed ? 1.0 / kDrScale : 1.0 / kDbScale;
        const double *deemphLine = &work.deemph[static_cast<size_t>(row) * fieldWidth];
        for (qint32 x = 0; x < fieldWidth; x++) {
            dst[x] = deemphLine[x] * unit * secamConfig.chromaGain;
        }
    }

    // Phase E: one-line hold. Each line carries only one component; fill the
    // other from the nearest same-component lines.
    fillChannel(work.dr, hasDr, fieldHeight, fieldWidth);
    fillChannel(work.db, hasDb, fieldHeight, fieldWidth);
}

void SecamDecoder::decodeFrames(const QVector<SourceField>& inputFields,
                                qint32 startIndex,
                                qint32 endIndex,
                                QVector<ComponentFrame>& componentFrames)
{
    const TbcMetaData::VideoParameters &videoParameters = secamConfig.videoParameters;
    bool ignoreUV = false;

    const qint32 fieldWidth = videoParameters.fieldWidth;
    const qint32 fieldHeight = videoParameters.fieldHeight;
    const qint32 frameHeight = (fieldHeight * 2) - 1;

    // ComponentFrame's Y/U/V are all expected on the same scale as the
    // original composite signal (see componentframe.h), i.e. spanning
    // black16bIre..white16bIre -- not the fraction of the luma range that the
    // colour difference signals demodulate to.
    const double yRange = static_cast<double>(videoParameters.white16bIre - videoParameters.black16bIre);

    // Optional hue rotation (--chroma-phase): rotate the (D'B, D'R) colour-
    // difference vector. dr/db are stored in (R'-Y')/(B'-Y') units (Phase D
    // divides out kDrScale/kDbScale), so a 2D rotation here is a true hue
    // rotation. Default 0 degrees is the identity (no change to the output).
    const double phaseRad = secamConfig.chromaPhase * M_PI / 180.0;
    const double cosPhase = std::cos(phaseRad);
    const double sinPhase = std::sin(phaseRad);

    FieldWork work[2];
    work[0].init(blockSize, fieldHeight, fieldWidth);
    work[1].init(blockSize, fieldHeight, fieldWidth);
    buildChromaMasks(work[0]);
    buildChromaMasks(work[1]);

    for (qint32 fieldIndex = startIndex, frameIndex = 0; fieldIndex < endIndex; fieldIndex += 2, frameIndex++) {
        componentFrames[frameIndex].init(videoParameters, ignoreUV);

        decodeField(inputFields[fieldIndex].data, work[0]);
        decodeField(inputFields[fieldIndex + 1].data, work[1]);

        // TEMP diagnostic: per-field calibration + parity vote, keyed to the
        // absolute field seqNo so frames can be ordered across threads.
        for (int half = 0; half < 2; half++) {
            const auto &sf = inputFields[fieldIndex + half].field;
            std::fprintf(stderr, "SECAMDIAG seqNo=%d isFirst=%d secamFLIR=%d half=%d fob=%.1f for=%.1f offset=%.1f calibrated=%d ident=%d drOnEven=%d agree=%d picture=%d confidence=%.2f noiseMad=%.1f\n",
                         sf.seqNo, sf.isFirstField ? 1 : 0, sf.secamFirstLineIsRed ? 1 : 0, half,
                         work[half].fob, work[half].for_, work[half].carrierOffset,
                         work[half].calibrated ? 1 : 0, work[half].identSource,
                         work[half].parityDrOnEven ? 1 : 0,
                         work[half].parityEvenIsRed, work[half].parityPicture,
                         work[half].parityConfidence, work[half].noiseMadHz);
        }

        for (qint32 y = 0; y < frameHeight; y++) {
            const qint32 half = y % 2;
            const qint32 row = y / 2;
            const SourceVideo::Data &data = inputFields[fieldIndex + half].data;
            const quint16 *inputLine = data.data() + (static_cast<size_t>(row) * fieldWidth);

            double *outY = componentFrames[frameIndex].y(y);
            double *outU = componentFrames[frameIndex].u(y);
            double *outV = componentFrames[frameIndex].v(y);
            const double *drRow = &work[half].dr[static_cast<size_t>(row) * fieldWidth];
            const double *dbRow = &work[half].db[static_cast<size_t>(row) * fieldWidth];

            for (qint32 x = 0; x < fieldWidth; x++) {
                // A chroma-only TBC has no luma content in the input line (the
                // real luma comes from the separate luma TBC downstream), so
                // the input passes straight through. A combined luma+chroma
                // input IS the composite: output the block-low-passed luma
                // instead, so the FM subcarrier block (steady carriers AND
                // colour-edge transient sidebands) does not ride into the
                // video as luma dots.
                outY[x] = work[half].combinedInput
                    ? work[half].lumaFiltered[static_cast<size_t>(row) * fieldWidth + x]
                    : inputLine[x];
                const double dbVal = dbRow[x];
                const double drVal = drRow[x];
                const double dbRot = dbVal * cosPhase - drVal * sinPhase;
                const double drRot = dbVal * sinPhase + drVal * cosPhase;
                outV[x] = (drRot * yRange) / DR_TO_V_SCALE;
                outU[x] = (dbRot * yRange) / DB_TO_U_SCALE;
            }
        }
    }
}

SecamThread::SecamThread(QAtomicInt &_abort, DecoderPool &_decoderPool,
                       const SecamDecoder::SecamConfiguration &_secamConfig, QObject *parent)
    : DecoderThread(_abort, _decoderPool, parent), secamConfig(_secamConfig)
{
}

void SecamThread::decodeFrames(const QVector<SourceField> &inputFields,
                              qint32 startIndex, qint32 endIndex,
                              QVector<ComponentFrame> &componentFrames)
{
    // Delegate to the centralized, public API
    auto &baseDecoder = static_cast<SecamDecoder&>(decoderPool.getDecoder());
    baseDecoder.decodeFrames(inputFields, startIndex, endIndex, componentFrames);
}
