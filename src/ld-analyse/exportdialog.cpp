#include "exportdialog.h"
#include "ui_exportdialog.h"

#include "tbcsource.h"

#include <QCoreApplication>
#include <QDir>
#include <QDateTime>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontMetrics>
#include <QMessageBox>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QAbstractItemView>
#include <signal.h>

namespace {
void setTableItem(QTableWidget *table, int row, int column, const QString &text)
{
    if (!table) {
        return;
    }
    QTableWidgetItem *item = table->item(row, column);
    if (!item) {
        item = new QTableWidgetItem();
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        table->setItem(row, column, item);
    }
    item->setText(text);
}
QString stripAnsiSequences(const QString &input)
{
    static const QRegularExpression ansiPattern(QStringLiteral("\\x1B\\[[0-9;?]*[A-Za-z]"));
    QString cleaned = input;
    cleaned.replace(ansiPattern, QString());
    return cleaned;
}
bool isIgnorableLogLine(const QString &line)
{
    return line.contains(QStringLiteral("No support for ANSI escape sequences"), Qt::CaseInsensitive);
}
QString quoteArgument(const QString &arg)
{
    if (arg.isEmpty()) {
        return QStringLiteral("\"\"");
    }
    if (arg.contains(QRegularExpression(QStringLiteral("[\\s\"]")))) {
        QString escaped = arg;
        escaped.replace(QStringLiteral("\""), QStringLiteral("\\\""));
        return QStringLiteral("\"%1\"").arg(escaped);
    }
    return arg;
}

QString shellQuoteArgument(const QString &arg)
{
    if (arg.isEmpty()) {
        return QStringLiteral("''");
    }
    QString escaped = arg;
    escaped.replace(QStringLiteral("'"), QStringLiteral("'\"'\"'"));
    return QStringLiteral("'%1'").arg(escaped);
}

QString formatShellCommand(const QString &program, const QStringList &args)
{
    QStringList parts;
    parts << shellQuoteArgument(program);
    for (const QString &arg : args) {
        parts << shellQuoteArgument(arg);
    }
    return parts.join(' ');
}

QStringList parseProfiles(const QString &rawOutput, QString *defaultProfile)
{
    QStringList profiles;
    const QString cleaned = stripAnsiSequences(rawOutput);
    const QStringList lines = cleaned.split(QRegularExpression(QStringLiteral("[\\r\\n]+")), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (!trimmed.startsWith(QStringLiteral("--"))) {
            continue;
        }
        QString name = trimmed.mid(2).trimmed();
        const int spaceIndex = name.indexOf(QRegularExpression(QStringLiteral("\\s")));
        if (spaceIndex > 0) {
            name = name.left(spaceIndex);
        }
        if (name.isEmpty()) {
            continue;
        }
        if (defaultProfile && trimmed.contains(QStringLiteral("default"), Qt::CaseInsensitive)) {
            *defaultProfile = name;
        }
        if (!profiles.contains(name)) {
            profiles << name;
        }
    }
    if (!profiles.isEmpty()) {
        return profiles;
    }

    const QRegularExpression profilePattern(QStringLiteral("--([A-Za-z0-9_\\-]+)"));
    QRegularExpressionMatchIterator matches = profilePattern.globalMatch(cleaned);
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        const QString name = match.captured(1);
        if (!name.isEmpty() && !profiles.contains(name)) {
            profiles << name;
        }
    }
    return profiles;
}


bool parseProgressLine(const QString &line, ExportDialog::ExportProcessStat *stat)
{
    if (!stat) {
        return false;
    }
    static const QRegularExpression progressPattern(
        QStringLiteral("^\\s*[/\\\\\\-|●○]?\\s*([a-z0-9\\-]+)\\s*(?:\\(([^)]+)\\))?\\s*([A-Za-z%]+)\\s*:\\s*(\\d+)(?:/(\\d+))?.*?errors:\\s*(\\d+)(?:.*?fps:\\s*([0-9.]+))?"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = progressPattern.match(line);
    if (!match.hasMatch()) {
        return false;
    }
    stat->process = match.captured(1).trimmed();
    stat->tbcType = match.captured(2).trimmed();
    stat->trackedName = match.captured(3).trimmed();
    stat->current = match.captured(4).trimmed();
    stat->total = match.captured(5).trimmed();
    stat->errors = match.captured(6).trimmed();
    stat->fps = match.captured(7).trimmed();
    if (stat->tbcType.isEmpty()) {
        stat->tbcType = QStringLiteral("—");
    }
    if (stat->trackedName.isEmpty()) {
        stat->trackedName = QStringLiteral("—");
    }
    if (stat->total.isEmpty()) {
        stat->total = QStringLiteral("—");
    }
    if (stat->errors.isEmpty()) {
        stat->errors = QStringLiteral("0");
    }
    if (stat->fps.isEmpty()) {
        stat->fps = QStringLiteral("—");
    }
    return !stat->process.isEmpty();
}

}

ExportDialog::ExportDialog(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::ExportDialog)
{
    ui->setupUi(this);

    ui->inputLineEdit->setReadOnly(true);
    ui->profileComboBox->setEditable(true);
    ui->profileComboBox->setInsertPolicy(QComboBox::NoInsert);
    if (ui->processStatsTable) {
        ui->processStatsTable->setColumnCount(7);
        ui->processStatsTable->setHorizontalHeaderLabels({
            tr("Process"),
            tr("TBC"),
            tr("Track"),
            tr("Current"),
            tr("Total"),
            tr("Errors"),
            tr("FPS")
        });
        ui->processStatsTable->verticalHeader()->setVisible(false);
        ui->processStatsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        ui->processStatsTable->setSelectionMode(QAbstractItemView::NoSelection);
        ui->processStatsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        ui->processStatsTable->setAlternatingRowColors(true);
        ui->processStatsTable->horizontalHeader()->setStretchLastSection(true);
        ui->processStatsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    }
    if (ui->logTextEdit) {
        ui->logTextEdit->setMaximumBlockCount(3);
        const int lineHeight = QFontMetrics(ui->logTextEdit->font()).lineSpacing();
        ui->logTextEdit->setFixedHeight(lineHeight * 3 + 8);
    }
    if (ui->cancelButton) {
        ui->cancelButton->setEnabled(false);
    }
    connect(ui->outputLineEdit, &QLineEdit::textEdited, this, [this]() {
        outputAutoSet = false;
    });

    exportProcess = new QProcess(this);
    connect(exportProcess, &QProcess::finished, this, &ExportDialog::handleProcessFinished);
    connect(exportProcess, &QProcess::errorOccurred, this, &ExportDialog::handleProcessError);
    connect(exportProcess, &QProcess::readyReadStandardOutput, this, &ExportDialog::handleProcessStdout);
    connect(exportProcess, &QProcess::readyReadStandardError, this, &ExportDialog::handleProcessStderr);
    connect(exportProcess, &QProcess::started, this, [this]() {
        appendLog(tr("tbc-video-export started (PID %1).").arg(exportProcess->processId()));
    });

    appendStatus(tr("Ready."));
    appendLog(tr("Ready."));
}

ExportDialog::~ExportDialog()
{
    delete ui;
}

void ExportDialog::setSource(TbcSource *source)
{
    tbcSource = source;
    updateFromSource();
    refreshProfiles();
}

void ExportDialog::updateFromSource()
{
    const bool hasSource = tbcSource && tbcSource->getIsSourceLoaded();
    const bool metadataOnly = hasSource && tbcSource->getIsMetadataOnly();
    exportAvailable = hasSource && !metadataOnly;

    if (!hasSource) {
        ui->inputLineEdit->clear();
        appendStatus(tr("No source loaded."));
        setBusy(exportProcess->state() != QProcess::NotRunning);
        return;
    }

    if (metadataOnly) {
        ui->inputLineEdit->clear();
        appendStatus(tr("Metadata-only mode (no TBC source)."));
        setBusy(exportProcess->state() != QProcess::NotRunning);
        return;
    }

    const QString inputFile = tbcSource->getCurrentSourceFilename();
    if (!inputFile.isEmpty()) {
        if (currentInputFile != inputFile) {
            currentInputFile = inputFile;
            ui->inputLineEdit->setText(inputFile);
            if (outputAutoSet || ui->outputLineEdit->text().trimmed().isEmpty()) {
                ui->outputLineEdit->setText(defaultOutputBaseName(inputFile));
                outputAutoSet = true;
            }
        }
    }

    setBusy(exportProcess->state() != QProcess::NotRunning);
    if (exportAvailable && exportProcess->state() == QProcess::NotRunning) {
        appendStatus(tr("Ready to export."));
    }
}

void ExportDialog::refreshProfiles()
{
    QString defaultProfile;
    QString errorMessage;
    QString profileOutput;

    const QString exportPath = resolveVideoExportPath();
    if (exportPath.isEmpty()) {
        appendStatus(tr("tbc-video-export not found."));
        appendLog(tr("tbc-video-export not found."));
        return;
    }

    QProcess listProcess;
    listProcess.setProcessChannelMode(QProcess::MergedChannels);
    listProcess.start(exportPath, QStringList() << QStringLiteral("--list-profiles"));
    if (!listProcess.waitForStarted(3000)) {
        appendStatus(tr("Failed to start profile listing."));
        appendLog(tr("Failed to start profile listing."));
        return;
    }
    if (!listProcess.waitForFinished(10000)) {
        listProcess.kill();
        appendStatus(tr("Profile list timed out."));
        appendLog(tr("Profile list timed out."));
        return;
    }
    profileOutput = QString::fromLocal8Bit(listProcess.readAllStandardOutput());

    if (listProcess.exitStatus() != QProcess::NormalExit || listProcess.exitCode() != 0) {
        errorMessage = profileOutput.trimmed();
        if (errorMessage.isEmpty()) {
            errorMessage = tr("Failed to list profiles.");
        }
        appendStatus(errorMessage);
        appendLog(errorMessage);
        return;
    }
    const QString currentText = ui->profileComboBox->currentText().trimmed();
    const QStringList profiles = parseProfiles(profileOutput, &defaultProfile);

    if (profiles.isEmpty()) {
        appendStatus(tr("No profiles found."));
        appendLog(tr("No profiles found."));
        return;
    }

    ui->profileComboBox->clear();
    ui->profileComboBox->addItems(profiles);

    if (!currentText.isEmpty() && profiles.contains(currentText)) {
        ui->profileComboBox->setCurrentText(currentText);
    } else if (!defaultProfile.isEmpty() && profiles.contains(defaultProfile)) {
        ui->profileComboBox->setCurrentText(defaultProfile);
    } else {
        ui->profileComboBox->setCurrentIndex(0);
    }
}


void ExportDialog::on_outputBrowseButton_clicked()
{
    QString suggestedOutput = ui->outputLineEdit->text().trimmed();
    if (suggestedOutput.isEmpty() && !currentInputFile.isEmpty()) {
        suggestedOutput = defaultOutputBaseName(currentInputFile);
    }

    const QString selected = QFileDialog::getSaveFileName(this,
                                                          tr("Select output base name"),
                                                          suggestedOutput,
                                                          tr("All Files (*)"));
    if (selected.isEmpty()) {
        return;
    }

    ui->outputLineEdit->setText(sanitizeOutputBaseName(selected));
    outputAutoSet = false;
}

void ExportDialog::on_audio1BrowseButton_clicked()
{
    const QString selected = QFileDialog::getOpenFileName(this,
                                                          tr("Select audio track 1"),
                                                          QString(),
                                                          tr("Audio Files (*.wav *.flac *.aiff *.aif *.mp3 *.m4a *.aac *.ogg);;All Files (*)"));
    if (!selected.isEmpty()) {
        ui->audio1LineEdit->setText(selected);
    }
}

void ExportDialog::on_audio2BrowseButton_clicked()
{
    const QString selected = QFileDialog::getOpenFileName(this,
                                                          tr("Select audio track 2"),
                                                          QString(),
                                                          tr("Audio Files (*.wav *.flac *.aiff *.aif *.mp3 *.m4a *.aac *.ogg);;All Files (*)"));
    if (!selected.isEmpty()) {
        ui->audio2LineEdit->setText(selected);
    }
}

void ExportDialog::on_audio3BrowseButton_clicked()
{
    const QString selected = QFileDialog::getOpenFileName(this,
                                                          tr("Select audio track 3"),
                                                          QString(),
                                                          tr("Audio Files (*.wav *.flac *.aiff *.aif *.mp3 *.m4a *.aac *.ogg);;All Files (*)"));
    if (!selected.isEmpty()) {
        ui->audio3LineEdit->setText(selected);
    }
}

void ExportDialog::on_audio4BrowseButton_clicked()
{
    const QString selected = QFileDialog::getOpenFileName(this,
                                                          tr("Select audio track 4"),
                                                          QString(),
                                                          tr("Audio Files (*.wav *.flac *.aiff *.aif *.mp3 *.m4a *.aac *.ogg);;All Files (*)"));
    if (!selected.isEmpty()) {
        ui->audio4LineEdit->setText(selected);
    }
}

void ExportDialog::on_exportButton_clicked()
{
    if (!exportAvailable) {
        if (!tbcSource || !tbcSource->getIsSourceLoaded()) {
            appendStatus(tr("Export unavailable: no source loaded."));
            appendLog(tr("Export unavailable: no source loaded."));
            QMessageBox::warning(this, tr("Error"), tr("No source file loaded."));
        } else if (tbcSource->getIsMetadataOnly()) {
            appendStatus(tr("Export unavailable: metadata-only mode."));
            appendLog(tr("Export unavailable: metadata-only mode."));
            QMessageBox::warning(this, tr("Error"), tr("Metadata-only mode does not include a TBC source file."));
        } else {
            appendStatus(tr("Export unavailable."));
            appendLog(tr("Export unavailable."));
            QMessageBox::warning(this, tr("Error"), tr("Export is not available."));
        }
        return;
    }
    if (exportProcess->state() != QProcess::NotRunning) {
        appendStatus(tr("Export already running."));
        appendLog(tr("Export already running."));
        return;
    }
    appendStatus(tr("Starting export..."));
    appendLog(tr("Starting export..."));

    QString errorMessage;
    const QStringList arguments = buildArguments(&errorMessage);
    if (arguments.isEmpty()) {
        if (!errorMessage.isEmpty()) {
            appendStatus(errorMessage);
            appendLog(errorMessage);
            QMessageBox::warning(this, tr("Error"), errorMessage);
        }
        return;
    }

    const QString exportPath = resolveVideoExportPath();
    if (exportPath.isEmpty()) {
        appendStatus(tr("tbc-video-export not found."));
        appendLog(tr("tbc-video-export not found."));
        QMessageBox::warning(this, tr("Error"), tr("tbc-video-export not found in PATH or alongside ld-analyse."));
        return;
    }
    const QString commandLine = formatCommand(exportPath, arguments);
    appendLog(tr("Command: %1").arg(commandLine));
    resetProcessStats();

    processStdout.clear();
    processStderr.clear();
    setBusy(true);
    exportProcess->setWorkingDirectory(QFileInfo(currentInputFile).absolutePath());
    {
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        QStringList prependDirs;
        const QString appDir = QCoreApplication::applicationDirPath();
        if (!appDir.isEmpty()) {
            prependDirs << appDir;
        }
        if (!currentInputFile.isEmpty()) {
            const QString inputDir = QFileInfo(currentInputFile).absolutePath();
            if (!inputDir.isEmpty()) {
                prependDirs << inputDir;
            }
        }
        const QString homeDir = QDir::homePath();
        if (!homeDir.isEmpty()) {
            prependDirs << QDir(homeDir).filePath(QStringLiteral(".local/bin"))
                        << QDir(homeDir).filePath(QStringLiteral("bin"));
        }

        QStringList pathEntries = env.value(QStringLiteral("PATH"))
                                      .split(QDir::listSeparator(), Qt::SkipEmptyParts);
        for (auto it = prependDirs.crbegin(); it != prependDirs.crend(); ++it) {
            if (!it->isEmpty() && !pathEntries.contains(*it)) {
                pathEntries.prepend(*it);
            }
        }
        if (!pathEntries.isEmpty()) {
            env.insert(QStringLiteral("PATH"), pathEntries.join(QDir::listSeparator()));
        }
        exportProcess->setProcessEnvironment(env);
    }
    QString programToRun = exportPath;
    QStringList argsToRun = arguments;
#if defined(Q_OS_UNIX)
    const QString scriptPath = QStandardPaths::findExecutable(QStringLiteral("script"));
    if (!scriptPath.isEmpty()) {
        const QString scriptCommand = formatShellCommand(exportPath, arguments);
        programToRun = scriptPath;
        argsToRun = {QStringLiteral("-q"), QStringLiteral("-e"), QStringLiteral("-c"),
                     scriptCommand, QStringLiteral("/dev/null")};
        appendLog(tr("Using script to enable ANSI progress output."));
    }
#endif
    exportProcess->start(programToRun, argsToRun);
    if (!exportProcess->waitForStarted(5000)) {
        appendStatus(tr("Failed to start tbc-video-export."));
        appendLog(tr("Failed to start tbc-video-export."));
        QMessageBox::warning(this, tr("Error"), tr("Failed to start tbc-video-export."));
        setBusy(false);
        return;
    }

    appendStatus(tr("Export running..."));
    appendLog(tr("Export running..."));
}
void ExportDialog::on_cancelButton_clicked()
{
    if (!exportProcess || exportProcess->state() == QProcess::NotRunning) {
        appendStatus(tr("No export running."));
        appendLog(tr("No export running."));
        return;
    }

    appendStatus(tr("Cancel requested..."));
    appendLog(tr("Cancel requested (sending Ctrl+C)."));

#if defined(Q_OS_UNIX)
    const qint64 pid = exportProcess->processId();
    if (pid > 0) {
        if (::kill(static_cast<pid_t>(pid), SIGINT) == 0) {
            appendLog(tr("Sent SIGINT to tbc-video-export."));
        } else {
            appendLog(tr("Failed to send SIGINT to tbc-video-export."));
        }
    } else {
        appendLog(tr("Export process ID unavailable; unable to send SIGINT."));
    }
#else
    exportProcess->terminate();
    appendLog(tr("Sent terminate request to tbc-video-export."));
#endif
}


void ExportDialog::handleProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    setBusy(false);
    const QString combinedOutput = processStdout + QStringLiteral("\n") + processStderr;
    const bool reportedFailure =
        combinedOutput.contains(QRegularExpression(QStringLiteral("\\bExport failed\\b"),
                                                   QRegularExpression::CaseInsensitiveOption))
        || combinedOutput.contains(QStringLiteral("InvalidOptsError"))
        || combinedOutput.contains(QStringLiteral("FileIOError"))
        || combinedOutput.contains(QStringLiteral("ProcessError"));

    if (exitStatus == QProcess::NormalExit && exitCode == 0 && !reportedFailure) {
        appendStatus(tr("Export complete."));
        appendLog(tr("Export complete."));
        return;
    }

    const QString errorText = processStderr.isEmpty() ? processStdout : processStderr;
    appendStatus(tr("Export failed."));
    appendLog(tr("Export failed."));
    QMessageBox::warning(this, tr("Export failed"),
                         errorText.isEmpty()
                             ? tr("tbc-video-export reported failure.")
                             : errorText);
    if (!errorText.trimmed().isEmpty()) {
        appendLog(errorText.trimmed());
    }
}

void ExportDialog::handleProcessError(QProcess::ProcessError)
{
    setBusy(false);
    const QString errorText = exportProcess ? exportProcess->errorString() : QString();
    appendStatus(errorText.isEmpty() ? tr("Export failed to start.") : errorText);
    appendLog(errorText.isEmpty() ? tr("Export failed to start.") : errorText);
    QMessageBox::warning(this, tr("Export failed"),
                         errorText.isEmpty() ? tr("tbc-video-export could not be started.") : errorText);
}

void ExportDialog::handleProcessStdout()
{
    const QString chunk = QString::fromLocal8Bit(exportProcess->readAllStandardOutput());
    processStdout += chunk;
    const QString cleaned = stripAnsiSequences(chunk);
    const QStringList lines = cleaned.split(QRegularExpression(QStringLiteral("[\\r\\n]+")), Qt::SkipEmptyParts);
    QString lastStatus;
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || isIgnorableLogLine(trimmed)) {
            continue;
        }
        ExportProcessStat stat;
        if (parseProgressLine(trimmed, &stat)) {
            updateProcessStat(stat);
            continue;
        }
        lastStatus = trimmed;
        appendLog(trimmed);
    }
    if (!lastStatus.isEmpty()) {
        appendStatus(lastStatus);
    }
}

void ExportDialog::handleProcessStderr()
{
    const QString chunk = QString::fromLocal8Bit(exportProcess->readAllStandardError());
    processStderr += chunk;
    const QString cleaned = stripAnsiSequences(chunk);
    const QStringList lines = cleaned.split(QRegularExpression(QStringLiteral("[\\r\\n]+")), Qt::SkipEmptyParts);
    QString lastStatus;
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || isIgnorableLogLine(trimmed)) {
            continue;
        }
        ExportProcessStat stat;
        if (parseProgressLine(trimmed, &stat)) {
            updateProcessStat(stat);
            continue;
        }
        lastStatus = trimmed;
        appendLog(trimmed);
    }
    if (!lastStatus.isEmpty()) {
        appendStatus(lastStatus);
    }
}
void ExportDialog::resetProcessStats()
{
    processRowMap.clear();
    if (ui->processStatsTable) {
        ui->processStatsTable->setRowCount(0);
    }
}

void ExportDialog::updateProcessStat(const ExportProcessStat &stat)
{
    if (!ui->processStatsTable) {
        return;
    }
    const QString key = stat.process + QStringLiteral("|") + stat.tbcType;
    int row = processRowMap.value(key, -1);
    if (row < 0) {
        row = ui->processStatsTable->rowCount();
        ui->processStatsTable->insertRow(row);
        processRowMap.insert(key, row);
    }

    setTableItem(ui->processStatsTable, row, 0, stat.process);
    setTableItem(ui->processStatsTable, row, 1, stat.tbcType);
    setTableItem(ui->processStatsTable, row, 2, stat.trackedName);
    setTableItem(ui->processStatsTable, row, 3, stat.current);
    setTableItem(ui->processStatsTable, row, 4, stat.total);
    setTableItem(ui->processStatsTable, row, 5, stat.errors);
    setTableItem(ui->processStatsTable, row, 6, stat.fps);
}

void ExportDialog::setBusy(bool busy)
{
    const bool enabled = exportAvailable && !busy;
    ui->exportButton->setEnabled(enabled);
    if (ui->cancelButton) {
        ui->cancelButton->setEnabled(busy);
    }
    ui->outputBrowseButton->setEnabled(enabled);
    ui->audio1BrowseButton->setEnabled(enabled);
    ui->audio2BrowseButton->setEnabled(enabled);
    ui->audio3BrowseButton->setEnabled(enabled);
    ui->audio4BrowseButton->setEnabled(enabled);
    ui->profileComboBox->setEnabled(enabled);
    if (ui->logProcessOutputCheckBox) {
        ui->logProcessOutputCheckBox->setEnabled(enabled);
    }
    ui->outputLineEdit->setEnabled(enabled);
    ui->audio1LineEdit->setEnabled(enabled);
    ui->audio2LineEdit->setEnabled(enabled);
    ui->audio3LineEdit->setEnabled(enabled);
    ui->audio4LineEdit->setEnabled(enabled);
}

QString ExportDialog::resolveVideoExportPath() const
{
    const QString fromPath = QStandardPaths::findExecutable(QStringLiteral("tbc-video-export"));
    if (!fromPath.isEmpty()) {
        return fromPath;
    }

    const QString appDir = QCoreApplication::applicationDirPath();
    QStringList candidateDirs;
    if (!appDir.isEmpty()) {
        candidateDirs << appDir;
    }

    if (!currentInputFile.isEmpty()) {
        const QString inputDir = QFileInfo(currentInputFile).absolutePath();
        if (!inputDir.isEmpty() && !candidateDirs.contains(inputDir)) {
            candidateDirs << inputDir;
        }
    }
    const QString homeDir = QDir::homePath();
    if (!homeDir.isEmpty()) {
        const QString localBin = QDir(homeDir).filePath(QStringLiteral(".local/bin"));
        const QString userBin = QDir(homeDir).filePath(QStringLiteral("bin"));
        if (!candidateDirs.contains(localBin)) {
            candidateDirs << localBin;
        }
        if (!candidateDirs.contains(userBin)) {
            candidateDirs << userBin;
        }
    }

    for (const QString &dir : candidateDirs) {
        const QString candidate = QDir(dir).filePath(QStringLiteral("tbc-video-export"));
        QFileInfo candidateInfo(candidate);
        if (candidateInfo.exists() && candidateInfo.isExecutable()) {
            return candidate;
        }
    }

    return QString();
}

QString ExportDialog::defaultOutputBaseName(const QString &inputFile) const
{
    QFileInfo info(inputFile);
    const QString baseName = info.completeBaseName();
    const QString dir = info.absolutePath();
    if (dir.isEmpty()) {
        return baseName;
    }
    return QDir(dir).filePath(baseName);
}

QString ExportDialog::sanitizeOutputBaseName(const QString &path) const
{
    QFileInfo info(path);
    const QString baseName = info.completeBaseName();
    const QString dir = info.absolutePath();
    if (dir.isEmpty()) {
        return baseName;
    }
    return QDir(dir).filePath(baseName);
}

QString ExportDialog::videoSystemArg(int system) const
{
    switch (static_cast<VideoSystem>(system)) {
    case PAL:
        return QStringLiteral("pal");
    case PAL_M:
        return QStringLiteral("pal-m");
    case NTSC:
    default:
        return QStringLiteral("ntsc");
    }
}

QStringList ExportDialog::collectAudioTracks() const
{
    QStringList tracks;
    const QStringList candidates = {
        ui->audio1LineEdit->text().trimmed(),
        ui->audio2LineEdit->text().trimmed(),
        ui->audio3LineEdit->text().trimmed(),
        ui->audio4LineEdit->text().trimmed()
    };
    for (const QString &track : candidates) {
        if (!track.isEmpty()) {
            tracks << track;
        }
    }
    return tracks;
}

QStringList ExportDialog::buildArguments(QString *errorMessage) const
{
    QStringList args;
    if (!tbcSource) {
        if (errorMessage) {
            *errorMessage = tr("No source loaded.");
        }
        return args;
    }

    const QString inputFile = tbcSource->getCurrentSourceFilename();
    if (inputFile.isEmpty()) {
        if (errorMessage) {
            *errorMessage = tr("No input file available.");
        }
        return args;
    }

    const QString outputBase = sanitizeOutputBaseName(ui->outputLineEdit->text().trimmed());
    if (outputBase.isEmpty()) {
        if (errorMessage) {
            *errorMessage = tr("Please provide an output base name.");
        }
        return args;
    }

    const QString profile = ui->profileComboBox->currentText().trimmed();
    if (!profile.isEmpty()) {
        args << QStringLiteral("--profile") << profile;
    }

    for (const QString &track : collectAudioTracks()) {
        args << QStringLiteral("--audio-track") << track;
    }
    if (ui->logProcessOutputCheckBox && ui->logProcessOutputCheckBox->isChecked()) {
        args << QStringLiteral("--log-process-output");
    }

    const LdDecodeMetaData::VideoParameters &videoParameters = tbcSource->getVideoParameters();
    if (videoParameters.isValid) {
        args << QStringLiteral("--video-system") << videoSystemArg(videoParameters.system);
    }

    if (!videoParameters.chromaDecoder.isEmpty()) {
        args << QStringLiteral("--chroma-decoder") << videoParameters.chromaDecoder;
    }
    if (videoParameters.chromaGain >= 0.0) {
        args << QStringLiteral("--chroma-gain") << QString::number(videoParameters.chromaGain, 'f', 6);
    }
    if (videoParameters.chromaPhase != -1.0) {
        args << QStringLiteral("--chroma-phase") << QString::number(videoParameters.chromaPhase, 'f', 3);
    }
    const bool isSplitSource = tbcSource && tbcSource->getSourceMode() != TbcSource::ONE_SOURCE;
    if (!isSplitSource && videoParameters.lumaNR >= 0.0) {
        args << QStringLiteral("--luma-nr") << QString::number(videoParameters.lumaNR, 'f', 3);
    }

    if (videoParameters.firstActiveFieldLine > 0) {
        args << QStringLiteral("--ffll") << QString::number(videoParameters.firstActiveFieldLine);
    }
    if (videoParameters.lastActiveFieldLine > 0) {
        args << QStringLiteral("--lfll") << QString::number(videoParameters.lastActiveFieldLine);
    }
    if (videoParameters.firstActiveFrameLine > 0) {
        args << QStringLiteral("--ffrl") << QString::number(videoParameters.firstActiveFrameLine);
    }
    if (videoParameters.lastActiveFrameLine > 0) {
        args << QStringLiteral("--lfrl") << QString::number(videoParameters.lastActiveFrameLine);
    }

    if (videoParameters.ntscPhaseCompensation >= 0) {
        args << (videoParameters.ntscPhaseCompensation ? QStringLiteral("--ntsc-phase-comp")
                                                       : QStringLiteral("--no-ntsc-phase-comp"));
    }

    if ((videoParameters.system == PAL || videoParameters.system == PAL_M)
        && videoParameters.palTransformThreshold >= 0.0) {
        args << QStringLiteral("--transform-threshold") << QString::number(videoParameters.palTransformThreshold, 'f', 3);
    }

    args << inputFile << outputBase;
    return args;
}

void ExportDialog::appendStatus(const QString &message)
{
    ui->statusLabel->setText(message);
}

void ExportDialog::appendLog(const QString &message)
{
    if (!ui || !ui->logTextEdit) {
        return;
    }
    const QString trimmed = message.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }
    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    const QString line = QStringLiteral("[%1] %2").arg(timestamp, trimmed);
    ui->logTextEdit->appendPlainText(line);
}

QString ExportDialog::formatCommand(const QString &program, const QStringList &args) const
{
    QStringList parts;
    parts << quoteArgument(program);
    for (const QString &arg : args) {
        parts << quoteArgument(arg);
    }
    return parts.join(' ');
}
