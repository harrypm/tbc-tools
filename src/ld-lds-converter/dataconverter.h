/************************************************************************

    dataconverter.h

    ld-lds-converter - 10-bit to 16-bit .lds converter for ld-decode
    Copyright (C) 2019 Simon Inns

    This file is part of tbc-tools.

    ld-lds-converter is free software: you can redistribute it and/or
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

#ifndef DATACONVERTER_H
#define DATACONVERTER_H

#include <QObject>
#include <QDebug>
#include <QFile>
#include <FLAC/stream_encoder.h>
#include <atomic>
#include <cstdio>

class DataConverter : public QObject
{
    Q_OBJECT
public:
    enum class OutputFormat {
        Flac,
        FlacLdf,
        S16Raw,
        RiffWave
    };

    // Flac and FlacLdf both write real FLAC containers; FlacLdf only differs
    // in the on-disk extension (.ldf) used as a "LaserDisc FLAC" context label.
    static bool isFlacFamily(OutputFormat outputFormatParam)
    {
        return outputFormatParam == OutputFormat::Flac || outputFormatParam == OutputFormat::FlacLdf;
    }

    explicit DataConverter(QString inputFileNameParam,
                           QString outputFileNameParam,
                           bool isPackingParam,
                           OutputFormat outputFormatParam,
                           int flacSampleRateParam = 40000,
                           int flacCompressionLevelParam = 8,
                           bool verifyOutputEnabledParam = false,
                           QObject *parent = nullptr);
    bool process(void);
    void requestCancel();
    bool wasCancelled() const;
    static QString outputExtensionForFormat(OutputFormat outputFormatParam);
    static QString defaultOutputPath(const QString &inputFileNameParam, bool isPackingParam, OutputFormat outputFormatParam);

signals:
    void progressUpdated(qint64 processedBytes, qint64 totalBytes);

public slots:

private:
    QString inputFileName;
    QString outputFileName;
    bool isPacking;
    OutputFormat outputFormat;
    int flacSampleRate;
    int flacCompressionLevel;

    QFile *inputFileHandle;
    QFile *outputFileHandle;
    FLAC__StreamEncoder *flacEncoder;
    qint64 totalInputBytes;
    qint64 processedInputBytes;
    qint64 nextOutputFlushInputBytes;
    bool verifyOutputEnabled;
    std::atomic_bool cancelRequested;
    std::atomic_bool conversionCancelled;
    FILE *flacOutputFileHandle;
    bool closeFlacOutputFileHandle;

    // Private methods
    bool openInputFile(void);
    void closeInputFile(void);
    bool openOutputFile(void);
    void closeOutputFile(void);
    bool flushOutputBuffersIfNeeded(bool forceFlush = false);
    bool verifyOutputFile(void) const;
    bool openFlacEncoder(void);
    bool writeRiffHeader(void);
    bool writeUnpackedSamples(const qint16 *samples, qint32 sampleCount);
    bool packFile(void);
    bool unpackFile(void);
};

#endif // DATACONVERTER_H
