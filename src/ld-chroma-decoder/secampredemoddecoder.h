/************************************************************************

    secampredemoddecoder.h

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

#ifndef SECAMPREDEMODDECODER_H
#define SECAMPREDEMODDECODER_H

#include <QObject>
#include <QAtomicInt>
#include <QThread>
#include <QDebug>

#include <vector>

#include "componentframe.h"
#include "tbcmetadata.h"
#include "sourcevideo.h"

#include "decoder.h"
#include "sourcefield.h"

class DecoderPool;

// Decoder for SECAM sources whose chroma TBC already carries demodulated
// Dr/Db (one component per line, alternating), as produced by vhs-decode's
// native SECAM chroma path (vhsdecode/secam.py). Unlike SecamDecoder (which
// FM-demodulates a restored studio SECAM block itself), this decoder does no
// FM demodulation of its own -- it only reconstructs simultaneous Dr/Db pairs
// (one-line hold) and writes them pre-scaled into ComponentFrame's U/V so
// that OutputWriter's existing Y'UV->R'G'B' matrix reproduces the correct
// SECAM colour without any change to OutputWriter.
//
// Because there is no FM rest carrier to read, line identity (whether a given
// field's first active line carries D'R or D'B) is taken from the per-field
// metadata flag secamFirstLineIsRed, optionally overridden at decode time via
// firstLineIsRedOverride (-1 = use metadata).
class SecamPredemodDecoder : public Decoder {
public:

    struct SecamConfiguration {
        double chromaGain = 1.0;
        // -1: use per-field metadata secamFirstLineIsRed.
        //  1: force first line is red (D'R).  0: force first line is blue (D'B).
        qint8 firstLineIsRedOverride = -1;
        TbcMetaData::VideoParameters videoParameters;
    };

    SecamPredemodDecoder();
    SecamPredemodDecoder(const SecamPredemodDecoder::SecamConfiguration &config);
    bool updateConfiguration(const TbcMetaData::VideoParameters &videoParameters,
                             const SecamPredemodDecoder::SecamConfiguration &configuration);
    bool configure(const TbcMetaData::VideoParameters &videoParameters) override;
    QThread *makeThread(QAtomicInt& abort, DecoderPool& decoderPool) override;

    /// Synchronously decode SECAM pre-demod chroma fields into component frames
    void decodeFrames(const QVector<SourceField>& inputFields,
                    qint32 startIndex,
                    qint32 endIndex,
                    QVector<ComponentFrame>& componentFrames);

private:
    SecamConfiguration secamConfig;
};

class SecamPredemodThread : public DecoderThread
{
    Q_OBJECT
public:
    explicit SecamPredemodThread(QAtomicInt &abort, DecoderPool &decoderPool,
                       const SecamPredemodDecoder::SecamConfiguration &secamConfig,
                       QObject *parent = nullptr);

protected:
    void decodeFrames(const QVector<SourceField> &inputFields, qint32 startIndex, qint32 endIndex,
                      QVector<ComponentFrame> &componentFrames) override;

private:
    // Settings
    const SecamPredemodDecoder::SecamConfiguration &secamConfig;
};

#endif // SECAMPREDEMODDECODER_H
