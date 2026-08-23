/************************************************************************

    secamdecoder.cpp

    ld-chroma-decoder - Colourisation filter for ld-decode

    This file is part of tbc-tools.

    ld-chroma-decoder is free software: you can redistribute it and/or
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

namespace {

// SECAM subcarrier rest frequencies (ITU-R BT.470-6 / BT.1700). vhs-decode's
// SECAM and ME-SECAM paths both restore the chroma block to these.
constexpr double SECAM_FOR = 4406250.0;
constexpr double SECAM_FOB = 4250000.0;
constexpr double SECAM_BLOCK_CENTRE = (SECAM_FOR + SECAM_FOB) / 2.0;

// Frequency deviation per unit of colour difference:
//   f = foB + 230 kHz * D'B, D'B =  1.505 * (B'-Y')
//   f = foR + 280 kHz * D'R, D'R = -1.902 * (R'-Y')
// The polarity inversion SECAM is known for on the R-Y component is carried by
// the -1.902 coefficient itself, so the D'R carrier falls as (R'-Y') rises.
// Confirmed on real method 1 captures: on a blue-dominated scene, where
// (R'-Y') < 0, the D'R lines read a consistently positive deviation.
constexpr double DB_HZ_PER_UNIT = 230.0e3 * 1.505;    // +346.15 kHz
constexpr double DR_HZ_PER_UNIT = -280.0e3 * 1.902;   // -532.56 kHz

// Quadrature demodulator lowpass. The legal subcarrier excursion is 3.900 to
// 4.756 MHz (BT.470), i.e. +-428 kHz around the block centre, so this passes
// the whole block while rejecting everything outside it.
constexpr double DEMOD_LOWPASS_HZ = 600.0e3;
constexpr qint32 DEMOD_NUM_TAPS = 25;

// SECAM LF pre-emphasis A(f) = (1 + jf/f1) / (1 + jf/3f1), applied to the
// colour difference signals before modulation at the studio; the tape path
// leaves it untouched, so it is undone here.
constexpr double DEEMPHASIS_F1 = 85.0e3;

// A line's measured rest carrier is only trusted as its discriminator zero if
// it lands this close to the nominal frequency for the identity it was fitted
// to; otherwise the nominal value is used. On a good capture the measurement
// is within a few hundred Hz.
constexpr double REST_TOLERANCE_HZ = 60.0e3;

// Colour killer: below this fraction of the field's median carrier envelope
// there is no carrier to read a frequency from, so the reading is noise.
constexpr double COLOUR_KILLER_RATIO = 0.25;

// The chroma TBC is written centred on this value. Only the residual matters
// here -- the quadrature mixer shifts DC to -SECAM_BLOCK_CENTRE, where the
// demodulator lowpass removes it -- so this need not be exact.
constexpr double CHROMA_MIDPOINT = 32768.0;

// OutputWriter's RGB48 path (outputwriter.cpp) converts Y'UV with the fixed
// matrix R = Y + 1.139883*V, G = Y - 0.394642*U - 0.580622*V,
// B = Y + 2.032062*U -- the standard PAL/NTSC matrix, where U/V are the
// *normalized* (B-Y)/(R-Y). SECAM's D'R/D'B demodulate straight to (R'-Y') and
// (B'-Y'), so storing them pre-divided by these same constants makes the
// existing matrix reproduce the correct SECAM inverse (R = Y+Dr, B = Y+Db,
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

void SecamDecoder::FieldWork::resize(qint32 fieldHeight, qint32 fieldWidth)
{
    const size_t size = static_cast<size_t>(fieldHeight) * fieldWidth;
    frequency.assign(size, 0.0);
    envelope.assign(size, 0.0);
    dr.assign(size, 0.0);
    db.assign(size, 0.0);
    lineIsRed.assign(fieldHeight, false);
    scratch.clear();
    scratch.reserve(fieldWidth);
}

SecamDecoder::SecamDecoder()
{
}

SecamDecoder::SecamDecoder(const SecamDecoder::SecamConfiguration &config)
{
    // Only the settings are taken here; DecoderPool calls configure() with the
    // real video parameters before any decoding happens.
    secamConfig = config;
}

bool SecamDecoder::updateConfiguration(const TbcMetaData::VideoParameters &videoParameters,
                                       const SecamDecoder::SecamConfiguration &configuration)
{
    if (!configure(videoParameters)) return false;
    secamConfig.chromaGain = configuration.chromaGain;

    return true;
}

bool SecamDecoder::configure(const TbcMetaData::VideoParameters &videoParameters)
{
    // SECAM/ME-SECAM sources carry the PAL line structure (625-line), and
    // vhs-decode reports them as SECAM, MESECAM, or PAL. All three are valid
    // here — the decoder works on any 625-line PAL-geometry source.
    if (videoParameters.system != PAL && videoParameters.system != PAL_M
        && videoParameters.system != SECAM && videoParameters.system != MESECAM) {
        qCritical() << "This decoder is for PAL-line (SECAM/MESECAM) video only";
        return false;
    }

    secamConfig.videoParameters = videoParameters;

    // Windowed-sinc lowpass for the quadrature demodulator.
    demodTaps.resize(DEMOD_NUM_TAPS);
    const double centre = (DEMOD_NUM_TAPS - 1) / 2.0;
    double sum = 0.0;
    for (qint32 i = 0; i < DEMOD_NUM_TAPS; i++) {
        const double n = i - centre;
        const double t = 2.0 * DEMOD_LOWPASS_HZ / videoParameters.sampleRate * n;
        // sinc(t), with the removable singularity at t == 0 filled in
        const double sinc = (n == 0.0) ? 1.0 : std::sin(M_PI * t) / (M_PI * t);
        // Hamming window
        const double window = 0.54 - 0.46 * std::cos(2.0 * M_PI * i / (DEMOD_NUM_TAPS - 1));
        demodTaps[i] = sinc * window;
        sum += demodTaps[i];
    }
    for (double &tap : demodTaps) tap /= sum;

    // Rest-carrier measurement window, inside the blanking interval that
    // vhs-decode regenerates as a clean, undeviated carrier. Starting at the
    // burst gate keeps the sync area out, and stopping short of active video
    // keeps the picture's own colour turn-on strip out. On a real method 1
    // capture this measures foB to under 100 Hz and foR to under 300 Hz.
    restStart = videoParameters.colourBurstStart;
    restEnd = std::min(videoParameters.activeVideoStart - 10, videoParameters.fieldWidth);
    if (restEnd <= restStart) {
        // Fall back to the first half of the blanking interval.
        restStart = videoParameters.activeVideoStart / 4;
        restEnd = videoParameters.activeVideoStart / 2;
    }
    if (restStart < 0 || restEnd <= restStart || videoParameters.fieldWidth < 2) {
        qCritical() << "SECAM decoder: no usable rest-carrier window in these video parameters";
        return false;
    }

    return true;
}

QThread *SecamDecoder::makeThread(QAtomicInt& abort, DecoderPool& decoderPool) {
    return new SecamThread(abort, decoderPool, secamConfig);
}

// Quadrature demodulate one field, line by line. Each TBC line's carrier phase
// is independent of its neighbours', so the mixing and discrimination are kept
// within a line -- the local oscillator's phase reset at each line start is
// harmless, since only the sample-to-sample phase *difference* is read.
void SecamDecoder::demodulateField(const SourceVideo::Data &data, FieldWork &work) const
{
    const TbcMetaData::VideoParameters &videoParameters = secamConfig.videoParameters;
    const qint32 fieldWidth = videoParameters.fieldWidth;
    const qint32 fieldHeight = videoParameters.fieldHeight;
    const qint32 halfTaps = DEMOD_NUM_TAPS / 2;
    const double hzPerRadian = videoParameters.sampleRate / (2.0 * M_PI);

    std::vector<std::complex<double>> mixed(fieldWidth);
    std::vector<std::complex<double>> filtered(fieldWidth);

    // Local oscillator at the block centre. It depends only on the position
    // within the line, so it is built once and reused for every line.
    std::vector<std::complex<double>> oscillator(fieldWidth);
    for (qint32 x = 0; x < fieldWidth; x++) {
        const double phase = -2.0 * M_PI * SECAM_BLOCK_CENTRE * x / videoParameters.sampleRate;
        oscillator[x] = std::complex<double>(std::cos(phase), std::sin(phase));
    }

    for (qint32 row = 0; row < fieldHeight; row++) {
        const quint16 *inputLine = data.data() + (static_cast<size_t>(row) * fieldWidth);

        for (qint32 x = 0; x < fieldWidth; x++) {
            mixed[x] = (static_cast<double>(inputLine[x]) - CHROMA_MIDPOINT) * oscillator[x];
        }

        for (qint32 x = 0; x < fieldWidth; x++) {
            std::complex<double> acc(0.0, 0.0);
            for (qint32 k = 0; k < DEMOD_NUM_TAPS; k++) {
                const qint32 j = x + k - halfTaps;
                if (j >= 0 && j < fieldWidth) acc += mixed[j] * demodTaps[k];
            }
            filtered[x] = acc;
        }

        double *freqLine = &work.frequency[static_cast<size_t>(row) * fieldWidth];
        double *envLine = &work.envelope[static_cast<size_t>(row) * fieldWidth];
        for (qint32 x = 0; x < fieldWidth; x++) {
            envLine[x] = std::abs(filtered[x]);

            // Discriminator: the phase advance from the previous sample is the
            // frequency offset from the block centre. The first sample has no
            // predecessor, so it repeats the second sample's reading.
            const qint32 curr = (x == 0) ? 1 : x;
            const std::complex<double> delta = filtered[curr] * std::conj(filtered[curr - 1]);
            freqLine[x] = std::arg(delta) * hzPerRadian + SECAM_BLOCK_CENTRE;
        }
    }
}

void SecamDecoder::decodeField(const SourceVideo::Data &data, FieldWork &work) const
{
    const TbcMetaData::VideoParameters &videoParameters = secamConfig.videoParameters;
    const qint32 fieldWidth = videoParameters.fieldWidth;
    const qint32 fieldHeight = videoParameters.fieldHeight;
    const qint32 firstLine = videoParameters.firstActiveFieldLine;

    demodulateField(data, work);

    // Read each line's rest carrier out of the regenerated blanking interval.
    std::vector<double> restCarrier(fieldHeight, 0.0);
    for (qint32 row = 0; row < fieldHeight; row++) {
        const double *freqLine = &work.frequency[static_cast<size_t>(row) * fieldWidth];
        work.scratch.assign(freqLine + restStart, freqLine + restEnd);
        restCarrier[row] = medianInPlace(work.scratch);
    }

    // D'R sits on the upper rest carrier, D'B on the lower. The sequence
    // alternates strictly at line rate (BT.470), so rather than trusting each
    // line's own measurement, fit the better of the two possible parities:
    // a noisy line can land on the wrong side, the majority never does.
    qint32 evenIsRed = 0;
    qint32 picture = 0;
    for (qint32 row = firstLine; row < fieldHeight; row++) {
        const bool measuredRed = restCarrier[row] > SECAM_BLOCK_CENTRE;
        if (measuredRed == ((row % 2) == 0)) evenIsRed++;
        picture++;
    }
    const bool drOnEven = (evenIsRed * 2) >= picture;

    for (qint32 row = 0; row < fieldHeight; row++) {
        work.lineIsRed[row] = ((row % 2) == 0) == drOnEven;
    }

    // Demodulate to colour difference values, using each line's own measured
    // rest carrier as the discriminator zero where it is trustworthy. This
    // absorbs any residual carrier offset left by the tape path.
    std::vector<bool> hasDr(fieldHeight, false);
    std::vector<bool> hasDb(fieldHeight, false);

    // Envelope level for the colour killer, over the picture's active area.
    work.scratch.clear();
    for (qint32 row = firstLine; row < fieldHeight; row += 4) {
        const double *envLine = &work.envelope[static_cast<size_t>(row) * fieldWidth];
        for (qint32 x = videoParameters.activeVideoStart; x < videoParameters.activeVideoEnd; x += 4) {
            work.scratch.push_back(envLine[x]);
        }
    }
    const double killerLevel = COLOUR_KILLER_RATIO * medianInPlace(work.scratch);

    // De-emphasis H(f) = (1 + jf/3f1) / (1 + jf/f1), bilinear-transformed.
    const double w1 = 2.0 * M_PI * DEEMPHASIS_F1;
    const double w3 = 3.0 * w1;
    const double k = 2.0 * videoParameters.sampleRate;
    const double a0 = 1.0 + k / w1;
    const double b0 = (1.0 + k / w3) / a0;
    const double b1 = (1.0 - k / w3) / a0;
    const double a1 = (1.0 - k / w1) / a0;

    for (qint32 row = 0; row < fieldHeight; row++) {
        const bool isRed = work.lineIsRed[row];
        double *dst = isRed ? &work.dr[static_cast<size_t>(row) * fieldWidth]
                            : &work.db[static_cast<size_t>(row) * fieldWidth];

        if (row < firstLine) {
            // Vertical interval: no picture, no colour.
            for (qint32 x = 0; x < fieldWidth; x++) dst[x] = 0.0;
            hasDr[row] = isRed;
            hasDb[row] = !isRed;
            continue;
        }

        const double nominal = isRed ? SECAM_FOR : SECAM_FOB;
        const double reference = (std::fabs(restCarrier[row] - nominal) < REST_TOLERANCE_HZ)
                                 ? restCarrier[row] : nominal;
        const double perHz = 1.0 / (isRed ? DR_HZ_PER_UNIT : DB_HZ_PER_UNIT);

        const double *freqLine = &work.frequency[static_cast<size_t>(row) * fieldWidth];
        const double *envLine = &work.envelope[static_cast<size_t>(row) * fieldWidth];

        double prevIn = 0.0;
        double prevOut = 0.0;
        for (qint32 x = 0; x < fieldWidth; x++) {
            double value = (envLine[x] < killerLevel) ? 0.0
                                                      : (freqLine[x] - reference) * perHz;
            const double out = b0 * value + b1 * prevIn - a1 * prevOut;
            prevIn = value;
            prevOut = out;
            dst[x] = out * secamConfig.chromaGain;
        }

        hasDr[row] = isRed;
        hasDb[row] = !isRed;
    }

    // One-line hold: each line carries only one of the two components.
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

    FieldWork work[2];
    work[0].resize(fieldHeight, fieldWidth);
    work[1].resize(fieldHeight, fieldWidth);

    for (qint32 fieldIndex = startIndex, frameIndex = 0; fieldIndex < endIndex; fieldIndex += 2, frameIndex++) {
        componentFrames[frameIndex].init(videoParameters, ignoreUV);

        decodeField(inputFields[fieldIndex].data, work[0]);
        decodeField(inputFields[fieldIndex + 1].data, work[1]);

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
                // Composite passthrough. This is discarded downstream when the
                // decoder is run on a chroma-only TBC (the real luma comes from
                // the separate luma TBC), and keeps the decoder sane if it is
                // ever run on a combined source.
                outY[x] = inputLine[x];
                outV[x] = (drRow[x] * yRange) / DR_TO_V_SCALE;
                outU[x] = (dbRow[x] * yRange) / DB_TO_U_SCALE;
            }
        }
    }
}

SecamThread::SecamThread(QAtomicInt& _abort, DecoderPool& _decoderPool,
                       const SecamDecoder::SecamConfiguration &_secamConfig, QObject *parent)
    : DecoderThread(_abort, _decoderPool, parent), secamConfig(_secamConfig)
{
}

void SecamThread::decodeFrames(const QVector<SourceField>& inputFields,
                              qint32 startIndex, qint32 endIndex,
                              QVector<ComponentFrame>& componentFrames)
{
    // Delegate to the centralized, public API
    auto &baseDecoder = static_cast<SecamDecoder&>(decoderPool.getDecoder());
    baseDecoder.decodeFrames(inputFields, startIndex, endIndex, componentFrames);
}
