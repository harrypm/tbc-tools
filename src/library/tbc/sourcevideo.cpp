/************************************************************************

    sourcevideo.cpp

    tbc-tools TBC library
    Copyright (C) 2018-2019 Simon Inns

    This file is part of tbc-tools.

    tbc-tools is free software: you can redistribute it and/or
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

#include "sourcevideo.h"

#include <cstdio>
#include <cstring>
#include <QDir>
#include <QLockFile>
#include <QRegularExpression>
#include <QStorageInfo>
#include "tbc/logging.h"

// Chunked reads: each block is this large, so a disk shared with another
// reader spends its time reading rather than seeking between the files
static constexpr qint64 kChunkBytes = 64LL * 1024 * 1024;

// Class constructor
SourceVideo::SourceVideo()
{
    // Default object settings
    isSourceVideoOpen = false;
    inputFilePos = -1;
    availableFields = -1;
    fieldLength = -1;
    fieldByteLength = -1;
    fieldLineLength = -1;

    // Set up the cache
    fieldCache.setMaxCost(100);
}

SourceVideo::~SourceVideo()
{
    if (isSourceVideoOpen) inputFile.close();
}

// Source Video file manipulation methods -----------------------------------------------------------------------------

// Open an input video data file. If filename is "-", read from stdin.
// Returns true on success.
bool SourceVideo::open(QString filename, qint32 _fieldLength, qint32 _fieldLineLength)
{
    fieldLength = _fieldLength;
    fieldByteLength = _fieldLength * 2;
    if (_fieldLineLength != -1) {
        fieldLineLength = _fieldLineLength * 2;
    } else fieldLineLength = -1;
    tbcDebugStream() << "SourceVideo::open(): Called with field byte length =" << fieldByteLength;

    if (isSourceVideoOpen) {
        // Video file is already open, close it
        qInfo() << "A source video input file is already open, cannot open a new one";
        return false;
    }

    // Open the source video file
    inputFile.setFileName(filename);
    if (filename == "-") {
        if (!inputFile.open(stdin, QIODevice::ReadOnly)) {
            // Failed to open stdin
            qWarning() << "Could not open stdin as source video input file";
            return false;
        }

        // When reading from stdin, we don't know how long the input will be
        availableFields = -1;
    } else {
        if (!inputFile.open(QIODevice::ReadOnly)) {
            // Failed to open named input file
            qWarning() << "Could not open" << filename << "as source video input file";
            return false;
        }

        // File open successful - configure source video parameters
        qint64 tAvailableFields = (inputFile.size() / fieldByteLength);
        availableFields = static_cast<qint32>(tAvailableFields);
        tbcDebugStream() << "SourceVideo::open(): Successful -" << availableFields << "fields available";
    }

    // Initialise cache
    fieldCache.clear();

    isSourceVideoOpen = true;
    inputFilePos = 0;

    return true;
}

// Close an input video data file
void SourceVideo::close()
{
    if (!isSourceVideoOpen) {
        tbcDebugStream() << "SourceVideo::close(): Called but no source video input file is open";
        return;
    }

    tbcDebugStream() << "SourceVideo::close(): Called, closing the source video file and emptying the frame cache";
    inputFile.close();
    isSourceVideoOpen = false;
    inputFilePos = -1;

    tbcDebugStream() << "SourceVideo::close(): Source video input file closed";
}

// Get the validity of the source video file
bool SourceVideo::isSourceValid()
{
    return isSourceVideoOpen;
}

// Get the number of fields available from the source video file.
// Returns -1 if the length is unknown (e.g. we're reading from stdin).
qint32 SourceVideo::getNumberOfAvailableFields()
{
    return availableFields;
}

// Get the number of samples in a field
qint32 SourceVideo::getFieldLength()
{
    return fieldLength;
}

// Frame data retrieval methods ---------------------------------------------------------------------------------------

// Method to retrieve a range of field lines from a single video field.
// If startFieldLine and endFieldLine are both -1, read the whole field.
void SourceVideo::setChunkedReads(bool enabled)
{
    // Only for files: a pipe is already read in order by one process
    chunkedReads = enabled && isSourceVideoOpen && availableFields != -1;
    chunkBuffer.clear();
    chunkStart = -1;
    if (!chunkedReads) return;

    // One lock per disk, so readers on different disks don't wait for each other
    const QStorageInfo storage(inputFile.fileName());
    QString device = QString::fromLocal8Bit(storage.isValid() ? storage.device() : QByteArray("tbc-tools"));
    device.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_.-]")), QStringLiteral("_"));
    chunkLockPath = QDir(QDir::tempPath()).filePath(QStringLiteral("tbc-tools-read-%1.lock").arg(device));
}

// Read the block starting at |position|. On a shared disk (a separate luma and
// chroma TBC read by two processes at once) interleaved small reads make the
// heads seek between the files - on NTFS via ntfs-3g a pair of readers fell
// from 245 MB/s to ~50 MB/s - so blocks are read whole, one process at a time.
bool SourceVideo::readChunk(qint64 position)
{
    const qint64 length = qMin(kChunkBytes, inputFile.size() - position);
    if (length <= 0) return false;
    chunkBuffer.resize(length);

    QLockFile lock(chunkLockPath);
    const bool locked = lock.lock();
    qint64 total = 0;
    if (inputFile.seek(position)) {
        while (total < length) {
            const qint64 received = inputFile.read(chunkBuffer.data() + total, length - total);
            if (received <= 0) break;
            total += received;
        }
    }
    if (locked) lock.unlock();

    inputFilePos = position + total;
    if (total != length) {
        chunkBuffer.clear();
        chunkStart = -1;
        return false;
    }
    chunkStart = position;
    return true;
}

void SourceVideo::setStreamStartField(qint32 fieldNumber)
{
    // The stream's first byte is that field's first byte, so reads from it
    // need no seek and earlier fields can't be read
    inputFilePos = static_cast<qint64>(fieldByteLength) * static_cast<qint64>(qMax(1, fieldNumber) - 1);
}

SourceVideo::Data SourceVideo::getVideoField(qint32 fieldNumber, qint32 startFieldLine, qint32 endFieldLine)
{
    // Adjust the field number to index from zero
    fieldNumber--;

    // Ensure source video is open
    if (!isSourceVideoOpen) qFatal("Application requested TBC field before opening TBC file - Fatal error");

    // Calculate the position of the require field line data
    qint64 requiredStartPosition = static_cast<qint64>(fieldByteLength) * static_cast<qint64>(fieldNumber);
    qint64 requiredReadLength;

    if (startFieldLine == -1 && endFieldLine == -1) {
        // Read the whole field

        // Check the cache (we only cache whole fields)
        if (fieldCache.contains(fieldNumber)) {
            return *fieldCache.object(fieldNumber);
        }

        requiredReadLength = static_cast<qint64>(fieldByteLength);
    } else {
        // Read a range of lines

        // Adjust the field line range to index from zero
        startFieldLine--;
        endFieldLine--;

        // Verify the required range
        if (fieldLineLength == -1) qFatal("Application did not set field line length when opening TBC file");
        if (startFieldLine < 0) qFatal("Application requested out-of-bounds field line");

        requiredStartPosition += static_cast<qint64>(fieldLineLength) * static_cast<qint64>(startFieldLine);
        requiredReadLength = static_cast<qint64>(endFieldLine - startFieldLine + 1) * static_cast<qint64>(fieldLineLength);
    }

    // Check the requested field and lines are valid
    if (availableFields != -1
        && (requiredStartPosition < 0
            || requiredStartPosition + requiredReadLength > (static_cast<qint64>(fieldByteLength) * availableFields))) {
        qFatal("Application requested field line range that exceeds the boundaries of the input TBC file");
    }

    // Resize the output buffer
    outputFieldData.resize(static_cast<qint32>(requiredReadLength) / 2);

    // Chunked reads: serve the data from the current block, reading the block
    // that starts here when it isn't in memory
    if (chunkedReads) {
        const bool inChunk = chunkStart >= 0 && requiredStartPosition >= chunkStart
                             && requiredStartPosition + requiredReadLength <= chunkStart + chunkBuffer.size();
        if (inChunk || readChunk(requiredStartPosition)) {
            std::memcpy(outputFieldData.data(), chunkBuffer.constData() + (requiredStartPosition - chunkStart),
                        static_cast<size_t>(requiredReadLength));
            if (startFieldLine == -1 && endFieldLine == -1) {
                fieldCache.insert(fieldNumber, new Data(outputFieldData), 1);
            }
            return outputFieldData;
        }
        // Fall through to a plain read if the block couldn't be read
    }

    // Seek to the correct file position (if not already there)
    if (inputFilePos != requiredStartPosition) {
        if (!inputFile.seek(requiredStartPosition)) {
            // Seek failed

            if (inputFilePos > requiredStartPosition) {
                qFatal("Could not seek backwards to required field position in input TBC file");
            } else {
                // Seeking forwards -- try reading and discarding data instead
                qint64 discardBytes = requiredStartPosition - inputFilePos;
                while (discardBytes > 0) {
                    qint64 readBytes = inputFile.read(reinterpret_cast<char *>(outputFieldData.data()),
                                                      qMin(discardBytes, static_cast<qint64>(outputFieldData.size() * 2)));
                    if (readBytes <= 0) {
                        qFatal("Could not seek or read forwards to required field position in input TBC file");
                    }
                    discardBytes -= readBytes;
                }
            }
        }
        inputFilePos = requiredStartPosition;
    }

    // Read the field lines from the input
    qint64 totalReceivedBytes = 0;
    qint64 receivedBytes = 0;
    do {
        receivedBytes = inputFile.read(reinterpret_cast<char *>(outputFieldData.data()) + totalReceivedBytes,
                                       requiredReadLength - totalReceivedBytes);
        if (receivedBytes > 0) {
            totalReceivedBytes += receivedBytes;
            inputFilePos += receivedBytes;
        }
    } while (receivedBytes > 0 && totalReceivedBytes < requiredReadLength);

    // Verify read was ok
    if (totalReceivedBytes != requiredReadLength) qFatal("Could not read field data from input TBC file");

    if (startFieldLine == -1 && endFieldLine == -1) {
        // Insert the field data into the cache
        fieldCache.insert(fieldNumber, new Data(outputFieldData), 1);
    }

    // Return the data
    return outputFieldData;
}













