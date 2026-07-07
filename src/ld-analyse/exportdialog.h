#ifndef EXPORTDIALOG_H
#define EXPORTDIALOG_H

#include <QWidget>
#include <QProcess>
#include <QHash>

class TbcSource;
class QResizeEvent;
class QSplitter;
class QTableWidget;

namespace Ui {
class ExportDialog;
}

class ExportDialog : public QWidget
{
    Q_OBJECT

public:
    enum class RunStage {
        Idle,
        MainExport,
        ProxyExport
    };
    struct ExportProcessStat {
        QString process;
        QString tbcType;
        QString trackedName;
        QString current;
        QString total;
        QString errors;
        QString fps;
        QString feedTag;
    };
    explicit ExportDialog(QWidget *parent = nullptr);
    ~ExportDialog();

    void setSource(TbcSource *source);
    void setGenerateProxyEnabledPreference(bool enabled);
    void setExportProfileConfigPreference(bool enabled, const QString &configPath);
    void setInPoint(int frameNumber);
    void setOutPoint(int frameNumber);
    bool hasAudioTracksConfigured() const;
    void loadAudioTracksForExport(const QStringList &trackFiles,
                                  const QStringList &trackNames = QStringList());
    void refreshResolutionOptions();
protected:
    void resizeEvent(QResizeEvent *event) override;
signals:
    void userEditRangeSelectionChanged(int inPoint, int outPoint, bool clearMetadataValues);
    void proxyGenerationPreferenceChanged(bool enabled);
    void exportProfileConfigPreferenceChanged(bool enabled, const QString &configPath);

private slots:
    void on_outputBrowseButton_clicked();
    void on_audio1BrowseButton_clicked();
    void on_audio2BrowseButton_clicked();
    void on_audio3BrowseButton_clicked();
    void on_audio4BrowseButton_clicked();
    void on_exportProfileConfigCheckBox_toggled(bool checked);
    void on_exportProfileConfigLoadButton_clicked();
    void on_exportProfileConfigEjectButton_clicked();
    void on_resetInOutButton_clicked();
    void on_exportButton_clicked();
    void on_cancelButton_clicked();

    void handleProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void handleProcessError(QProcess::ProcessError error);
    void handleProcessStdout();
    void handleProcessStderr();
    void handleParallelProxyProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void handleParallelProxyProcessError(QProcess::ProcessError error);
    void handleParallelProxyProcessStdout();
    void handleParallelProxyProcessStderr();

private:
    void syncResolutionModeSelectionFromSource();
    void refreshOutputResolutionModeOptions();
    void updateOutputResolutionModeControlsEnabledState(bool enabled);
    void updateFromSource();
    void refreshProfiles();
    void updateProfileDependentControls();
    void updateFeedLogPaneMode();
    void updateFeedLogPaneSizing();
    void emitRangeSelectionChanged(bool clearMetadataValues);
    void setBusy(bool busy);
    void updateRangeControlsForSource(bool resetToFullRange);
    void updateRangeLengthLabel();
    double timecodeFrameRate() const;
    int timecodeFrameBaseRate() const;
    QString frameToTimecode(int frameNumber) const;
    int timecodeToFrame(const QString &timecodeText, bool *ok = nullptr) const;
    void syncRangeTimecodeEditors();
    QString resolveVideoExportPath() const;
    QString resolveFfmpegPath() const;
    QString defaultOutputBaseName(const QString &inputFile) const;
    QString sourceDataDirectory() const;
    QString sourceStorageTemporaryRoot() const;
    QString sourceStorageTemporaryPath(const QString &fileName) const;
    QString sanitizeOutputBaseName(const QString &path) const;
    bool findExistingOutputFiles(const QString &outputBase, QStringList *existingFiles) const;
    QString selectedMainCodecId() const;
    QString selectedMainContainerId() const;
    QString selectedExportProfileConfigPath(QString *errorMessage = nullptr) const;
    int selectedMainBitDepth() const;
    QString selectedExportProfileName() const;
    QString proxyExportProfileName(const QString &proxyCodecId) const;
    QString videoSystemArg(int system) const;
    QStringList collectAudioTracks() const;
    bool shouldGenerateProxyForSelection() const;
    QString selectedProxyCodecId() const;
    QString proxyOutputPath(const QString &outputBase, const QString &proxyCodecId) const;
    bool startProxyExport(QString *errorMessage, bool forceOverwrite = false);
    void prepareVideoExportLaunch(const QString &exportPath,
                                  const QStringList &exportArgs,
                                  const QString &contextLabel,
                                  QString *programOut,
                                  QStringList *argsOut);
    void clearRunState();
    bool prepareTrimmedAudioTracks(int zeroBasedStartFrame, int rangeLengthFrames,
                                   QStringList *audioTracks, QString *errorMessage);
    QStringList buildArguments(QString *errorMessage, const QString &inputTbcJsonOverride = QString(),
                               bool overwriteExisting = false,
                               const QString &configFileOverride = QString(),
                               const QStringList &audioTracks = QStringList(),
                               int startFrameOneBasedOverride = -1,
                               int lengthOverride = -1,
                               const QString &profileOverride = QString(),
                               const QString &outputBaseOverride = QString()) const;
    QString createTemporaryExportConfig(QString *errorMessage,
                                        const QString &profileName,
                                        const QString &audioProfileName,
                                        const QString &baseConfigOverridePath = QString());
    QString createTemporaryMetadataSnapshot(QString *errorMessage);
    void cleanupTemporaryMetadataSnapshot();
    void updateExportProfileConfigPathUi();
    void emitExportProfileConfigPreferenceChanged();
    void appendStatus(const QString &message);
    void appendFeedStatus(const QString &message, const QString &feedTag);
    void clearFeedStatusLines();
    void appendFeedLog(const QString &message, const QString &feedTag);
    void clearFeedLogLines();
    void appendLog(const QString &message);
    QString writeExportFailureLog(const QString &failureTitle,
                                  const QString &summaryMessage,
                                  const QString &detailedOutput) const;
    void showExportFailureNotification(const QString &dialogTitle,
                                       const QString &summaryMessage,
                                       const QString &detailedOutput,
                                       const QString &fallbackMessage);
    QString formatCommand(const QString &program, const QStringList &args) const;
    void resetProcessStats();
    void initializeProcessStats();
    void updateProcessStatsPaneMode();
    QTableWidget *processStatsTableForFeed(const QString &feedTag) const;
    void updateProcessStat(const ExportProcessStat &stat);
    void processOutputLine(const QString &line, QString *lastStatus, const QString &feedTag);
    void consumeProcessOutputChunk(const QString &chunk, QString *pendingBuffer, const QString &feedTag);
    void flushPendingProcessOutput(const QString &feedTag);

    Ui::ExportDialog *ui;
    TbcSource *tbcSource = nullptr;
    QProcess *exportProcess = nullptr;
    QProcess *parallelProxyProcess = nullptr;
    QString currentInputFile;
    QString processStdout;
    QString processStderr;
    QString pendingStdoutBuffer;
    QString pendingStderrBuffer;
    QString parallelProxyStdout;
    QString parallelProxyStderr;
    QString pendingParallelProxyStdoutBuffer;
    QString pendingParallelProxyStderrBuffer;
    QString exportProfileConfigPath;
    QString temporaryInputJsonPath;
    QString temporaryExportConfigPath;
    QStringList temporaryAudioTrackPaths;
    RunStage activeRunStage = RunStage::Idle;
    bool generateProxyForCurrentRun = false;
    bool audioConfiguredForCurrentRun = false;
    bool overwriteExistingForCurrentRun = false;
    QString outputBaseForCurrentRun;
    QString proxyCodecForCurrentRun;
    QString proxyOutputPathForCurrentRun;
    bool outputAutoSet = true;
    bool exportAvailable = false;
    bool cancelRequested = false;
    bool parallelProxyRunning = false;
    bool parallelProxyFinished = false;
    bool parallelProxySucceeded = false;
    bool mainExportFinished = false;
    bool mainExportSucceeded = false;
    bool splitStatsByFeed = false;
    QString mainFeedStatusLine;
    QString proxyFeedStatusLine;
    QString mainFeedLogLine;
    QString proxyFeedLogLine;
    QSplitter *processStatsSplitter = nullptr;
    QTableWidget *mainProcessStatsTable = nullptr;
    QTableWidget *proxyProcessStatsTable = nullptr;
    QHash<QString, int> processRowMap;
};

#endif // EXPORTDIALOG_H
