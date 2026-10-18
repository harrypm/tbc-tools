/************************************************************************

    secamdecoder.h

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

#ifndef SECAMDECODER_H
#define SECAMDECODER_H

#include <QObject>
#include <QAtomicInt>
#include <QThread>
#include <QDebug>

#include <complex>
#include <vector>

#include <fftw3.h>

#include "componentframe.h"
#include "tbcmetadata.h"
#include "sourcevideo.h"

#include "decoder.h"
#include "sourcefield.h"

class DecoderPool;

// Decoder for SECAM sources whose chroma TBC carries a restored studio SECAM
// FM chroma block, as produced by vhs-decode's --system SECAM (method 1, the
// standard divide-by-4 recording used in France) and --system MESECAM paths.
// Both restore the block to the same studio rest carriers, foB = 4.25 MHz and
// foR = 4.40625 MHz.
//
// The demodulation pipeline is the one vapoursynth-analog's SECAM decoder
// uses (libchromadec v0.1.0, chd::decoders::secam::SecamDecoder): a
// block-FFT analytic signal per row with the receiver's HF "bell" (cloche)
// network (the closed-form inverse of the encoder's anti-bell
// pre-correction) applied in the frequency domain, a designed
// differentiating-FIR discriminator on the baseband-mixed analytic signal
// (the exact phase derivative Im(conj(a) * a') / |a|^2), per-field carrier
// calibration and D'R/D'B line identification from the back-porch reference
// carriers (measured on a bell-free band response so the bell's noise
// shaping cannot bias the carrier estimates), mask recentring when the
// measured FM block sits away from the nominal carriers (VHS colour-under
// converters translate the whole block, anti-bell included -- MESECAM's
// ~+90 kHz offset), the exact inverse of the LF pre-correction, and
// colour-difference scaling to the ComponentFrame's composite-domain U/V.
// FM click ("SECAM fire") concealment and the deviation rail run on the raw
// per-sample deviation in D'B/D'R units before de-emphasis.
//
// Integration contract kept from the previous tbc-tools decoder (see the
// 2026-09-13 dev note + the pre-port restore point in development-logs):
// SecamConfiguration's fields and all GUI/CLI wiring, ComponentFrame U/V
// pre-scaled by OutputWriter's fixed matrix constants, the one-line hold
// fill to a full U/V raster, the post-demodulation hue rotation, the colour
// killer, V-interval neutralization below nominalFirstActiveFieldLine, and
// the SECAMCFG/SECAMPROBE/SECAMDIAG stderr diagnostics.
class SecamDecoder : public Decoder {
public:

    struct SecamConfiguration {
        double chromaGain = 1.0;
        double chromaPhase = 0.0;  // hue rotation of (D'B, D'R) in degrees
        // FM click ("SECAM fire") concealment level. 0 bypasses the stage;
        // 1.0 is the default. Higher = more aggressive detection.
        double clickNrLevel = 1.0;
        // Original first active field line before any full-frame widening.
        // Rows below this are V-interval (bottles/test lines) and must
        // render neutral in full-frame mode even though they carry carrier;
        // -1 means use videoParameters.firstActiveFieldLine as-is.
        qint32 nominalFirstActiveFieldLine = -1;
        TbcMetaData::VideoParameters videoParameters;
    };

    SecamDecoder();
    SecamDecoder(const SecamDecoder::SecamConfiguration &config);
    ~SecamDecoder() override;
    bool updateConfiguration(const TbcMetaData::VideoParameters &videoParameters,
                             const SecamDecoder::SecamConfiguration &configuration);
    bool configure(const TbcMetaData::VideoParameters &videoParameters) override;
    QThread *makeThread(QAtomicInt& abort, DecoderPool& decoderPool) override;

    /// Synchronously decode SECAM chroma fields into component frames
    void decodeFrames(const QVector<SourceField>& inputFields,
                    qint32 startIndex,
                    qint32 endIndex,
                    QVector<ComponentFrame>& componentFrames);

private:
    // Per-call working buffers for one field's demodulation. Everything the
    // libchromadec pipeline mutates per field lives here, because the
    // DecoderPool runs concurrent SecamThreads that all call the shared
    // decoder's decodeFrames -- the decoder object itself stays immutable
    // (plans/taps/masks built once in configure) so no locking is needed.
    //
    // The `Ref` pair is the bell-free band path feeding carrier calibration
    // and line identification; the bell-filtered pair feeds the picture
    // demodulation (libchromadec keeps the bell's noise shaping out of the
    // porch estimates, which would otherwise bias the carrier medians).
    struct FieldWork {
        // FFT scratch (fftw-aligned), sized blockSize
        fftw_complex *fftIn = nullptr;
        fftw_complex *fftFreq = nullptr;
        fftw_complex *fftWork = nullptr;
        fftw_complex *fftOut = nullptr;

        std::vector<std::complex<double>> analytic;    // bell+band, mixed down
        std::vector<std::complex<double>> analyticRef; // band only, mixed down
        std::vector<double> fInst;                     // Hz, bell path
        std::vector<double> fInstRef;                  // Hz, bell-free path
        std::vector<double> demod;      // deviation in D'B/D'R units, pre-concealment
        std::vector<double> deemph;     // after the exact LF de-emphasis
        std::vector<double> lumaFiltered; // composite low-passed below the FM block (combined input)
        std::vector<double> dr;         // (R'-Y'), filled to every line
        std::vector<double> db;         // (B'-Y'), filled to every line
        std::vector<bool> lineIsRed;

        // Frequency-domain masks (one value per FFT bin), built for the
        // current carrierOffset and rebuilt when calibration recentres
        std::vector<std::complex<double>> maskChroma;  // analytic x band x bell
        std::vector<double> maskBand;                  // analytic x band (bell-free)
        std::vector<double> maskLuma;                  // low-pass below the block (combined luma)
        double carrierOffset = 0.0;                    // Hz from nominal block centre
        double mixFrequency = 0.0;                     // baseband mix, follows the offset

        // Per-field carrier calibration + line identification
        std::vector<double> porchFreq;     // median discriminated porch Hz per row
        std::vector<double> porchEnvelope; // mean analytic-ref envelope per row
        std::vector<bool> rowValid;        // porch envelope over the validity gate
        double fob = 0.0;                  // measured (or fallback) Db carrier
        double for_ = 0.0;                 // measured (or fallback) Dr carrier
        bool calibrated = false;
        double lastGoodFob = 0.0;
        double lastGoodFor = 0.0;
        bool haveLastGoodCalibration = false;

        // Input type (detected once per call from sync-tip presence): a
        // combined luma+chroma TBC uses the narrow block-centred demod band;
        // a split chroma TBC uses libchromadec's wide band
        bool combinedInput = false;
        bool inputChecked = false;

        // TEMP diagnostic (SECAMDIAG)
        qint32 parityEvenIsRed = 0;
        qint32 parityPicture = 0;
        bool parityDrOnEven = false;
        double parityConfidence = 0.0;
        double noiseMadHz = 0.0;
        qint32 identSource = 0;  // 0 = porch fit, 1 = content-statistics fallback

        void init(qint32 blockSize, qint32 fieldHeight, qint32 fieldWidth);
        ~FieldWork();
        FieldWork() = default;
        FieldWork(const FieldWork &) = delete;
        FieldWork &operator=(const FieldWork &) = delete;
    };

    /// FFT one row into the bell+band and bell-free mixed-down analytic signals
    void filterRowAnalytic(const quint16 *fieldData, qint32 numSamples, qint32 row,
                           FieldWork &work) const;

    /// FFT one row and notch the FM block band out of it (combined-input luma:
    /// the composite with the subcarrier block removed, so no luma dots)
    void filterRowLuma(const quint16 *fieldData, qint32 numSamples, qint32 row,
                       FieldWork &work) const;

    /// Instantaneous frequency (Hz) over one row of an analytic signal via
    /// the differentiating FIR on the mixed-down analytic signal
    void discriminateRow(const std::complex<double> *analyticData, qint32 numSamples,
                         qint32 row, double *fInstRow, FieldWork &work) const;

    /// Apply the exact LF de-emphasis mask to one row of the deviation signal
    void filterRowDeemphasis(FieldWork &work, qint32 numSamples, qint32 row) const;

    /// (Re)build maskChroma/maskBand and mixFrequency for work.carrierOffset
    void buildChromaMasks(FieldWork &work) const;

    /// Demodulate a field and reconstruct simultaneous D'R/D'B in work.dr/work.db
    void decodeField(const SourceVideo::Data &data, FieldWork &work) const;

    // Immutable after configure() (shared read-only by all decoder threads)
    qint32 blockSize = 0;      // power of two >= fieldWidth + 512
    qint32 fftMargin = 0;      // (blockSize - fieldWidth) / 2 context samples
    double sampleRate = 0.0;
    fftw_plan forwardPlan = nullptr;
    fftw_plan inversePlan = nullptr;
    fftw_complex *planScratchIn = nullptr;   // plan-time arrays only
    fftw_complex *planScratchOut = nullptr;
    std::vector<double> diffTaps;            // maximally-flat central difference
    std::vector<std::complex<double>> maskDeemphasis; // exact LF inverse, offset-independent
    qint32 porchStart = 0;                   // back-porch measurement window
    qint32 porchEnd = 0;

    void freeFftwState();

    SecamConfiguration secamConfig;
};

class SecamThread : public DecoderThread
{
    Q_OBJECT
public:
    explicit SecamThread(QAtomicInt &abort, DecoderPool &decoderPool,
                       const SecamDecoder::SecamConfiguration &secamConfig,
                       QObject *parent = nullptr);

protected:
    void decodeFrames(const QVector<SourceField> &inputFields, qint32 startIndex, qint32 endIndex,
                      QVector<ComponentFrame> &componentFrames) override;

private:
    // Settings
    const SecamDecoder::SecamConfiguration &secamConfig;
};

#endif // SECAMDECODER_H
