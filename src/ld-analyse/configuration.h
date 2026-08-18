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

    // Overall settings structure
    struct Settings {
        qint32 version;
        Directories directories;
        Windows windows;
        ViewOptions viewOptions;
        UpdateCheck updateCheck;
        CudaPlugin cudaPlugin;
    } settings;

    void setDefault(void);
};

#endif // CONFIGURATION_H
