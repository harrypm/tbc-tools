/******************************************************************************
 * configuration.h
 * tbc-analyse - TBC output analysis GUI
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2018-2025 Simon Inns
 *
 * This file is part of tbc-tools.
 ******************************************************************************/

#ifndef CONFIGURATION_H
#define CONFIGURATION_H

#include <QObject>
#include <QCoreApplication>
#include <QSettings>
#include <QStandardPaths>
#include <QApplication>
#include <QDir>
#include <QDebug>

#include "tbc/vbiprocessingoptions.h"

// Advanced export options (Export page -> Advanced options dialog). Defaults
// mirror the tbc-video-export CLI defaults; the persisted state applies to
// both the main and the proxy exports.
struct ExportAdvancedOptions {
    bool exportMetadata = true;             // --export-metadata
    bool disableMetadataEmbedding = false;  // --no-attach-json
    bool letterboxCrop = false;             // --letterbox (default active-area framing only)
    bool forceAnamorphic = false;           // --force-anamorphic (not with letterbox crop)
    bool reverseFieldOrder = false;         // --reverse (forces --no-dropout-correct)
    bool twoStep = false;                   // --two-step
    int outputPadding = 0;                  // --output-padding (0 = off)
    int threads = 0;                        // --threads (0 = process defaults)
};

class Configuration : public QObject
{
    Q_OBJECT
public:
    explicit Configuration(QObject *parent = nullptr);
    ~Configuration() override;

    void writeConfiguration(void);
    void readConfiguration(void);

    // Get and set methods - Directories
    void setSourceDirectory(QString sourceDirectory);
    QString getSourceDirectory(void);
    void setPngDirectory(QString pngDirectory);
    QString getPngDirectory(void);

    // Get and set methods - windows
    void setMainWindowGeometry(QByteArray mainWindowGeometry);
    QByteArray getMainWindowGeometry(void);
    void setMainWindowScaleFactor(double mainWindowScaleFactor);
    double getMainWindowScaleFactor(void);
    void setVbiDialogGeometry(QByteArray vbiDialogGeometry);
    QByteArray getVbiDialogGeometry(void);
    void setOscilloscopeDialogGeometry(QByteArray oscilloscopeDialogGeometry);
    QByteArray getOscilloscopeDialogGeometry(void);
    void setVectorscopeDialogGeometry(QByteArray vectorscopeDialogGeometry);
    QByteArray getVectorscopeDialogGeometry(void);
    void setWaveformMonitorDialogGeometry(QByteArray waveformMonitorDialogGeometry);
    QByteArray getWaveformMonitorDialogGeometry(void);
    void setDropoutAnalysisDialogGeometry(QByteArray dropoutAnalysisDialogGeometry);
    QByteArray getDropoutAnalysisDialogGeometry(void);
    void setVisibleDropoutAnalysisDialogGeometry(QByteArray visibleDropoutDialogGeometry);
    QByteArray getVisibleDropoutAnalysisDialogGeometry(void);
    void setBlackSnrAnalysisDialogGeometry(QByteArray blackSnrAnalysisDialogGeometry);
    QByteArray getBlackSnrAnalysisDialogGeometry(void);
    void setWhiteSnrAnalysisDialogGeometry(QByteArray whiteSnrAnalysisDialogGeometry);
    QByteArray getWhiteSnrAnalysisDialogGeometry(void);
    void setClosedCaptionDialogGeometry(QByteArray closedCaptionDialogGeometry);
    QByteArray getClosedCaptionDialogGeometry(void);
    void setVideoParametersDialogGeometry(QByteArray videoParametersConfigDialogGeometry);
    QByteArray getVideoParametersDialogGeometry(void);
    void setChromaDecoderConfigDialogGeometry(QByteArray chromaDecoderConfigDialogGeometry);
    QByteArray getChromaDecoderConfigDialogGeometry(void);

    // Get and set methods - view options
    void setToggleChromaDuringSeek(bool toggleChromaDuringSeek);
    bool getToggleChromaDuringSeek(void);
    void setGenerateProxyEnabled(bool generateProxyEnabled);
    bool getGenerateProxyEnabled(void);
    void setExportProfileConfigEnabled(bool exportProfileConfigEnabled);
    bool getExportProfileConfigEnabled(void);
    void setExportProfileConfigPath(QString exportProfileConfigPath);
    QString getExportProfileConfigPath(void);
    void setResizeFrameWithWindow(bool resizeFrameWithWindow);
    bool getResizeFrameWithWindow(void);
    void setShowExportBoundary(bool showExportBoundary);
    bool getShowExportBoundary(void);
    void setExportBoundaryThickness(qint32 exportBoundaryThickness);
    qint32 getExportBoundaryThickness(void);

    // Get and set methods - RF segment export (FLAC-Chop)
    void setRfExportFlacChopPath(QString flacChopPath);
    QString getRfExportFlacChopPath(void);
    void setRfExportSourcePath(QString sourcePath);
    QString getRfExportSourcePath(void);
    void setRfExportPadBeforeMs(qint32 padBeforeMs);
    qint32 getRfExportPadBeforeMs(void);
    void setRfExportPadAfterMs(qint32 padAfterMs);
    qint32 getRfExportPadAfterMs(void);
    void setSaveAllAlsoExportRf(bool saveAllAlsoExportRf);
    bool getSaveAllAlsoExportRf(void);

    // Get and set methods - update checker
    void setUpdateCheckEnabled(bool updateCheckEnabled);
    bool getUpdateCheckEnabled(void);
    void setLastUpdateCheckTimestamp(QString lastUpdateCheckTimestamp);
    QString getLastUpdateCheckTimestamp(void);
    void setSkippedUpdateVersion(QString skippedUpdateVersion);
    QString getSkippedUpdateVersion(void);

    // Get and set methods - CUDA plugin registry
    void setCudaPluginInstalledVersion(QString version);
    QString getCudaPluginInstalledVersion(void);
    void setCudaPluginReleaseTag(QString tag);
    QString getCudaPluginReleaseTag(void);
    void setCudaPluginSha256(QString sha256);
    QString getCudaPluginSha256(void);
    void setCudaPluginEnabled(bool enabled);
    bool getCudaPluginEnabled(void);
    void setCudaPluginTrusted(bool trusted);
    bool getCudaPluginTrusted(void);
    void setCudaPluginInstallPath(QString path);
    QString getCudaPluginInstallPath(void);

    // Get and set methods - VBI processing options
    void setVbiProcessingOptions(const VbiProcessingOptions &options);
    VbiProcessingOptions getVbiProcessingOptions(void);

    // Get and set methods - advanced export options
    void setExportAdvancedOptions(const ExportAdvancedOptions &options);
    ExportAdvancedOptions getExportAdvancedOptions(void);

signals:

public slots:

private:
    QSettings *configuration;

    // Directories
    struct Directories {
        QString sourceDirectory; // Last used directory for .tbc files
        QString pngDirectory; // Last used directory for .png files
    };

    // Window geometry and settings
    struct Windows {
        QByteArray mainWindowGeometry;
        double mainWindowScaleFactor;
        QByteArray vbiDialogGeometry;
        QByteArray oscilloscopeDialogGeometry;
        QByteArray vectorscopeDialogGeometry;
        QByteArray waveformMonitorDialogGeometry;
        QByteArray dropoutAnalysisDialogGeometry;
        QByteArray visibleDropoutAnalysisDialogGeometry;
        QByteArray blackSnrAnalysisDialogGeometry;
        QByteArray whiteSnrAnalysisDialogGeometry;
        QByteArray closedCaptionDialogGeometry;
        QByteArray videoParametersDialogGeometry;
        QByteArray chromaDecoderConfigDialogGeometry;
    };

    // View options
    struct ViewOptions {
        bool toggleChromaDuringSeek;
        bool generateProxyEnabled;
        bool exportProfileConfigEnabled;
        QString exportProfileConfigPath;
        bool resizeFrameWithWindow;
        bool showExportBoundary;
        qint32 exportBoundaryThickness;
        bool exportBoundaryThicknessUserSet;
    };

    // Update checker options
    struct UpdateCheck {
        bool enabled;                     // Master toggle for the weekly automatic check
        QString lastCheckTimestamp;       // ISO-8601 UTC of the last attempted check (empty = never)
        QString skippedVersion;           // Normalised version the user dismissed (empty = none)
    };

    // CUDA plugin registry entry (one plugin: the CUDA runtime for nnTransform3D GPU accel)
    struct CudaPlugin {
        QString installedVersion;         // Normalised version string (empty = not installed)
        QString releaseTag;               // GitHub release tag the package came from (e.g. cuda-plugin-v1)
        QString sha256;                   // Aggregate SHA-256 of the installed package (verification digest)
        bool enabled;                     // Whether the plugin should be loaded at runtime
        bool trusted;                     // User-granted trust (must be true before download/load)
        QString installPath;              // Absolute path where the plugin DLLs/SOs were installed
    };

    // RF segment export options (FLAC-Chop integration, issue #29)
    struct RfExport {
        QString flacChopPath;     // Persisted FLAC-Chop binary path (empty = auto-resolve)
        QString sourcePath;       // RF capture remembered from the last successful export (empty = discover)
        qint32 padBeforeMs;       // Padding before the frame's first field (ms)
        qint32 padAfterMs;        // Padding after the frame's end boundary (ms)
        bool saveAllAlsoExportRf; // "Also export source RF segment" checkbox in save-all-PNGs
    };

    // Overall settings structure
    struct Settings {
        qint32 version;
        Directories directories;
        Windows windows;
        ViewOptions viewOptions;
        UpdateCheck updateCheck;
        CudaPlugin cudaPlugin;
        RfExport rfExport;
        VbiProcessingOptions vbiProcessing;
        ExportAdvancedOptions exportAdvancedOptions;
    } settings;

    void setDefault(void);
};

#endif // CONFIGURATION_H
