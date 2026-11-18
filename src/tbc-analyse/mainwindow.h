/******************************************************************************
 * mainwindow.h
 * tbc-analyse - TBC output analysis GUI
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2018-2026 Simon Inns
 * SPDX-FileCopyrightText: 2026 Harry Munday
 *
 * This file is part of tbc-tools.
 ******************************************************************************/

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QProcess>
#include <QProgressDialog>
#include <QtDebug>
#include <QStandardPaths>
#include <QDir>
#include <QFileDialog>
#include <QMessageBox>
#include <QLabel>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMouseEvent>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QKeyEvent>
#include <QRect>
#include <QElapsedTimer>
#include <QTimer>
#include <QVector>
#include <QActionGroup>
#include <QImage>
#include <QFutureWatcher>

#include "oscilloscopedialog.h"
#include "rgbscopedialog.h"
#include "yuvrangedialog.h"
#include "vectorscopedialog.h"
#include "waveformmonitordialog.h"
#include "fieldtimingdialog.h"
#include "aboutdialog.h"
#include "vbidialog.h"
#include "vbiprocessingdialog.h"
#include "dropoutanalysisdialog.h"
#include "visibledropoutanalysisdialog.h"
#include "blacksnranalysisdialog.h"
#include "whitesnranalysisdialog.h"
#include "busydialog.h"
#include "closedcaptionsdialog.h"
#include "videoparametersdialog.h"
#include "chromadecoderconfigdialog.h"
#include "editsegmentswidget.h"
#include "exportdialog.h"
#include "metadataconversiondialog.h"
#include "metadatastatusdialog.h"
#include "configuration.h"
#include "updatechecker.h"
#include "pluginmanagerdialog.h"
#include "metadataeditordialog.h"
#include "tbcsource.h"

namespace Ui {
class MainWindow;
}
class AudioAlignmentDialog;
class EfmHandlerDialog;
class MetadataExportDialog;
class NotesViewerDialog;
class TeletextViewerDialog;
class TimelineMarkerSlider;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QString inputFilenameParam, bool metadataOnlyParam = false, QWidget *parent = nullptr);
	TbcSource& getTbcSource();
    ~MainWindow();

private slots:
    // Menu bar handlers
    void on_actionExit_triggered();
    void on_actionOpen_TBC_file_triggered();
    void on_actionReload_TBC_triggered();
    void on_actionMetadata_Conversion_triggered();
    void on_actionMetadata_Status_triggered();
    void on_actionSave_Metadata_triggered();
    void on_actionLine_scope_triggered();
    void on_actionRGB_scope_triggered();
    void on_actionYUV_range_scope_triggered();
    void on_actionVectorscope_triggered();
    void on_actionWaveform_monitor_triggered();
    void on_actionField_timing_scope_triggered();
    void on_actionTBC_Tools_Wiki_triggered();
    void on_actionAbout_ld_analyse_triggered();
    void on_actionCheck_for_Updates_triggered();
    void on_actionPluginManager_triggered();
    void on_actionMetadata_Editor_triggered();
    void on_actionDropout_analysis_triggered();
    void on_actionVisible_Dropout_analysis_triggered();
    void on_actionSNR_analysis_triggered();
    void on_actionWhite_SNR_analysis_triggered();
    void on_actionSave_frame_as_PNG_triggered();
    void on_actionSave_all_modes_as_PNGs_triggered();
    void on_actionExport_source_RF_segment_for_frame_triggered();
    void on_actionOpen_frame_RF_segment_in_FlacChop_triggered();
    void on_actionCopy_current_display_to_clipboard_triggered();
    void on_actionZoom_In_triggered();
    void on_actionZoom_Out_triggered();
    void on_actionZoom_1x_triggered();
    void on_actionZoom_2x_triggered();
    void on_actionZoom_3x_triggered();
    void on_actionClosed_Captions_triggered();
    void on_actionVideo_parameters_triggered();
    void on_actionChroma_decoder_configuration_triggered();
    void on_actionToggleChromaDuringSeek_triggered();
    void on_actionExport_Decode_Metadata_triggered();
    void on_actionProcess_VBI_triggered();
    void on_actionFix_JSON_SNR_triggered();
    void on_actionAuto_Audio_Align_triggered();
    void on_actionEFM_Handler_triggered();
    void on_actionLDS_Converter_triggered();
    void on_actionTeletext_Viewer_triggered();
    void on_actionVBI_triggered();

    // Media control frame handlers
    void on_previousPushButton_clicked();
    void on_nextPushButton_clicked();
    void on_previousPushButton_pressed();
    void on_previousPushButton_released();
    void on_nextPushButton_pressed();
    void on_nextPushButton_released();
    void on_endPushButton_clicked();
    void on_startPushButton_clicked();
    void on_endPushButton_pressed();
    void on_endPushButton_released();
    void on_startPushButton_pressed();
    void on_startPushButton_released();
    void on_playPushButton_toggled(bool checked);
    void on_posNumberSpinBox_editingFinished();
    void on_posTimecodeLineEdit_editingFinished();
    void on_posHorizontalSlider_valueChanged(int value);
    void on_posHorizontalSlider_sliderPressed();
    void on_posHorizontalSlider_sliderReleased();
    void on_posHorizontalSlider_customContextMenuRequested(const QPoint &pos);
    void on_videoPushButton_clicked();
    void on_aspectPushButton_clicked();
    void on_dropoutsPushButton_clicked();
    void on_sourcesPushButton_clicked();
    void on_viewPushButton_clicked();
    void on_fieldOrderPushButton_clicked();
    void on_zoomInPushButton_clicked();
    void on_zoomOutPushButton_clicked();
    void on_originalSizePushButton_clicked();
    void on_mouseModePushButton_clicked();
    void on_vectorscopeSelectionPushButton_toggled(bool checked);
    //void on_autoResizeButton_clicked();
	void on_toggleAutoResize_toggled(bool checked);
	void on_actionResizeFrameWithWindow_toggled(bool checked);

    // Miscellaneous handlers
    void scopeCoordsChangedSignalHandler(qint32 xCoord, qint32 yCoord);
    void vectorscopeChangedSignalHandler();
    void mousePressEvent(QMouseEvent *event);
    void mouseMoveEvent(QMouseEvent *event);
    void mouseReleaseEvent(QMouseEvent *event);
    void videoParametersChangedSignalHandler(const TbcMetaData::VideoParameters &videoParameters);
    void videoLevelsChangedSignalHandler(qint32 blackLevel, qint32 whiteLevel);
    void chromaDecoderConfigChangedSignalHandler();
    void exportRangeSelectionChangedSignalHandler(int inPoint, int outPoint, bool clearMetadataValues);
    void exportBoundaryToggledSignalHandler(bool enabled);
    void exportBoundaryThicknessChangedSignalHandler(int thickness);

    // Tbc Source signal handlers
    void on_busy(QString infoMessage);
    void on_finishedLoading(bool success);
    void on_finishedSaving(bool success);
    void on_asyncFrameRenderFinished();

    // RF segment export (FLAC-Chop) background process handlers
    void handleRfExportFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void handleRfExportError(QProcess::ProcessError processError);
    void handleRfExportOutput();
	
	// UI handler
	void resize_on_aspect();
protected:
    void keyPressEvent(QKeyEvent *event) override;
    bool event(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    enum class ExportBoundaryHandle {
        None,
        Top,
        Left,
        Right
    };
    struct UiStateSnapshot {
        bool valid = false;
        qint32 frameNumber = 1;
        qint32 fieldNumber = 1;
        TbcSource::ViewMode viewMode = TbcSource::ViewMode::FRAME_VIEW;
        bool stretchField = true;
        bool reverseFieldOrder = false;
        bool highlightDropouts = false;
        bool chromaEnabled = false;
        TbcSource::ChromaDecodeMode chromaDecodeMode = TbcSource::ChromaDecodeMode::HYBRID_CHROMA_MODE;
        TbcSource::SourceMode sourceMode = TbcSource::SourceMode::ONE_SOURCE;
        bool aspectRatioEnabled = false;
        double imageScaleFactor = 1.0;
        int activeMainTabIndex = 0;
        bool showVbiDialog = false;
        bool showDropoutDialog = false;
        bool showVisibleDropoutDialog = false;
        bool showBlackSnrDialog = false;
        bool showWhiteSnrDialog = false;
        bool showVideoParametersDialog = false;
        bool showChromaConfigDialog = false;
    };
    Ui::MainWindow *ui;

    // Dialogues
    OscilloscopeDialog* oscilloscopeDialog;
    RgbScopeDialog *rgbScopeDialog;
    YuvRangeDialog *yuvRangeDialog;
    VectorscopeDialog* vectorscopeDialog;
    WaveformMonitorDialog* waveformMonitorDialog;
    FieldTimingDialog* fieldTimingDialog;
    AboutDialog* aboutDialog;
    VbiDialog* vbiDialog;
    DropoutAnalysisDialog* dropoutAnalysisDialog;
    VisibleDropOutAnalysisDialog* visibleDropoutAnalysisDialog;
    BlackSnrAnalysisDialog* blackSnrAnalysisDialog;
    WhiteSnrAnalysisDialog* whiteSnrAnalysisDialog;
    BusyDialog* busyDialog;
    ClosedCaptionsDialog *closedCaptionDialog;
    VideoParametersDialog *videoParametersDialog;
    ChromaDecoderConfigDialog *chromaDecoderConfigDialog;
    MetadataConversionDialog *metadataConversionDialog;
    MetadataStatusDialog *metadataStatusDialog;
    EditSegmentsWidget *editSegmentsWidget = nullptr;
    int editSegmentAtCurrentFrame() const;
    // The video parameters as the picture shows them: in the Edit tab inside a
    // segment, the global ones with that segment's active area
    TbcMetaData::VideoParameters displayedVideoParameters() const;
    QString videoParametersDialogSegmentKey;  // what the dialog last showed
    ExportDialog *exportDialog;
    AudioAlignmentDialog *audioAlignmentDialog = nullptr;
    EfmHandlerDialog *efmHandlerDialog = nullptr;
    MetadataExportDialog *metadataExportDialog = nullptr;
    NotesViewerDialog *notesViewerDialog = nullptr;
    TeletextViewerDialog *teletextViewerDialog = nullptr;

    // Class globals
    Configuration configuration;
    QLabel sourceVideoStatus;
    QLabel fieldNumberStatus;
    QLabel vbiStatus;
    QLabel timeCodeStatus;
    QLabel cursorStatus;
    TbcSource tbcSource;
    bool displayAspectRatio;
    bool showExportBoundary = false;
    qint32 exportBoundaryThickness = 4;
	bool autoResize = true;
	bool resizeFrameWithWindow = true;
    qint32 lastScopeLine;
    qint32 lastScopeDot;
    qint32 currentFieldNumber = 1;
    qint32 currentFrameNumber = 1;
    bool vectorscopeSelectionDragging = false;
    QPoint vectorscopeSelectionAnchor;
    ExportBoundaryHandle exportBoundaryDragHandle = ExportBoundaryHandle::None;
    ExportBoundaryHandle exportBoundarySelectedHandle = ExportBoundaryHandle::None;
    double scaleFactor;
    QPalette buttonPalette;
    QString lastFilename;
    bool metadataJsonLoaded = false;
    QString metadataJsonFilename;
    QString metadataTempSqliteFilename;
    QImage cursorReadoutImage;
    QTimer* playbackTimer = nullptr;
    QTimer* resizeTimer = nullptr;
    // Real-time playback clock: ticks are scheduled against deadlines on
    // this clock so render time does not stretch the frame period.
    QElapsedTimer playbackClock;
    qint32 playbackStartNumber = 0;  // frame (or field in field view) when playback started
    bool playbackRunning = false;
    QTimer* rgbScopeRefreshTimer = nullptr;
    bool rgbScopeRefreshPending = false;
    qint64 rgbScopeLastRefreshMs = 0;
    QTimer* yuvRangeScopeRefreshTimer = nullptr;
    bool yuvRangeScopeRefreshPending = false;
    qint64 yuvRangeScopeLastRefreshMs = 0;
    YuvRangeSettings yuvRangeSettings;
    
    // Chroma toggle during seek
    bool chromaSeekMode;
    bool originalChromaState;
    QTimer* seekTimer;
    QTimer* chapterSkipHoldDelayTimer = nullptr;
    QTimer* chapterSkipHoldRepeatTimer = nullptr;
    QPushButton* chapterSkipHoldButton = nullptr;
    qint32 chapterSkipHoldDeltaFrames = 0;
    bool chapterSkipHoldTriggered = false;
    bool suppressStartButtonClick = false;
    bool suppressEndButtonClick = false;

    // Update checker
    UpdateChecker *updateChecker = nullptr;
    bool updateCheckSilent = false; // true for the automatic weekly check (suppresses non-update popups)
    PluginManagerDialog *pluginManagerDialog = nullptr;
    MetadataEditorDialog *metadataEditorDialog = nullptr;
    void maybePerformWeeklyUpdateCheck();
    void recordUpdateCheckAttempt();
    void showUpdateAvailableDialog(const QString &latestVersion, const QString &releaseUrl, const QString &releaseName);
    void onUpdateAvailable(const QString &latestVersion, const QString &releaseUrl, const QString &releaseName);
    void onUpdateUpToDate(const QString &currentVersion, const QString &latestVersion, const QString &releaseUrl);
    void onUpdateCheckFailed(const QString &errorString);

    // Update GUI methods
    void setGuiEnabled(bool enabled);
    void resetGui();
    void updateGuiLoaded();
    void updateGuiUnloaded();
    void updateVideoPushButton();
    void updateAspectPushButton();
    void updateSourcesPushButton();
    void updateMetadataStatusPanel();
    void updateBottomStatusReadout();
    void setViewValues();
    double timecodeFrameRate() const;
    int timecodeFrameBaseRate() const;
    QString frameToTimecode(qint32 frameNumber) const;
    QString framesToDurationTimecode(qint32 frameCount) const;
    qint32 timecodeToFrame(const QString &timecodeText, bool *ok = nullptr) const;
    void updatePositionEditorValue(qint32 currentNumber);
    QString playbackStartToolTip() const;
    double playbackFrameIntervalMs() const;
    void scheduleNextPlaybackTick();
    void setPlaybackRunning(bool running);
    void stepPlayback();
    void setCurrentFrame(qint32 frame);
    void setCurrentField(qint32 field);
    void skipFramesBy(qint32 frameDelta);
    void sanitizeCurrentPosition();

	// Image display methods
    void showImage();
    void updateImage();
    qint32 getAspectAdjustment() const;
    QImage renderedCurrentImageForExport();
    // Copy the current video frame (renderedCurrentImageForExport) to the
    // clipboard. Shared by the main-window Ctrl+C action and the graph-window
    // copyFrameRequested() signal handlers.
    void copyCurrentFrameToClipboard();
    bool isViewerTabActive() const;
    QString outputRootDirectoryForCurrentSource();
    QString outputBaseNameForCurrentSource();
    void applyEfmHandlerAutoloads(const QString &directoryPath);
    void updateImageViewer();
    bool shouldRenderFrameAsync() const;
    void startAsyncFrameRender();
    QVector<QRect> getActiveVideoRects() const;
    bool isExportBoundaryDragAvailable() const;
    ExportBoundaryHandle exportBoundaryHandleAtViewerPoint(const QPoint &viewerPoint) const;
    void updateExportBoundaryHoverCursor(const QPoint &viewerPoint);
    void applyExportBoundaryDragAtViewerPoint(const QPoint &viewerPoint);
    void applyExportBoundaryWheelStep(qint32 step);
    void hideImage();
    bool mapViewerToSourceCoordinates(const QPoint &viewerPoint, qint32 &sourceX, qint32 &sourceY) const;
    void updateCursorReadout(const QPoint &viewerPoint);
    void clearCursorReadout();
    void resizeFrameToWindow();
    void enterChromaSeekMode(QPushButton* button);
    void exitChromaSeekMode(QPushButton* button);
    void cancelInFlightAsyncFrameRender();

    // TBC source signal handlers
    void loadTbcFile(QString inputFileName, bool forceMetadataOnly = false, bool preserveStatusDuringReload = false);
    void cleanupTempMetadataFile();
    void updateOscilloscopeDialogue();
    void updateRgbScopeDialogue(bool force = false);
    void updateYuvRangeScopeDialogue(bool force = false);
    void updateVectorscopeDialogue();
    void updateWaveformMonitorDialogue();
    void updateFieldTimingDialogue();
    void populateThemesMenu();
    void applyThemeStyle(const QString &styleName);
    void refreshThemeDependentUi();
    void mouseScanLineSelect(qint32 oX, qint32 oY);
	void resizeEvent(QResizeEvent *event);
    void requestSourceOpen(const QString &inputFileName);
    void processPendingSourceOpenRequest();
    bool runExternalToolWithProgress(const QString &program, const QStringList &arguments,
                                     const QString &toolDisplayName, QString *errorMessage);

    // RF segment export via FLAC-Chop (issue #29). Resolves the FLAC-Chop
    // binary and the source RF FLAC capture, probes the capture for its real
    // sample rate and length, converts the current frame's metadata fileLoc
    // range into a padded RF sample range (fileLoc is scaled from the decode
    // chain's fixed 40 Msps internal domain to the probed real_rate_hz, and
    // the ms padding is converted with the same probed rate), then chops in
    // the background via QProcess.
    struct RfExportProbeResult {
        bool ok = false;
        double realRateHz = -1.0;
        qint64 totalSamples = -1;
        bool totalSamplesKnown = false;
        bool isRf = false;
        QStringList warnings;
        QString errorString;
    };
    RfExportProbeResult probeRfSource(const QString &flacChopPath, const QString &rfSourcePath);
    void runRfSegmentExport(bool interactive, bool launchGuiOnly = false);
    void queueAnalysisRefreshPreservingUserState(const QString &processedInputFile);
    UiStateSnapshot captureUiStateSnapshot() const;
    void applyUiStateSnapshot(const UiStateSnapshot &snapshot);
    QActionGroup *themesActionGroup = nullptr;
    QAction *saveAllModesPngAction = nullptr;
    QAction *exportSourceRfSegmentAction = nullptr;
    QAction *openRfInFlacChopGuiAction = nullptr;
    QAction *copyCurrentDisplayAction = nullptr;
    QAction *notesViewerAction = nullptr;
    QPushButton *vectorscopeSelectionPushButton = nullptr;
    TimelineMarkerSlider *timelineMarkerSlider = nullptr;
    UiStateSnapshot pendingUiStateSnapshot;
    bool restoreUiStateAfterReload = false;
    QString pendingSourceOpenFilename;
    bool sourceOperationInProgress = false;
    QProcess *rfExportProcess = nullptr;
    QProgressDialog *rfExportProgressDialog = nullptr;
    QString rfExportOutputPath;
    QString rfExportOutputTail;
    void updateTimelineMarkers();
    void updateNotesViewerState();
    void setInPointAtCurrentFrame();
    void setOutPointAtCurrentFrame();
    qint32 sliderPositionForFrame(qint32 frameNumber) const;
    qint32 frameForSliderPosition(qint32 sliderPosition) const;
    QFutureWatcher<QImage> asyncFrameRenderWatcher;
    bool asyncFrameRenderInProgress = false;
    bool asyncFrameRenderQueued = false;
    qint32 asyncFrameRenderFrameNumber = -1;
    qint32 asyncFrameRenderFieldNumber = -1;
    QImage asyncFrameImage;
    bool themeRefreshPending = false;
};

#endif // MAINWINDOW_H
