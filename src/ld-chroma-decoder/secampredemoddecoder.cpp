/************************************************************************

    secampredemoddecoder.cpp

    ld-chroma-decoder - Colourisation filter for ld-decode

    Copyright (C) 2026 Hugo Caille
    Copyright (C) 2026 Harry Munday

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

#include "secampredemoddecoder.h"

#include "decoderpool.h"

namespace {

// Matches vhsdecode/secam.py's CHROMA_SCALE: maps the clipped [-1, 1] Dr/Db
// value produced by the FM demodulator back from the u16 range it was packed
// into for storage in the chroma TBC.
constexpr double CHROMA_SCALE = 20000.0;

// OutputWriter's RGB48 path (outputwriter.cpp) converts Y'UV with the fixed
// matrix R = Y + 1.139883*V, G = Y - 0.394642*U - 0.580622*V,
// B = Y + 2.032062*U -- the standard PAL/NTSC matrix, where U/V are the
// *normalized* (B-Y)/(R-Y). SECAM's Dr/Db are already (R-Y)/(B-Y) directly
// (no extra normalization), so storing them pre-divided by these same
// constants makes the existing matrix reproduce the correct SECAM inverse
// (R = Y+Dr, B = Y+Db, G = Y-(kr.Dr+kb.Db)/kg) without touching OutputWriter
// at all -- verified by direct substitution.
constexpr double DR_TO_V_SCALE = 1.139883;
constexpr double DB_TO_U_SCALE = 2.032062;

// Bidirectional nearest-row fill (one-line hold): rows not marked valid in
// `has` are replaced by the average of the nearest valid rows above and
// below (or whichever side exists), mirroring the reconstruction already
// verified visually in vhsdecode's own preview tool
// (secam_preview.py::fill_channel).
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

SecamPredemodDecoder::SecamPredemodDecoder()
{
}

SecamPredemodDecoder::SecamPredemodDecoder(const SecamPredemodDecoder::SecamConfiguration &config)
{
    // Only the settings are taken here; DecoderPool calls configure() with the
    // real video parameters before any decoding happens.
    secamConfig = config;
}

bool SecamPredemodDecoder::updateConfiguration(const TbcMetaData::VideoParameters &videoParameters,
                                               const SecamPredemodDecoder::SecamConfiguration &configuration)
{
    if (!configure(videoParameters)) return false;
    secamConfig.chromaGain = configuration.chromaGain;
    secamConfig.firstLineIsRedOverride = configuration.firstLineIsRedOverride;

    return true;
}

bool SecamPredemodDecoder::configure(const TbcMetaData::VideoParameters &videoParameters)
{
    // SECAM/ME-SECAM sources carry the PAL line structure (625-line), and
    // vhs-decode reports them as SECAM, MESECAM, or PAL. All three are valid
    // here -- the decoder works on any 625-line PAL-geometry source that
    // carries pre-demodulated Dr/Db. (The fork only accepted PAL/PAL_M, which
    // rejected real SECAM/MESECAM captures; see test-artifacts/secam-crash.log
    // for the same class of rejection in the FM-demod decoder.)
    if (videoParameters.system != PAL && videoParameters.system != PAL_M
        && videoParameters.system != SECAM && videoParameters.system != MESECAM) {
        qCritical() << "This decoder is for PAL-line (SECAM/MESECAM) video only";
        return false;
    }

    secamConfig.videoParameters = videoParameters;

    return true;
}

QThread *SecamPredemodDecoder::makeThread(QAtomicInt& abort, DecoderPool& decoderPool) {
    return new SecamPredemodThread(abort, decoderPool, secamConfig);
}

void SecamPredemodDecoder::decodeFrames(const QVector<SourceField>& inputFields,
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
    // black16bIre..white16bIre -- not the normalized [-1, 1] fraction of
    // maximum deviation that our Dr/Db are decoded into. Rescale so that
    // full saturation (native +-1) maps to a swing comparable to the full
    // black-white range, matching what OutputWriter's uvScale (yRange-based)
    // expects.
    const double yRange = static_cast<double>(videoParameters.white16bIre - videoParameters.black16bIre);
    const double gain = secamConfig.chromaGain;

    // Reconstructed Dr/Db for both fields of the frame currently being
    // decoded, and which rows genuinely carried that colour before filling.
    std::vector<double> dr[2];
    std::vector<double> db[2];
    dr[0].resize(fieldHeight * fieldWidth);
    dr[1].resize(fieldHeight * fieldWidth);
    db[0].resize(fieldHeight * fieldWidth);
    db[1].resize(fieldHeight * fieldWidth);
    std::vector<bool> hasDr(fieldHeight);
    std::vector<bool> hasDb(fieldHeight);

    for (qint32 fieldIndex = startIndex, frameIndex = 0; fieldIndex < endIndex; fieldIndex += 2, frameIndex++) {
        componentFrames[frameIndex].init(videoParameters, ignoreUV);

        for (qint32 half = 0; half < 2; half++) {
            const SourceField &sf = inputFields[fieldIndex + half];
            const SourceVideo::Data &data = sf.data;
            // Line identity: a decode-time override wins; otherwise use the
            // per-field metadata flag written by vhs-decode / the Metadata
            // Editor. With no override and a default metadata value, even rows
            // are treated as D'B (firstLineIsRed == false), which matches the
            // SECAM field that leads on the D'B line.
            const bool firstLineIsRed = (secamConfig.firstLineIsRedOverride >= 0)
                                        ? (secamConfig.firstLineIsRedOverride != 0)
                                        : sf.field.secamFirstLineIsRed;

            for (qint32 row = 0; row < fieldHeight; row++) {
                const quint16 *rowData = data.data() + (row * fieldWidth);
                const bool isRed = ((row % 2) == 0) == firstLineIsRed;
                hasDr[row] = isRed;
                hasDb[row] = !isRed;
                double *dst = isRed ? &dr[half][row * fieldWidth] : &db[half][row * fieldWidth];
                for (qint32 x = 0; x < fieldWidth; x++) {
                    dst[x] = (static_cast<double>(rowData[x]) - 32767.0) / CHROMA_SCALE * gain;
                }
            }

            fillChannel(dr[half], hasDr, fieldHeight, fieldWidth);
            fillChannel(db[half], hasDb, fieldHeight, fieldWidth);
        }

        for (qint32 y = 0; y < frameHeight; y++) {
            const qint32 half = y % 2;
            const qint32 row = y / 2;
            const SourceVideo::Data &data = inputFields[fieldIndex + half].data;
            const quint16 *rowData = data.data() + (row * fieldWidth);

            double *outY = componentFrames[frameIndex].y(y);
            double *outU = componentFrames[frameIndex].u(y);
            double *outV = componentFrames[frameIndex].v(y);
            const double *drRow = &dr[half][row * fieldWidth];
            const double *dbRow = &db[half][row * fieldWidth];

            for (qint32 x = 0; x < fieldWidth; x++) {
                // Composite passthrough; discarded downstream when this
                // decoder is run on the chroma TBC only (the real luma comes
                // from the separate mono-decoded luma TBC), kept here so
                // this decoder also behaves sanely if ever run standalone.
                outY[x] = rowData[x];
                outV[x] = (drRow[x] * yRange) / DR_TO_V_SCALE;
                outU[x] = (dbRow[x] * yRange) / DB_TO_U_SCALE;
            }
        }
    }
}

SecamPredemodThread::SecamPredemodThread(QAtomicInt &_abort, DecoderPool &_decoderPool,
                       const SecamPredemodDecoder::SecamConfiguration &_secamConfig, QObject *parent)
    : DecoderThread(_abort, _decoderPool, parent), secamConfig(_secamConfig)
{
}

void SecamPredemodThread::decodeFrames(const QVector<SourceField> &inputFields,
                              qint32 startIndex, qint32 endIndex,
                              QVector<ComponentFrame> &componentFrames)
{
    // Delegate to the centralized, public API
    auto &baseDecoder = static_cast<SecamPredemodDecoder&>(decoderPool.getDecoder());
    baseDecoder.decodeFrames(inputFields, startIndex, endIndex, componentFrames);
}
