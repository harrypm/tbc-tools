/************************************************************************

    secamdecoder.h

    tbc-chroma-decoder - Colourisation filter for the tbc format.

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

#ifndef SECAMDECODER_H
#define SECAMDECODER_H

#include <QObject>
#include <QAtomicInt>
#include <QThread>
#include <QDebug>

#include <complex>
#include <vector>

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
// This decoder therefore does the FM demodulation itself: it quadrature
// demodulates each line, reads the D'R/D'B line identity and the discriminator
// zero from the blanking-interval rest carrier vhs-decode regenerates, undoes
// the SECAM LF pre-emphasis, and reconstructs simultaneous D'R/D'B by one-line
// hold. The result is written into ComponentFrame's U/V pre-scaled by
// OutputWriter's existing RGB48/YUV444P16 matrix constants, so the standard
// Y'UV->R'G'B' conversion reproduces SECAM's own inverse matrix without any
// change to OutputWriter.
class SecamDecoder : public Decoder {
public:

    struct SecamConfiguration {
        double chromaGain = 1.0;
        TbcMetaData::VideoParameters videoParameters;
    };

    SecamDecoder();
    SecamDecoder(const SecamDecoder::SecamConfiguration &config);
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
    // Per-call working buffers for one field's demodulation.
    struct FieldWork {
        std::vector<double> frequency;   // instantaneous frequency, Hz
        std::vector<double> envelope;    // carrier envelope
        std::vector<double> dr;          // (R'-Y'), filled to every line
        std::vector<double> db;          // (B'-Y'), filled to every line
        std::vector<bool> lineIsRed;
        std::vector<double> scratch;

        void resize(qint32 fieldHeight, qint32 fieldWidth);
    };

    /// Quadrature demodulate one field into work.frequency/work.envelope
    void demodulateField(const SourceVideo::Data &data, FieldWork &work) const;

    /// Demodulate a field and reconstruct simultaneous D'R/D'B in work.dr/work.db
    void decodeField(const SourceVideo::Data &data, FieldWork &work) const;

    /// Windowed-sinc lowpass used by the quadrature demodulator
    std::vector<double> demodTaps;

    // Rest-carrier measurement window inside the regenerated blanking interval
    qint32 restStart = 0;
    qint32 restEnd = 0;

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
