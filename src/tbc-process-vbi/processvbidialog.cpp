/******************************************************************************
 * processvbidialog.cpp
 * tbc-process-vbi - standalone VBI/VITS processing GUI
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Harry Munday
 *
 * This file is part of tbc-tools.
 ******************************************************************************/

#include "processvbidialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollBar>
#include <QSpinBox>
#include <QStandardPaths>
#include <QVBoxLayout>

namespace {
bool isRunnableFile(const QString &candidatePath)
{
    const QFileInfo candidateInfo(candidatePath);
#if defined(Q_OS_WIN)
    return candidateInfo.exists() && candidateInfo.isFile();
#else
    return candidateInfo.exists() && candidateInfo.isFile() && candidateInfo.isExecutable();
#endif
}

QString quoteForShell(const QString &arg)
{
    if (arg.isEmpty()) {
        return QStringLiteral("''");
    }
    QString escaped = arg;
    escaped.replace(QStringLiteral("'"), QStringLiteral("'\"'\"'"));
    return QStringLiteral("'%1'").arg(escaped);
}

QString chooseStartDirectory(const QString &preferredDirectory)
{
    const QString normalizedPreferred = QDir::cleanPath(preferredDirectory.trimmed());
    if (!normalizedPreferred.isEmpty() && QFileInfo::exists(normalizedPreferred)) {
        return normalizedPreferred;
    }
    return QDir::homePath();
}

QStringList tbcInputFilter()
{
    return {QStringLiteral("TBC files (*.tbc *.ytbc *.ctbc *.tbcy *.tbcc)"),
            QStringLiteral("Metadata (*.db *.json)"),
            QStringLiteral("All Files (*)")};
}
} // namespace

ProcessVbiDialog::ProcessVbiDialog(QWidget *parent) :
    QDialog(parent)
{
    buildUi();
    setBusy(false);
    appendStatus(tr("Ready."));
}

void ProcessVbiDialog::setSourceDirectory(const QString &directory)
{
    const QString normalizedDirectory = QDir::cleanPath(directory.trimmed());
    if (normalizedDirectory.isEmpty()) {
        return;
    }
    sourceDirectory = normalizedDirectory;
}

void ProcessVbiDialog::setDefaultInputTbc(const QString &tbcFilename)
{
    const QString normalizedInput = normalizePath(tbcFilename);
    if (normalizedInput.isEmpty()) {
        return;
    }
    const QFileInfo inputInfo(normalizedInput);
    if (inputInfo.exists() && inputInfo.isFile()) {
        inputTbcLineEdit->setText(inputInfo.absoluteFilePath());
        sourceDirectory = inputInfo.absolutePath();
        updateControlStates();
    } else {
        inputTbcLineEdit->setText(normalizedInput);
    }
}

void ProcessVbiDialog::setDefaultOutputMetadata(const QString &metadataFilename)
{
    const QString normalizedMetadata = normalizePath(metadataFilename);
    if (normalizedMetadata.isEmpty()) {
        return;
    }
    outputMetadataLineEdit->setText(normalizedMetadata);
}

void ProcessVbiDialog::buildUi()
{
    setWindowTitle(tr("Process VBI"));
    resize(760, 720);
    setMinimumSize(720, 660);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(8, 8, 8, 8);

    // --- I/O group ---
    QGroupBox *ioGroupBox = new QGroupBox(tr("Input / output"), this);
    QGridLayout *ioLayout = new QGridLayout(ioGroupBox);
    ioLayout->setHorizontalSpacing(6);
    ioLayout->setVerticalSpacing(6);

    QLabel *inputLabel = new QLabel(tr("Input TBC"), ioGroupBox);
    ioLayout->addWidget(inputLabel, 0, 0, 1, 1);
    inputTbcLineEdit = new QLineEdit(ioGroupBox);
    inputTbcLineEdit->setPlaceholderText(tr("Source .tbc file to process"));
    ioLayout->addWidget(inputTbcLineEdit, 0, 1, 1, 2);
    inputTbcBrowseButton = new QPushButton(tr("Browse..."), ioGroupBox);
    ioLayout->addWidget(inputTbcBrowseButton, 0, 3, 1, 1);

    QLabel *outputLabel = new QLabel(tr("Output metadata"), ioGroupBox);
    ioLayout->addWidget(outputLabel, 1, 0, 1, 1);
    outputMetadataLineEdit = new QLineEdit(ioGroupBox);
    outputMetadataLineEdit->setPlaceholderText(tr("Optional: defaults to the input's auto-detected sidecar (.tbc.db/.json)"));
    ioLayout->addWidget(outputMetadataLineEdit, 1, 1, 1, 2);
    outputMetadataBrowseButton = new QPushButton(tr("Browse..."), ioGroupBox);
    ioLayout->addWidget(outputMetadataBrowseButton, 1, 3, 1, 1);

    mainLayout->addWidget(ioGroupBox);

    // --- Processing options group ---
    QGroupBox *optionsGroupBox = new QGroupBox(tr("Processing options"), this);
    QVBoxLayout *optionsLayout = new QVBoxLayout(optionsGroupBox);

    vbiCoreCheckBox = new QCheckBox(tr("VBI core (frame number / timecode / chapter / user code)"), optionsGroupBox);
    vbiCoreCheckBox->setToolTip(tr("Biphase-coded VBI on field lines 16-18 (PAL) / equivalent NTSC lines."));
    vbiCoreCheckBox->setChecked(true);
    optionsLayout->addWidget(vbiCoreCheckBox);

    ntscCheckBox = new QCheckBox(tr("NTSC-specific (FM code / white flag / video ID)"), optionsGroupBox);
    ntscCheckBox->setToolTip(tr("NTSC LaserDisc-specific data on field lines 10/11/20. Only applies to NTSC sources."));
    ntscCheckBox->setChecked(true);
    optionsLayout->addWidget(ntscCheckBox);

    vitcCheckBox = new QCheckBox(tr("VITC (vertical interval timecode)"), optionsGroupBox);
    vitcCheckBox->setToolTip(tr("SMPTE VITC timecode scanned across its candidate field lines."));
    vitcCheckBox->setChecked(true);
    optionsLayout->addWidget(vitcCheckBox);

    closedCaptionsCheckBox = new QCheckBox(tr("Closed captions (CEA-608)"), optionsGroupBox);
    closedCaptionsCheckBox->setToolTip(tr("Line 21 (525-line) / line 22 (625-line) closed caption data."));
    closedCaptionsCheckBox->setChecked(true);
    optionsLayout->addWidget(closedCaptionsCheckBox);

    teletextCheckBox = new QCheckBox(tr("Teletext (HTML export)"), optionsGroupBox);
    teletextCheckBox->setToolTip(tr("Decode teletext and write generated HTML pages. Off by default - most tapes have no teletext. Failure is non-fatal."));
    optionsLayout->addWidget(teletextCheckBox);

    vitsCheckBox = new QCheckBox(tr("VITS metrics (SNR)"), optionsGroupBox);
    vitsCheckBox->setToolTip(tr("Run the VITS analyser in-process to (re)compute white/black SNR metrics. Off by default. Failure is non-fatal."));
    optionsLayout->addWidget(vitsCheckBox);

    // Teletext advanced options - enabled only when teletext is ticked.
    teletextGroupBox = new QGroupBox(tr("Teletext options"), optionsGroupBox);
    QFormLayout *teletextForm = new QFormLayout(teletextGroupBox);

    QHBoxLayout *htmlDirRow = new QHBoxLayout();
    teletextHtmlDirLineEdit = new QLineEdit(teletextGroupBox);
    teletextHtmlDirLineEdit->setPlaceholderText(tr("Default: <input>_teletext_html beside the TBC"));
    QPushButton *teletextBrowseButton = new QPushButton(tr("Browse..."), teletextGroupBox);
    htmlDirRow->addWidget(teletextHtmlDirLineEdit);
    htmlDirRow->addWidget(teletextBrowseButton);
    teletextForm->addRow(tr("HTML output directory:"), htmlDirRow);

    teletextTapeFormatLineEdit = new QLineEdit(teletextGroupBox);
    teletextTapeFormatLineEdit->setText(QStringLiteral("vhs"));
    teletextForm->addRow(tr("Tape format profile:"), teletextTapeFormatLineEdit);

    teletextMinDuplicatesSpinBox = new QSpinBox(teletextGroupBox);
    teletextMinDuplicatesSpinBox->setRange(1, 999);
    teletextMinDuplicatesSpinBox->setValue(1);
    teletextForm->addRow(tr("Minimum duplicates (squash):"), teletextMinDuplicatesSpinBox);

    optionsLayout->addWidget(teletextGroupBox);
    mainLayout->addWidget(optionsGroupBox);

    // --- Status / progress / log ---
    statusLabel = new QLabel(this);
    statusLabel->setWordWrap(true);
    mainLayout->addWidget(statusLabel);

    progressBar = new QProgressBar(this);
    progressBar->setRange(0, 1);
    progressBar->setValue(0);
    mainLayout->addWidget(progressBar);

    logTextEdit = new QPlainTextEdit(this);
    logTextEdit->setReadOnly(true);
    logTextEdit->setMinimumHeight(120);
    logTextEdit->setMaximumHeight(200);
    QFont logFont = logTextEdit->font();
    logFont.setStyleHint(QFont::TypeWriter);
    logTextEdit->setFont(logFont);
    mainLayout->addWidget(logTextEdit);

    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch(1);
    runButton = new QPushButton(tr("Run"), this);
    cancelButton = new QPushButton(tr("Cancel"), this);
    closeButton = new QPushButton(tr("Close"), this);
    buttonLayout->addWidget(runButton);
    buttonLayout->addWidget(cancelButton);
    buttonLayout->addWidget(closeButton);
    mainLayout->addLayout(buttonLayout);

    // --- Connections ---
    connect(inputTbcBrowseButton, &QPushButton::clicked, this, &ProcessVbiDialog::onBrowseInputTbcClicked);
    connect(outputMetadataBrowseButton, &QPushButton::clicked, this, &ProcessVbiDialog::onBrowseOutputMetadataClicked);
    connect(teletextBrowseButton, &QPushButton::clicked, this, &ProcessVbiDialog::onBrowseTeletextDirClicked);
    connect(teletextCheckBox, &QCheckBox::toggled, this, &ProcessVbiDialog::onTeletextToggled);
    connect(runButton, &QPushButton::clicked, this, &ProcessVbiDialog::onRunClicked);
    connect(cancelButton, &QPushButton::clicked, this, &ProcessVbiDialog::onCancelClicked);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::close);

    auto stateUpdater = [this]() { updateControlStates(); };
    connect(inputTbcLineEdit, &QLineEdit::textChanged, this, stateUpdater);
    connect(vitsCheckBox, &QCheckBox::toggled, this, stateUpdater);

    onTeletextToggled(teletextCheckBox->isChecked());
    updateControlStates();
}

void ProcessVbiDialog::setBusy(bool enabled)
{
    runInProgress = enabled;
    if (enabled) {
        cancelRequested = false;
    }

    inputTbcLineEdit->setEnabled(!enabled);
    inputTbcBrowseButton->setEnabled(!enabled);
    outputMetadataLineEdit->setEnabled(!enabled);
    outputMetadataBrowseButton->setEnabled(!enabled);
    vbiCoreCheckBox->setEnabled(!enabled);
    ntscCheckBox->setEnabled(!enabled);
    vitcCheckBox->setEnabled(!enabled);
    closedCaptionsCheckBox->setEnabled(!enabled);
    teletextCheckBox->setEnabled(!enabled);
    vitsCheckBox->setEnabled(!enabled);
    teletextGroupBox->setEnabled(!enabled && teletextCheckBox->isChecked());
    runButton->setEnabled(!enabled);
    closeButton->setEnabled(!enabled);
    cancelButton->setEnabled(enabled);

    if (!enabled) {
        updateControlStates();
    }
}

void ProcessVbiDialog::updateControlStates()
{
    if (runInProgress) {
        return;
    }

    const QFileInfo inputInfo(normalizePath(inputTbcLineEdit->text()));
    const bool hasInput = !inputInfo.fileName().isEmpty();
    runButton->setEnabled(hasInput);

    teletextGroupBox->setEnabled(teletextCheckBox->isChecked());
}

QString ProcessVbiDialog::normalizePath(const QString &path) const
{
    QString normalized = path.trimmed();
    normalized.replace(QLatin1Char('\\'), QLatin1Char('/'));
    if (normalized.isEmpty()) {
        return QString();
    }
    return QDir::cleanPath(normalized);
}

void ProcessVbiDialog::appendStatus(const QString &text)
{
    statusLabel->setText(text);
}

void ProcessVbiDialog::appendLog(const QString &text)
{
    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    logTextEdit->appendPlainText(QStringLiteral("[%1] %2").arg(timestamp, text));
    QScrollBar *scrollBar = logTextEdit->verticalScrollBar();
    if (scrollBar) {
        scrollBar->setValue(scrollBar->maximum());
    }
}

QString ProcessVbiDialog::formatCommand(const QString &program, const QStringList &arguments) const
{
    QStringList commandParts;
    commandParts << quoteForShell(program);
    for (const QString &argument : arguments) {
        commandParts << quoteForShell(argument);
    }
    return commandParts.join(QLatin1Char(' '));
}

QString ProcessVbiDialog::resolveLdProcessVbi() const
{
    if (!cachedToolPath.isEmpty() && isRunnableFile(cachedToolPath)) {
        return cachedToolPath;
    }

    const QString toolName = QStringLiteral("tbc-process-vbi");
    QStringList candidateNames = {toolName};
#if defined(Q_OS_WIN)
    candidateNames.append(toolName + QStringLiteral(".exe"));
#endif

    QStringList searchRoots;
    const QString appDir = QCoreApplication::applicationDirPath();
    const auto appendRoot = [&searchRoots](const QString &root) {
        const QString cleanRoot = QDir::cleanPath(root.trimmed());
        if (!cleanRoot.isEmpty() && !searchRoots.contains(cleanRoot)) {
            searchRoots.append(cleanRoot);
        }
    };
    appendRoot(appDir);
    appendRoot(QDir(appDir).filePath(QStringLiteral(".")));
    appendRoot(QDir(appDir).filePath(QStringLiteral("..")));
    appendRoot(QDir(appDir).filePath(QStringLiteral("../bin")));
    appendRoot(QDir(appDir).filePath(QStringLiteral("../../bin")));
    appendRoot(QDir(appDir).filePath(QStringLiteral("../../../bin")));
    appendRoot(QDir::currentPath());
    appendRoot(QDir(QDir::currentPath()).filePath(QStringLiteral("bin")));
    appendRoot(QDir(QDir::currentPath()).filePath(QStringLiteral("build/bin")));
    appendRoot(QDir(QDir::currentPath()).filePath(QStringLiteral("../build/bin")));

    for (const QString &root : searchRoots) {
        if (root.isEmpty()) {
            continue;
        }
        const QDir rootDir(root);
        for (const QString &candidateName : candidateNames) {
            const QString candidatePath = rootDir.filePath(candidateName);
            if (isRunnableFile(candidatePath)) {
                cachedToolPath = candidatePath;
                return cachedToolPath;
            }
        }
    }

    for (const QString &candidateName : candidateNames) {
        const QString fromPath = QStandardPaths::findExecutable(candidateName);
        if (!fromPath.isEmpty() && isRunnableFile(fromPath)) {
            cachedToolPath = fromPath;
            return cachedToolPath;
        }
    }

    cachedToolPath.clear();
    return QString();
}

bool ProcessVbiDialog::toolSupportsOption(const QString &toolPath, const QString &option) const
{
    if (toolPath.isEmpty() || option.isEmpty()) {
        return false;
    }

    // Reuse a cached --help probe for this tool path.
    if (cachedToolPath != toolPath || cachedHelpText.isEmpty()) {
        QProcess probe;
        probe.setProcessChannelMode(QProcess::MergedChannels);
        probe.start(toolPath, QStringList{QStringLiteral("--help")});
        if (!probe.waitForStarted(5000) || !probe.waitForFinished(10000)) {
            probe.kill();
            cachedHelpText.clear();
            cachedToolPath = toolPath;
            return false;
        }
        cachedHelpText = QString::fromLocal8Bit(probe.readAll());
        cachedToolPath = toolPath;
    }

    // Match the option token as a standalone word so "--no-vbi-core" doesn't
    // false-match "--no-vbi-core-something-else".
    const QStringList lines = cachedHelpText.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        if (line.contains(option, Qt::CaseSensitive)) {
            return true;
        }
    }
    return false;
}

QStringList ProcessVbiDialog::buildToolArguments(const QString &toolPath, const VbiProcessingOptions &opts) const
{
    QStringList args;

    if (!opts.vbiCore && toolSupportsOption(toolPath, QStringLiteral("--no-vbi-core"))) {
        args << QStringLiteral("--no-vbi-core");
    }
    if (!opts.ntsc && toolSupportsOption(toolPath, QStringLiteral("--no-ntsc"))) {
        args << QStringLiteral("--no-ntsc");
    }
    if (!opts.vitc && toolSupportsOption(toolPath, QStringLiteral("--no-vitc"))) {
        args << QStringLiteral("--no-vitc");
    }
    if (!opts.closedCaptions && toolSupportsOption(toolPath, QStringLiteral("--no-closed-captions"))) {
        args << QStringLiteral("--no-closed-captions");
    }

    const bool toolHasTeletextFlag = toolSupportsOption(toolPath, QStringLiteral("--teletext"));
    if (opts.teletext) {
        if (toolHasTeletextFlag) {
            args << QStringLiteral("--teletext");
        }
        if (!opts.teletextHtmlDir.isEmpty() && toolSupportsOption(toolPath, QStringLiteral("--teletext-html-dir"))) {
            args << QStringLiteral("--teletext-html-dir") << opts.teletextHtmlDir;
        }
        if (!opts.teletextTapeFormat.trimmed().isEmpty()
            && opts.teletextTapeFormat.trimmed().compare(QStringLiteral("vhs"), Qt::CaseInsensitive) != 0
            && toolSupportsOption(toolPath, QStringLiteral("--teletext-tape-format"))) {
            args << QStringLiteral("--teletext-tape-format") << opts.teletextTapeFormat.trimmed();
        }
        if (opts.teletextMinDuplicates > 1
            && toolSupportsOption(toolPath, QStringLiteral("--teletext-min-duplicates"))) {
            args << QStringLiteral("--teletext-min-duplicates") << QString::number(opts.teletextMinDuplicates);
        }
    } else if (!toolHasTeletextFlag && toolSupportsOption(toolPath, QStringLiteral("--no-teletext-html"))) {
        // Old CLI build where teletext defaults on: explicitly suppress it.
        args << QStringLiteral("--no-teletext-html");
    }

    if (opts.vits && toolSupportsOption(toolPath, QStringLiteral("--vits"))) {
        args << QStringLiteral("--vits");
    }

    return args;
}

bool ProcessVbiDialog::runProcessStep(const QString &program, const QStringList &arguments, QString *errorMessage)
{
    if (errorMessage) {
        errorMessage->clear();
    }

    appendStatus(tr("Running tbc-process-vbi..."));
    appendLog(QStringLiteral("$ %1").arg(formatCommand(program, arguments)));

    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(program, arguments);

    if (!process.waitForStarted(5000)) {
        if (errorMessage) {
            *errorMessage = tr("Unable to start tbc-process-vbi: %1").arg(program);
        }
        return false;
    }

    progressBar->setRange(0, 0); // indeterminate while the CLI runs

    QByteArray pendingOutputBuffer;
    QString lastOutputLine;
    bool terminateSent = false;
    QElapsedTimer cancelTimer;

    auto consumeOutputChunk = [&](const QByteArray &chunk) {
        if (chunk.isEmpty()) {
            return;
        }
        QByteArray normalized = chunk;
        normalized.replace('\r', '\n');
        pendingOutputBuffer.append(normalized);

        qsizetype lineBreakIndex = -1;
        while ((lineBreakIndex = pendingOutputBuffer.indexOf('\n')) >= 0) {
            QByteArray lineBytes = pendingOutputBuffer.left(lineBreakIndex);
            pendingOutputBuffer.remove(0, lineBreakIndex + 1);
            const QString lineText = QString::fromLocal8Bit(lineBytes).trimmed();
            if (lineText.isEmpty()) {
                continue;
            }
            lastOutputLine = lineText;
            appendLog(lineText);
        }
    };

    while (process.state() != QProcess::NotRunning) {
        if (cancelRequested) {
            if (!terminateSent) {
                process.terminate();
                terminateSent = true;
                cancelTimer.start();
            } else if (cancelTimer.isValid() && cancelTimer.elapsed() > 2000) {
                process.kill();
            }
        }
        process.waitForReadyRead(100);
        consumeOutputChunk(process.readAllStandardOutput());
        QCoreApplication::processEvents();
    }

    consumeOutputChunk(process.readAllStandardOutput());
    if (!pendingOutputBuffer.trimmed().isEmpty()) {
        const QString trailingLine = QString::fromLocal8Bit(pendingOutputBuffer).trimmed();
        if (!trailingLine.isEmpty()) {
            lastOutputLine = trailingLine;
            appendLog(trailingLine);
        }
    }

    progressBar->setRange(0, 1);
    progressBar->setValue(1);

    if (cancelRequested) {
        if (errorMessage) {
            *errorMessage = tr("Cancelled by user.");
        }
        return false;
    }

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (errorMessage) {
            *errorMessage = !lastOutputLine.isEmpty()
                                ? lastOutputLine
                                : tr("tbc-process-vbi failed with exit code %1.").arg(process.exitCode());
        }
        return false;
    }

    appendLog(tr("tbc-process-vbi completed."));
    return true;
}

void ProcessVbiDialog::onBrowseInputTbcClicked()
{
    const QString startPath = !normalizePath(inputTbcLineEdit->text()).isEmpty()
        ? normalizePath(inputTbcLineEdit->text())
        : chooseStartDirectory(sourceDirectory);
    const QString path = QFileDialog::getOpenFileName(this, tr("Select input TBC file"), startPath, tbcInputFilter().join(QStringLiteral(";;")));
    if (!path.isEmpty()) {
        inputTbcLineEdit->setText(path);
        const QFileInfo info(path);
        if (info.exists()) {
            sourceDirectory = info.absolutePath();
        }
        updateControlStates();
    }
}

void ProcessVbiDialog::onBrowseOutputMetadataClicked()
{
    const QString startPath = !normalizePath(outputMetadataLineEdit->text()).isEmpty()
        ? normalizePath(outputMetadataLineEdit->text())
        : chooseStartDirectory(sourceDirectory);
    const QString path = QFileDialog::getSaveFileName(this, tr("Select output metadata file"), startPath, QStringLiteral("Metadata (*.db *.json);;All Files (*)"));
    if (!path.isEmpty()) {
        outputMetadataLineEdit->setText(path);
    }
}

void ProcessVbiDialog::onBrowseTeletextDirClicked()
{
    const QString startDir = normalizePath(teletextHtmlDirLineEdit->text());
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Select teletext HTML output directory"),
                                                           startDir.isEmpty() ? QString() : startDir);
    if (!dir.isEmpty()) {
        teletextHtmlDirLineEdit->setText(dir);
    }
}

void ProcessVbiDialog::onTeletextToggled(bool checked)
{
    teletextGroupBox->setEnabled(checked);
}

void ProcessVbiDialog::onRunClicked()
{
    const QString inputTbc = QFileInfo(normalizePath(inputTbcLineEdit->text())).absoluteFilePath();
    if (inputTbc.isEmpty() || !QFileInfo::exists(inputTbc)) {
        QMessageBox::warning(this, tr("Input required"), tr("Please select a valid input TBC file."));
        return;
    }

    const QString toolPath = resolveLdProcessVbi();
    if (toolPath.isEmpty()) {
        QMessageBox::warning(this, tr("Tool not found"),
                             tr("tbc-process-vbi was not found alongside the application or in PATH."));
        return;
    }

    // Gather options from the UI.
    VbiProcessingOptions opts;
    opts.vbiCore = vbiCoreCheckBox->isChecked();
    opts.ntsc = ntscCheckBox->isChecked();
    opts.vitc = vitcCheckBox->isChecked();
    opts.closedCaptions = closedCaptionsCheckBox->isChecked();
    opts.teletext = teletextCheckBox->isChecked();
    opts.vits = vitsCheckBox->isChecked();
    opts.teletextHtmlDir = teletextHtmlDirLineEdit->text().trimmed();
    opts.teletextTapeFormat = teletextTapeFormatLineEdit->text().trimmed();
    opts.teletextMinDuplicates = teletextMinDuplicatesSpinBox->value();

    QStringList arguments;
    const QString outputMetadata = normalizePath(outputMetadataLineEdit->text());
    if (!outputMetadata.isEmpty() && toolSupportsOption(toolPath, QStringLiteral("--output-metadata"))) {
        arguments << QStringLiteral("--output-metadata") << outputMetadata;
    }
    arguments << buildToolArguments(toolPath, opts);
    arguments << inputTbc;

    setBusy(true);
    QString errorMessage;
    const bool ok = runProcessStep(toolPath, arguments, &errorMessage);
    setBusy(false);

    if (!ok) {
        appendStatus(tr("Failed: %1").arg(errorMessage));
        if (!errorMessage.contains(tr("Cancelled by user."), Qt::CaseInsensitive)) {
            QMessageBox::warning(this, tr("Process failed"),
                                 errorMessage.isEmpty() ? tr("tbc-process-vbi failed.") : errorMessage);
        }
    } else {
        appendStatus(tr("VBI processing completed for %1").arg(inputTbc));
    }
}

void ProcessVbiDialog::onCancelClicked()
{
    cancelRequested = true;
}
