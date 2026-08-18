/******************************************************************************
 * configuration.cpp
 * tbc-analyse - TBC output analysis GUI
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2018-2025 Simon Inns
 *
 * This file is part of tbc-tools.
 ******************************************************************************/

#include "configuration.h"
#include "tbc/logging.h"

// This define should be incremented if the settings file format changes
static const qint32 SETTINGSVERSION = 5;

Configuration::Configuration(QObject *parent) : QObject(parent)
{
    // Open the application's configuration file
    QString configurationPath;
    QString configurationFileName;

    configurationPath = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) ;
    configurationFileName = "ld-analyse.ini" ;
    configuration = new QSettings(configurationPath + "/"+ configurationFileName, QSettings::IniFormat);

    // Read the configuration
    readConfiguration();

    // Are the configuration settings valid?
    if (settings.version != SETTINGSVERSION) {
        tbcDebugStream() << "Configuration::Configuration(): Configuration invalid or wrong version ("
                         << settings.version << "!=" << SETTINGSVERSION << "). Setting to default values";

        // Set default configuration
        setDefault();
    }
}

Configuration::~Configuration()
{
    delete configuration;
}

void Configuration::writeConfiguration(void)
{
    // Write the valid configuration flag
    configuration->setValue("version", settings.version);

    // Directories
    configuration->beginGroup("directories");
    configuration->setValue("sourceDirectory", settings.directories.sourceDirectory);
    configuration->setValue("pngDirectory", settings.directories.pngDirectory);
    configuration->endGroup();

    // Windows
    configuration->beginGroup("windows");
    configuration->setValue("mainWindowGeometry", settings.windows.mainWindowGeometry);
    configuration->setValue("mainWindowScaleFactor", settings.windows.mainWindowScaleFactor);
    configuration->setValue("vbiDialogGeometry", settings.windows.vbiDialogGeometry);
    configuration->setValue("oscilloscopeDialogGeometry", settings.windows.oscilloscopeDialogGeometry);
    configuration->setValue("vectorscopeDialogGeometry", settings.windows.vectorscopeDialogGeometry);
    configuration->setValue("waveformMonitorDialogGeometry", settings.windows.waveformMonitorDialogGeometry);
    configuration->setValue("dropoutAnalysisDialogGeometry", settings.windows.dropoutAnalysisDialogGeometry);
    configuration->setValue("visibleDropoutAnalysisDialogGeometry", settings.windows.visibleDropoutAnalysisDialogGeometry);
    configuration->setValue("blackSnrAnalysisDialogGeometry", settings.windows.blackSnrAnalysisDialogGeometry);
    configuration->setValue("whiteSnrAnalysisDialogGeometry", settings.windows.whiteSnrAnalysisDialogGeometry);
    configuration->setValue("closedCaptionDialogGeometry", settings.windows.closedCaptionDialogGeometry);
    configuration->setValue("videoParametersDialogGeometry", settings.windows.videoParametersDialogGeometry);
    configuration->setValue("chromaDecoderConfigDialogGeometry", settings.windows.chromaDecoderConfigDialogGeometry);
    configuration->endGroup();

    // View options
    configuration->beginGroup("viewOptions");
    configuration->setValue("toggleChromaDuringSeek", settings.viewOptions.toggleChromaDuringSeek);
    configuration->setValue("generateProxyEnabled", settings.viewOptions.generateProxyEnabled);
    configuration->setValue("exportProfileConfigEnabled", settings.viewOptions.exportProfileConfigEnabled);
    configuration->setValue("exportProfileConfigPath", settings.viewOptions.exportProfileConfigPath);
    configuration->setValue("resizeFrameWithWindow", settings.viewOptions.resizeFrameWithWindow);
    configuration->setValue("showExportBoundary", settings.viewOptions.showExportBoundary);
    configuration->setValue("exportBoundaryThickness", settings.viewOptions.exportBoundaryThickness);
    configuration->endGroup();

    // Update checker
    configuration->beginGroup("updateCheck");
    configuration->setValue("enabled", settings.updateCheck.enabled);
    configuration->setValue("lastCheckTimestamp", settings.updateCheck.lastCheckTimestamp);
    configuration->setValue("skippedVersion", settings.updateCheck.skippedVersion);
    configuration->endGroup();

    // CUDA plugin registry
    configuration->beginGroup("cudaPlugin");
    configuration->setValue("installedVersion", settings.cudaPlugin.installedVersion);
    configuration->setValue("releaseTag", settings.cudaPlugin.releaseTag);
    configuration->setValue("sha256", settings.cudaPlugin.sha256);
    configuration->setValue("enabled", settings.cudaPlugin.enabled);
    configuration->setValue("trusted", settings.cudaPlugin.trusted);
    configuration->setValue("installPath", settings.cudaPlugin.installPath);
    configuration->endGroup();

    // Sync the settings with disk
    tbcDebugStream() << "Configuration::writeConfiguration(): Writing configuration to disk";
    configuration->sync();
}

void Configuration::readConfiguration(void)
{
    tbcDebugStream() << "Configuration::readConfiguration(): Reading configuration from" << configuration->fileName();

    // Read the valid configuration flag
    settings.version = configuration->value("version").toInt();

    // Directories
    configuration->beginGroup("directories");
    settings.directories.sourceDirectory = configuration->value("sourceDirectory").toString();
    settings.directories.pngDirectory = configuration->value("pngDirectory").toString();
    configuration->endGroup();

    // Windows
    configuration->beginGroup("windows");
    settings.windows.mainWindowGeometry = configuration->value("mainWindowGeometry").toByteArray();
    settings.windows.mainWindowScaleFactor = configuration->value("mainWindowScaleFactor").toReal();
    settings.windows.vbiDialogGeometry = configuration->value("vbiDialogGeometry").toByteArray();
    settings.windows.oscilloscopeDialogGeometry = configuration->value("oscilloscopeDialogGeometry").toByteArray();
    settings.windows.vectorscopeDialogGeometry = configuration->value("vectorscopeDialogGeometry").toByteArray();
    settings.windows.waveformMonitorDialogGeometry = configuration->value("waveformMonitorDialogGeometry").toByteArray();
    settings.windows.dropoutAnalysisDialogGeometry = configuration->value("dropoutAnalysisDialogGeometry").toByteArray();
    settings.windows.visibleDropoutAnalysisDialogGeometry = configuration->value("visibleDropoutAnalysisDialogGeometry").toByteArray();
    settings.windows.blackSnrAnalysisDialogGeometry = configuration->value("blackSnrAnalysisDialogGeometry").toByteArray();
    settings.windows.whiteSnrAnalysisDialogGeometry = configuration->value("whiteSnrAnalysisDialogGeometry").toByteArray();
    settings.windows.closedCaptionDialogGeometry = configuration->value("closedCaptionDialogGeometry").toByteArray();
    settings.windows.videoParametersDialogGeometry = configuration->value("videoParametersDialogGeometry").toByteArray();
    settings.windows.chromaDecoderConfigDialogGeometry = configuration->value("chromaDecoderConfigDialogGeometry").toByteArray();
    configuration->endGroup();

    // View options
    configuration->beginGroup("viewOptions");
    settings.viewOptions.toggleChromaDuringSeek = configuration->value("toggleChromaDuringSeek", false).toBool();
    settings.viewOptions.generateProxyEnabled = configuration->value("generateProxyEnabled", false).toBool();
    settings.viewOptions.exportProfileConfigEnabled = configuration->value("exportProfileConfigEnabled", false).toBool();
    settings.viewOptions.exportProfileConfigPath = configuration->value("exportProfileConfigPath", QString()).toString();
    settings.viewOptions.resizeFrameWithWindow = configuration->value("resizeFrameWithWindow", true).toBool();
    settings.viewOptions.showExportBoundary = configuration->value("showExportBoundary", true).toBool();
    settings.viewOptions.exportBoundaryThickness = configuration->value("exportBoundaryThickness", 4).toInt();
    if (settings.viewOptions.exportBoundaryThickness < 1) settings.viewOptions.exportBoundaryThickness = 1;
    if (settings.viewOptions.exportBoundaryThickness > 8) settings.viewOptions.exportBoundaryThickness = 8;
    configuration->endGroup();

    // Update checker (additive keys - older config files fall back to defaults)
    configuration->beginGroup("updateCheck");
    settings.updateCheck.enabled = configuration->value("enabled", true).toBool();
    settings.updateCheck.lastCheckTimestamp = configuration->value("lastCheckTimestamp", QString()).toString();
    settings.updateCheck.skippedVersion = configuration->value("skippedVersion", QString()).toString();
    configuration->endGroup();

    // CUDA plugin registry (additive keys - older config files fall back to defaults)
    configuration->beginGroup("cudaPlugin");
    settings.cudaPlugin.installedVersion = configuration->value("installedVersion", QString()).toString();
    settings.cudaPlugin.releaseTag = configuration->value("releaseTag", QString()).toString();
    settings.cudaPlugin.sha256 = configuration->value("sha256", QString()).toString();
    settings.cudaPlugin.enabled = configuration->value("enabled", false).toBool();
    settings.cudaPlugin.trusted = configuration->value("trusted", false).toBool();
    settings.cudaPlugin.installPath = configuration->value("installPath", QString()).toString();
    configuration->endGroup();
}

void Configuration::setDefault(void)
{
    // Set up the default values
    settings.version = SETTINGSVERSION;

    // Directories
    settings.directories.sourceDirectory = QDir::homePath();
    settings.directories.pngDirectory = QDir::homePath();

    // Windows
    settings.windows.mainWindowGeometry = QByteArray();
    settings.windows.mainWindowScaleFactor = 1.0;
    settings.windows.vbiDialogGeometry = QByteArray();
    settings.windows.oscilloscopeDialogGeometry = QByteArray();
    settings.windows.vectorscopeDialogGeometry = QByteArray();
    settings.windows.waveformMonitorDialogGeometry = QByteArray();
    settings.windows.dropoutAnalysisDialogGeometry = QByteArray();
    settings.windows.visibleDropoutAnalysisDialogGeometry = QByteArray();
    settings.windows.blackSnrAnalysisDialogGeometry = QByteArray();
    settings.windows.whiteSnrAnalysisDialogGeometry = QByteArray();
    settings.windows.closedCaptionDialogGeometry = QByteArray();
    settings.windows.videoParametersDialogGeometry = QByteArray();
    settings.windows.chromaDecoderConfigDialogGeometry = QByteArray();

    // View options
    settings.viewOptions.toggleChromaDuringSeek = false;
    settings.viewOptions.generateProxyEnabled = false;
    settings.viewOptions.exportProfileConfigEnabled = false;
    settings.viewOptions.exportProfileConfigPath = QString();
    settings.viewOptions.resizeFrameWithWindow = true;
    settings.viewOptions.showExportBoundary = true;
    settings.viewOptions.exportBoundaryThickness = 4;

    // Update checker
    settings.updateCheck.enabled = true;
    settings.updateCheck.lastCheckTimestamp = QString();
    settings.updateCheck.skippedVersion = QString();

    // CUDA plugin registry
    settings.cudaPlugin.installedVersion = QString();
    settings.cudaPlugin.releaseTag = QString();
    settings.cudaPlugin.sha256 = QString();
    settings.cudaPlugin.enabled = false;
    settings.cudaPlugin.trusted = false;
    settings.cudaPlugin.installPath = QString();

    // Write the configuration
    writeConfiguration();
}

// Functions to get and set configuration values ----------------------------------------------------------------------

// Directories
void Configuration::setSourceDirectory(QString sourceDirectory)
{
    settings.directories.sourceDirectory = sourceDirectory;
}

QString Configuration::getSourceDirectory(void)
{
    return settings.directories.sourceDirectory;
}

void Configuration::setPngDirectory(QString pngDirectory)
{
    settings.directories.pngDirectory = pngDirectory;
}

QString Configuration::getPngDirectory(void)
{
    return settings.directories.pngDirectory;
}

// Windows
void Configuration::setMainWindowGeometry(QByteArray mainWindowGeometry)
{
    settings.windows.mainWindowGeometry = mainWindowGeometry;
}

QByteArray Configuration::getMainWindowGeometry(void)
{
    return settings.windows.mainWindowGeometry;
}

void Configuration::setMainWindowScaleFactor(double mainWindowScaleFactor)
{
    settings.windows.mainWindowScaleFactor = mainWindowScaleFactor;
}

double Configuration::getMainWindowScaleFactor(void)
{
    return settings.windows.mainWindowScaleFactor;
}

void Configuration::setVbiDialogGeometry(QByteArray vbiDialogGeometry)
{
    settings.windows.vbiDialogGeometry = vbiDialogGeometry;
}

QByteArray Configuration::getVbiDialogGeometry(void)
{
    return settings.windows.vbiDialogGeometry;
}

void Configuration::setOscilloscopeDialogGeometry(QByteArray oscilloscopeDialogGeometry)
{
    settings.windows.oscilloscopeDialogGeometry = oscilloscopeDialogGeometry;
}

QByteArray Configuration::getOscilloscopeDialogGeometry(void)
{
    return settings.windows.oscilloscopeDialogGeometry;
}

void Configuration::setVectorscopeDialogGeometry(QByteArray vectorscopeDialogGeometry)
{
    settings.windows.vectorscopeDialogGeometry = vectorscopeDialogGeometry;
}

QByteArray Configuration::getVectorscopeDialogGeometry(void)
{
    return settings.windows.vectorscopeDialogGeometry;
}

void Configuration::setWaveformMonitorDialogGeometry(QByteArray waveformMonitorDialogGeometry)
{
    settings.windows.waveformMonitorDialogGeometry = waveformMonitorDialogGeometry;
}

QByteArray Configuration::getWaveformMonitorDialogGeometry(void)
{
    return settings.windows.waveformMonitorDialogGeometry;
}

void Configuration::setDropoutAnalysisDialogGeometry(QByteArray dropoutAnalysisDialogGeometry)
{
    settings.windows.dropoutAnalysisDialogGeometry = dropoutAnalysisDialogGeometry;
}

QByteArray Configuration::getDropoutAnalysisDialogGeometry(void)
{
    return settings.windows.dropoutAnalysisDialogGeometry;
}

void Configuration::setVisibleDropoutAnalysisDialogGeometry(QByteArray visibleDropoutAnalysisDialogGeometry)
{
    settings.windows.visibleDropoutAnalysisDialogGeometry = visibleDropoutAnalysisDialogGeometry;
}

QByteArray Configuration::getVisibleDropoutAnalysisDialogGeometry(void)
{
    return settings.windows.visibleDropoutAnalysisDialogGeometry;
}

void Configuration::setBlackSnrAnalysisDialogGeometry(QByteArray blackSnrAnalysisDialogGeometry)
{
    settings.windows.blackSnrAnalysisDialogGeometry = blackSnrAnalysisDialogGeometry;
}

QByteArray Configuration::getBlackSnrAnalysisDialogGeometry(void)
{
    return settings.windows.blackSnrAnalysisDialogGeometry;
}

void Configuration::setWhiteSnrAnalysisDialogGeometry(QByteArray whiteSnrAnalysisDialogGeometry)
{
    settings.windows.whiteSnrAnalysisDialogGeometry = whiteSnrAnalysisDialogGeometry;
}

QByteArray Configuration::getWhiteSnrAnalysisDialogGeometry(void)
{
    return settings.windows.whiteSnrAnalysisDialogGeometry;
}

void Configuration::setClosedCaptionDialogGeometry(QByteArray closedCaptionDialogGeometry)
{
    settings.windows.closedCaptionDialogGeometry = closedCaptionDialogGeometry;
}

QByteArray Configuration::getClosedCaptionDialogGeometry(void)
{
    return settings.windows.closedCaptionDialogGeometry;
}

void Configuration::setVideoParametersDialogGeometry(QByteArray videoParametersDialogGeometry)
{
    settings.windows.videoParametersDialogGeometry = videoParametersDialogGeometry;
}

QByteArray Configuration::getVideoParametersDialogGeometry(void)
{
    return settings.windows.videoParametersDialogGeometry;
}

void Configuration::setChromaDecoderConfigDialogGeometry(QByteArray chromaDecoderConfigDialogGeometry)
{
    settings.windows.chromaDecoderConfigDialogGeometry = chromaDecoderConfigDialogGeometry;
}

QByteArray Configuration::getChromaDecoderConfigDialogGeometry(void)
{
    return settings.windows.chromaDecoderConfigDialogGeometry;
}

// View options
void Configuration::setToggleChromaDuringSeek(bool toggleChromaDuringSeek)
{
    settings.viewOptions.toggleChromaDuringSeek = toggleChromaDuringSeek;
}

bool Configuration::getToggleChromaDuringSeek(void)
{
    return settings.viewOptions.toggleChromaDuringSeek;
}

void Configuration::setGenerateProxyEnabled(bool generateProxyEnabled)
{
    settings.viewOptions.generateProxyEnabled = generateProxyEnabled;
}

bool Configuration::getGenerateProxyEnabled(void)
{
    return settings.viewOptions.generateProxyEnabled;
}
void Configuration::setExportProfileConfigEnabled(bool exportProfileConfigEnabled)
{
    settings.viewOptions.exportProfileConfigEnabled = exportProfileConfigEnabled;
}

bool Configuration::getExportProfileConfigEnabled(void)
{
    return settings.viewOptions.exportProfileConfigEnabled;
}

void Configuration::setExportProfileConfigPath(QString exportProfileConfigPath)
{
    settings.viewOptions.exportProfileConfigPath = exportProfileConfigPath;
}

QString Configuration::getExportProfileConfigPath(void)
{
    return settings.viewOptions.exportProfileConfigPath;
}

void Configuration::setResizeFrameWithWindow(bool resizeFrameWithWindow)
{
    settings.viewOptions.resizeFrameWithWindow = resizeFrameWithWindow;
}

bool Configuration::getResizeFrameWithWindow(void)
{
    return settings.viewOptions.resizeFrameWithWindow;
}

void Configuration::setShowExportBoundary(bool showExportBoundary)
{
    settings.viewOptions.showExportBoundary = showExportBoundary;
}

bool Configuration::getShowExportBoundary(void)
{
    return settings.viewOptions.showExportBoundary;
}

void Configuration::setExportBoundaryThickness(qint32 exportBoundaryThickness)
{
    settings.viewOptions.exportBoundaryThickness = exportBoundaryThickness;
}

qint32 Configuration::getExportBoundaryThickness(void)
{
    return settings.viewOptions.exportBoundaryThickness;
}

// Update checker
void Configuration::setUpdateCheckEnabled(bool updateCheckEnabled)
{
    settings.updateCheck.enabled = updateCheckEnabled;
}

bool Configuration::getUpdateCheckEnabled(void)
{
    return settings.updateCheck.enabled;
}

void Configuration::setLastUpdateCheckTimestamp(QString lastUpdateCheckTimestamp)
{
    settings.updateCheck.lastCheckTimestamp = lastUpdateCheckTimestamp;
}

QString Configuration::getLastUpdateCheckTimestamp(void)
{
    return settings.updateCheck.lastCheckTimestamp;
}

void Configuration::setSkippedUpdateVersion(QString skippedUpdateVersion)
{
    settings.updateCheck.skippedVersion = skippedUpdateVersion;
}

QString Configuration::getSkippedUpdateVersion(void)
{
    return settings.updateCheck.skippedVersion;
}

// CUDA plugin registry
void Configuration::setCudaPluginInstalledVersion(QString version)
{
    settings.cudaPlugin.installedVersion = version;
}

QString Configuration::getCudaPluginInstalledVersion(void)
{
    return settings.cudaPlugin.installedVersion;
}

void Configuration::setCudaPluginReleaseTag(QString tag)
{
    settings.cudaPlugin.releaseTag = tag;
}

QString Configuration::getCudaPluginReleaseTag(void)
{
    return settings.cudaPlugin.releaseTag;
}

void Configuration::setCudaPluginSha256(QString sha256)
{
    settings.cudaPlugin.sha256 = sha256;
}

QString Configuration::getCudaPluginSha256(void)
{
    return settings.cudaPlugin.sha256;
}

void Configuration::setCudaPluginEnabled(bool enabled)
{
    settings.cudaPlugin.enabled = enabled;
}

bool Configuration::getCudaPluginEnabled(void)
{
    return settings.cudaPlugin.enabled;
}

void Configuration::setCudaPluginTrusted(bool trusted)
{
    settings.cudaPlugin.trusted = trusted;
}

bool Configuration::getCudaPluginTrusted(void)
{
    return settings.cudaPlugin.trusted;
}

void Configuration::setCudaPluginInstallPath(QString path)
{
    settings.cudaPlugin.installPath = path;
}

QString Configuration::getCudaPluginInstallPath(void)
{
    return settings.cudaPlugin.installPath;
}
