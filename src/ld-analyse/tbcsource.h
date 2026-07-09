/******************************************************************************
 * tbcsource.h
 * ld-analyse - TBC output analysis GUI
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2018-2025 Simon Inns
 * SPDX-FileCopyrightText: 2021-2022 Adam Sampson
 *
 * This file is part of ld-decode-tools.
 ******************************************************************************/

#ifndef TBCSOURCE_H
#define TBCSOURCE_H

#include <QObject>
#include <QImage>
#include <QPainter>
#include <QSize>
#include <QtConcurrent/QtConcurrent>
#include <QDebug>
#include <vector>

// TBC library includes
#include "sourcevideo.h"
#include "lddecodemetadata.h"
#include "linenumber.h"
#include "vbidecoder.h"
#include "videoiddecoder.h"
#include "vitcdecoder.h"

// Chroma decoder includes
#include "palcolour.h"
#include "comb.h"
#include "monodecoder.h"

struct YuvRangeSettings;

class TbcSource : public QObject
{
    Q_OBJECT
public:
    explicit TbcSource(QObject *parent = nullptr);

    struct ScanLineData {
        QString systemDescription;
        LineNumber lineNumber;
        QVector<qint32> composite;
        QVector<qint32> luma;
        QVector<qint32> chroma;
        QVector<bool> isDropout;
        qint32 blackIre;
        qint32 whiteIre;
        qint32 fieldWidth;
        double sampleRate = -1.0;
        qint32 colourBurstStart;
        qint32 colourBurstEnd;
        qint32 activeVideoStart;
        qint32 activeVideoEnd;
        bool isActiveLine;
    };

    struct FieldTimingData {
        qint32 firstFieldNumber = 0;
        qint32 secondFieldNumber = 0;
        bool hasSecondField = false;
        qint32 fieldWidth = 0;
        qint32 firstFieldHeight = 0;
        qint32 secondFieldHeight = 0;
        std::vector<uint16_t> firstFieldComposite;
        std::vector<uint16_t> secondFieldComposite;
        std::vector<uint16_t> firstFieldLuma;
        std::vector<uint16_t> secondFieldLuma;
        std::vector<uint16_t> firstFieldChroma;
        std::vector<uint16_t> secondFieldChroma;
        bool valid = false;
    };

    void loadSource(QString inputFileName, QString metadataFilename = QString());
    void loadMetadata(QString metadataFilename, QString displayFilename = QString());
    void unloadSource();
    bool getIsSourceLoaded() const;
    bool getIsMetadataOnly() const;
    void saveSourceMetadata();
    bool writeMetadataSnapshot(const QString &metadataFilename, QString *errorMessage = nullptr) const;
    QString getCurrentSourceFilename() const;
    QString getCurrentMetadataFilename() const;
    QString getLastIOError() const;

    void setHighlightDropouts(bool _state);
    void setChromaDecoder(bool _state);
    enum ChromaDecodeMode {
        ACTIVE_ONLY_CHROMA_MODE,
        HYBRID_CHROMA_MODE,
        FULL_FRAME_CHROMA_MODE
    };
    void setChromaDecodeMode(ChromaDecodeMode mode);
    ChromaDecodeMode getChromaDecodeMode() const;
    void setFieldView(bool _state);
    void setFieldOrder(bool _state);
	void setCombine(bool _state);
    bool getHighlightDropouts() const;
    bool getChromaDecoder() const;
    bool getFieldOrder() const;

    enum ViewMode {
        FRAME_VIEW,
        SPLIT_VIEW,
        FIELD_VIEW,
        RGB_SCOPE_VIEW,
    };
    void setViewMode(ViewMode viewMode);
    void setStretchField(bool _stretch);

    ViewMode getViewMode() const;
    bool getFrameViewEnabled() const;
    bool getFieldViewEnabled() const;
    bool getSplitViewEnabled() const;
    bool getRgbScopeViewEnabled() const;
    bool getStretchField() const;

    enum SourceMode {
        ONE_SOURCE,
        LUMA_SOURCE,
        CHROMA_SOURCE,
        BOTH_SOURCES,
    };
    SourceMode getSourceMode() const;
    void setSourceMode(SourceMode sourceMode);

    void load(qint32 frameNumber, qint32 fieldNumber);

    QImage getImage();
    QImage getRgbScopeImage(const QSize &targetSize = QSize());
    QImage getYuvRangeScopeImage(const QSize &targetSize, const YuvRangeSettings &settings);
    QImage getYuvRangeOverlayImage(const YuvRangeSettings &settings);
    qint32 getNumberOfFrames() const;
    qint32 getNumberOfFields() const;
    bool getIsWidescreen() const;
    VideoSystem getSystem() const;
    QString getSystemDescription() const;
    qint32 getFrameHeight() const;
    qint32 getFrameWidth() const;

    VbiDecoder::Vbi getFrameVbi();
    bool getIsFrameVbiValid();
    VideoIdDecoder::VideoId getFrameVideoId();
    bool getIsFrameVideoIdValid();
    VitcDecoder::Vitc getFrameVitc();
    bool getIsFrameVitcValid();

    QVector<double> getBlackSnrGraphData();
    QVector<double> getWhiteSnrGraphData();
    QVector<double> getDropOutGraphData();
    QVector<double> getVisibleDropOutGraphData();
    qint32 getGraphDataSize();

    bool getIsDropoutPresent() const;

    const LdDecodeMetaData::VideoParameters &getVideoParameters() const;
    void setVideoParameters(const LdDecodeMetaData::VideoParameters &videoParameters);

    const ComponentFrame &getComponentFrame();
    ScanLineData getScanLineData(qint32 scanLine);
    FieldTimingData getFieldTimingData();

    qint32 getFirstFieldNumber() const;
    qint32 getSecondFieldNumber() const;

    qint32 getCcData0() const;
    qint32 getCcData1() const;

    void setChromaConfiguration(const PalColour::Configuration &palConfiguration, const Comb::Configuration &ntscConfiguration);
    void requestNnTransform3DCancel();
    const PalColour::Configuration &getPalConfiguration() const;
    const Comb::Configuration &getNtscConfiguration() const;
    const MonoDecoder::MonoConfiguration &getMonoConfiguration() const;
    const OutputWriter::Configuration &getOutputConfiguration() const;
    bool hasChapterMap() const;

    qint32 startOfNextChapter(qint32 currentFrameNumber);
    qint32 startOfChapter(qint32 currentFrameNumber);

signals:
    void busy(QString information);
    void finishedLoading(bool success);
    void finishedSaving(bool success);

private slots:
    void finishBackgroundLoad();
    void finishBackgroundSave();

private:
    bool sourceReady;

    // Frame data
    QVector<double> blackSnrGraphData;
    QVector<double> whiteSnrGraphData;
    QVector<double> dropoutGraphData;
    QVector<double> visibleDropoutGraphData;

    // Image options
    bool chromaOn;
    bool dropoutsOn;
    bool reverseFoOn;
    bool stretchFieldOn;
    ViewMode viewMode;

    // Source globals
    SourceVideo sourceVideo;
    SourceVideo chromaSourceVideo;
    SourceMode sourceMode;
    LdDecodeMetaData ldDecodeMetaData;
    QString currentSourceFilename;
    QString currentMetadataFilename;
    QString requestedMetadataFilename;
    QString lastIOError;

    // Chroma decoder objects
    PalColour palColour;
    Comb ntscColour;
	MonoDecoder monoDecoder;
    OutputWriter outputWriter;

    // VBI decoders
    VbiDecoder vbiDecoder;
    VideoIdDecoder videoIdDecoder;
    VitcDecoder vitcDecoder;

    // Background loader globals
    QFutureWatcher<bool> watcher;
    QFuture<bool> future;

    // Metadata for the loaded frame
    qint32 firstFieldNumber, secondFieldNumber;
    LdDecodeMetaData::Field firstField, secondField;
    qint32 loadedFieldNumber, loadedFrameNumber;

    // Source fields needed to decode the loaded frame
    QVector<SourceField> inputFields;
    QVector<SourceField> chromaInputFields;
    qint32 inputStartIndex, inputEndIndex;
    bool inputFieldsValid;

    // Chroma decoder output for the loaded frame
    QVector<ComponentFrame> componentFrames;
    QVector<ComponentFrame> yFrames;
    QVector<ComponentFrame> cFrames;
    bool decodedFrameValid;

    // RGB image data for the loaded frame
    QImage cache;
    bool cacheValid;
    QImage rgbScopeCache;
    bool rgbScopeCacheValid;
    QSize rgbScopeCacheSize;
    QImage yuvRangeScopeCache;
    bool yuvRangeScopeCacheValid;
    QSize yuvRangeScopeCacheSize;
    qint32 yuvRangeScopeCacheLumaMin;
    qint32 yuvRangeScopeCacheLumaMax;
    qint32 yuvRangeScopeCacheChromaMin;
    qint32 yuvRangeScopeCacheChromaMax;
    QImage yuvRangeOverlayCache;
    bool yuvRangeOverlayCacheValid;
    qint32 yuvRangeOverlayCacheLumaMin;
    qint32 yuvRangeOverlayCacheLumaMax;
    qint32 yuvRangeOverlayCacheChromaMin;
    qint32 yuvRangeOverlayCacheChromaMax;
    qint32 yuvRangeOverlayCacheAlpha;
    ViewMode yuvRangeOverlayCacheViewMode;
    bool yuvRangeOverlayCacheStretchField;
    qint32 yuvRangeOverlayCacheFieldNumber;

    // Chroma decoder configuration
    PalColour::Configuration palConfiguration;
    Comb::Configuration ntscConfiguration;
	MonoDecoder::MonoConfiguration monoConfiguration;
    OutputWriter::Configuration outputConfiguration;
    ChromaDecodeMode chromaDecodeMode = HYBRID_CHROMA_MODE;
	bool combine = false;

    // Chapter map
    QVector<qint32> chapterMap;

    void resetState();
    void invalidateImageCache();
    void configureChromaDecoder();
    void loadInputFields();
    void decodeFrame();
    QImage generateQImage(ViewMode renderViewMode, const QSize &targetSize = QSize());
    QImage generateChromaImage();
    QImage generateMonoImage();
    void generateData();
    bool startBackgroundLoad(QString sourceFilename);
    bool startBackgroundLoadMetadata(QString metadataFilename, QString displayFilename);
    bool startBackgroundSave(QString metadataFilename);
    void applyChromaSettingsFromMetadata(const LdDecodeMetaData::VideoParameters &videoParameters);

    bool metadataOnly;
    ComponentFrame metadataOnlyFrame;
};

#endif // TBCSOURCE_H
