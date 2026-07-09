/******************************************************************************
 * tbcsource.cpp
 * ld-analyse - TBC output analysis GUI
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2018-2025 Simon Inns
 * SPDX-FileCopyrightText: 2021-2022 Adam Sampson
 *
 * This file is part of ld-decode-tools.
 ******************************************************************************/

#include <array>
#include <cmath>
#include <limits>
#include <QDir>
#include <QDateTime>
#include <QFileInfo>
#include <QStringList>

#include "tbcsource.h"
#include "tbc/logging.h"
#include "yuvrangedialog.h"

#include "sourcefield.h"

namespace {
struct YuvRangeScale {
    double yScale = 0.0;
    double cbScale = 0.0;
    double crScale = 0.0;
    double blackLevel = 0.0;
    bool valid = false;
};

struct YuvCodeValues {
    double y = 0.0;
    double u = 128.0;
    double v = 128.0;
};

YuvRangeScale makeYuvRangeScale(const LdDecodeMetaData::VideoParameters &videoParameters)
{
    static constexpr double oneMinusKb = 1.0 - 0.114;
    static constexpr double oneMinusKr = 1.0 - 0.299;
    static constexpr double kb = 0.49211104112248356308804691718185;
    static constexpr double kr = 0.87728321993817866838972487283129;

    YuvRangeScale scale;
    const double signalRange = static_cast<double>(videoParameters.white16bIre - videoParameters.black16bIre);
    if (signalRange <= 0.0) {
        return scale;
    }

    scale.yScale = 219.0 / signalRange;
    scale.cbScale = (112.0 / (oneMinusKb * kb)) / signalRange;
    scale.crScale = (112.0 / (oneMinusKr * kr)) / signalRange;
    scale.blackLevel = static_cast<double>(videoParameters.black16bIre);
    scale.valid = true;
    return scale;
}

YuvCodeValues convertComponentSampleToYuvCodeValues(double y, double u, double v, const YuvRangeScale &scale)
{
    YuvCodeValues codeValues;
    codeValues.y = ((y - scale.blackLevel) * scale.yScale) + 16.0;
    codeValues.u = (u * scale.cbScale) + 128.0;
    codeValues.v = (v * scale.crScale) + 128.0;
    return codeValues;
}

bool yuvRangeSettingsMatch(const YuvRangeSettings &settings,
                           qint32 lumaMin,
                           qint32 lumaMax,
                           qint32 chromaMin,
                           qint32 chromaMax)
{
    return settings.lumaMin == lumaMin
           && settings.lumaMax == lumaMax
           && settings.chromaMin == chromaMin
           && settings.chromaMax == chromaMax;
}

QString backupFilenameWithTimestampFallback(const QString &inputMetadataFilename,
                                            const QString &backupSuffix)
{
    const QString defaultBackupFilename = inputMetadataFilename + backupSuffix;
    if (!QFileInfo::exists(defaultBackupFilename)) {
        return defaultBackupFilename;
    }

    const QString timestamp = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd_HHmmss"));
    QString backupFilename = inputMetadataFilename + QStringLiteral(".") + timestamp + backupSuffix;
    qint32 collisionCounter = 1;
    while (QFileInfo::exists(backupFilename)) {
        backupFilename = inputMetadataFilename + QStringLiteral(".") + timestamp
                         + QStringLiteral("_") + QString::number(collisionCounter) + backupSuffix;
        collisionCounter++;
    }
    return backupFilename;
}
void applyFullFrameDecodeBounds(LdDecodeMetaData::VideoParameters &videoParameters)
{
    const qint32 frameHeight = (videoParameters.fieldHeight * 2) - 1;
    constexpr qint32 horizontalMargin = 16;

    qint32 decodeStart = horizontalMargin;
    qint32 decodeEnd = videoParameters.fieldWidth - horizontalMargin;
    if (decodeEnd <= decodeStart) {
        decodeStart = 0;
        decodeEnd = videoParameters.fieldWidth;
    }

    videoParameters.activeVideoStart = decodeStart;
    videoParameters.activeVideoEnd = decodeEnd;
    videoParameters.firstActiveFieldLine = 0;
    videoParameters.lastActiveFieldLine = videoParameters.fieldHeight;
    videoParameters.firstActiveFrameLine = 0;
    videoParameters.lastActiveFrameLine = frameHeight;
}

QImage renderRgbParadeScopeImage(const QVector<QRgb> &rgbData,
                                 qint32 frameWidth,
                                 qint32 frameHeight,
                                 const LdDecodeMetaData::VideoParameters &videoParameters,
                                 const QSize &targetSize)
{
    if (frameWidth <= 0 || frameHeight <= 0 || rgbData.size() < (frameWidth * frameHeight)) {
        return QImage();
    }

    const qint32 requestedWidth = targetSize.width() > 0 ? targetSize.width() : frameWidth;
    const qint32 requestedHeight = targetSize.height() > 0 ? targetSize.height() : frameHeight;
    const qint32 scopeWidth = qBound<qint32>(192, requestedWidth, 2048);
    const qint32 scopeHeight = qBound<qint32>(120, requestedHeight, 1536);

    QImage scopeImage(scopeWidth, scopeHeight, QImage::Format_RGB32);
    scopeImage.fill(Qt::black);

    const qint32 leftAxisWidth = qBound<qint32>(20, scopeWidth / 11, 64);
    const qint32 rightPadding = qBound<qint32>(4, scopeWidth / 64, 16);
    const qint32 topPadding = qBound<qint32>(12, scopeHeight / 14, 52);
    const qint32 bottomPadding = qBound<qint32>(10, scopeHeight / 12, 48);
    const QRect plotRect(leftAxisWidth,
                         topPadding,
                         qMax<qint32>(1, scopeWidth - leftAxisWidth - rightPadding),
                         qMax<qint32>(1, scopeHeight - topPadding - bottomPadding));
    const qint32 plotHeight = qMax<qint32>(1, plotRect.height());

    qint32 sourceXStart = qBound<qint32>(0, videoParameters.activeVideoStart, frameWidth - 1);
    qint32 sourceXEnd = qBound<qint32>(sourceXStart + 1, videoParameters.activeVideoEnd, frameWidth);
    qint32 sourceYStart = qBound<qint32>(0, videoParameters.firstActiveFrameLine, frameHeight - 1);
    qint32 sourceYEnd = qBound<qint32>(sourceYStart + 1, videoParameters.lastActiveFrameLine, frameHeight);

    if (sourceXEnd <= sourceXStart) {
        sourceXStart = 0;
        sourceXEnd = frameWidth;
    }
    if (sourceYEnd <= sourceYStart) {
        sourceYStart = 0;
        sourceYEnd = frameHeight;
    }

    const qint32 minWaveformWidth = qMax<qint32>(64, frameWidth / 8);
    if ((sourceXEnd - sourceXStart) < minWaveformWidth) {
        sourceXStart = 0;
        sourceXEnd = frameWidth;
    }
    const qint32 minWaveformHeight = qMax<qint32>(32, frameHeight / 8);
    if ((sourceYEnd - sourceYStart) < minWaveformHeight) {
        sourceYStart = 0;
        sourceYEnd = frameHeight;
    }

    const qint32 sourceWidth = qMax<qint32>(1, sourceXEnd - sourceXStart);
    const qint32 sourceHeight = qMax<qint32>(1, sourceYEnd - sourceYStart);
    const qint32 xDenominator = qMax<qint32>(1, sourceWidth - 1);
    const qint32 channelRenderWidth = qMax<qint32>(1, plotRect.width() / 3);
    const qint32 sampleStepX = qMax<qint32>(1, sourceWidth / channelRenderWidth);
    const qint32 sampleStepY = qMax<qint32>(1, sourceHeight / qMax<qint32>(1, plotHeight * 2));

    auto mapCodeValueToY = [&](qint32 value) {
        const qint32 clamped = qBound<qint32>(0, value, 255);
        if (plotHeight <= 1) {
            return plotRect.top();
        }
        const qint32 localY = (plotHeight - 1) - ((clamped * (plotHeight - 1)) / 255);
        return plotRect.top() + localY;
    };

    struct WaveformChannel {
        qint32 startX = 0;
        qint32 width = 1;
        QVector<quint16> bins;
    };
    std::array<WaveformChannel, 3> waveformChannels;
    for (qint32 channel = 0; channel < 3; ++channel) {
        const qint32 channelStart = plotRect.left() + ((channel * plotRect.width()) / 3);
        const qint32 channelEnd = plotRect.left() + (((channel + 1) * plotRect.width()) / 3);
        WaveformChannel &waveformChannel = waveformChannels[static_cast<size_t>(channel)];
        waveformChannel.startX = channelStart;
        waveformChannel.width = qMax<qint32>(1, channelEnd - channelStart);
        waveformChannel.bins.fill(0, waveformChannel.width * plotHeight);
    }

    auto channelValueAt = [](QRgb pixel, qint32 channel) -> qint32 {
        switch (channel) {
        case 0: return qRed(pixel);
        case 1: return qGreen(pixel);
        default: return qBlue(pixel);
        }
    };

    for (qint32 sourceY = sourceYStart; sourceY < sourceYEnd; sourceY += sampleStepY) {
        const qint32 lineOffset = sourceY * frameWidth;
        for (qint32 sourceX = sourceXStart; sourceX < sourceXEnd; sourceX += sampleStepX) {
            const QRgb sourcePixel = rgbData[lineOffset + sourceX];
            const qint32 sourceOffsetX = sourceX - sourceXStart;
            for (qint32 channel = 0; channel < 3; ++channel) {
                WaveformChannel &waveformChannel = waveformChannels[static_cast<size_t>(channel)];
                const qint32 localX = (waveformChannel.width <= 1)
                    ? 0
                    : ((sourceOffsetX * (waveformChannel.width - 1)) / xDenominator);
                const qint32 value = channelValueAt(sourcePixel, channel);
                const qint32 mappedY = mapCodeValueToY(value) - plotRect.top();
                quint16 &binValue = waveformChannel.bins[(mappedY * waveformChannel.width) + localX];
                if (binValue < std::numeric_limits<quint16>::max()) {
                    ++binValue;
                }
            }
        }
    }

    const std::array<QColor, 3> waveformColors = {
        QColor(255, 96, 96),
        QColor(96, 255, 128),
        QColor(128, 176, 255)
    };
    for (qint32 channel = 0; channel < 3; ++channel) {
        const WaveformChannel &waveformChannel = waveformChannels[static_cast<size_t>(channel)];
        const QColor channelColor = waveformColors[static_cast<size_t>(channel)];
        for (qint32 localY = 0; localY < plotHeight; ++localY) {
            QRgb *scanLine = reinterpret_cast<QRgb *>(scopeImage.scanLine(plotRect.top() + localY));
            for (qint32 localX = 0; localX < waveformChannel.width; ++localX) {
                const quint16 density = waveformChannel.bins[(localY * waveformChannel.width) + localX];
                if (density == 0) {
                    continue;
                }

                qint32 intensity = static_cast<qint32>(std::sqrt(static_cast<double>(density)) * 48.0);
                intensity = qBound<qint32>(20, intensity, 255);
                const qint32 addRed = (channelColor.red() * intensity) / 255;
                const qint32 addGreen = (channelColor.green() * intensity) / 255;
                const qint32 addBlue = (channelColor.blue() * intensity) / 255;

                const qint32 targetX = waveformChannel.startX + localX;
                const QRgb current = scanLine[targetX];
                scanLine[targetX] = qRgb(
                    qMin<qint32>(255, qRed(current) + addRed),
                    qMin<qint32>(255, qGreen(current) + addGreen),
                    qMin<qint32>(255, qBlue(current) + addBlue));
            }
        }
    }

    QPainter scopePainter(&scopeImage);
    scopePainter.setRenderHint(QPainter::Antialiasing, false);

    scopePainter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    scopePainter.setPen(QPen(QColor(150, 150, 150), 1));
    scopePainter.drawRect(plotRect.adjusted(0, 0, -1, -1));
    scopePainter.drawLine(plotRect.left() + (plotRect.width() / 3),
                          plotRect.top(),
                          plotRect.left() + (plotRect.width() / 3),
                          plotRect.bottom());
    scopePainter.drawLine(plotRect.left() + ((plotRect.width() * 2) / 3),
                          plotRect.top(),
                          plotRect.left() + ((plotRect.width() * 2) / 3),
                          plotRect.bottom());

    const std::array<qint32, 5> rangeGuideValues = { 0, 64, 128, 192, 255 };
    for (const qint32 value : rangeGuideValues) {
        const bool majorGuide = (value == 0 || value == 128 || value == 255);
        scopePainter.setPen(QPen(majorGuide ? QColor(110, 110, 110) : QColor(80, 80, 80), 1));
        const qint32 yPosition = mapCodeValueToY(value);
        scopePainter.drawLine(plotRect.left(), yPosition, plotRect.right(), yPosition);
    }

    const std::array<qint32, 2> legalRangeMarkers = { 16, 235 };
    scopePainter.setPen(QPen(QColor(150, 150, 150, 200), 1, Qt::DashLine));
    for (const qint32 value : legalRangeMarkers) {
        const qint32 yPosition = mapCodeValueToY(value);
        scopePainter.drawLine(plotRect.left(), yPosition, plotRect.right(), yPosition);
    }

    QFont axisFont = scopePainter.font();
    axisFont.setPointSize(qMax<qint32>(7, scopeHeight / 42));
    scopePainter.setFont(axisFont);
    const qint32 axisLabelWidth = qMax<qint32>(10, leftAxisWidth - 6);
    for (const qint32 value : rangeGuideValues) {
        const qint32 yPosition = mapCodeValueToY(value);
        scopePainter.setPen(QColor(205, 205, 205));
        scopePainter.drawText(QRect(2, yPosition - 8, axisLabelWidth, 16),
                              Qt::AlignRight | Qt::AlignVCenter,
                              QString::number(value));
    }

    scopePainter.setPen(QColor(170, 170, 170));
    scopePainter.drawText(QRect(plotRect.left(),
                                qMax<qint32>(0, scopeHeight - bottomPadding),
                                plotRect.width(),
                                bottomPadding),
                          Qt::AlignLeft | Qt::AlignVCenter,
                          QStringLiteral("8-bit range 0-255"));

    QFont labelFont = scopePainter.font();
    labelFont.setPointSize(qMax<qint32>(8, scopeHeight / 36));
    labelFont.setBold(true);
    scopePainter.setFont(labelFont);

    const std::array<QString, 3> labels = { QStringLiteral("R"), QStringLiteral("G"), QStringLiteral("B") };
    const std::array<QColor, 3> labelColors = {
        QColor(255, 96, 96),
        QColor(96, 255, 128),
        QColor(128, 176, 255)
    };

    for (qint32 channel = 0; channel < 3; ++channel) {
        const qint32 channelStart = plotRect.left() + ((channel * plotRect.width()) / 3);
        const qint32 channelEnd = plotRect.left() + (((channel + 1) * plotRect.width()) / 3);
        scopePainter.setPen(labelColors[static_cast<size_t>(channel)]);
        scopePainter.drawText(QRect(channelStart,
                                    2,
                                    qMax<qint32>(8, channelEnd - channelStart),
                                    qMax<qint32>(10, topPadding - 2)),
                              Qt::AlignHCenter | Qt::AlignVCenter,
                              labels[static_cast<size_t>(channel)]);
    }

    scopePainter.end();
    return scopeImage.convertToFormat(QImage::Format_RGB32);
}

QImage renderYuvRangeScopeImage(const ComponentFrame &componentFrame,
                                const LdDecodeMetaData::VideoParameters &videoParameters,
                                const QSize &targetSize,
                                const YuvRangeSettings &settings)
{
    const qint32 requestedWidth = targetSize.width() > 0 ? targetSize.width() : videoParameters.fieldWidth;
    const qint32 requestedHeight = targetSize.height() > 0 ? targetSize.height() : videoParameters.fieldHeight;
    const qint32 scopeWidth = qBound<qint32>(320, requestedWidth, 2048);
    const qint32 scopeHeight = qBound<qint32>(220, requestedHeight, 1536);

    auto renderMessage = [&](const QString &message) {
        QImage messageImage(scopeWidth, scopeHeight, QImage::Format_RGB32);
        messageImage.fill(Qt::black);
        QPainter messagePainter(&messageImage);
        messagePainter.setPen(QColor(210, 210, 210));
        messagePainter.drawText(messageImage.rect(), Qt::AlignCenter, message);
        messagePainter.end();
        return messageImage;
    };

    const qint32 frameWidth = componentFrame.getWidth();
    const qint32 frameHeight = componentFrame.getHeight();
    if (frameWidth <= 0 || frameHeight <= 0) {
        return renderMessage(QStringLiteral("No YUV range data"));
    }

    const YuvRangeScale rangeScale = makeYuvRangeScale(videoParameters);
    if (!rangeScale.valid) {
        return renderMessage(QStringLiteral("Invalid video levels"));
    }

    qint32 sourceXStart = qBound<qint32>(0, videoParameters.activeVideoStart, frameWidth - 1);
    qint32 sourceXEnd = qBound<qint32>(sourceXStart + 1, videoParameters.activeVideoEnd, frameWidth);
    qint32 sourceYStart = qBound<qint32>(0, videoParameters.firstActiveFrameLine, frameHeight - 1);
    qint32 sourceYEnd = qBound<qint32>(sourceYStart + 1, videoParameters.lastActiveFrameLine, frameHeight);

    if (sourceXEnd <= sourceXStart) {
        sourceXStart = 0;
        sourceXEnd = frameWidth;
    }
    if (sourceYEnd <= sourceYStart) {
        sourceYStart = 0;
        sourceYEnd = frameHeight;
    }

    struct HistogramChannel {
        std::array<quint64, 256> bins = {};
        quint64 below = 0;
        quint64 above = 0;
        quint64 total = 0;
    };
    std::array<HistogramChannel, 3> channels;

    auto addSample = [&](qint32 channel, double codeValue, qint32 legalLow, qint32 legalHigh) {
        HistogramChannel &histogramChannel = channels[static_cast<size_t>(channel)];
        if (codeValue < static_cast<double>(legalLow)) {
            histogramChannel.below++;
        } else if (codeValue > static_cast<double>(legalHigh)) {
            histogramChannel.above++;
        }
        const qint32 bin = qBound<qint32>(0, static_cast<qint32>(std::floor(codeValue + 0.5)), 255);
        histogramChannel.bins[static_cast<size_t>(bin)]++;
        histogramChannel.total++;
    };

    for (qint32 sourceY = sourceYStart; sourceY < sourceYEnd; ++sourceY) {
        const double *yLine = componentFrame.y(sourceY);
        const double *uLine = componentFrame.u(sourceY);
        const double *vLine = componentFrame.v(sourceY);
        for (qint32 sourceX = sourceXStart; sourceX < sourceXEnd; ++sourceX) {
            const YuvCodeValues codeValues = convertComponentSampleToYuvCodeValues(yLine[sourceX], uLine[sourceX], vLine[sourceX], rangeScale);
            addSample(0, codeValues.y, settings.lumaMin, settings.lumaMax);
            addSample(1, codeValues.u, settings.chromaMin, settings.chromaMax);
            addSample(2, codeValues.v, settings.chromaMin, settings.chromaMax);
        }
    }

    QImage scopeImage(scopeWidth, scopeHeight, QImage::Format_RGB32);
    scopeImage.fill(Qt::black);

    const qint32 leftAxisWidth = qBound<qint32>(34, scopeWidth / 12, 76);
    const qint32 rightPadding = qBound<qint32>(8, scopeWidth / 64, 18);
    const qint32 topPadding = qBound<qint32>(6, scopeHeight / 60, 16);
    const qint32 bottomPadding = qBound<qint32>(28, scopeHeight / 11, 62);
    const qint32 rowGap = qBound<qint32>(6, scopeHeight / 55, 18);
    const qint32 headerHeight = qBound<qint32>(18, scopeHeight / 30, 30);
    const qint32 availableHeight = qMax<qint32>(24, scopeHeight - topPadding - bottomPadding - (rowGap * 2) - (headerHeight * 3));
    const qint32 rowHeight = qMax<qint32>(1, availableHeight / 3);
    const qint32 plotWidth = qMax<qint32>(1, scopeWidth - leftAxisWidth - rightPadding);

    auto binToX = [&](const QRect &plotRect, qint32 bin) {
        return plotRect.left() + ((qBound<qint32>(0, bin, 255) * qMax<qint32>(1, plotRect.width() - 1)) / 255);
    };
    auto percentText = [](quint64 count, quint64 total) {
        if (total == 0) {
            return QStringLiteral("0.000");
        }
        const double percent = (static_cast<double>(count) * 100.0) / static_cast<double>(total);
        return QString::number(percent, 'f', percent < 1.0 ? 3 : 2);
    };

    QPainter scopePainter(&scopeImage);
    scopePainter.setRenderHint(QPainter::Antialiasing, false);
    QFont axisFont = scopePainter.font();
    axisFont.setPointSize(qMax<qint32>(7, scopeHeight / 46));
    scopePainter.setFont(axisFont);

    const std::array<QString, 3> labels = {
        QStringLiteral("Y"),
        QStringLiteral("U"),
        QStringLiteral("V")
    };
    const std::array<QColor, 3> channelColors = {
        QColor(245, 245, 245),
        QColor(96, 180, 255),
        QColor(255, 118, 180)
    };

    for (qint32 channel = 0; channel < 3; ++channel) {
        const qint32 headerTop = topPadding + (channel * (headerHeight + rowHeight + rowGap));
        const qint32 rowTop = headerTop + headerHeight;
        const QRect plotRect(leftAxisWidth, rowTop, plotWidth, rowHeight);
        const QRect statsRect(plotRect.left(), headerTop, plotRect.width(), headerHeight);
        const qint32 legalLow = channel == 0 ? settings.lumaMin : settings.chromaMin;
        const qint32 legalHigh = channel == 0 ? settings.lumaMax : settings.chromaMax;
        const HistogramChannel &histogramChannel = channels[static_cast<size_t>(channel)];

        const QString statsText = QStringLiteral("below %1 (%2%)  above %3 (%4%)")
            .arg(histogramChannel.below)
            .arg(percentText(histogramChannel.below, histogramChannel.total))
            .arg(histogramChannel.above)
            .arg(percentText(histogramChannel.above, histogramChannel.total));
        scopePainter.setPen(channelColors[static_cast<size_t>(channel)]);
        scopePainter.drawText(QRect(2, headerTop, leftAxisWidth - 6, headerHeight),
                              Qt::AlignRight | Qt::AlignVCenter,
                              labels[static_cast<size_t>(channel)]);
        scopePainter.setPen(QColor(220, 220, 220));
        scopePainter.drawText(statsRect,
                              Qt::AlignLeft | Qt::AlignVCenter,
                              statsText);

        const qint32 lowStartX = binToX(plotRect, 0);
        const qint32 lowEndX = binToX(plotRect, legalLow - 1);
        const qint32 highStartX = binToX(plotRect, legalHigh + 1);
        const qint32 highEndX = binToX(plotRect, 255);
        if (legalLow > 0) {
            scopePainter.fillRect(QRect(lowStartX, plotRect.top(), qMax<qint32>(1, lowEndX - lowStartX + 1), plotRect.height()),
                                  QColor(150, 24, 24, 95));
        }
        if (legalHigh < 255 && highEndX >= highStartX) {
            scopePainter.fillRect(QRect(highStartX, plotRect.top(), qMax<qint32>(1, highEndX - highStartX + 1), plotRect.height()),
                                  QColor(150, 24, 24, 95));
        }

        scopePainter.setPen(QPen(QColor(80, 80, 80), 1));
        const std::array<qint32, 5> guideValues = { 0, 64, 128, 192, 255 };
        for (const qint32 guideValue : guideValues) {
            const qint32 guideX = binToX(plotRect, guideValue);
            scopePainter.drawLine(guideX, plotRect.top(), guideX, plotRect.bottom());
        }

        scopePainter.setPen(QPen(QColor(215, 215, 215, 210), 1, Qt::DashLine));
        const qint32 lowX = binToX(plotRect, legalLow);
        const qint32 highX = binToX(plotRect, legalHigh);
        scopePainter.drawLine(lowX, plotRect.top(), lowX, plotRect.bottom());
        scopePainter.drawLine(highX, plotRect.top(), highX, plotRect.bottom());

        quint64 maxBin = 0;
        for (const quint64 binCount : histogramChannel.bins) {
            maxBin = qMax(maxBin, binCount);
        }
        const double maxDensity = maxBin > 0 ? std::sqrt(static_cast<double>(maxBin)) : 1.0;
        for (qint32 bin = 0; bin < 256; ++bin) {
            const quint64 binCount = histogramChannel.bins[static_cast<size_t>(bin)];
            if (binCount == 0) {
                continue;
            }

            const qint32 binX = binToX(plotRect, bin);
            const qint32 nextBinX = binToX(plotRect, qMin<qint32>(255, bin + 1));
            const qint32 barWidth = qMax<qint32>(1, nextBinX - binX + 1);
            const qint32 barHeight = qBound<qint32>(1,
                                                    static_cast<qint32>((std::sqrt(static_cast<double>(binCount)) / maxDensity) * static_cast<double>(plotRect.height() - 2)),
                                                    qMax<qint32>(1, plotRect.height() - 2));
            const bool outOfRange = bin < legalLow || bin > legalHigh;
            const QColor barColor = outOfRange ? QColor(255, 72, 72) : channelColors[static_cast<size_t>(channel)];
            scopePainter.fillRect(QRect(binX,
                                         plotRect.bottom() - barHeight,
                                         barWidth,
                                         barHeight),
                                  barColor);
        }

        scopePainter.setPen(QPen(QColor(150, 150, 150), 1));
        scopePainter.drawRect(plotRect.adjusted(0, 0, -1, -1));
    }

    scopePainter.setPen(QColor(170, 170, 170));
    const QRect bottomLabelRect(leftAxisWidth, scopeHeight - bottomPadding, plotWidth, bottomPadding);
    scopePainter.drawText(bottomLabelRect,
                          Qt::AlignLeft | Qt::AlignVCenter,
                          QStringLiteral("8-bit code values; Y legal %1-%2, U/V legal %3-%4")
                              .arg(settings.lumaMin)
                              .arg(settings.lumaMax)
                              .arg(settings.chromaMin)
                              .arg(settings.chromaMax));
    const std::array<qint32, 8> bottomValues = {
        0,
        settings.lumaMin,
        settings.chromaMin,
        128,
        settings.lumaMax,
        settings.chromaMax,
        240,
        255
    };
    for (const qint32 value : bottomValues) {
        const qint32 x = leftAxisWidth + ((value * qMax<qint32>(1, plotWidth - 1)) / 255);
        scopePainter.drawText(QRect(x - 18, scopeHeight - bottomPadding, 36, qMax<qint32>(12, bottomPadding / 2)),
                              Qt::AlignHCenter | Qt::AlignTop,
                              QString::number(value));
    }

    scopePainter.end();
    return scopeImage.convertToFormat(QImage::Format_RGB32);
}

QImage renderYuvRangeOverlayImage(const ComponentFrame &componentFrame,
                                  const LdDecodeMetaData::VideoParameters &videoParameters,
                                  const YuvRangeSettings &settings,
                                  TbcSource::ViewMode renderViewMode,
                                  bool stretchField,
                                  qint32 loadedFieldNumber)
{
    const qint32 frameWidth = componentFrame.getWidth();
    const qint32 frameHeight = componentFrame.getHeight();
    if (frameWidth <= 0 || frameHeight <= 0) {
        return QImage();
    }
    if (renderViewMode == TbcSource::ViewMode::RGB_SCOPE_VIEW) {
        return QImage();
    }

    const YuvRangeScale rangeScale = makeYuvRangeScale(videoParameters);
    if (!rangeScale.valid) {
        return QImage();
    }

    qint32 sourceXStart = qBound<qint32>(0, videoParameters.activeVideoStart, frameWidth - 1);
    qint32 sourceXEnd = qBound<qint32>(sourceXStart + 1, videoParameters.activeVideoEnd, frameWidth);
    qint32 sourceYStart = qBound<qint32>(0, videoParameters.firstActiveFrameLine, frameHeight - 1);
    qint32 sourceYEnd = qBound<qint32>(sourceYStart + 1, videoParameters.lastActiveFrameLine, frameHeight);

    if (sourceXEnd <= sourceXStart) {
        sourceXStart = 0;
        sourceXEnd = frameWidth;
    }
    if (sourceYEnd <= sourceYStart) {
        sourceYStart = 0;
        sourceYEnd = frameHeight;
    }

    QImage overlayImage(frameWidth, frameHeight, QImage::Format_ARGB32);
    overlayImage.fill(Qt::transparent);
    const qint32 overlayAlpha = qBound<qint32>(0, settings.overlayAlpha, 255);
    const QRgb belowColor = qRgba(0, 255, 255, overlayAlpha);
    const QRgb aboveColor = qRgba(0, 255, 0, overlayAlpha);

    auto sampleOverlayColor = [&](qint32 sourceX, qint32 sourceY) {
        if (sourceX < sourceXStart || sourceX >= sourceXEnd || sourceY < sourceYStart || sourceY >= sourceYEnd) {
            return qRgba(0, 0, 0, 0);
        }

        const double *yLine = componentFrame.y(sourceY);
        const double *uLine = componentFrame.u(sourceY);
        const double *vLine = componentFrame.v(sourceY);
        const YuvCodeValues codeValues = convertComponentSampleToYuvCodeValues(yLine[sourceX], uLine[sourceX], vLine[sourceX], rangeScale);
        const bool belowRange = codeValues.y < settings.lumaMin
                                || codeValues.u < settings.chromaMin
                                || codeValues.v < settings.chromaMin;
        const bool aboveRange = codeValues.y > settings.lumaMax
                                || codeValues.u > settings.chromaMax
                                || codeValues.v > settings.chromaMax;
        if (aboveRange) {
            return aboveColor;
        }
        return belowRange ? belowColor : qRgba(0, 0, 0, 0);
    };

    if (renderViewMode == TbcSource::ViewMode::FRAME_VIEW) {
        for (qint32 sourceY = sourceYStart; sourceY < sourceYEnd; ++sourceY) {
            QRgb *overlayLine = reinterpret_cast<QRgb *>(overlayImage.scanLine(sourceY));
            for (qint32 sourceX = sourceXStart; sourceX < sourceXEnd; ++sourceX) {
                const QRgb overlayColor = sampleOverlayColor(sourceX, sourceY);
                if (qAlpha(overlayColor) > 0) {
                    overlayLine[sourceX] = overlayColor;
                }
            }
        }
        return overlayImage;
    }

    if (renderViewMode == TbcSource::ViewMode::SPLIT_VIEW) {
        for (qint32 sourceY = sourceYStart; sourceY < sourceYEnd; ++sourceY) {
            const qint32 outputY = (sourceY / 2) + (sourceY % 2 == 0 ? 0 : (frameHeight / 2) + 1);
            if (outputY < 0 || outputY >= frameHeight) {
                continue;
            }
            QRgb *overlayLine = reinterpret_cast<QRgb *>(overlayImage.scanLine(outputY));
            for (qint32 sourceX = sourceXStart; sourceX < sourceXEnd; ++sourceX) {
                const QRgb overlayColor = sampleOverlayColor(sourceX, sourceY);
                if (qAlpha(overlayColor) > 0) {
                    overlayLine[sourceX] = overlayColor;
                }
            }
        }
        return overlayImage;
    }

    const qint32 fieldHeight = videoParameters.fieldHeight;
    const qint32 startOffset = stretchField ? 0 : frameHeight / 4;
    const qint32 outputHeight = stretchField ? frameHeight : fieldHeight;
    qint32 fieldY = loadedFieldNumber % 2 ? 0 : 1;
    qint32 maxFieldY = frameHeight - 1;
    if (maxFieldY >= 0 && (maxFieldY % 2) != (fieldY % 2)) {
        maxFieldY -= 1;
    }

    for (qint32 outputYLocal = 0; outputYLocal < outputHeight; ++outputYLocal) {
        const qint32 outputY = outputYLocal + startOffset;
        if (outputY >= 0 && outputY < frameHeight) {
            const qint32 sourceY = qBound<qint32>(0, fieldY, qMax<qint32>(0, maxFieldY));
            QRgb *overlayLine = reinterpret_cast<QRgb *>(overlayImage.scanLine(outputY));
            for (qint32 sourceX = sourceXStart; sourceX < sourceXEnd; ++sourceX) {
                const QRgb overlayColor = sampleOverlayColor(sourceX, sourceY);
                if (qAlpha(overlayColor) > 0) {
                    overlayLine[sourceX] = overlayColor;
                }
            }
        }

        if (!stretchField || outputYLocal % 2 != 0) {
            fieldY += 2;
        }
    }

    return overlayImage;
}
}

TbcSource::TbcSource(QObject *parent) : QObject(parent)
{
    resetState();

    // Configure the chroma decoder
    palConfiguration = palColour.getConfiguration();
    palConfiguration.chromaFilter = PalColour::transform2DFilter;
    ntscConfiguration = ntscColour.getConfiguration();
    outputConfiguration.pixelFormat = OutputWriter::PixelFormat::RGB48;
    outputConfiguration.paddingAmount = 1;
    outputConfiguration.trimToActiveRegion = false;
    outputConfiguration.fullFrameDecode = false;
    chromaDecodeMode = HYBRID_CHROMA_MODE;
}

// Public methods -----------------------------------------------------------------------------------------------------

// Method to load a TBC source file
void TbcSource::loadSource(QString sourceFilename, QString metadataFilename)
{
    resetState();
    requestedMetadataFilename = metadataFilename;

    // Set the current file name
    QFileInfo inFileInfo(sourceFilename);
    currentSourceFilename = inFileInfo.fileName();
    tbcDebugStream() << "TbcSource::loadSource(): Opening TBC source file:" << currentSourceFilename;

    // Set up and fire-off background loading thread
    tbcDebugStream() << "TbcSource::loadSource(): Setting up background loader thread";
    disconnect(&watcher, &QFutureWatcher<bool>::finished, nullptr, nullptr);
    connect(&watcher, &QFutureWatcher<bool>::finished, this, &TbcSource::finishBackgroundLoad);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    future = QtConcurrent::run(this, &TbcSource::startBackgroundLoad, sourceFilename);
#else
    future = QtConcurrent::run(&TbcSource::startBackgroundLoad, this, sourceFilename);
#endif
    watcher.setFuture(future);
}

bool TbcSource::writeMetadataSnapshot(const QString &metadataFilename, QString *errorMessage) const
{
    if (!sourceReady) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("No source loaded.");
        }
        return false;
    }

    if (metadataFilename.trimmed().isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("No metadata snapshot filename provided.");
        }
        return false;
    }

    if (!ldDecodeMetaData.write(metadataFilename)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Could not write temporary metadata snapshot.");
        }
        return false;
    }

    return true;
}

// Method to load a metadata file without a TBC source
void TbcSource::loadMetadata(QString metadataFilename, QString displayFilename)
{
    resetState();
    metadataOnly = true;

    if (displayFilename.isEmpty()) {
        displayFilename = metadataFilename;
    }

    // Set the current file name for display purposes
    QFileInfo inFileInfo(displayFilename);
    currentSourceFilename = inFileInfo.fileName();
    tbcDebugStream() << "TbcSource::loadMetadata(): Opening metadata file:" << displayFilename;

    // Set up and fire-off background loading thread
    tbcDebugStream() << "TbcSource::loadMetadata(): Setting up background loader thread";
    disconnect(&watcher, &QFutureWatcher<bool>::finished, nullptr, nullptr);
    connect(&watcher, &QFutureWatcher<bool>::finished, this, &TbcSource::finishBackgroundLoad);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    future = QtConcurrent::run(this, &TbcSource::startBackgroundLoadMetadata, metadataFilename, displayFilename);
#else
    future = QtConcurrent::run(&TbcSource::startBackgroundLoadMetadata, this, metadataFilename, displayFilename);
#endif
    watcher.setFuture(future);
}

// Method to unload a TBC source file
void TbcSource::unloadSource()
{
    ntscColour.requestNnTransform3DCancel();
    sourceVideo.close();
    if (sourceMode != ONE_SOURCE) chromaSourceVideo.close();
    resetState();
}

// Start saving the metadata file for the current source
void TbcSource::saveSourceMetadata()
{
    // Start a background saving thread
    tbcDebugStream() << "TbcSource::saveSourceMetadata(): Starting background save thread";
    disconnect(&watcher, &QFutureWatcher<bool>::finished, nullptr, nullptr);
    connect(&watcher, &QFutureWatcher<bool>::finished, this, &TbcSource::finishBackgroundSave);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    future = QtConcurrent::run(this, &TbcSource::startBackgroundSave, currentMetadataFilename);
#else
    future = QtConcurrent::run(&TbcSource::startBackgroundSave, this, currentMetadataFilename);
#endif
    watcher.setFuture(future);
}

// Method returns true is a TBC source is loaded
bool TbcSource::getIsSourceLoaded() const
{
    return sourceReady;
}

bool TbcSource::getIsMetadataOnly() const
{
    return metadataOnly;
}

// Method returns the filename of the current TBC source
QString TbcSource::getCurrentSourceFilename() const
{
    if (!sourceReady) return QString();
    return currentSourceFilename;
}

QString TbcSource::getCurrentMetadataFilename() const
{
    if (!sourceReady) return QString();
    return currentMetadataFilename;
}

// Return a description of the last IO error
QString TbcSource::getLastIOError() const
{
    return lastIOError;
}

// Method to set the highlight dropouts mode (true = dropouts highlighted)
void TbcSource::setHighlightDropouts(bool _state)
{
    invalidateImageCache();
    dropoutsOn = _state;
}

// Method to set the chroma decoder mode (true = on)
void TbcSource::setChromaDecoder(bool _state)
{
    if (chromaOn == _state) {
        return;
    }
    ntscColour.requestNnTransform3DCancel();
    invalidateImageCache();
    chromaOn = _state;
}

void TbcSource::setChromaDecodeMode(TbcSource::ChromaDecodeMode mode)
{
    if (chromaDecodeMode == mode) {
        return;
    }
    ntscColour.requestNnTransform3DCancel();

    chromaDecodeMode = mode;
    switch (chromaDecodeMode) {
    case ACTIVE_ONLY_CHROMA_MODE:
        outputConfiguration.trimToActiveRegion = true;
        outputConfiguration.fullFrameDecode = false;
        break;
    case HYBRID_CHROMA_MODE:
        outputConfiguration.trimToActiveRegion = false;
        outputConfiguration.fullFrameDecode = false;
        break;
    case FULL_FRAME_CHROMA_MODE:
        outputConfiguration.trimToActiveRegion = false;
        outputConfiguration.fullFrameDecode = true;
        break;
    }

    invalidateImageCache();
    if (sourceReady) {
        configureChromaDecoder();
    }
}

TbcSource::ChromaDecodeMode TbcSource::getChromaDecodeMode() const
{
    return chromaDecodeMode;
}

// Method to set the view mode
void TbcSource::setViewMode(ViewMode _viewMode)
{
    invalidateImageCache();
    viewMode = _viewMode;
}

// Method to set stretch field mode (true = on)
void TbcSource::setStretchField(bool _stretch)
{
    invalidateImageCache();
    stretchFieldOn = _stretch;
}

// Method to set the field order (true = reversed, false = normal)
void TbcSource::setFieldOrder(bool _state)
{
    invalidateImageCache();
    reverseFoOn = _state;

    if (reverseFoOn) ldDecodeMetaData.setIsFirstFieldFirst(false);
    else ldDecodeMetaData.setIsFirstFieldFirst(true);
}

// Method to set if we combine source
void TbcSource::setCombine(bool _state)
{
	combine = _state;
}

// Method to get the state of the highlight dropouts mode
bool TbcSource::getHighlightDropouts() const
{
    return dropoutsOn;
}

// Method to get the state of the chroma decoder mode
bool TbcSource::getChromaDecoder() const
{
    return chromaOn;
}

// Method to get the view mode
TbcSource::ViewMode TbcSource::getViewMode() const
{
    return viewMode;
}

// Method to determine if frame view is enabled
bool TbcSource::getFrameViewEnabled() const
{
    return viewMode == ViewMode::FRAME_VIEW;
}

// Method to determine if field view is enabled
bool TbcSource::getFieldViewEnabled() const
{
    return viewMode == ViewMode::FIELD_VIEW;
}

// Method to determine if split view is enabled
bool TbcSource::getSplitViewEnabled() const
{
    return viewMode == ViewMode::SPLIT_VIEW;
}

bool TbcSource::getRgbScopeViewEnabled() const
{
    return viewMode == ViewMode::RGB_SCOPE_VIEW;
}

// Method to get the state of the stretch field mode
bool TbcSource::getStretchField() const
{
    return stretchFieldOn;
}

// Method to get the field order
bool TbcSource::getFieldOrder() const
{
    return reverseFoOn;
}

// Return the source mode
TbcSource::SourceMode TbcSource::getSourceMode() const
{
    return sourceMode;
}

// Set the source mode
void TbcSource::setSourceMode(TbcSource::SourceMode _sourceMode)
{
    if (sourceMode == ONE_SOURCE) return;
    if (sourceMode == _sourceMode) return;

    ntscColour.requestNnTransform3DCancel();
    invalidateImageCache();
    sourceMode = _sourceMode;
}

// Load the metadata for a field/frame
void TbcSource::load(qint32 frameNumber, qint32 fieldNumber)
{
    loadedFieldNumber = fieldNumber;

    // If there's no source, or we've already loaded that frame, nothing to do
    if (!sourceReady || loadedFrameNumber == frameNumber) return;
    loadedFrameNumber = frameNumber;
    inputFieldsValid = false;
    invalidateImageCache();

    // Get the required field numbers
    firstFieldNumber = ldDecodeMetaData.getFirstFieldNumber(frameNumber);
    secondFieldNumber = ldDecodeMetaData.getSecondFieldNumber(frameNumber);

    // Make sure we have a valid response from the frame determination
    if (firstFieldNumber == -1 || secondFieldNumber == -1) {
        qCritical() << "Could not determine field numbers!";

        // Jump back one frame
        if (frameNumber != 1) {
            frameNumber--;

            firstFieldNumber = ldDecodeMetaData.getFirstFieldNumber(frameNumber);
            secondFieldNumber = ldDecodeMetaData.getSecondFieldNumber(frameNumber);
        }
        tbcDebugStream() << "TbcSource::load(): Jumping back one frame due to error";
    }

    // Get the field metadata
    firstField = ldDecodeMetaData.getField(firstFieldNumber);
    secondField = ldDecodeMetaData.getField(secondFieldNumber);
}

// Method to get a QImage from a field or frame number
QImage TbcSource::getImage()
{
    if (metadataOnly) return QImage();
    if ((getFieldViewEnabled() ? loadedFieldNumber : loadedFrameNumber) == -1) return QImage();

    // Check cached QImage
    if (!getFieldViewEnabled() && cacheValid) {
        return cache;
    }

    // Get a QImage for the output
    auto outputImage = generateQImage(viewMode);

    // Highlight dropouts
    if (dropoutsOn && !getRgbScopeViewEnabled()) {
        // Create a painter object
        QPainter imagePainter(&outputImage);

        // Get the metadata for the video parameters
        LdDecodeMetaData::VideoParameters videoParameters = ldDecodeMetaData.getVideoParameters();

        // Calculate the frame height
        const auto frameHeight = (videoParameters.fieldHeight * 2) - 1;
        const auto fieldN = getFieldViewEnabled() ? 1 : 2;

        // This will run once for field view and twice for frame/split view
        for (auto i = 0; i < fieldN; i++) {
            // Set current field based on loop iteration or field number
            auto isFirstField = getViewMode() == ViewMode::FIELD_VIEW ? loadedFieldNumber % 2 != 0 : i == 0;
            auto currentField = isFirstField ? &firstField : &secondField;
            imagePainter.setPen(isFirstField ? Qt::red : Qt::blue);

            // Draw the drop out data for the current field
            for (auto dropOutIndex = 0; dropOutIndex < currentField->dropOuts.size(); dropOutIndex++) {
                const auto startx = currentField->dropOuts.startx(dropOutIndex);
                const auto endx = currentField->dropOuts.endx(dropOutIndex);
                const auto fieldLine = currentField->dropOuts.fieldLine(dropOutIndex);

                switch (getViewMode()) {
                    case ViewMode::FRAME_VIEW: {
                        qint32 lineY;
                        if (isFirstField) {
                            lineY = (fieldLine - 1) * 2;
                        } else {
                            lineY = (fieldLine * 2) - 1;
                        }

                        imagePainter.drawLine(startx, lineY, endx, lineY);
                        break;
                    }

                    case ViewMode::SPLIT_VIEW: {
                        qint32 lineY;
                        if (isFirstField) {
                            lineY = fieldLine - 1;
                        } else {
                            lineY = fieldLine + (frameHeight / 2);
                        }

                        imagePainter.drawLine(startx, lineY, endx, lineY);
                        break;
                    }

                    case ViewMode::FIELD_VIEW: {
                        // Draw line off-center if 1:1, else double lines
                        if (getStretchField()) {
                            qint32 lineY = fieldLine - 1;

                            imagePainter.drawLine(startx, lineY * 2, endx, lineY * 2);
                            imagePainter.drawLine(startx, lineY * 2 + 1, endx, lineY * 2 + 1);
                        } else {
                            qint32 lineY = fieldLine - 1 + (frameHeight / 4);

                            imagePainter.drawLine(startx, lineY, endx, lineY);
                        }
                        break;
                    }

                    case ViewMode::RGB_SCOPE_VIEW: {
                        break;
                    }
                }
            }
        }

        // End the painter object
        imagePainter.end();
    }

    cache = outputImage;
    cacheValid = true;

    return outputImage;
}

QImage TbcSource::getRgbScopeImage(const QSize &targetSize)
{
    if (metadataOnly) return QImage();
    if (loadedFrameNumber == -1 && loadedFieldNumber == -1) return QImage();

    const QSize normalizedTargetSize = targetSize.expandedTo(QSize(1, 1));
    if (rgbScopeCacheValid && rgbScopeCacheSize == normalizedTargetSize) {
        return rgbScopeCache;
    }

    rgbScopeCache = generateQImage(ViewMode::RGB_SCOPE_VIEW, normalizedTargetSize);
    rgbScopeCacheValid = true;
    rgbScopeCacheSize = normalizedTargetSize;
    return rgbScopeCache;
}

QImage TbcSource::getYuvRangeScopeImage(const QSize &targetSize, const YuvRangeSettings &settings)
{
    if (metadataOnly) return QImage();
    if (loadedFrameNumber == -1 && loadedFieldNumber == -1) return QImage();

    const QSize normalizedTargetSize = targetSize.expandedTo(QSize(1, 1));
    if (yuvRangeScopeCacheValid
        && yuvRangeScopeCacheSize == normalizedTargetSize
        && yuvRangeSettingsMatch(settings,
                                 yuvRangeScopeCacheLumaMin,
                                 yuvRangeScopeCacheLumaMax,
                                 yuvRangeScopeCacheChromaMin,
                                 yuvRangeScopeCacheChromaMax)) {
        return yuvRangeScopeCache;
    }

    const ComponentFrame &componentFrame = getComponentFrame();
    yuvRangeScopeCache = renderYuvRangeScopeImage(componentFrame, ldDecodeMetaData.getVideoParameters(), normalizedTargetSize, settings);
    yuvRangeScopeCacheValid = true;
    yuvRangeScopeCacheSize = normalizedTargetSize;
    yuvRangeScopeCacheLumaMin = settings.lumaMin;
    yuvRangeScopeCacheLumaMax = settings.lumaMax;
    yuvRangeScopeCacheChromaMin = settings.chromaMin;
    yuvRangeScopeCacheChromaMax = settings.chromaMax;
    return yuvRangeScopeCache;
}

QImage TbcSource::getYuvRangeOverlayImage(const YuvRangeSettings &settings)
{
    if (metadataOnly) return QImage();
    if (loadedFrameNumber == -1 && loadedFieldNumber == -1) return QImage();

    if (yuvRangeOverlayCacheValid
        && yuvRangeSettingsMatch(settings,
                                 yuvRangeOverlayCacheLumaMin,
                                 yuvRangeOverlayCacheLumaMax,
                                 yuvRangeOverlayCacheChromaMin,
                                 yuvRangeOverlayCacheChromaMax)
        && yuvRangeOverlayCacheAlpha == settings.overlayAlpha
        && yuvRangeOverlayCacheViewMode == viewMode
        && yuvRangeOverlayCacheStretchField == stretchFieldOn
        && yuvRangeOverlayCacheFieldNumber == loadedFieldNumber) {
        return yuvRangeOverlayCache;
    }

    const ComponentFrame &componentFrame = getComponentFrame();
    yuvRangeOverlayCache = renderYuvRangeOverlayImage(componentFrame, ldDecodeMetaData.getVideoParameters(), settings, viewMode, stretchFieldOn, loadedFieldNumber);
    yuvRangeOverlayCacheValid = true;
    yuvRangeOverlayCacheLumaMin = settings.lumaMin;
    yuvRangeOverlayCacheLumaMax = settings.lumaMax;
    yuvRangeOverlayCacheChromaMin = settings.chromaMin;
    yuvRangeOverlayCacheChromaMax = settings.chromaMax;
    yuvRangeOverlayCacheAlpha = settings.overlayAlpha;
    yuvRangeOverlayCacheViewMode = viewMode;
    yuvRangeOverlayCacheStretchField = stretchFieldOn;
    yuvRangeOverlayCacheFieldNumber = loadedFieldNumber;
    return yuvRangeOverlayCache;
}

// Method to get the number of available frames
qint32 TbcSource::getNumberOfFrames() const
{
    if (!sourceReady) return 0;
    return ldDecodeMetaData.getNumberOfFrames();
}

// Method to get the number of available fields
qint32 TbcSource::getNumberOfFields() const
{
    if (!sourceReady) return 0;
    return ldDecodeMetaData.getNumberOfFields();
}

// Method returns true if the TBC source is anamorphic (false for 4:3)
bool TbcSource::getIsWidescreen() const
{
    if (!sourceReady) return false;
    return ldDecodeMetaData.getVideoParameters().isWidescreen;
}

// Return the source's VideoSystem
VideoSystem TbcSource::getSystem() const
{
    if (!sourceReady) return NTSC;
    return ldDecodeMetaData.getVideoParameters().system;
}

// Return the source's VideoSystem description
QString TbcSource::getSystemDescription() const
{
    if (!sourceReady) return "None";
    return ldDecodeMetaData.getVideoSystemDescription();
}

// Method to get the frame height in scanlines
qint32 TbcSource::getFrameHeight() const
{
    if (!sourceReady) return 0;

    // Get the metadata for the fields
    LdDecodeMetaData::VideoParameters videoParameters = ldDecodeMetaData.getVideoParameters();

    // Calculate the frame height
    return (videoParameters.fieldHeight * 2) - 1;
}

// Method to get the frame width in dots
qint32 TbcSource::getFrameWidth() const
{
    if (!sourceReady) return 0;

    // Get the metadata for the fields
    LdDecodeMetaData::VideoParameters videoParameters = ldDecodeMetaData.getVideoParameters();

    // Return the frame width
    return (videoParameters.fieldWidth);
}

// Get black SNR data for graphing
QVector<double> TbcSource::getBlackSnrGraphData()
{
    return blackSnrGraphData;
}

// Get white SNR data for graphing
QVector<double> TbcSource::getWhiteSnrGraphData()
{
    return whiteSnrGraphData;
}

// Get dropout data for graphing
QVector<double> TbcSource::getDropOutGraphData()
{
    return dropoutGraphData;
}

// Get visible dropout data for graphing
QVector<double> TbcSource::getVisibleDropOutGraphData()
{
    return visibleDropoutGraphData;
}

// Method to get the size of the graphing data
qint32 TbcSource::getGraphDataSize()
{
    // All data vectors are the same size, just return the size on one
    return dropoutGraphData.size();
}

// Method returns true if frame contains dropouts
bool TbcSource::getIsDropoutPresent() const
{
    if (loadedFrameNumber == -1) return false;

    if (firstField.dropOuts.size() > 0) return true;
    if (secondField.dropOuts.size() > 0) return true;
    return false;
}

// Get the decoded ComponentFrame for the current frame
const ComponentFrame &TbcSource::getComponentFrame()
{
    if (metadataOnly) return metadataOnlyFrame;
    // Load and decode SourceFields for the current frame
    loadInputFields();
    decodeFrame();

    return componentFrames[0];
}

// Get the VideoParameters for the current source
const LdDecodeMetaData::VideoParameters &TbcSource::getVideoParameters() const
{
    return ldDecodeMetaData.getVideoParameters();
}

// Update the VideoParameters for the current source
void TbcSource::setVideoParameters(const LdDecodeMetaData::VideoParameters &videoParameters)
{
    ntscColour.requestNnTransform3DCancel();
    invalidateImageCache();

    // Update the metadata
    ldDecodeMetaData.setVideoParameters(videoParameters);

    // Reconfigure the chroma decoder
    configureChromaDecoder();
}

// Get scan line data from the field/frame
TbcSource::ScanLineData TbcSource::getScanLineData(qint32 scanLine)
{
    if (metadataOnly) return ScanLineData();
    if (loadedFrameNumber == -1) return ScanLineData();

    ScanLineData scanLineData;
    LdDecodeMetaData::VideoParameters videoParameters = ldDecodeMetaData.getVideoParameters();
    auto frameLine = 0;
    bool isFirstField = true;

    switch (getViewMode()) {
        case ViewMode::FRAME_VIEW: {
            frameLine = scanLine;
            isFirstField = (scanLine % 2) == 0;
            break;
        }

        case ViewMode::RGB_SCOPE_VIEW: {
            return ScanLineData();
        }

        case ViewMode::SPLIT_VIEW: {
            if (scanLine <= videoParameters.fieldHeight) {
                isFirstField = true;
                frameLine = scanLine;

                // Offset for LineNumber
                scanLine = (scanLine * 2) - 1;
            } else {
                isFirstField = false;
                frameLine = scanLine - videoParameters.fieldHeight;

                // Offset for LineNumber
                scanLine = (scanLine - videoParameters.fieldHeight) * 2;
            }
            break;
        }

        case ViewMode::FIELD_VIEW: {
            isFirstField = loadedFieldNumber % 2 != 0;

            // Ensure frameLine accounts for fields and duplicated lines
            if (getStretchField()) {
                frameLine = (scanLine + 1) / 2;

                // Offset for LineNumber
                if (scanLine % 2 == 0 && isFirstField && scanLine > 1) scanLine--;
                if (scanLine % 2 != 0 && !isFirstField) scanLine++;
            } else {
                // Return if coords in unused area
                const auto startOffset = getFrameHeight() / 4;

                if (scanLine <= startOffset || scanLine > startOffset + videoParameters.fieldHeight) {
                    return ScanLineData();
                }

                frameLine = scanLine - startOffset;

                // Offset for LineNumber
                if (isFirstField) scanLine = (frameLine * 2) - 1;
                if (!isFirstField) scanLine = (frameLine * 2);
            }

            break;
        }
    }

    // Set the system and line number
    scanLineData.systemDescription = ldDecodeMetaData.getVideoSystemDescription();
    scanLineData.lineNumber = LineNumber::fromFrame1(scanLine, videoParameters.system);
    const LineNumber &lineNumber = scanLineData.lineNumber;

    // Set the video parameters
    scanLineData.blackIre = videoParameters.black16bIre;
    scanLineData.whiteIre = videoParameters.white16bIre;
    scanLineData.fieldWidth = videoParameters.fieldWidth;
    scanLineData.sampleRate = videoParameters.sampleRate;
    scanLineData.colourBurstStart = videoParameters.colourBurstStart;
    scanLineData.colourBurstEnd = videoParameters.colourBurstEnd;
    scanLineData.activeVideoStart = videoParameters.activeVideoStart;
    scanLineData.activeVideoEnd = videoParameters.activeVideoEnd;

    // Is this line part of the active region?
    scanLineData.isActiveLine = (frameLine - 1) >= videoParameters.firstActiveFrameLine
                                && (frameLine -1) < videoParameters.lastActiveFrameLine;

    // Decode/load first; this can refresh input field vectors.
    const ComponentFrame &componentFrame = getComponentFrame();
    if (componentFrame.getWidth() < videoParameters.fieldWidth
        || frameLine < 1
        || frameLine > componentFrame.getHeight()) {
        return ScanLineData();
    }

    const qint32 firstInputIndex = inputStartIndex;
    const qint32 secondInputIndex = inputStartIndex + 1;
    if (firstInputIndex < 0
        || secondInputIndex < 0
        || firstInputIndex >= inputFields.size()
        || secondInputIndex >= inputFields.size()) {
        return ScanLineData();
    }

    // Get the field video and dropout data
    // Put chroma in "composite" field if using Y/C input.
    const SourceVideo::Data &fieldData = isFirstField ? inputFields[firstInputIndex].data
                            : inputFields[secondInputIndex].data;
    DropOuts &dropouts = isFirstField ? firstField.dropOuts : secondField.dropOuts;

    scanLineData.composite.resize(videoParameters.fieldWidth);
    scanLineData.luma.resize(videoParameters.fieldWidth);
    scanLineData.isDropout.resize(videoParameters.fieldWidth);


    for (qint32 xPosition = 0; xPosition < videoParameters.fieldWidth; xPosition++) {
        // Get the 16-bit composite value for the current pixel (frame data is numbered 0-624 or 0-524)
        scanLineData.composite[xPosition] = fieldData[(lineNumber.field0() * videoParameters.fieldWidth) + xPosition];

        // Get the decoded luma value for the current pixel (only computed in the active region)
        scanLineData.luma[xPosition] = static_cast<qint32>(componentFrame.y(frameLine - 1)[xPosition]);

        scanLineData.isDropout[xPosition] = false;
        for (qint32 doCount = 0; doCount < dropouts.size(); doCount++) {
            if (dropouts.fieldLine(doCount) == lineNumber.field1()) {
                if (xPosition >= dropouts.startx(doCount) && xPosition <= dropouts.endx(doCount)) scanLineData.isDropout[xPosition] = true;
            }
        }
    }

    if (sourceMode == BOTH_SOURCES
        && firstInputIndex >= 0
        && secondInputIndex >= 0
        && firstInputIndex < chromaInputFields.size()
        && secondInputIndex < chromaInputFields.size()) {
        const auto &chromaFieldData = isFirstField ? chromaInputFields[firstInputIndex].data
                     : chromaInputFields[secondInputIndex].data;
        // We need to offset by half a word since the input is 16-bit unsigned.
        const short offset = std::numeric_limits<int16_t>::min();
        scanLineData.chroma.resize(videoParameters.fieldWidth);
        for (qint32 xPosition = 0; xPosition < videoParameters.fieldWidth; xPosition++) {
            scanLineData.chroma[xPosition] = chromaFieldData[(lineNumber.field0() * videoParameters.fieldWidth) + xPosition] + offset;
            // Combine Y and C to "simulate" composite for the scope.
            scanLineData.composite[xPosition] += scanLineData.chroma[xPosition];
        }

    }

    return scanLineData;
}

TbcSource::FieldTimingData TbcSource::getFieldTimingData()
{
    FieldTimingData timingData;

    if (metadataOnly || loadedFrameNumber == -1) {
        return timingData;
    }

    const LdDecodeMetaData::VideoParameters videoParameters = ldDecodeMetaData.getVideoParameters();
    if (videoParameters.fieldWidth <= 0) {
        return timingData;
    }

    loadInputFields();

    const qint32 firstInputIndex = inputStartIndex;
    const qint32 secondInputIndex = inputStartIndex + 1;
    if (firstInputIndex < 0
        || secondInputIndex < 0
        || firstInputIndex >= inputFields.size()
        || secondInputIndex >= inputFields.size()) {
        return timingData;
    }

    const auto toStdVector = [](const SourceVideo::Data &sourceSamples) {
        std::vector<uint16_t> samples;
        samples.reserve(sourceSamples.size());
        for (const quint16 sample : sourceSamples) {
            samples.push_back(static_cast<uint16_t>(sample));
        }
        return samples;
    };

    const auto calculateFieldHeight = [&](const SourceVideo::Data &sourceSamples) {
        if (videoParameters.fieldWidth <= 0) {
            return 0;
        }
        const qint32 derivedHeight = sourceSamples.size() / videoParameters.fieldWidth;
        return derivedHeight > 0 ? derivedHeight : videoParameters.fieldHeight;
    };

    const bool showBothFields = (viewMode == ViewMode::FRAME_VIEW
                                 || viewMode == ViewMode::SPLIT_VIEW
                                 || viewMode == ViewMode::RGB_SCOPE_VIEW);
    const bool useFirstField = (loadedFieldNumber % 2) != 0;
    const qint32 selectedInputIndex = useFirstField ? firstInputIndex : secondInputIndex;

    timingData.fieldWidth = videoParameters.fieldWidth;
    if (showBothFields) {
        timingData.firstFieldNumber = firstFieldNumber;
        timingData.secondFieldNumber = secondFieldNumber;
        timingData.hasSecondField = true;
        timingData.firstFieldComposite = toStdVector(inputFields[firstInputIndex].data);
        timingData.secondFieldComposite = toStdVector(inputFields[secondInputIndex].data);
        timingData.firstFieldHeight = calculateFieldHeight(inputFields[firstInputIndex].data);
        timingData.secondFieldHeight = calculateFieldHeight(inputFields[secondInputIndex].data);
    } else {
        timingData.firstFieldNumber = useFirstField ? firstFieldNumber : secondFieldNumber;
        timingData.hasSecondField = false;
        timingData.firstFieldComposite = toStdVector(inputFields[selectedInputIndex].data);
        timingData.firstFieldHeight = calculateFieldHeight(inputFields[selectedInputIndex].data);
        timingData.secondFieldHeight = 0;
    }

    if (sourceMode == BOTH_SOURCES
        && firstInputIndex >= 0
        && secondInputIndex >= 0
        && firstInputIndex < chromaInputFields.size()
        && secondInputIndex < chromaInputFields.size()) {
        const auto populateYcData = [&](qint32 inputIndex,
                                        std::vector<uint16_t> &ySamples,
                                        std::vector<uint16_t> &cSamples) {
            ySamples = toStdVector(inputFields[inputIndex].data);
            cSamples = toStdVector(chromaInputFields[inputIndex].data);
        };

        if (showBothFields) {
            populateYcData(firstInputIndex, timingData.firstFieldLuma, timingData.firstFieldChroma);
            populateYcData(secondInputIndex, timingData.secondFieldLuma, timingData.secondFieldChroma);
        } else {
            populateYcData(selectedInputIndex, timingData.firstFieldLuma, timingData.firstFieldChroma);
        }
    }

    timingData.valid = !timingData.firstFieldComposite.empty();
    return timingData;
}

// Method to return the decoded VBI data for the frame
VbiDecoder::Vbi TbcSource::getFrameVbi()
{
    if (loadedFrameNumber == -1) return VbiDecoder::Vbi();

    return vbiDecoder.decodeFrame(firstField.vbi.vbiData[0], firstField.vbi.vbiData[1], firstField.vbi.vbiData[2],
                                  secondField.vbi.vbiData[0], secondField.vbi.vbiData[1], secondField.vbi.vbiData[2]);
}

// Method returns true if the VBI is valid for the frame
bool TbcSource::getIsFrameVbiValid()
{
    if (loadedFrameNumber == -1) return false;

    if (firstField.vbi.vbiData[0] == -1 || firstField.vbi.vbiData[1] == -1 || firstField.vbi.vbiData[2] == -1) return false;
    if (secondField.vbi.vbiData[0] == -1 || secondField.vbi.vbiData[1] == -1 || secondField.vbi.vbiData[2] == -1) return false;

    return true;
}

// Method to return the decoded VIDEO ID data for the frame
VideoIdDecoder::VideoId TbcSource::getFrameVideoId()
{
    if (loadedFrameNumber == -1) return VideoIdDecoder::VideoId();

    return videoIdDecoder.decodeFrame(firstField.ntsc.videoIdData, secondField.ntsc.videoIdData);
}

// Method returns true if the VIDEO ID is present for the frame
bool TbcSource::getIsFrameVideoIdValid()
{
    if (loadedFrameNumber == -1) return false;

    if (!firstField.ntsc.isVideoIdDataValid || !secondField.ntsc.isVideoIdDataValid) return false;

    return true;
}

// Method to return the decoded VITC data for the frame
VitcDecoder::Vitc TbcSource::getFrameVitc()
{
    if (loadedFrameNumber == -1) return VitcDecoder::Vitc();

    const VideoSystem system = ldDecodeMetaData.getVideoParameters().system;
    if (firstField.vitc.inUse) return vitcDecoder.decode(firstField.vitc.vitcData, system);
    if (secondField.vitc.inUse) return vitcDecoder.decode(secondField.vitc.vitcData, system);

    return VitcDecoder::Vitc();
}

// Method returns true if the VITC is valid for the frame
bool TbcSource::getIsFrameVitcValid()
{
    if (loadedFrameNumber == -1) return false;

    return firstField.vitc.inUse || secondField.vitc.inUse;
}

// Method to get the field number of the first field of the frame
qint32 TbcSource::getFirstFieldNumber() const
{
    if (loadedFrameNumber == -1) return 0;
    return firstFieldNumber;
}

// Method to get the field number of the second field of the frame
qint32 TbcSource::getSecondFieldNumber() const
{
    if (loadedFrameNumber == -1) return 0;
    return secondFieldNumber;
}

qint32 TbcSource::getCcData0() const
{
    if (loadedFrameNumber == -1) return 0;

    if (firstField.closedCaption.data0 != -1) return firstField.closedCaption.data0;
    return secondField.closedCaption.data0;
}

qint32 TbcSource::getCcData1() const
{
    if (loadedFrameNumber == -1) return 0;

    if (firstField.closedCaption.data1 != -1) return firstField.closedCaption.data1;
    return secondField.closedCaption.data1;
}

void TbcSource::setChromaConfiguration(const PalColour::Configuration &_palConfiguration,
                                       const Comb::Configuration &_ntscConfiguration)
{
    ntscColour.requestNnTransform3DCancel();
    invalidateImageCache();

    palConfiguration = _palConfiguration;
    ntscConfiguration = _ntscConfiguration;

    configureChromaDecoder();
}

void TbcSource::requestNnTransform3DCancel()
{
    ntscColour.requestNnTransform3DCancel();
}

const PalColour::Configuration &TbcSource::getPalConfiguration() const
{
    return palConfiguration;
}

const Comb::Configuration &TbcSource::getNtscConfiguration() const
{
    return ntscConfiguration;
}

const MonoDecoder::MonoConfiguration &TbcSource::getMonoConfiguration() const
{
    return monoConfiguration;
}

const OutputWriter::Configuration &TbcSource::getOutputConfiguration() const
{
    return outputConfiguration;
}

bool TbcSource::hasChapterMap() const
{
    return !chapterMap.isEmpty();
}

// Return the frame number of the start of the next chapter
qint32 TbcSource::startOfNextChapter(qint32 currentFrameNumber)
{
    // Do we have a chapter map?
    if (chapterMap.size() == 0) return getNumberOfFrames();

    qint32 mapLocation = -1;
    for (qint32 i = 0; i < chapterMap.size(); i++) {
        if (chapterMap[i] > currentFrameNumber) {
            mapLocation = i;
            break;
        }
    }

    // Found?
    if (mapLocation != -1) {
        return chapterMap[mapLocation];
    }

    return getNumberOfFrames();
}

// Return the frame number of the start of the current chapter
qint32 TbcSource::startOfChapter(qint32 currentFrameNumber)
{
    // Do we have a chapter map?
    if (chapterMap.size() == 0) return 1;

    qint32 mapLocation = -1;
    for (qint32 i = chapterMap.size() - 1; i >= 0; i--) {
        if (chapterMap[i] < currentFrameNumber) {
            mapLocation = i;
            break;
        }
    }

    // Found?
    if (mapLocation != -1) {
        return chapterMap[mapLocation];
    }

    return 1;
}


// Private methods ----------------------------------------------------------------------------------------------------

// Re-initialise state for a new source video
void TbcSource::resetState()
{
    // Default frame image options
    chromaOn = true;
    chromaDecodeMode = HYBRID_CHROMA_MODE;
    outputConfiguration.trimToActiveRegion = false;
    outputConfiguration.fullFrameDecode = false;
    dropoutsOn = false;
    viewMode = ViewMode::FRAME_VIEW;
    reverseFoOn = false;
    sourceReady = false;
    sourceMode = ONE_SOURCE;
    metadataOnly = false;
    requestedMetadataFilename.clear();

    // Cache state
    loadedFrameNumber = -1;
    loadedFieldNumber = -1;
    inputFieldsValid = false;
    decodedFrameValid = false;
    rgbScopeCache = QImage();
    rgbScopeCacheValid = false;
    rgbScopeCacheSize = QSize();
    yuvRangeScopeCache = QImage();
    yuvRangeScopeCacheValid = false;
    yuvRangeScopeCacheSize = QSize();
    yuvRangeScopeCacheLumaMin = 16;
    yuvRangeScopeCacheLumaMax = 235;
    yuvRangeScopeCacheChromaMin = 16;
    yuvRangeScopeCacheChromaMax = 240;
    yuvRangeOverlayCache = QImage();
    yuvRangeOverlayCacheValid = false;
    yuvRangeOverlayCacheLumaMin = 16;
    yuvRangeOverlayCacheLumaMax = 235;
    yuvRangeOverlayCacheChromaMin = 16;
    yuvRangeOverlayCacheChromaMax = 240;
    yuvRangeOverlayCacheAlpha = 180;
    yuvRangeOverlayCacheViewMode = ViewMode::FRAME_VIEW;
    yuvRangeOverlayCacheStretchField = false;
    yuvRangeOverlayCacheFieldNumber = -1;
    cache = QImage();
    cacheValid = false;
}

// Mark any cached data for the current field/frame as invalid
void TbcSource::invalidateImageCache()
{
    // Note this includes the input fields, because the number of fields we
    // load depends on the decoder parameters
    inputFieldsValid = false;
    decodedFrameValid = false;
    rgbScopeCacheValid = false;
    rgbScopeCacheSize = QSize();
    yuvRangeScopeCacheValid = false;
    yuvRangeScopeCacheSize = QSize();
    yuvRangeOverlayCacheValid = false;
    cacheValid = false;
}

// Configure the chroma decoder for its settings and the VideoParameters
void TbcSource::configureChromaDecoder()
{
    // Configure the chroma decoder
    LdDecodeMetaData::VideoParameters videoParameters = ldDecodeMetaData.getVideoParameters();
    LdDecodeMetaData::VideoParameters decodeVideoParameters = videoParameters;
    const bool applyHybridFullFrameBounds = (chromaDecodeMode == HYBRID_CHROMA_MODE);
    if (outputConfiguration.fullFrameDecode || applyHybridFullFrameBounds) {
        applyFullFrameDecodeBounds(decodeVideoParameters);
    }
	
    if (videoParameters.system == PAL || videoParameters.system == PAL_M) {
		monoConfiguration.yNRLevel = palConfiguration.yNRLevel;
        palColour.updateConfiguration(decodeVideoParameters, palConfiguration);
    } else {
		monoConfiguration.yNRLevel = ntscConfiguration.yNRLevel;
        ntscColour.updateConfiguration(decodeVideoParameters, ntscConfiguration);
    }
	monoDecoder.updateConfiguration(decodeVideoParameters, monoConfiguration);

    // Configure the OutputWriter.
    // Because we have padding disabled, this won't change the VideoParameters.
    outputWriter.updateConfiguration(videoParameters, outputConfiguration);
}

void TbcSource::applyChromaSettingsFromMetadata(const LdDecodeMetaData::VideoParameters &videoParameters)
{
    if (videoParameters.chromaGain >= 0.0) {
        palConfiguration.chromaGain = videoParameters.chromaGain;
        ntscConfiguration.chromaGain = videoParameters.chromaGain;
    }

    if (videoParameters.chromaPhase != -1.0) {
        palConfiguration.chromaPhase = videoParameters.chromaPhase;
        ntscConfiguration.chromaPhase = videoParameters.chromaPhase;
    }

    if (videoParameters.lumaNR >= 0.0) {
        palConfiguration.yNRLevel = videoParameters.lumaNR;
        ntscConfiguration.yNRLevel = videoParameters.lumaNR;
        monoConfiguration.yNRLevel = videoParameters.lumaNR;
    }
    if (videoParameters.ntscAdaptive != -1) {
        ntscConfiguration.adaptive = (videoParameters.ntscAdaptive == 1);
    }

    if (videoParameters.ntscAdaptThreshold >= 0.0) {
        ntscConfiguration.adaptThreshold = videoParameters.ntscAdaptThreshold;
    }

    if (videoParameters.ntscChromaWeight >= 0.0) {
        ntscConfiguration.chromaWeight = videoParameters.ntscChromaWeight;
    }

    if (videoParameters.ntscPhaseCompensation != -1) {
        ntscConfiguration.phaseCompensation = (videoParameters.ntscPhaseCompensation == 1);
    } else if (videoParameters.system == NTSC) {
        ntscConfiguration.phaseCompensation = true;
    }

    if (videoParameters.palTransformThreshold >= 0.0) {
        palConfiguration.transformThreshold = videoParameters.palTransformThreshold;
    }

    if (!videoParameters.chromaDecoder.isEmpty()) {
        const QString decoder = videoParameters.chromaDecoder.toLower();
        if (videoParameters.system == PAL || videoParameters.system == PAL_M) {
            if (decoder == "pal2d") {
                palConfiguration.chromaFilter = PalColour::palColourFilter;
            } else if (decoder == "transform2d") {
                palConfiguration.chromaFilter = PalColour::transform2DFilter;
            } else if (decoder == "transform3d") {
                palConfiguration.chromaFilter = PalColour::transform3DFilter;
            } else if (decoder == "mono") {
                palConfiguration.chromaFilter = PalColour::mono;
            }
        } else if (videoParameters.system == NTSC) {
            if (decoder == "ntsc1d") {
                ntscConfiguration.dimensions = 1;
                ntscConfiguration.nnTransform3D = false;
            } else if (decoder == "ntsc2d") {
                ntscConfiguration.dimensions = 2;
                ntscConfiguration.nnTransform3D = false;
            } else if (decoder == "ntsc3d") {
                ntscConfiguration.dimensions = 3;
                ntscConfiguration.nnTransform3D = false;
            } else if (decoder == "nntransform3d" || decoder == "nntsc3d") {
                ntscConfiguration.dimensions = 3;
                ntscConfiguration.nnTransform3D = true;
            } else if (decoder == "mono") {
                ntscConfiguration.dimensions = 0;
                ntscConfiguration.nnTransform3D = false;
            }
        }
    }
}

// Ensure the SourceFields for the current frame are loaded
void TbcSource::loadInputFields()
{
    if (metadataOnly) return;
    if (inputFieldsValid) return;

    // Work out how many frames ahead/behind we need to fetch
    qint32 lookBehind, lookAhead;
    if (getSystem() == PAL || getSystem() == PAL_M) {
        lookBehind = palConfiguration.getLookBehind();
        lookAhead = palConfiguration.getLookAhead();
    } else {
        lookBehind = ntscConfiguration.getLookBehind();
        lookAhead = ntscConfiguration.getLookAhead();
    }

    if (sourceMode == CHROMA_SOURCE) {
        // Load chroma directly into inputFields
        SourceField::loadFields(chromaSourceVideo, ldDecodeMetaData,
                                loadedFrameNumber, 1, lookBehind, lookAhead,
                                inputFields, inputStartIndex, inputEndIndex);
    } else {
        // Load the only source, or luma, into inputFields
        SourceField::loadFields(sourceVideo, ldDecodeMetaData,
                                loadedFrameNumber, 1, lookBehind, lookAhead,
                                inputFields, inputStartIndex, inputEndIndex);
    }

    if (sourceMode == BOTH_SOURCES) {
        // Load chroma into chromaInputFields
        SourceField::loadFields(chromaSourceVideo, ldDecodeMetaData,
                                loadedFrameNumber, 1, lookBehind, lookAhead,
                                chromaInputFields, inputStartIndex, inputEndIndex);
		if(combine)
		{
			// Separate chroma is offset (see chroma_to_u16 in vhsdecode/chroma.py)
			static constexpr qint32 CHROMA_OFFSET = 32767;

			// Add chroma to luma, removing the offset
			for (qint32 fieldIndex = inputStartIndex; fieldIndex < inputEndIndex; fieldIndex++) {
				auto &sourceData = inputFields[fieldIndex].data;
				const auto &chromaData = chromaInputFields[fieldIndex].data;

				for (qint32 i = 0; i < sourceData.size(); i++) {
					qint32 sum = static_cast<qint32>(sourceData[i]) + static_cast<qint32>(chromaData[i]) - CHROMA_OFFSET;
					sourceData[i] = static_cast<quint16>(qBound(0, sum, 65535));
				}
			}
		}
    }

    inputFieldsValid = true;
}

// Ensure the current frame has been decoded
void TbcSource::decodeFrame()
{
    if (metadataOnly) return;
    if (decodedFrameValid) return;

    loadInputFields();
	
	bool isMono = false;

    // Decode the current frame to components
    componentFrames.resize(1);
    yFrames.resize(1);
    cFrames.resize(1);
	if(!combine && sourceMode == BOTH_SOURCES)
	{
		monoDecoder.decodeFrames(inputFields, inputStartIndex, inputEndIndex, yFrames);
		if ((getSystem() == PAL || getSystem() == PAL_M) && palConfiguration.chromaFilter != PalColour::mono) {
			// PAL source
			palColour.decodeFrames(chromaInputFields, inputStartIndex, inputEndIndex, cFrames);
		} else if(getSystem() == NTSC && ntscConfiguration.dimensions != 0){
			// NTSC source
			ntscColour.decodeFrames(chromaInputFields, inputStartIndex, inputEndIndex, cFrames);
		}
		else
		{
			isMono = true;
		}
		
		for (qint32 fieldIndex = inputStartIndex, frameIndex = 0; fieldIndex < inputEndIndex; fieldIndex += 2, frameIndex++)
		{
			//isMono ? cFrames[frameIndex].init(ldDecodeMetaData.getVideoParameters(), false);
			componentFrames[frameIndex].init(ldDecodeMetaData.getVideoParameters(), false);
			componentFrames[frameIndex].setY(*yFrames[frameIndex].getY());
			if(!isMono){	
				componentFrames[frameIndex].setU(*cFrames[frameIndex].getU());
				componentFrames[frameIndex].setV(*cFrames[frameIndex].getV());
			}
		}
	}
	else
	{
		if (getSystem() == PAL || getSystem() == PAL_M) {
			if(palConfiguration.chromaFilter == palColour.ChromaFilterMode::mono){
				monoDecoder.decodeFrames(inputFields, inputStartIndex, inputEndIndex, componentFrames);
			} else {
				// PAL source
				palColour.decodeFrames(inputFields, inputStartIndex, inputEndIndex, componentFrames);
			}
		} else {
			if(ntscConfiguration.dimensions == 0){
				monoDecoder.decodeFrames(inputFields, inputStartIndex, inputEndIndex, componentFrames);
			} else {
				// NTSC source
				ntscColour.decodeFrames(inputFields, inputStartIndex, inputEndIndex, componentFrames);
			}
		}
	}

    decodedFrameValid = true;
}

// Method to create a QImage for a source video frame
QImage TbcSource::generateQImage(ViewMode renderViewMode, const QSize &targetSize)
{
    // Get the metadata for the video parameters
    LdDecodeMetaData::VideoParameters videoParameters = ldDecodeMetaData.getVideoParameters();

    // Calculate the frame height
    const qint32 frameHeight = (videoParameters.fieldHeight * 2) - 1;
    const qint32 frameWidth = videoParameters.fieldWidth;

    // Set the frame image
    auto outputImage = QImage(frameWidth, frameHeight, QImage::Format_RGB32);

    // Fill the QImage with black
    outputImage.fill(Qt::black);

    // Create RGB32 data and set h/w + offsets
    QVector<QRgb> rgbData;
    qint32 inputHeight, inputWidth, fieldHeight, inputOffset, outputOffset;

    if (chromaOn) {
        // Show debug information
        if (renderViewMode == ViewMode::FIELD_VIEW) {
            tbcDebugStream() << "TbcSource::generateQImage(): Generating a chroma image from field"
                             << loadedFieldNumber << "(" << videoParameters.fieldWidth << "x" << videoParameters.fieldHeight << ")";
        } else {
            tbcDebugStream() << "TbcSource::generateQImage(): Generating a chroma image from frame"
                             << loadedFrameNumber << "(" << videoParameters.fieldWidth << "x" << frameHeight << ")";
        }

        inputHeight = frameHeight;
        inputWidth = frameWidth;
        fieldHeight = videoParameters.fieldHeight;
        inputOffset = 0;
        outputOffset = 0;

        // Chroma decode the current frame
        decodeFrame();

        // Convert component video to RGB
        OutputFrame outputFrame;
        outputWriter.convert(componentFrames[0], outputFrame);

        const auto rgb48Ptr = reinterpret_cast<quint16 *>(outputFrame.data());
        rgbData.reserve(inputHeight * inputWidth * 3);

        // Create RGB32 from RGB48
        for (auto i = 0; i < inputHeight * inputWidth * 3; i += 3) {
            rgbData.push_back(qRgb(static_cast<qint32>(rgb48Ptr[i + 0] / 256),
                                   static_cast<qint32>(rgb48Ptr[i + 1] / 256),
                                   static_cast<qint32>(rgb48Ptr[i + 2] / 256)));
        }
    } else {
        // Show debug information
        if (renderViewMode == ViewMode::FIELD_VIEW) {
            tbcDebugStream() << "TbcSource::generateQImage(): Generating a source image from field"
                             << loadedFieldNumber << "(" << videoParameters.fieldWidth << "x" << videoParameters.fieldHeight << ")";
        } else {
            tbcDebugStream() << "TbcSource::generateQImage(): Generating a source image from frame"
                             << loadedFrameNumber << "(" << videoParameters.fieldWidth << "x" << frameHeight << ")";
        }

        inputHeight = frameHeight;
        inputWidth = frameWidth;
        fieldHeight = videoParameters.fieldHeight;
        inputOffset = 0;
        outputOffset = 0;

        // Load SourceFields for the current frame
        loadInputFields();

        // Get pointers to the 16-bit greyscale data
        const quint16 *firstFieldPointer = inputFields[inputStartIndex].data.data();
        const quint16 *secondFieldPointer = inputFields[inputStartIndex + 1].data.data();
        rgbData.reserve(inputHeight * inputWidth * 3);

        // Create RGB32 from Gray16
        for (auto y = 0; y < fieldHeight * 2; y++) {
            auto *ptr = y % 2 == 0 ? firstFieldPointer : secondFieldPointer;

            for (auto x = 0; x < inputWidth; x++) {
                auto value = static_cast<qint32>(ptr[((y / 2) * inputWidth) + x] / 256);
                rgbData.push_back(qRgb(value, value, value));
            }
        }
    }

    if (renderViewMode == ViewMode::RGB_SCOPE_VIEW) {
        const QImage scopeImage = renderRgbParadeScopeImage(rgbData, frameWidth, frameHeight, videoParameters, targetSize);
        return scopeImage.isNull() ? outputImage : scopeImage;
    }

    // Copy RGB data to QImage
    switch (renderViewMode) {
        case ViewMode::FRAME_VIEW: {
            for (auto y = 0; y < inputHeight; y++) {
                auto *outputLine = reinterpret_cast<QRgb*>(outputImage.scanLine(y + inputOffset));
                std::copy_n(&rgbData[y * inputWidth], inputWidth, &outputLine[outputOffset]);
            }
            break;
        }

        case ViewMode::RGB_SCOPE_VIEW: {
            for (auto y = 0; y < inputHeight; y++) {
                auto *outputLine = reinterpret_cast<QRgb*>(outputImage.scanLine(y + inputOffset));
                std::copy_n(&rgbData[y * inputWidth], inputWidth, &outputLine[outputOffset]);
            }
            break;
        }

        case ViewMode::SPLIT_VIEW: {
            for (auto y = 0; y < inputHeight; y++) {
                const auto startOffset = (inputOffset / 2) + (y % 2 == 0 ? 0 : (frameHeight / 2) + 1);
                auto *outputLine = reinterpret_cast<QRgb*>(outputImage.scanLine((y / 2) + startOffset));
                std::copy_n(&rgbData[y * inputWidth], inputWidth, &outputLine[outputOffset]);
            }
            break;
        }

        case ViewMode::FIELD_VIEW: {
            auto startOffset = getStretchField() ? inputOffset : (frameHeight / 4) + (inputOffset / 2);
            auto height = getStretchField() ? inputHeight : fieldHeight;
            auto fieldY = loadedFieldNumber % 2 ? 0 : 1;
            qint32 maxFieldY = inputHeight - 1;
            if (maxFieldY >= 0 && (maxFieldY % 2) != (fieldY % 2)) {
                maxFieldY -= 1;
            }

            for (auto y = 0; y < height; y++) {
                auto *outputLine = reinterpret_cast<QRgb*>(outputImage.scanLine(y + startOffset));
                const qint32 clampedFieldY = qBound<qint32>(0, fieldY, qMax<qint32>(0, maxFieldY));
                std::copy_n(&rgbData[clampedFieldY * inputWidth], inputWidth, &outputLine[outputOffset]);

                // Only increment fieldY every other iteration, or if field stretch disabled
                if (!getStretchField() || y % 2 != 0) {
                    fieldY += 2;
                }
            }
            break;
        }
    }

    return outputImage;
}

// Generate the data points for the Drop-out and SNR analysis graphs, and the chapter map.
// We do these all at the same time to reduce calls to the metadata.
void TbcSource::generateData()
{
    dropoutGraphData.clear();
    visibleDropoutGraphData.clear();
    blackSnrGraphData.clear();
    whiteSnrGraphData.clear();

    dropoutGraphData.resize(ldDecodeMetaData.getNumberOfFrames());
    visibleDropoutGraphData.resize(ldDecodeMetaData.getNumberOfFrames());
    blackSnrGraphData.resize(ldDecodeMetaData.getNumberOfFrames());
    whiteSnrGraphData.resize(ldDecodeMetaData.getNumberOfFrames());

    bool ignoreChapters = false;
    qint32 lastChapter = -1;
    qint32 giveUpCounter = 0;
    chapterMap.clear();

    const qint32 numFrames = ldDecodeMetaData.getNumberOfFrames();
    for (qint32 frameNumber = 0; frameNumber < numFrames; frameNumber++) {
        double doLength = 0;
        double visibleDoLength = 0;
        double blackSnrTotal = 0;
        double whiteSnrTotal = 0;

        // SNR data may be missing in some fields, so we count the points to prevent
        // the frame average from being thrown-off by missing data
        double blackSnrPoints = 0;
        double whiteSnrPoints = 0;

        const LdDecodeMetaData::Field &firstField = ldDecodeMetaData.getField(ldDecodeMetaData.getFirstFieldNumber(frameNumber + 1));
        const LdDecodeMetaData::Field &secondField = ldDecodeMetaData.getField(ldDecodeMetaData.getSecondFieldNumber(frameNumber + 1));

        // Get the first field DOs
        if (firstField.dropOuts.size() > 0) {
            // Calculate the total length of the dropouts
            for (qint32 i = 0; i < firstField.dropOuts.size(); i++) {
                doLength += static_cast<double>(firstField.dropOuts.endx(i) - firstField.dropOuts.startx(i));
            }
        }

        // Get the second field DOs
        if (secondField.dropOuts.size() > 0) {
            // Calculate the total length of the dropouts
            for (qint32 i = 0; i < secondField.dropOuts.size(); i++) {
                doLength += static_cast<double>(secondField.dropOuts.endx(i) - secondField.dropOuts.startx(i));
            }
        }

        // Get the first field visible DOs
        const LdDecodeMetaData::VideoParameters &videoParameters = ldDecodeMetaData.getVideoParameters();

        if (firstField.dropOuts.size() > 0) {
            // Calculate the total length of the visible dropouts
            for (qint32 i = 0; i < firstField.dropOuts.size(); i++) {
                // Does the drop out start in the visible area?
                if ((firstField.dropOuts.fieldLine(i) >= videoParameters.firstActiveFieldLine) &&
                    (firstField.dropOuts.fieldLine(i) <= videoParameters.lastActiveFieldLine)) {
                    // Check if dropout overlaps with active video area
                    qint32 dropStartx = firstField.dropOuts.startx(i);
                    qint32 dropEndx = firstField.dropOuts.endx(i);
                    
                    if (dropStartx < videoParameters.activeVideoEnd && dropEndx > videoParameters.activeVideoStart) {
                        // Clamp to active video boundaries
                        qint32 startx = qMax(dropStartx, videoParameters.activeVideoStart);
                        qint32 endx = qMin(dropEndx, videoParameters.activeVideoEnd);
                        
                        if (endx > startx) {
                            visibleDoLength += static_cast<double>(endx - startx);
                        }
                    }
                }
            }
        }

        // Get the second field visible DOs
        if (secondField.dropOuts.size() > 0) {
            // Calculate the total length of the visible dropouts
            for (qint32 i = 0; i < secondField.dropOuts.size(); i++) {
                // Does the drop out start in the visible area?
                if ((secondField.dropOuts.fieldLine(i) >= videoParameters.firstActiveFieldLine) &&
                    (secondField.dropOuts.fieldLine(i) <= videoParameters.lastActiveFieldLine)) {
                    // Check if dropout overlaps with active video area
                    qint32 dropStartx = secondField.dropOuts.startx(i);
                    qint32 dropEndx = secondField.dropOuts.endx(i);
                    
                    if (dropStartx < videoParameters.activeVideoEnd && dropEndx > videoParameters.activeVideoStart) {
                        // Clamp to active video boundaries
                        qint32 startx = qMax(dropStartx, videoParameters.activeVideoStart);
                        qint32 endx = qMin(dropEndx, videoParameters.activeVideoEnd);
                        
                        if (endx > startx) {
                            visibleDoLength += static_cast<double>(endx - startx);
                        }
                    }
                }
            }
        }

        // Get the first field SNRs
        if (firstField.vitsMetrics.inUse) {
            if (firstField.vitsMetrics.bPSNR > 0) {
                blackSnrTotal += firstField.vitsMetrics.bPSNR;
                blackSnrPoints++;
            }
            if (firstField.vitsMetrics.wSNR > 0) {
                whiteSnrTotal += firstField.vitsMetrics.wSNR;
                whiteSnrPoints++;
            }
        }

        // Get the second field SNRs
        if (secondField.vitsMetrics.inUse) {
            if (secondField.vitsMetrics.bPSNR > 0) {
                blackSnrTotal += secondField.vitsMetrics.bPSNR;
                blackSnrPoints++;
            }
            if (secondField.vitsMetrics.wSNR > 0) {
                whiteSnrTotal += secondField.vitsMetrics.wSNR;
                whiteSnrPoints++;
            }
        }

        // Add the result to the vectors
        dropoutGraphData[frameNumber] = doLength;
        visibleDropoutGraphData[frameNumber] = visibleDoLength;
        blackSnrGraphData[frameNumber] = blackSnrTotal / blackSnrPoints; // Calc average for frame
        whiteSnrGraphData[frameNumber] = whiteSnrTotal / whiteSnrPoints; // Calc average for frame

        if (ignoreChapters) continue;

        // Decode the VBI
        VbiDecoder::Vbi vbi = vbiDecoder.decodeFrame(
            firstField.vbi.vbiData[0], firstField.vbi.vbiData[1], firstField.vbi.vbiData[2],
            secondField.vbi.vbiData[0], secondField.vbi.vbiData[1], secondField.vbi.vbiData[2]);

        // Get the chapter number
        qint32 currentChapter = vbi.chNo;
        if (currentChapter != -1) {
            if (currentChapter != lastChapter) {
                lastChapter = currentChapter;
                chapterMap.append(frameNumber);
            } else giveUpCounter++;
        }

        if (frameNumber == 100 && giveUpCounter < 50) {
            tbcDebugStream() << "Not seeing valid chapter numbers, giving up chapter mapping";
            ignoreChapters = true;
        }
    }
}

bool TbcSource::startBackgroundLoad(QString sourceFilename)
{
    // Open the TBC metadata file
    tbcDebugStream() << "TbcSource::startBackgroundLoad(): Processing metadata...";
    emit busy("Processing metadata...");

    const QFileInfo sourceInfo(sourceFilename);
    const QString sourceFileName = sourceInfo.fileName();
    const QString sourceDir = sourceInfo.absolutePath();
    const QString lowerSourceFileName = sourceFileName.toLower();
    const bool isSuffixChroma = lowerSourceFileName.endsWith("_chroma.tbc");
    const bool isPrefixChroma = lowerSourceFileName.startsWith("chroma_") && lowerSourceFileName.endsWith(".tbc");
    const bool isYtbc = lowerSourceFileName.endsWith(".ytbc");
    const bool isCtbc = lowerSourceFileName.endsWith(".ctbc");
    const bool isTbcy = lowerSourceFileName.endsWith(".tbcy");
    const bool isTbcc = lowerSourceFileName.endsWith(".tbcc");
    const bool isAltChroma = isCtbc || isTbcc;
    const bool isAltLuma = isYtbc || isTbcy;
    const bool isChromaTbc = isSuffixChroma || isPrefixChroma || isAltChroma;

    QString lumaFilename = sourceFilename;
    QString chromaSourceFilename;

    if (isSuffixChroma) {
        const QString baseName = sourceFileName.left(sourceFileName.size() - QString("_chroma.tbc").size());
        lumaFilename = QDir(sourceDir).filePath(baseName + ".tbc");
        chromaSourceFilename = sourceFilename;
    } else if (isPrefixChroma) {
        const QString lumaFileName = sourceFileName.mid(QString("chroma_").size());
        lumaFilename = QDir(sourceDir).filePath(lumaFileName);
        chromaSourceFilename = sourceFilename;
    } else if (isAltChroma) {
        const QString baseName = sourceInfo.completeBaseName();
        const QString lumaExtension = isCtbc ? QStringLiteral(".ytbc") : QStringLiteral(".tbcy");
        lumaFilename = QDir(sourceDir).filePath(baseName + lumaExtension);
        chromaSourceFilename = sourceFilename;
    } else {
        const QString baseName = sourceInfo.completeBaseName();
        if (isAltLuma) {
            const QString chromaExtension = isYtbc ? QStringLiteral(".ctbc") : QStringLiteral(".tbcc");
            const QString chromaCandidate = QDir(sourceDir).filePath(baseName + chromaExtension);
            if (QFileInfo::exists(chromaCandidate)) {
                chromaSourceFilename = chromaCandidate;
            }
        } else {
            const QString suffixCandidate = QDir(sourceDir).filePath(baseName + "_chroma.tbc");
            const QString prefixCandidate = QDir(sourceDir).filePath("chroma_" + baseName + ".tbc");
            const QString ctbcCandidate = QDir(sourceDir).filePath(baseName + ".ctbc");
            const QString tbccCandidate = QDir(sourceDir).filePath(baseName + ".tbcc");
            if (QFileInfo::exists(suffixCandidate)) {
                chromaSourceFilename = suffixCandidate;
            } else if (QFileInfo::exists(prefixCandidate)) {
                chromaSourceFilename = prefixCandidate;
            } else if (QFileInfo::exists(ctbcCandidate)) {
                chromaSourceFilename = ctbcCandidate;
            } else if (QFileInfo::exists(tbccCandidate)) {
                chromaSourceFilename = tbccCandidate;
            }
        }
    }
    auto metadataCandidatesForFile = [](const QFileInfo &info) {
        QStringList candidates;
        candidates << (info.filePath() + QStringLiteral(".json"));
        candidates << (info.filePath() + QStringLiteral(".db"));
        const QString basePath = QDir(info.absolutePath()).filePath(info.completeBaseName());
        const QString baseJsonCandidate = basePath + QStringLiteral(".json");
        if (!candidates.contains(baseJsonCandidate)) {
            candidates << baseJsonCandidate;
        }
        const QString baseDbCandidate = basePath + QStringLiteral(".db");
        if (!candidates.contains(baseDbCandidate)) {
            candidates << baseDbCandidate;
        }
        const QString suffix = info.suffix().toLower();
        if (suffix == QLatin1String("ytbc")
            || suffix == QLatin1String("ctbc")
            || suffix == QLatin1String("tbcy")
            || suffix == QLatin1String("tbcc")) {
            const QString altJsonCandidate = QDir(info.absolutePath())
                                                 .filePath(info.completeBaseName() + QStringLiteral(".tbc.json"));
            if (!candidates.contains(altJsonCandidate)) {
                candidates << altJsonCandidate;
            }
            const QString altDbCandidate = QDir(info.absolutePath())
                                               .filePath(info.completeBaseName() + QStringLiteral(".tbc.db"));
            if (!candidates.contains(altDbCandidate)) {
                candidates << altDbCandidate;
            }
        }
        return candidates;
    };

    QStringList metadataCandidates = metadataCandidatesForFile(QFileInfo(sourceFilename));
    if (isChromaTbc && lumaFilename != sourceFilename) {
        const QStringList lumaCandidates = metadataCandidatesForFile(QFileInfo(lumaFilename));
        for (const QString &candidate : lumaCandidates) {
            if (!metadataCandidates.contains(candidate)) {
                metadataCandidates << candidate;
            }
        }
    }

    QStringList metadataReadCandidates;
    const auto appendMetadataReadCandidate = [&metadataReadCandidates](const QString &candidate) {
        if (candidate.isEmpty()) {
            return;
        }
        if (!metadataReadCandidates.contains(candidate, Qt::CaseInsensitive)) {
            metadataReadCandidates << candidate;
        }
    };
    if (!requestedMetadataFilename.isEmpty()) {
        QFileInfo requestedInfo(requestedMetadataFilename);
        QString requestedPath = requestedMetadataFilename;
        if (requestedInfo.isRelative()) {
            requestedPath = QDir(sourceDir).filePath(requestedMetadataFilename);
        }
        appendMetadataReadCandidate(requestedPath);
    }
    for (const QString &candidate : metadataCandidates) {
        appendMetadataReadCandidate(candidate);
    }

    if (isChromaTbc && QFileInfo::exists(lumaFilename)) {
        // Open both of them, defaulting to the chroma view.
        sourceFilename = lumaFilename;
    }

    QString metadataFileName;
    QStringList failedMetadataCandidates;
    for (const QString &candidate : metadataReadCandidates) {
        if (!QFileInfo::exists(candidate)) {
            continue;
        }
        if (ldDecodeMetaData.read(candidate)) {
            metadataFileName = candidate;
            break;
        }
        failedMetadataCandidates << candidate;
    }

    if (metadataFileName.isEmpty()) {
        if (!failedMetadataCandidates.isEmpty()) {
            qWarning() << "Open TBC metadata failed for candidates" << failedMetadataCandidates;
        } else {
            qWarning() << "Could not locate any metadata file from candidates" << metadataReadCandidates;
        }
        currentSourceFilename.clear();

        // Show an error to the user and give up
        lastIOError = "Could not load source TBC metadata file";
        return false;
    }

    // Get the video parameters from the metadata
    LdDecodeMetaData::VideoParameters videoParameters = ldDecodeMetaData.getVideoParameters();
    applyChromaSettingsFromMetadata(videoParameters);
    metadataOnlyFrame.init(videoParameters);

    // Open the new source video
    tbcDebugStream() << "TbcSource::startBackgroundLoad(): Loading TBC file...";
    emit busy("Loading TBC file...");
    if (!sourceVideo.open(sourceFilename, videoParameters.fieldWidth * videoParameters.fieldHeight)) {
        // Open failed
        qWarning() << "Open TBC file failed for filename" << sourceFilename;
        currentSourceFilename.clear();

        // Show an error to the user and give up
        lastIOError = "Could not open source TBC data file";
        return false;
    }

    // Is there a separate chroma TBC file?
    if (!chromaSourceFilename.isEmpty()
        && chromaSourceFilename != sourceFilename
        && QFileInfo::exists(chromaSourceFilename)) {
        // Yes! Open it.
        tbcDebugStream() << "TbcSource::startBackgroundLoad(): Loading chroma TBC file...";
        emit busy("Loading chroma TBC file...");
        if (!chromaSourceVideo.open(chromaSourceFilename, videoParameters.fieldWidth * videoParameters.fieldHeight)) {
            // Open failed
            qWarning() << "Open chroma TBC file failed for filename" << chromaSourceFilename;
            currentSourceFilename.clear();
            sourceVideo.close();

            // Show an error to the user and give up
            lastIOError = "Could not open source chroma TBC data file";
            return false;
        }

        sourceMode = isChromaTbc ? CHROMA_SOURCE : BOTH_SOURCES;
    }

    // Both the video and metadata files are now open
    sourceReady = true;
    currentSourceFilename = sourceFilename;
    currentMetadataFilename = metadataFileName;

    // Configure the chroma decoder
    if (videoParameters.system == PAL || videoParameters.system == PAL_M) {
        palColour.updateConfiguration(videoParameters, palConfiguration);
    } else {
        if (videoParameters.ntscPhaseCompensation == -1) {
            // No metadata override: default phase compensation on for NTSC,
            // including single-source combined/CVBS inputs.
            ntscConfiguration.phaseCompensation = true;
        }
        ntscColour.updateConfiguration(videoParameters, ntscConfiguration);
    }

    // Analyse the metadata
    emit busy("Generating graph data and chapter map...");
    generateData();

    return true;
}

bool TbcSource::startBackgroundLoadMetadata(QString metadataFilename, QString displayFilename)
{

    // Open the metadata file
    tbcDebugStream() << "TbcSource::startBackgroundLoadMetadata(): Processing metadata...";
    emit busy("Processing metadata...");

    if (!ldDecodeMetaData.read(metadataFilename)) {
        qWarning() << "Open metadata failed for filename" << metadataFilename;
        currentSourceFilename.clear();
        lastIOError = "Could not load metadata file";
        return false;
    }

    // Get the video parameters from the metadata
    LdDecodeMetaData::VideoParameters videoParameters = ldDecodeMetaData.getVideoParameters();
    applyChromaSettingsFromMetadata(videoParameters);
    metadataOnlyFrame.init(videoParameters);

    sourceReady = true;
    currentMetadataFilename = metadataFilename;
    currentSourceFilename = displayFilename;
    sourceMode = ONE_SOURCE;

    configureChromaDecoder();

    // Analyse the metadata
    emit busy("Generating graph data and chapter map...");
    generateData();

    return true;
}

void TbcSource::finishBackgroundLoad()
{
    // Send a finished loading message to the main window
    emit finishedLoading(future.result());
}

bool TbcSource::startBackgroundSave(QString metadataFilename)
{
    tbcDebugStream() << "TbcSource::startBackgroundSave(): Saving to" << metadataFilename;
    emit busy("Saving metadata...");

    // The general idea here is that decoding takes a long time -- so we want
    // to be careful not to destroy the user's only copy of their metadata file if
    // something goes wrong!

    // Write the metadata out to a new temporary file.
    QString newMetadataFilename = metadataFilename + ".new";
    if (!ldDecodeMetaData.write(newMetadataFilename)) {
        // Writing failed
        lastIOError = "Could not write to new metadata file";
        return false;
    }

    // Preserve the current metadata by moving it to a backup file.
    // If a default backup already exists, create a timestamped backup name.
    if (QFile::exists(metadataFilename)) {
        const QString backupFilename =
            backupFilenameWithTimestampFallback(metadataFilename, QStringLiteral(".bup"));
        if (!QFile::rename(metadataFilename, backupFilename)) {
            // Renaming failed
            lastIOError = "Could not rename existing metadata file to backup";
            return false;
        }
    }

    // Rename the new file to the target name
    if (!QFile::rename(newMetadataFilename, metadataFilename)) {
        // Renaming failed
        lastIOError = "Could not rename new metadata file to target name";
        return false;
    }

    tbcDebugStream() << "TbcSource::startBackgroundSave(): Save complete";
    return true;
}

void TbcSource::finishBackgroundSave()
{
    // Send a finished saving message to the main window
    emit finishedSaving(future.result());
}
