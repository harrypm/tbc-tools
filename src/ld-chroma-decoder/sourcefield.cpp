/************************************************************************

    sourcefield.cpp

    ld-chroma-decoder - Colourisation filter for ld-decode
    Copyright (C) 2019 Adam Sampson

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

#include "sourcefield.h"

#include "sourcevideo.h"

void SourceField::loadFields(SourceVideo &sourceVideo, TbcMetaData &metaData,
                             qint32 firstFrameNumber, qint32 numFrames,
                             qint32 lookBehindFrames, qint32 lookAheadFrames,
                             QVector<SourceField> &fields, qint32 &startIndex, qint32 &endIndex)
{
    const TbcMetaData::VideoParameters &videoParameters = metaData.getVideoParameters();

    // Work out indexes.
    // fields will contain {lookbehind fields... [startIndex] real fields... [endIndex] lookahead fields...}.
    startIndex = 2 * lookBehindFrames;
    endIndex = startIndex + (2 * numFrames);
    fields.resize(endIndex + (2 * lookAheadFrames));

    // Populate fields
    const qint32 numInputFrames = metaData.getNumberOfFrames();
    const qint32 numMetadataFields = metaData.getNumberOfFields();
    const qint32 numSourceFields = sourceVideo.getNumberOfAvailableFields();
    bool warnedMetadataFieldRange = false;
    bool warnedSourceFieldRange = false;
    qint32 frameNumber = firstFrameNumber - lookBehindFrames;
    for (qint32 i = 0; i < fields.size(); i += 2) {

        // Is this frame outside the bounds of the input file?
        // If so, use real metadata (from frame 1) and black fields.
        bool useBlankFrame = frameNumber < 1 || frameNumber > numInputFrames;

        // Resolve field numbers for this frame.
        qint32 metadataFrame = useBlankFrame ? 1 : frameNumber;
        qint32 firstFieldNumber = metaData.getFirstFieldNumber(metadataFrame);
        qint32 secondFieldNumber = metaData.getSecondFieldNumber(metadataFrame);

        auto fieldNumbersAreMetadataSafe = [numMetadataFields](qint32 firstField, qint32 secondField) {
            return firstField >= 1 && secondField >= 1
                   && firstField <= numMetadataFields && secondField <= numMetadataFields;
        };

        bool metadataFieldRangeInvalid = !fieldNumbersAreMetadataSafe(firstFieldNumber, secondFieldNumber);
        bool sourceFieldRangeInvalid = numSourceFields != -1
                                       && (firstFieldNumber > numSourceFields || secondFieldNumber > numSourceFields);

        // Metadata and source files can become mismatched (for example if the source
        // TBC is truncated while metadata still references more fields).  In that case,
        // fall back to blank frames instead of requesting out-of-range field data.
        if (!useBlankFrame && (metadataFieldRangeInvalid || sourceFieldRangeInvalid)) {
            if (metadataFieldRangeInvalid && !warnedMetadataFieldRange) {
                qWarning() << "SourceField::loadFields(): Metadata field range invalid for frame"
                           << frameNumber << "(first=" << firstFieldNumber
                           << "second=" << secondFieldNumber
                           << "metadata fields=" << numMetadataFields << "). Using blank fallback frames.";
                warnedMetadataFieldRange = true;
            }

            if (sourceFieldRangeInvalid && !warnedSourceFieldRange) {
                qWarning() << "SourceField::loadFields(): Source file has fewer fields than metadata for frame"
                           << frameNumber << "(first=" << firstFieldNumber
                           << "second=" << secondFieldNumber
                           << "source fields=" << numSourceFields
                           << "). Source file appears damaged or truncated; using blank fallback frames.";
                warnedSourceFieldRange = true;
            }

            useBlankFrame = true;
            metadataFrame = 1;
            firstFieldNumber = metaData.getFirstFieldNumber(metadataFrame);
            secondFieldNumber = metaData.getSecondFieldNumber(metadataFrame);
            metadataFieldRangeInvalid = !fieldNumbersAreMetadataSafe(firstFieldNumber, secondFieldNumber);
        }

        if (!metadataFieldRangeInvalid) {
            fields[i].field = metaData.getField(firstFieldNumber);
            fields[i + 1].field = metaData.getField(secondFieldNumber);
        } else {
            // If metadata is irrecoverably inconsistent, synthesize minimal field
            // metadata and continue with blank picture data.
            fields[i].field = TbcMetaData::Field();
            fields[i + 1].field = TbcMetaData::Field();
            fields[i].field.isFirstField = true;
            fields[i + 1].field.isFirstField = false;
            useBlankFrame = true;
        }

        const quint16 black = videoParameters.black16bIre;

        if (useBlankFrame) {
            // Fill both fields with black
            fields[i].data.fill(black, sourceVideo.getFieldLength());
            fields[i + 1].data.fill(black, sourceVideo.getFieldLength());
        } else {
            // Fetch the input fields
            fields[i].data = sourceVideo.getVideoField(firstFieldNumber);
            fields[i + 1].data = sourceVideo.getVideoField(secondFieldNumber);

            if ((videoParameters.system == PAL || videoParameters.system == PAL_M) && videoParameters.isSubcarrierLocked) {
                // With subcarrier-locked 4fSC PAL sampling, we have four
                // "extra" samples over the course of the frame, so the two
                // fields will be horizontally misaligned by two samples. Shift
                // the second field to the left to compensate.
                //
                // XXX This should be done elsewhere, as it affects other tools
                // too.

                fields[i + 1].data.remove(0, 2);
                for (int j = 0; j < 2; j++) {
                    fields[i + 1].data.append(black);
                }
            }
        }

        frameNumber++;
    }
}
