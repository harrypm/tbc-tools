/************************************************************************

    closedcaption.cpp

    ld-process-vbi - VBI and IEC NTSC specific processor for ld-decode
    Copyright (C) 2018-2019 Simon Inns
    Copyright (C) 2023 Adam Sampson

    This file is part of tbc-tools.

    ld-process-vbi is free software: you can redistribute it and/or
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

#include "closedcaption.h"
#include "tbc/logging.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

/*!
    \class ClosedCaption

    Decoder for EIA/CEA-608 data lines, widely used for closed
    captioning in NTSC, and occasionally in other standards.

    References:

    [CTA] "Line 21 Data Services", (https://shop.cta.tech/products/line-21-data-services)
    ANSI/CTA-608-E S-2019, April 2008.

    The decode/synchronization approach is adapted from eshaz/cc_decoder:
    https://github.com/eshaz/cc_decoder
*/

namespace {
constexpr double ccClockRate = 32.0;
constexpr double runInBits = 6.5;
constexpr qint32 startBitCount = 3;      // 001
constexpr qint32 dataBitsPerByte = 7;
constexpr qint32 bitsPerByte = 8;        // 7 payload + parity
constexpr qint32 payloadBits = 16;       // 2 bytes
constexpr double bitPaddingFraction = 0.10;
constexpr double minStdDevForCorrection = 0.30;

struct BitMeasurement {
    bool valid = false;
    bool value = false;
    double mean = 0.0;
    double stdDev = 0.0;
};

struct DecodedByte {
    qint32 value = -1;
    bool parityOk = false;
};

struct CandidateDecode {
    bool valid = false;
    qint32 startBitSample = 0;
    double threshold = 0.0;
    double score = std::numeric_limits<double>::lowest();
    qint32 parityValidCount = -1;
    DecodedByte firstByte;
    DecodedByte secondByte;
};

double rangeMean(const QVector<double> &cumulative, qint32 start, qint32 end)
{
    if (end <= start) {
        return 0.0;
    }
    return (cumulative[end] - cumulative[start]) / static_cast<double>(end - start);
}

double rangeStdDev(const QVector<double> &cumulative,
                   const QVector<double> &cumulativeSquares,
                   qint32 start, qint32 end)
{
    if (end <= start) {
        return 0.0;
    }

    const double mean = rangeMean(cumulative, start, end);
    const double meanSquares = (cumulativeSquares[end] - cumulativeSquares[start])
        / static_cast<double>(end - start);
    const double variance = qMax(0.0, meanSquares - (mean * mean));
    return std::sqrt(variance);
}

BitMeasurement measureBit(const QVector<double> &normalizedLine,
                          const QVector<double> &cumulative,
                          const QVector<double> &cumulativeSquares,
                          qint32 bitStartSample,
                          double samplesPerBit,
                          qint32 bitPadding,
                          qint32 bitIndex,
                          double threshold)
{
    const double bitStart = static_cast<double>(bitStartSample) + (static_cast<double>(bitIndex) * samplesPerBit);
    qint32 start = static_cast<qint32>(std::lround(bitStart)) + bitPadding;
    qint32 end = static_cast<qint32>(std::lround(bitStart + samplesPerBit)) - bitPadding;
    start = qBound(0, start, normalizedLine.size());
    end = qBound(0, end, normalizedLine.size());

    BitMeasurement measurement;
    if (end <= start) {
        return measurement;
    }

    measurement.mean = rangeMean(cumulative, start, end);
    measurement.stdDev = rangeStdDev(cumulative, cumulativeSquares, start, end);
    measurement.value = (measurement.mean > threshold);
    measurement.valid = true;
    return measurement;
}

DecodedByte decodeByte(const QVector<double> &normalizedLine,
                       const QVector<double> &cumulative,
                       const QVector<double> &cumulativeSquares,
                       qint32 bitStartSample,
                       double samplesPerBit,
                       qint32 bitPadding,
                       qint32 firstBitIndex,
                       double threshold)
{
    DecodedByte output;

    std::array<bool, dataBitsPerByte> bits {};
    qint32 worstErrorIndex = -1;
    double worstError = 0.0;
    qint32 errorCount = 0;
    qint32 parityCalculated = 1; // odd parity

    for (qint32 bit = 0; bit < dataBitsPerByte; bit++) {
        const BitMeasurement measurement = measureBit(normalizedLine, cumulative, cumulativeSquares,
                                                      bitStartSample, samplesPerBit, bitPadding,
                                                      firstBitIndex + bit, threshold);
        if (!measurement.valid) {
            return output;
        }

        bits[bit] = measurement.value;
        if (measurement.value) {
            parityCalculated++;
        }

        if (measurement.stdDev > minStdDevForCorrection) {
            errorCount++;
            if (measurement.stdDev > worstError) {
                worstError = measurement.stdDev;
                worstErrorIndex = bit;
            }
        }
    }
    parityCalculated %= 2;

    const BitMeasurement parityMeasurement = measureBit(normalizedLine, cumulative, cumulativeSquares,
                                                        bitStartSample, samplesPerBit, bitPadding,
                                                        firstBitIndex + dataBitsPerByte, threshold);
    if (!parityMeasurement.valid) {
        return output;
    }
    qint32 parityBit = parityMeasurement.value ? 1 : 0;

    // Correct a single likely data-bit error when parity indicates one.
    if (errorCount == 1
        && worstErrorIndex >= 0
        && parityBit != parityCalculated
        && parityMeasurement.stdDev < minStdDevForCorrection) {
        bits[worstErrorIndex] = !bits[worstErrorIndex];
        parityCalculated = parityBit;
    }

    qint32 byteValue = 0;
    for (qint32 bit = 0; bit < dataBitsPerByte; bit++) {
        if (bits[bit]) {
            byteValue |= (1 << bit);
        }
    }

    output.value = byteValue;
    output.parityOk = (parityBit == parityCalculated);
    return output;
}

CandidateDecode decodeAtStartBit(const QVector<double> &normalizedLine,
                                 const QVector<double> &cumulative,
                                 const QVector<double> &cumulativeSquares,
                                 qint32 startBitSample,
                                 double samplesPerBit,
                                 qint32 bitPadding)
{
    CandidateDecode candidate;
    candidate.startBitSample = startBitSample;

    const qint32 runInSamples = static_cast<qint32>(std::lround(runInBits * samplesPerBit));
    const qint32 preambleStart = startBitSample - runInSamples;
    if (preambleStart < 0) {
        return candidate;
    }

    candidate.threshold = rangeMean(cumulative, preambleStart, startBitSample);
    const double runInStdDev = rangeStdDev(cumulative, cumulativeSquares, preambleStart, startBitSample);

    const BitMeasurement startBit0 = measureBit(normalizedLine, cumulative, cumulativeSquares,
                                                startBitSample, samplesPerBit, bitPadding, 0,
                                                candidate.threshold);
    const BitMeasurement startBit1 = measureBit(normalizedLine, cumulative, cumulativeSquares,
                                                startBitSample, samplesPerBit, bitPadding, 1,
                                                candidate.threshold);
    const BitMeasurement startBit2 = measureBit(normalizedLine, cumulative, cumulativeSquares,
                                                startBitSample, samplesPerBit, bitPadding, 2,
                                                candidate.threshold);
    if (!startBit0.valid || !startBit1.valid || !startBit2.valid) {
        return candidate;
    }

    // Start bits must be 001.
    if (startBit0.value || startBit1.value || !startBit2.value) {
        return candidate;
    }

    // Prefer candidates where the start bits are well-separated from the local threshold.
    candidate.score = (candidate.threshold - startBit0.mean)
                    + (candidate.threshold - startBit1.mean)
                    + (startBit2.mean - candidate.threshold)
                    + runInStdDev;

    candidate.firstByte = decodeByte(normalizedLine, cumulative, cumulativeSquares,
                                     startBitSample, samplesPerBit, bitPadding,
                                     startBitCount, candidate.threshold);
    candidate.secondByte = decodeByte(normalizedLine, cumulative, cumulativeSquares,
                                      startBitSample, samplesPerBit, bitPadding,
                                      startBitCount + bitsPerByte, candidate.threshold);

    candidate.parityValidCount = (candidate.firstByte.parityOk ? 1 : 0)
                               + (candidate.secondByte.parityOk ? 1 : 0);
    candidate.valid = true;
    return candidate;
}
}

// Public method to read CEA-608 Closed Captioning data.
// Return true if CC data was decoded successfully, false otherwise.
bool ClosedCaption::decodeLine(const SourceVideo::Data& lineData,
                               const LdDecodeMetaData::VideoParameters& videoParameters,
                               LdDecodeMetaData::Field& fieldMetadata)
{
    // Reset data to invalid
    fieldMetadata.closedCaption.inUse = false;
    fieldMetadata.closedCaption.data0 = -1;
    fieldMetadata.closedCaption.data1 = -1;

    if (lineData.isEmpty()) {
        tbcDebugStream() << "ClosedCaption::decodeLine(): Empty line data";
        return false;
    }

    const auto [minimum, maximum] = std::minmax_element(lineData.cbegin(), lineData.cend());
    if (*minimum == *maximum) {
        tbcDebugStream() << "ClosedCaption::decodeLine(): Line has no dynamic range";
        return false;
    }

    QVector<double> normalizedLine;
    normalizedLine.reserve(lineData.size());
    const double scale = 1.0 / static_cast<double>(*maximum - *minimum);
    for (quint16 sample : lineData) {
        normalizedLine.append((static_cast<double>(sample) - static_cast<double>(*minimum)) * scale);
    }

    QVector<double> cumulative(normalizedLine.size() + 1, 0.0);
    QVector<double> cumulativeSquares(normalizedLine.size() + 1, 0.0);
    for (qint32 i = 0; i < normalizedLine.size(); i++) {
        const double value = normalizedLine[i];
        cumulative[i + 1] = cumulative[i] + value;
        cumulativeSquares[i + 1] = cumulativeSquares[i] + (value * value);
    }

    // Bit clock is 32 x fH [CTA p14, note 1]
    const double samplesPerBit = static_cast<double>(videoParameters.fieldWidth) / ccClockRate;
    if (samplesPerBit <= 0.0) {
        tbcDebugStream() << "ClosedCaption::decodeLine(): Invalid samples-per-bit";
        return false;
    }

    // Following the colourburst, the line starts with 2-7 cycles of sine wave
    // at the bit clock rate, then start bits 001, then 16 bits of data. [CTA p14]
    const qint32 bitPadding = qMax<qint32>(1, static_cast<qint32>(std::lround(bitPaddingFraction * samplesPerBit)));
    const qint32 minimumSamples = static_cast<qint32>(std::lround((startBitCount + payloadBits) * samplesPerBit));
    qint32 searchStart = videoParameters.colourBurstEnd + static_cast<qint32>(std::lround(2.0 * samplesPerBit));
    searchStart = qMax<qint32>(0, searchStart);
    const qint32 searchEnd = normalizedLine.size() - minimumSamples;

    CandidateDecode bestCandidate;
    for (qint32 startBitSample = searchStart; startBitSample <= searchEnd; startBitSample++) {
        const CandidateDecode candidate = decodeAtStartBit(normalizedLine, cumulative, cumulativeSquares,
                                                           startBitSample, samplesPerBit, bitPadding);
        if (!candidate.valid) {
            continue;
        }
        if ((candidate.parityValidCount > bestCandidate.parityValidCount)
            || (candidate.parityValidCount == bestCandidate.parityValidCount
                && candidate.score > bestCandidate.score)) {
            bestCandidate = candidate;
        }
    }

    if (!bestCandidate.valid) {
        tbcDebugStream() << "ClosedCaption::decodeLine(): No valid start bits found";
        return false;
    }

    tbcDebugStream() << "ClosedCaption::decodeLine(): Selected start bit at sample"
                     << bestCandidate.startBitSample << "score" << bestCandidate.score;
    tbcDebugStream().nospace() << "ClosedCaption::decodeLine(): Bytes are: "
                               << bestCandidate.firstByte.value << " ("
                               << (bestCandidate.firstByte.parityOk ? 1 : 0) << ") - "
                               << bestCandidate.secondByte.value << " ("
                               << (bestCandidate.secondByte.parityOk ? 1 : 0) << ")";

    if (!bestCandidate.firstByte.parityOk) {
        tbcDebugStream() << "ClosedCaption::decodeLine(): First byte failed parity check!";
    } else {
        fieldMetadata.closedCaption.data0 = bestCandidate.firstByte.value;
    }

    if (!bestCandidate.secondByte.parityOk) {
        tbcDebugStream() << "ClosedCaption::decodeLine(): Second byte failed parity check!";
    } else {
        fieldMetadata.closedCaption.data1 = bestCandidate.secondByte.value;
    }

    fieldMetadata.closedCaption.inUse = (fieldMetadata.closedCaption.data0 != -1)
        || (fieldMetadata.closedCaption.data1 != -1);
    return true;
}
