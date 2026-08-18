/************************************************************************

    ffmetadata.h

    tbc-export-metadata - Export ld-decode metadata into other formats
    Copyright (C) 2020 Adam Sampson

    This file is part of tbc-tools.

    tbc-export-metadata is free software: you can redistribute it and/or
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

#ifndef FFMETADATA_H
#define FFMETADATA_H

#include <QString>
#include <QtGlobal>

#include "lddecodemetadata.h"

/*!
    Write an FFMETADATA1 file containing navigation information.

    This is FFmpeg's generic metadata format, and can be used to provide
    metadata for chapter-supporting formats like Matroska.
    Format description: <https://ffmpeg.org/ffmpeg-formats.html#Metadata-1>

    @param startFrameOneBased Optional 1-based start frame for range export.
           Pass a value < 1 to export from the first frame.
    @param lengthFrames Optional frame length for range export.
           Pass a value < 1 to export to the end of the input.
    @param includeVitcTimecode When true, include FFmpeg-style VITC timecode
           in the output header when available.

    Returns true on success, false on failure.
*/
bool writeFfmetadata(LdDecodeMetaData &metaData,
                     const QString &fileName,
                     qint32 startFrameOneBased = -1,
                     qint32 lengthFrames = -1,
                     bool includeVitcTimecode = true);

#endif
