/******************************************************************************
 * tbcsource.h
 * tbc-analyse - TBC output analysis GUI
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2018-2025 Simon Inns
 * SPDX-FileCopyrightText: 2021-2022 Adam Sampson
 *
 * This file is part of tbc-tools.
 ******************************************************************************/

#ifndef TBCSOURCE_H
#define TBCSOURCE_H

#include <QObject>
#include <QImage>
#include <QPainter>
#include <QSize>
#include <QtConcurrent/QtConcurrent>
#include <QDebug>
#include <QThreadPool>
#include <memory>
#include <vector>

// TBC library includes
#include "sourcevideo.h"
#include "tbcmetadata.h"
#include "linenumber.h"
#include "vbidecoder.h"
#include "videoiddecoder.h"
#include "vitcdecoder.h"

// Chroma decoder includes
#include "palcolour.h"
#include "comb.h"
#include "monodecoder.h"
#include "secamdecoder.h"
#include "secampredemoddecoder.h"

struct YuvRangeSettings;

// The chroma decoders used by one decode thread. TbcSource keeps one set for
// the displayed frame plus one per playback decode-ahead worker, because each
// decoder holds per-frame working state and FFTW plans.
struct ChromaDecoders
{
    PalColour palColour;
    SecamDecoder secamDecoder;
    SecamPredemodDecoder secamPredemodDecoder;
    Comb ntscColour;
    MonoDecoder monoDecoder;

    // Which decoder to use, set before each decode
    VideoSystem system = PAL;
    PalColour::ChromaFilterMode chromaFilter = PalColour::transform2DFilter;
    qint32 ntscDimensions = 2;
    bool separateChroma = false;
    TbcMetaData::VideoParameters videoParameters;
    quint64 generation = 0;

    void decode(const QVector<SourceField> &inputFields, const QVector<SourceField> &chromaInputFields,
                qint32 startIndex, qint32 endIndex, QVector<ComponentFrame> &componentFrames);
};

class TbcSource : public QObject
{
    Q_OBJECT
public:
    explicit TbcSource(QObject *parent = nullptr);
    ~TbcSource() override;

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
    void setPlaybackPrefetch(bool enabled);
    enum ChromaDecodeMode {
        ACTIVE_ONLY_CHROMA_MODE,
        HYBRID_CHROMA_MODE,
        FULL_FRAME_CHROMA_MODE
    };
    void setChromaDecodeMode(ChromaDecodeMode mode);
    ChromaDecodeMode getChromaDecodeMode() const;
    // Move the active area the preview levels/trims to (an export segment's
    // framing in the Edit tab); returns true if it changed
    bool setPreviewFramingOffset(qint32 offsetX, qint32 offsetY);
    static void applyFramingOffset(TbcMetaData::VideoParameters &videoParameters, qint32 offsetX, qint32 offsetY);
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

    const TbcMetaData::VideoParameters &getVideoParameters() const;
    void setVideoParameters(const TbcMetaData::VideoParameters &videoParameters);

    const TbcMetaData::PcmAudioParameters &getPcmAudioParameters() const;
    void setPcmAudioParameters(const TbcMetaData::PcmAudioParameters &pcmAudioParameters);

    const ComponentFrame &getComponentFrame();
    ScanLineData getScanLineData(qint32 scanLine);
    FieldTimingData getFieldTimingData();

    qint32 getFirstFieldNumber() const;
    qint32 getSecondFieldNumber() const;
    // RF segment export support: first field of an arbitrary frame number, and
    // the raw metadata sample location (fileLoc) of a sequential field number
    // (-1 for padding fields with no source data). fileLoc is a block-aligned
    // sample index in the decode chain's fixed 40 Msps internal domain, not in
    // the original RF capture's sample rate.
    qint32 getFrameFirstFieldNumber(qint32 frameNumber) const;
    qint64 getFieldFileLoc(qint32 sequentialFieldNumber) const;
    bool getSecamFirstLineIsRed(qint32 fieldNumber) const;

    qint32 getCcData0() const;
    qint32 getCcData1() const;

    void setChromaConfiguration(const PalColour::Configuration &palConfiguration, const Comb::Configuration &ntscConfiguration);
    void setSecamPredemodFirstLineIsRedOverride(qint8 override);
    qint8 getSecamPredemodFirstLineIsRedOverride() const { return secamPredemodFirstLineIsRedOverride; }
    void setSecamFirstLineIsRed(qint32 fieldNumber, bool value, bool applyToAll);
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
    TbcMetaData metaData;
    QString currentSourceFilename;
    QString currentMetadataFilename;
    QString requestedMetadataFilename;
    QString lastIOError;

    // Chroma decoder objects
    ChromaDecoders decoders;
    OutputWriter outputWriter;

    // Playback decode-ahead: upcoming frames are decoded on worker threads,
    // each with its own decoder set, so colour playback isn't limited to the
    // speed of a single-threaded decode
    struct PrefetchJob {
        qint32 frameNumber;
        quint64 generation;
        size_t decoderIndex;
        QVector<SourceField> inputFields;
        QVector<SourceField> chromaInputFields;
        qint32 startIndex, endIndex;
        QVector<ComponentFrame> componentFrames;
        QFuture<void> future;
    };
    bool playbackPrefetch = false;
    quint64 prefetchGeneration = 1;
    QThreadPool prefetchPool;
    std::vector<std::unique_ptr<ChromaDecoders>> prefetchDecoders;
    std::vector<std::unique_ptr<PrefetchJob>> prefetchJobs;

    // VBI decoders
    VbiDecoder vbiDecoder;
    VideoIdDecoder videoIdDecoder;
    VitcDecoder vitcDecoder;

    // Background loader globals
    QFutureWatcher<bool> watcher;
    QFuture<bool> future;

    // Metadata for the loaded frame
    qint32 firstFieldNumber, secondFieldNumber;
    TbcMetaData::Field firstField, secondField;
    qint32 loadedFieldNumber, loadedFrameNumber;

    // Source fields needed to decode the loaded frame
    QVector<SourceField> inputFields;
    QVector<SourceField> chromaInputFields;
    qint32 inputStartIndex, inputEndIndex;
    bool inputFieldsValid;

    // Chroma decoder output for the loaded frame
    QVector<ComponentFrame> componentFrames;
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
    SecamDecoder::SecamConfiguration secamConfiguration;
    SecamPredemodDecoder::SecamConfiguration secamPredemodConfiguration;
    qint8 secamPredemodFirstLineIsRedOverride = -1;
    Comb::Configuration ntscConfiguration;
	MonoDecoder::MonoConfiguration monoConfiguration;
    OutputWriter::Configuration outputConfiguration;
    ChromaDecodeMode chromaDecodeMode = HYBRID_CHROMA_MODE;
    qint32 previewFramingOffsetX = 0;
    qint32 previewFramingOffsetY = 0;
	bool combine = false;

    // Chapter map
    QVector<qint32> chapterMap;

    void resetState();
    void invalidateImageCache(bool keepPrefetch = false);
    void configureChromaDecoder();
    void configureDecoders(ChromaDecoders &set);
    void updateDecodeRouting(ChromaDecoders &set);
    void loadFrameFields(qint32 frameNumber, QVector<SourceField> &fields, QVector<SourceField> &chromaFields,
                         qint32 &startIndex, qint32 &endIndex);
    void loadInputFields();
    void schedulePrefetch();
    void stopPrefetch();
    void decodeFrame();
    QImage generateQImage(ViewMode renderViewMode, const QSize &targetSize = QSize());
    QImage generateChromaImage();
    QImage generateMonoImage();
    void generateData();
    bool startBackgroundLoad(QString sourceFilename);
    bool startBackgroundLoadMetadata(QString metadataFilename, QString displayFilename);
    bool startBackgroundSave(QString metadataFilename);
    void applyChromaSettingsFromMetadata(const TbcMetaData::VideoParameters &videoParameters);

    bool metadataOnly;
    ComponentFrame metadataOnlyFrame;
};

#endif // TBCSOURCE_H
