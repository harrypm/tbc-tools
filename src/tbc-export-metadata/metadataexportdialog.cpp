/******************************************************************************
 * metadataexportdialog.cpp
 * tbc-export-metadata - Export ld-decode metadata into other formats
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 *
 * This file is part of tbc-tools.
 ******************************************************************************/

#include "metadataexportdialog.h"
#include "ui_metadataexportdialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDir>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHash>
#include <QLineEdit>
#include <QMessageBox>
#include <QMimeData>
#include <QProcess>
#include <QSignalBlocker>
#include <QUrl>

namespace {
QWidget *dialogParentWidget(QWidget *widget)
{
    if (!widget) {
        return nullptr;
    }
    QWidget *window = widget->window();
    return window ? window : widget;
}

QStringList dialogNameFilters(const QString &filters)
{
    return filters.split(QStringLiteral(";;"), Qt::SkipEmptyParts);
}

void applyCommonFileDialogOptions(QFileDialog *dialog)
{
    if (!dialog) {
        return;
    }
    dialog->setOption(QFileDialog::DontResolveSymlinks, true);
#if defined(Q_OS_MACOS)
    dialog->setOption(QFileDialog::DontUseNativeDialog, true);
#endif
}

bool parsePositiveInteger(const QString &text, qint32 *value)
{
    bool ok = false;
    const qint32 parsedValue = text.toInt(&ok);
    if (!ok || parsedValue < 1) {
        return false;
    }
    if (value) {
        *value = parsedValue;
    }
    return true;
}

class WaitCursorGuard
{
public:
    WaitCursorGuard() { QApplication::setOverrideCursor(Qt::WaitCursor); }
    ~WaitCursorGuard() { QApplication::restoreOverrideCursor(); }
};

QString helpOutputForExecutable(const QString &executablePath)
{
    static QHash<QString, QString> helpOutputCache;
    const QString cacheKey = QDir::cleanPath(executablePath);
    if (helpOutputCache.contains(cacheKey)) {
        return helpOutputCache.value(cacheKey);
    }

    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(executablePath, {QStringLiteral("--help")});
    QString helpOutput;
    if (process.waitForStarted(2000)) {
        if (!process.waitForFinished(6000)) {
            process.kill();
            process.waitForFinished(1000);
        }
        helpOutput = QString::fromLocal8Bit(process.readAllStandardOutput());
    }

    helpOutputCache.insert(cacheKey, helpOutput);
    return helpOutput;
}
} // namespace

MetadataExportDialog::MetadataExportDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::MetadataExportDialog)
{
    ui->setupUi(this);
    exportExecutablePath = QCoreApplication::applicationFilePath();
    setAcceptDrops(true);

    if (ui->debugCheckBox && ui->quietCheckBox) {
        connect(ui->debugCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
            if (!checked || !ui->quietCheckBox || !ui->quietCheckBox->isChecked()) {
                return;
            }
            const QSignalBlocker blocker(ui->quietCheckBox);
            ui->quietCheckBox->setChecked(false);
        });
        connect(ui->quietCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
            if (!checked || !ui->debugCheckBox || !ui->debugCheckBox->isChecked()) {
                return;
            }
            const QSignalBlocker blocker(ui->debugCheckBox);
            ui->debugCheckBox->setChecked(false);
        });
    }

    const auto connectStatusClear = [this](QLineEdit *lineEdit) {
        if (!lineEdit) {
            return;
        }
        connect(lineEdit, &QLineEdit::textEdited, this, [this]() {
            if (ui->statusLabel) {
                ui->statusLabel->clear();
            }
        });
    };
    connectStatusClear(ui->inputLineEdit);
    connectStatusClear(ui->ffmetadataStartLineEdit);
    connectStatusClear(ui->ffmetadataLengthLineEdit);

    const auto connectStatusClearCheckBox = [this](QCheckBox *checkBox) {
        if (!checkBox) {
            return;
        }
        connect(checkBox, &QCheckBox::toggled, this, [this]() {
            if (ui->statusLabel) {
                ui->statusLabel->clear();
            }
        });
    };
    connectStatusClearCheckBox(ui->vitsCsvCheckBox);
    connectStatusClearCheckBox(ui->vbiCsvCheckBox);
    connectStatusClearCheckBox(ui->userMarkersTxtCheckBox);
    connectStatusClearCheckBox(ui->userMarkersCsvCheckBox);
    connectStatusClearCheckBox(ui->audacityLabelsCheckBox);
    connectStatusClearCheckBox(ui->ffmetadataCheckBox);
    connectStatusClearCheckBox(ui->ffmpegVitcCheckBox);
    connectStatusClearCheckBox(ui->ffmetadataVitcTimecodeCheckBox);
    connectStatusClearCheckBox(ui->closedCaptionsCheckBox);

    if (ui->ffmetadataCheckBox) {
        connect(ui->ffmetadataCheckBox, &QCheckBox::toggled, this, [this]() {
            updateFfmetadataControlsEnabled();
        });
    }
    updateOptionCompatibilityState();
    updateFfmetadataControlsEnabled();
}

MetadataExportDialog::~MetadataExportDialog()
{
    delete ui;
}

void MetadataExportDialog::setSourceDirectory(const QString &directory)
{
    if (!directory.isEmpty()) {
        sourceDirectory = directory;
    }
}
void MetadataExportDialog::setDefaultInputFile(const QString &inputFile)
{
    const QString normalizedInput = normalizedPath(inputFile);
    if (normalizedInput.isEmpty()) {
        return;
    }
    ui->inputLineEdit->setText(normalizedInput);
    if (ui->statusLabel) {
        ui->statusLabel->clear();
    }
    const QFileInfo inputInfo(normalizedInput);
    if (inputInfo.exists() && inputInfo.isFile()) {
        sourceDirectory = inputInfo.absolutePath();
    }
}

void MetadataExportDialog::setExportExecutablePath(const QString &executablePath)
{
    const QString trimmedPath = executablePath.trimmed();
    if (!trimmedPath.isEmpty()) {
        exportExecutablePath = trimmedPath;
    }
    updateOptionCompatibilityState();
}

void MetadataExportDialog::setInitialOptions(const InitialOptions &options)
{
    ui->inputLineEdit->setText(normalizedPath(options.inputFile));
    ui->vitsCsvCheckBox->setChecked(options.exportVitsCsv);
    ui->vbiCsvCheckBox->setChecked(options.exportVbiCsv);
    ui->userMarkersTxtCheckBox->setChecked(options.exportUserMarkersTxt);
    ui->userMarkersCsvCheckBox->setChecked(options.exportUserMarkersCsv);
    ui->audacityLabelsCheckBox->setChecked(options.exportAudacityLabels);
    ui->ffmetadataCheckBox->setChecked(options.exportFfmetadata);
    ui->ffmpegVitcCheckBox->setChecked(options.exportFfmpegVitc);
    ui->ffmetadataVitcTimecodeCheckBox->setChecked(options.exportFfmetadataVitcTimecode);
    ui->closedCaptionsCheckBox->setChecked(options.exportClosedCaptions);
    ui->ffmetadataStartLineEdit->setText(options.ffmetadataStart > 0 ? QString::number(options.ffmetadataStart) : QString());
    ui->ffmetadataLengthLineEdit->setText(options.ffmetadataLength > 0 ? QString::number(options.ffmetadataLength) : QString());
    ui->debugCheckBox->setChecked(options.debug);
    ui->quietCheckBox->setChecked(options.quiet);
    updateOptionCompatibilityState();
    updateFfmetadataControlsEnabled();

    const QFileInfo inputInfo(ui->inputLineEdit->text());
    if (inputInfo.exists() && inputInfo.isFile()) {
        sourceDirectory = inputInfo.absolutePath();
    }
}

void MetadataExportDialog::dragEnterEvent(QDragEnterEvent *event)
{
    if (!event || !event->mimeData() || !event->mimeData()->hasUrls()) {
        if (event) {
            event->ignore();
        }
        return;
    }
    const QList<QUrl> urls = event->mimeData()->urls();
    if (urls.isEmpty()) {
        event->ignore();
        return;
    }
    const QString localPath = urls.constFirst().toLocalFile();
    if (!isSupportedInputPath(localPath)) {
        event->ignore();
        return;
    }
    event->acceptProposedAction();
}

void MetadataExportDialog::dragMoveEvent(QDragMoveEvent *event)
{
    if (!event || !event->mimeData() || !event->mimeData()->hasUrls()) {
        if (event) {
            event->ignore();
        }
        return;
    }
    const QList<QUrl> urls = event->mimeData()->urls();
    if (urls.isEmpty()) {
        event->ignore();
        return;
    }
    const QString localPath = urls.constFirst().toLocalFile();
    if (!isSupportedInputPath(localPath)) {
        event->ignore();
        return;
    }
    event->acceptProposedAction();
}

void MetadataExportDialog::dropEvent(QDropEvent *event)
{
    if (!event || !event->mimeData() || !event->mimeData()->hasUrls()) {
        if (event) {
            event->ignore();
        }
        return;
    }
    const QList<QUrl> urls = event->mimeData()->urls();
    if (urls.isEmpty()) {
        event->ignore();
        return;
    }

    const QString localPath = urls.constFirst().toLocalFile();
    if (!isSupportedInputPath(localPath)) {
        if (ui && ui->statusLabel) {
            ui->statusLabel->setText(tr("Drop a valid metadata file (.db or .json)."));
        }
        event->ignore();
        return;
    }

    const QString normalizedInput = normalizedPath(localPath);
    ui->inputLineEdit->setText(normalizedInput);
    if (ui->statusLabel) {
        ui->statusLabel->clear();
    }
    const QFileInfo selectedInfo(normalizedInput);
    if (selectedInfo.exists() && selectedInfo.isFile()) {
        sourceDirectory = selectedInfo.absolutePath();
    }
    event->acceptProposedAction();
}

void MetadataExportDialog::on_inputBrowseButton_clicked()
{
    const QString filter = tr("Metadata input (*.db *.json);;SQLite metadata (*.db);;JSON metadata (*.json);;All Files (*)");
    const QString startPath = ui->inputLineEdit->text().trimmed().isEmpty()
                                  ? sourceDirectory
                                  : ui->inputLineEdit->text().trimmed();
    QFileDialog dialog(dialogParentWidget(this), tr("Select metadata input"), startPath);
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    dialog.setFileMode(QFileDialog::ExistingFile);
    dialog.setNameFilters(dialogNameFilters(filter));
    applyCommonFileDialogOptions(&dialog);
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty()) {
        return;
    }

    const QString normalizedInput = normalizedPath(dialog.selectedFiles().constFirst());
    ui->inputLineEdit->setText(normalizedInput);
    if (ui->statusLabel) {
        ui->statusLabel->clear();
    }
    const QFileInfo selectedInfo(normalizedInput);
    if (selectedInfo.exists() && selectedInfo.isFile()) {
        sourceDirectory = selectedInfo.absolutePath();
    }
}


void MetadataExportDialog::on_exportButton_clicked()
{
    const QString inputFile = normalizedPath(ui->inputLineEdit->text());
    const QString startText = ui->ffmetadataStartLineEdit->text().trimmed();
    const QString lengthText = ui->ffmetadataLengthLineEdit->text().trimmed();

    {
        const QSignalBlocker inputBlocker(ui->inputLineEdit);
        ui->inputLineEdit->setText(inputFile);
    }

    if (inputFile.isEmpty()) {
        ui->statusLabel->setText(tr("Please choose an input metadata file."));
        return;
    }
    if (!QFileInfo::exists(inputFile)) {
        ui->statusLabel->setText(tr("Input metadata file does not exist."));
        return;
    }
    if (!isSupportedInputPath(inputFile)) {
        ui->statusLabel->setText(tr("Input must be a metadata file with .db or .json extension."));
        return;
    }

    QStringList arguments;
    if (ui->debugCheckBox && ui->debugCheckBox->isChecked()) {
        arguments << QStringLiteral("--debug");
    } else if (ui->quietCheckBox && ui->quietCheckBox->isChecked()) {
        arguments << QStringLiteral("--quiet");
    }

    QStringList outputFiles;
    const auto addOutput = [this, &arguments, &outputFiles, &inputFile](QCheckBox *checkBox,
                                                                         const QString &optionName,
                                                                         const QString &suffix) -> bool {
        if (!checkBox || !checkBox->isChecked()) {
            return true;
        }
        const QString outputFile = defaultOutputPath(inputFile, suffix);
        if (outputFile.isEmpty()) {
            ui->statusLabel->setText(tr("Unable to generate output filename for %1.")
                                         .arg(checkBox->text()));
            return false;
        }
        arguments << optionName << outputFile;
        outputFiles << outputFile;
        return true;
    };

    if (!addOutput(ui->vitsCsvCheckBox, QStringLiteral("--vits-csv"), QStringLiteral("_vits.csv"))
        || !addOutput(ui->vbiCsvCheckBox, QStringLiteral("--vbi-csv"), QStringLiteral("_vbi.csv"))
        || !addOutput(ui->userMarkersTxtCheckBox, QStringLiteral("--user-markers-txt"), QStringLiteral("_user-markers-log.txt"))
        || !addOutput(ui->userMarkersCsvCheckBox, QStringLiteral("--user-markers-csv"), QStringLiteral("_user-markers-log.csv"))
        || !addOutput(ui->audacityLabelsCheckBox, QStringLiteral("--audacity-labels"), QStringLiteral("_audacity-labels.txt"))
        || !addOutput(ui->ffmetadataCheckBox, QStringLiteral("--ffmetadata"), QStringLiteral("_ffmetadata.txt"))
        || !addOutput(ui->ffmpegVitcCheckBox, QStringLiteral("--ffmpeg-vitc"), QStringLiteral("_FFmpeg_VITC.txt"))
        || !addOutput(ui->closedCaptionsCheckBox, QStringLiteral("--closed-captions"), QStringLiteral("_closed-captions.scc"))) {
        return;
    }

    if (outputFiles.isEmpty()) {
        ui->statusLabel->setText(tr("Select at least one output format."));
        return;
    }

    const bool ffmetadataSelected = ui->ffmetadataCheckBox && ui->ffmetadataCheckBox->isChecked();
    if (ffmetadataSelected) {
        if (ui->ffmetadataVitcTimecodeCheckBox && !ui->ffmetadataVitcTimecodeCheckBox->isChecked()) {
            arguments << QStringLiteral("--ffmetadata-no-vitc-timecode");
        }
        if (!startText.isEmpty()) {
            qint32 startValue = -1;
            if (!parsePositiveInteger(startText, &startValue)) {
                ui->statusLabel->setText(tr("FFMETADATA start frame must be a positive integer."));
                return;
            }
            arguments << QStringLiteral("--start") << QString::number(startValue);
        }
        if (!lengthText.isEmpty()) {
            qint32 lengthValue = -1;
            if (!parsePositiveInteger(lengthText, &lengthValue)) {
                ui->statusLabel->setText(tr("FFMETADATA length must be a positive integer."));
                return;
            }
            arguments << QStringLiteral("--length") << QString::number(lengthValue);
        }
    }

    QStringList existingOutputs;
    for (const QString &outputFile : outputFiles) {
        if (QFileInfo::exists(outputFile)) {
            existingOutputs << outputFile;
        }
    }
    if (!existingOutputs.isEmpty()) {
        const QMessageBox::StandardButton overwriteChoice = QMessageBox::question(
            this,
            tr("Overwrite output files?"),
            tr("The following output files already exist:\n%1\n\nOverwrite them?")
                .arg(existingOutputs.join(QLatin1Char('\n'))),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        if (overwriteChoice != QMessageBox::Yes) {
            ui->statusLabel->setText(tr("Export cancelled."));
            return;
        }
    }

    arguments << inputFile;

    WaitCursorGuard waitCursor;
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    const QString processProgram = exportExecutablePath.isEmpty()
                                       ? QCoreApplication::applicationFilePath()
                                       : exportExecutablePath;
    process.start(processProgram, arguments);
    if (!process.waitForStarted(5000)) {
        ui->statusLabel->setText(tr("Failed to start export process."));
        QMessageBox::warning(this, tr("Export failed"),
                             tr("Unable to start tbc-export-metadata process."));
        return;
    }
    if (!process.waitForFinished(-1)) {
        process.kill();
        process.waitForFinished(1000);
        ui->statusLabel->setText(tr("Export process timed out."));
        QMessageBox::warning(this, tr("Export failed"),
                             tr("tbc-export-metadata did not finish."));
        return;
    }

    const QString processOutput = QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed();
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        ui->statusLabel->setText(tr("Export failed."));
        QMessageBox::warning(this, tr("Export failed"),
                             processOutput.isEmpty()
                                 ? tr("tbc-export-metadata failed with exit code %1.").arg(process.exitCode())
                                 : processOutput);
        return;
    }

    ui->statusLabel->setText(tr("Export complete."));
    const QFileInfo inputInfo(inputFile);
    if (inputInfo.exists() && inputInfo.isFile()) {
        sourceDirectory = inputInfo.absolutePath();
    }
    QMessageBox::information(this,
                             tr("Export complete"),
                             tr("Metadata export completed successfully."));
}

QString MetadataExportDialog::normalizedPath(const QString &path) const
{
    QString normalized = path.trimmed();
    if (normalized.isEmpty()) {
        return QString();
    }
    normalized.replace(QLatin1Char('\\'), QLatin1Char('/'));
    normalized = QDir::cleanPath(normalized);
    if (normalized == QLatin1String(".")) {
        return QString();
    }
    return QDir::toNativeSeparators(normalized);
}

QString MetadataExportDialog::defaultOutputPath(const QString &inputPath, const QString &suffix) const
{
    const QString normalizedInput = normalizedPath(inputPath);
    if (normalizedInput.isEmpty()) {
        return QString();
    }
    QFileInfo inputInfo(normalizedInput);
    QString baseName = inputBaseName(normalizedInput);
    if (baseName.isEmpty()) {
        baseName = inputInfo.completeBaseName();
    }
    if (baseName.isEmpty()) {
        baseName = inputInfo.fileName();
    }
    QString directory = inputInfo.absolutePath();
    if (directory.isEmpty() || directory == QLatin1String(".")) {
        directory = sourceDirectory;
    }
    if (directory.isEmpty()) {
        return QDir::toNativeSeparators(baseName + suffix);
    }
    return QDir::toNativeSeparators(QDir(directory).filePath(baseName + suffix));
}

QString MetadataExportDialog::inputBaseName(const QString &inputPath) const
{
    QString fileName = QFileInfo(inputPath).fileName();
    if (fileName.isEmpty()) {
        return QString();
    }

    QString lowerFileName = fileName.toLower();
    const QStringList trailingSuffixes{
        QStringLiteral(".new"),
        QStringLiteral(".bup"),
        QStringLiteral(".bak"),
        QStringLiteral(".tmp")
    };
    for (const QString &suffix : trailingSuffixes) {
        if (!lowerFileName.endsWith(suffix)) {
            continue;
        }
        fileName.chop(suffix.size());
        lowerFileName.chop(suffix.size());
        break;
    }
    if (lowerFileName.endsWith(QStringLiteral(".json"))) {
        fileName.chop(5);
        return fileName;
    }
    if (lowerFileName.endsWith(QStringLiteral(".db"))) {
        fileName.chop(3);
        return fileName;
    }
    return QFileInfo(inputPath).completeBaseName();
}

bool MetadataExportDialog::isJsonPath(const QString &path) const
{
    const QStringList parts = QFileInfo(path)
                                  .fileName()
                                  .toLower()
                                  .split(QLatin1Char('.'), Qt::SkipEmptyParts);
    if (parts.isEmpty()) {
        return false;
    }
    const QString &last = parts.last();
    if (last == QLatin1String("json")) {
        return true;
    }
    if ((last == QLatin1String("new")
         || last == QLatin1String("bup")
         || last == QLatin1String("bak")
         || last == QLatin1String("tmp"))
        && parts.size() >= 2) {
        return parts.at(parts.size() - 2) == QLatin1String("json");
    }
    return false;
}

bool MetadataExportDialog::isSupportedInputPath(const QString &path) const
{
    if (path.isEmpty()) {
        return false;
    }
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile()) {
        return false;
    }
    if (info.suffix().compare(QStringLiteral("db"), Qt::CaseInsensitive) == 0) {
        return true;
    }
    return isJsonPath(path);
}

bool MetadataExportDialog::exportToolSupportsOption(const QString &optionName) const
{
    if (optionName.trimmed().isEmpty()) {
        return false;
    }
    const QString executablePath = exportExecutablePath.isEmpty()
                                       ? QCoreApplication::applicationFilePath()
                                       : exportExecutablePath;
    if (executablePath.isEmpty()) {
        return false;
    }
    const QString helpOutput = helpOutputForExecutable(executablePath);
    if (helpOutput.isEmpty()) {
        return false;
    }
    return helpOutput.contains(optionName);
}

void MetadataExportDialog::updateOptionCompatibilityState()
{
    const bool supportsUserMarkersTxt = exportToolSupportsOption(QStringLiteral("--user-markers-txt"));
    const bool supportsUserMarkersCsv = exportToolSupportsOption(QStringLiteral("--user-markers-csv"));

    const auto applyCompatibility = [this](QCheckBox *checkBox,
                                           QLabel *label,
                                           bool supported,
                                           const QString &optionName) {
        if (label) {
            label->setEnabled(supported);
        }
        if (!checkBox) {
            return;
        }

        if (!supported) {
            if (checkBox->isChecked()) {
                const QSignalBlocker blocker(checkBox);
                checkBox->setChecked(false);
            }
            checkBox->setEnabled(false);
            checkBox->setToolTip(tr("Unavailable: selected export tool does not support %1.")
                                     .arg(optionName));
        } else {
            checkBox->setEnabled(true);
            checkBox->setToolTip(QString());
        }
    };

    applyCompatibility(ui->userMarkersTxtCheckBox,
                       ui->userMarkersTxtLabel,
                       supportsUserMarkersTxt,
                       QStringLiteral("--user-markers-txt"));
    applyCompatibility(ui->userMarkersCsvCheckBox,
                       ui->userMarkersCsvLabel,
                       supportsUserMarkersCsv,
                       QStringLiteral("--user-markers-csv"));
}

void MetadataExportDialog::updateFfmetadataControlsEnabled()
{
    const bool enabled = ui->ffmetadataCheckBox && ui->ffmetadataCheckBox->isChecked();
    if (ui->ffmetadataRangeLabel) {
        ui->ffmetadataRangeLabel->setEnabled(enabled);
    }
    if (ui->ffmetadataVitcTimecodeLabel) {
        ui->ffmetadataVitcTimecodeLabel->setEnabled(enabled);
    }
    if (ui->ffmetadataVitcTimecodeCheckBox) {
        ui->ffmetadataVitcTimecodeCheckBox->setEnabled(enabled);
    }
    if (ui->ffmetadataStartLineEdit) {
        ui->ffmetadataStartLineEdit->setEnabled(enabled);
    }
    if (ui->ffmetadataLengthLineEdit) {
        ui->ffmetadataLengthLineEdit->setEnabled(enabled);
    }
}
