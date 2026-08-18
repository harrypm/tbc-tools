/******************************************************************************
 * audioalignmentdialog.cpp
 * tbc-audio-align - Audio alignment GUI for ld-decode
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 *
 * This file is part of tbc-tools.
 ******************************************************************************/

#include "audioalignmentdialog.h"
#include "ui_audioalignmentdialog.h"

#include "audioalignmentutil.h"

#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QMetaObject>
#include <QMessageBox>
#include <QPointer>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QThread>

AudioAlignmentDialog::AudioAlignmentDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::AudioAlignmentDialog)
{
    ui->setupUi(this);
    setAttribute(Qt::WA_TranslucentBackground, false);
    setAttribute(Qt::WA_NoSystemBackground, false);
    setAutoFillBackground(true);
    setWindowOpacity(1.0);

    const QString rfTimebaseWarning = tr("RF Video Sample Rate must match the decoder timebase used to generate metadata JSON: use 20000000 for 20 Msps no-resampling captures, 16000000 for 16 Msps captures, or 40000000 when the decoder used internal resampling to 40 Msps.");
    if (ui->rfVideoRatePresetComboBox) {
        ui->rfVideoRatePresetComboBox->setCurrentIndex(0);
        ui->rfVideoRatePresetComboBox->setToolTip(rfTimebaseWarning);
        connect(ui->rfVideoRatePresetComboBox,
                QOverload<int>::of(&QComboBox::currentIndexChanged),
                this,
                [this](int presetIndex) {
            const bool customSelected = (presetIndex == 3);
            if (ui->rfVideoSampleRateCustomSpinBox) {
                ui->rfVideoSampleRateCustomSpinBox->setEnabled(customSelected);
                if (!customSelected) {
                    if (presetIndex == 1) {
                        ui->rfVideoSampleRateCustomSpinBox->setValue(20000000);
                    } else if (presetIndex == 2) {
                        ui->rfVideoSampleRateCustomSpinBox->setValue(16000000);
                    } else {
                        ui->rfVideoSampleRateCustomSpinBox->setValue(40000000);
                    }
                }
            }
        });
    }
    if (ui->rfVideoSampleRateCustomSpinBox) {
        ui->rfVideoSampleRateCustomSpinBox->setValue(40000000);
        ui->rfVideoSampleRateCustomSpinBox->setEnabled(false);
        ui->rfVideoSampleRateCustomSpinBox->setToolTip(rfTimebaseWarning);
    }
    if (ui->rfVideoRatePresetLabel) {
        ui->rfVideoRatePresetLabel->setToolTip(rfTimebaseWarning);
    }
    if (ui->rfRateWarningLabel) {
        ui->rfRateWarningLabel->setToolTip(rfTimebaseWarning);
    }

    if (ui->overwriteCheckBox) {
        ui->overwriteCheckBox->setChecked(true);
    }
    const QString loadTracksForExportTooltip = tr("When launched from ld-analyse, load aligned track files and names into Export audio tracks.");
    if (ui->loadTracksForExportCheckBox) {
        ui->loadTracksForExportCheckBox->setToolTip(loadTracksForExportTooltip);
    }
    if (ui->loadTracksForExportLabel) {
        ui->loadTracksForExportLabel->setToolTip(loadTracksForExportTooltip);
    }
    if (ui->cancelButton) {
        ui->cancelButton->setToolTip(tr("Force stop the currently running alignment process."));
    }

    if (ui->statusLabel) {
        ui->statusLabel->setText(tr("Using internal AAA workflow (ffprobe + ffmpeg + VhsDecodeAutoAudioAlign)."));
    }

    if (ui->linearInputLineEdit) {
        connect(ui->linearInputLineEdit, &QLineEdit::textChanged, this, [this]() {
            updateLinearOutputFromInput(false);
            if (ui->statusLabel) {
                ui->statusLabel->clear();
            }
        });
    }
    if (ui->hifiInputLineEdit) {
        connect(ui->hifiInputLineEdit, &QLineEdit::textChanged, this, [this]() {
            updateHifiOutputFromInput(false);
            if (ui->statusLabel) {
                ui->statusLabel->clear();
            }
        });
    }
    if (ui->linearOutputLineEdit) {
        connect(ui->linearOutputLineEdit, &QLineEdit::textEdited, this, [this]() {
            userEditedLinearOutput = true;
            if (ui->statusLabel) {
                ui->statusLabel->clear();
            }
        });
    }
    if (ui->hifiOutputLineEdit) {
        connect(ui->hifiOutputLineEdit, &QLineEdit::textEdited, this, [this]() {
            userEditedHifiOutput = true;
            if (ui->statusLabel) {
                ui->statusLabel->clear();
            }
        });
    }
    if (ui->jsonLineEdit) {
        connect(ui->jsonLineEdit, &QLineEdit::textChanged, this, [this]() {
            tryAutoDetectInputsFromJson(false);
            if (ui->statusLabel) {
                ui->statusLabel->clear();
            }
        });
    }
}

void AudioAlignmentDialog::setAlignmentUiBusy(bool busy)
{
    alignmentInProgress = busy;
    const bool enabled = !busy;
    auto setWidgetEnabled = [enabled](QWidget *widget) {
        if (widget) {
            widget->setEnabled(enabled);
        }
    };

    setWidgetEnabled(ui->jsonLineEdit);
    setWidgetEnabled(ui->jsonBrowseButton);
    setWidgetEnabled(ui->linearInputLineEdit);
    setWidgetEnabled(ui->linearInputBrowseButton);
    setWidgetEnabled(ui->linearOutputLineEdit);
    setWidgetEnabled(ui->linearOutputBrowseButton);
    setWidgetEnabled(ui->hifiInputLineEdit);
    setWidgetEnabled(ui->hifiInputBrowseButton);
    setWidgetEnabled(ui->hifiOutputLineEdit);
    setWidgetEnabled(ui->hifiOutputBrowseButton);
    setWidgetEnabled(ui->rfVideoRatePresetComboBox);
    setWidgetEnabled(ui->rfVideoSampleRateCustomSpinBox);
    setWidgetEnabled(ui->overwriteCheckBox);
    setWidgetEnabled(ui->loadTracksForExportCheckBox);

    if (ui->alignButton) {
        ui->alignButton->setEnabled(enabled);
        ui->alignButton->setText(busy ? tr("Aligning...") : tr("Align"));
    }
    if (ui->cancelButton) {
        ui->cancelButton->setEnabled(busy);
        ui->cancelButton->setText(tr("Cancel / Force Stop"));
    }
}

void AudioAlignmentDialog::startAlignmentRun(const QString &jsonFileName,
                                             const QVector<AlignmentTrackRequest> &trackRequests,
                                             bool overwriteOutput,
                                             quint32 rfVideoSampleRateHz)
{
    if (alignmentInProgress || alignmentWorkerThread) {
        QApplication::restoreOverrideCursor();
        if (ui && ui->statusLabel) {
            ui->statusLabel->setText(tr("Alignment is already running..."));
        }
        return;
    }

    alignmentCancelRequested.store(false, std::memory_order_relaxed);
    setAlignmentUiBusy(true);

    QPointer<AudioAlignmentDialog> self(this);
    alignmentWorkerThread = QThread::create([self, jsonFileName, trackRequests, overwriteOutput, rfVideoSampleRateHz]() {
        AlignmentRunResult runResult;
        runResult.success = true;
        const int totalTracks = qMax(1, static_cast<int>(trackRequests.size()));
        int completedTracks = 0;
        auto cancellationRequested = [self]() -> bool {
            return !self || self->alignmentCancelRequested.load(std::memory_order_relaxed);
        };

        auto postStatus = [self](const QString &statusText) {
            if (!self) {
                return;
            }
            QMetaObject::invokeMethod(self.data(), [self, statusText]() {
                if (!self || !self->ui || !self->ui->statusLabel) {
                    return;
                }
                self->ui->statusLabel->setText(statusText);
            }, Qt::QueuedConnection);
        };
        auto updateProgressStatus = [&postStatus, &completedTracks, totalTracks](const QString &trackLabel,
                                                                                  int trackPercent,
                                                                                  const QString &stageMessage) {
            const int boundedTrackPercent = qBound(0, trackPercent, 100);
            const double completedFraction = static_cast<double>(completedTracks)
                                             + (static_cast<double>(boundedTrackPercent) / 100.0);
            const int overallPercent = qBound(0,
                                              static_cast<int>(((completedFraction / qMax(1, totalTracks)) * 100.0) + 0.5),
                                              100);
            QString progressText = stageMessage.trimmed();
            if (progressText.isEmpty()) {
                progressText = QObject::tr("Running alignment for %1...").arg(trackLabel);
            } else {
                progressText = QObject::tr("%1: %2").arg(trackLabel, progressText);
            }
            postStatus(QObject::tr("%1 (%2%)").arg(progressText).arg(overallPercent));
        };

        for (const AlignmentTrackRequest &trackRequest : trackRequests) {
            if (cancellationRequested()) {
                runResult.success = false;
                runResult.cancelled = true;
                runResult.errorMessage = QObject::tr("Operation cancelled by user.");
                break;
            }
            updateProgressStatus(trackRequest.trackLabel, 0, QString());

            QString trackErrorMessage;
            const bool success = AudioAlignmentUtil::runStreamAlign(
                jsonFileName,
                trackRequest.inputFile,
                trackRequest.outputFile,
                rfVideoSampleRateHz,
                overwriteOutput,
                [&](int stagePercent, const QString &stageMessage) {
                    updateProgressStatus(trackRequest.trackLabel, stagePercent, stageMessage);
                },
                cancellationRequested,
                &trackErrorMessage);
            if (!success) {
                runResult.success = false;
                runResult.errorMessage = trackErrorMessage;
                runResult.cancelled = cancellationRequested()
                                      || trackErrorMessage.contains(QStringLiteral("cancelled"),
                                                                    Qt::CaseInsensitive);
                if (!runResult.cancelled) {
                    runResult.failureTrackLabel = trackRequest.trackLabel;
                }
                break;
            }

            completedTracks++;
            updateProgressStatus(trackRequest.trackLabel, 100, QObject::tr("Alignment complete."));
            if (trackRequest.isLinearTrack) {
                runResult.linearAligned = true;
                runResult.linearOutputFile = trackRequest.outputFile;
            } else {
                runResult.hifiAligned = true;
                runResult.hifiOutputFile = trackRequest.outputFile;
            }
        }

        if (!self) {
            return;
        }
        QMetaObject::invokeMethod(self.data(), [self, runResult]() {
            if (!self) {
                return;
            }
            self->finishAlignmentRun(runResult);
        }, Qt::QueuedConnection);
    });

    if (!alignmentWorkerThread) {
        setAlignmentUiBusy(false);
        QApplication::restoreOverrideCursor();
        if (ui && ui->statusLabel) {
            ui->statusLabel->setText(tr("Unable to start alignment worker."));
        }
        return;
    }

    connect(alignmentWorkerThread, &QThread::finished, this, [this]() {
        if (!alignmentWorkerThread) {
            return;
        }
        alignmentWorkerThread->deleteLater();
        alignmentWorkerThread = nullptr;
    });
    alignmentWorkerThread->start();
}

void AudioAlignmentDialog::finishAlignmentRun(const AlignmentRunResult &result)
{
    QApplication::restoreOverrideCursor();
    setAlignmentUiBusy(false);
    alignmentCancelRequested.store(false, std::memory_order_relaxed);

    if (result.cancelled) {
        if (ui && ui->statusLabel) {
            ui->statusLabel->setText(tr("Alignment cancelled."));
        }
        return;
    }

    if (!result.success) {
        const QString failedTrackLabel = result.failureTrackLabel.isEmpty()
                                             ? tr("Selected")
                                             : result.failureTrackLabel;
        if (ui && ui->statusLabel) {
            ui->statusLabel->setText(tr("%1 alignment failed.").arg(failedTrackLabel));
        }
        QMessageBox::warning(this, tr("Error"),
                             result.errorMessage.isEmpty()
                                 ? tr("Auto audio alignment failed for %1.").arg(failedTrackLabel)
                                 : result.errorMessage);
        return;
    }

    if (ui && ui->statusLabel) {
        if (result.linearAligned && result.hifiAligned) {
            ui->statusLabel->setText(tr("Alignment complete for Baseband and HiFi-Decode tracks."));
        } else if (result.linearAligned) {
            ui->statusLabel->setText(tr("Alignment complete for Baseband track."));
        } else {
            ui->statusLabel->setText(tr("Alignment complete for HiFi-Decode track."));
        }
    }

    const bool shouldPrepareExportTracks = ui->loadTracksForExportCheckBox
                                           && ui->loadTracksForExportCheckBox->isChecked();
    if (shouldPrepareExportTracks) {
        QStringList preparedTrackFiles;
        QStringList preparedTrackNames;
        if (result.linearAligned && !result.linearOutputFile.trimmed().isEmpty()) {
            preparedTrackFiles << result.linearOutputFile;
            preparedTrackNames << tr("Baseband");
        }
        if (result.hifiAligned && !result.hifiOutputFile.trimmed().isEmpty()) {
            preparedTrackFiles << result.hifiOutputFile;
            preparedTrackNames << tr("HiFi-Decode");
        }
        if (!preparedTrackFiles.isEmpty()) {
            emit exportTracksPrepared(preparedTrackFiles, preparedTrackNames);
        }

        bool payloadReady = true;
        if (!exportTrackOutputFile.trimmed().isEmpty()) {
            QString payloadError;
            payloadReady = writeExportTrackPayload(result.linearOutputFile,
                                                   result.hifiOutputFile,
                                                   result.linearAligned,
                                                   result.hifiAligned,
                                                   &payloadError);
            if (!payloadReady) {
                QMessageBox::warning(this, tr("Export track auto-load"),
                                     payloadError.isEmpty()
                                         ? tr("Alignment completed, but export track auto-load data could not be written.")
                                         : payloadError);
            }
        }

        if (payloadReady && ui && ui->statusLabel && !preparedTrackFiles.isEmpty()) {
            ui->statusLabel->setText(ui->statusLabel->text() + tr(" Export tracks prepared."));
        }
    }

    const QString outputPathForDirectory = result.hifiAligned
                                               ? result.hifiOutputFile
                                               : result.linearOutputFile;
    const QFileInfo outputInfo(outputPathForDirectory);
    if (!outputInfo.absolutePath().isEmpty()) {
        sourceDirectory = outputInfo.absolutePath();
    }
}

AudioAlignmentDialog::~AudioAlignmentDialog()
{
    if (alignmentWorkerThread) {
        alignmentCancelRequested.store(true, std::memory_order_relaxed);
        alignmentWorkerThread->wait();
        delete alignmentWorkerThread;
        alignmentWorkerThread = nullptr;
    }
    delete ui;
}

void AudioAlignmentDialog::closeEvent(QCloseEvent *event)
{
    if (alignmentInProgress) {
        if (ui && ui->statusLabel) {
            const bool stopRequested = alignmentCancelRequested.load(std::memory_order_relaxed);
            ui->statusLabel->setText(stopRequested
                                         ? tr("Alignment stop requested. Please wait for the running process to exit.")
                                         : tr("Alignment is currently running. Use Cancel / Force Stop to stop it."));
        }
        event->ignore();
        return;
    }

    QDialog::closeEvent(event);
}

void AudioAlignmentDialog::setSourceDirectory(const QString &directory)
{
    if (!directory.isEmpty()) {
        sourceDirectory = directory;
    }
}

void AudioAlignmentDialog::setDefaultJson(const QString &jsonFilename)
{
    const QString normalizedJson = AudioAlignmentUtil::normalizePathForCurrentPlatform(jsonFilename);
    if (normalizedJson.isEmpty()) {
        return;
    }

    ui->jsonLineEdit->setText(normalizedJson);
    const QFileInfo jsonInfo(normalizedJson);
    if (jsonInfo.exists()) {
        sourceDirectory = jsonInfo.absolutePath();
    }
    tryAutoDetectInputsFromJson(false);
}

void AudioAlignmentDialog::setDefaultInputFile(const QString &inputFilename)
{
    const QString normalizedInput = AudioAlignmentUtil::normalizePathForCurrentPlatform(inputFilename);
    if (normalizedInput.isEmpty()) {
        return;
    }

    ui->linearInputLineEdit->setText(normalizedInput);
    updateLinearOutputFromInput(true);
    const QFileInfo inputInfo(normalizedInput);
    if (inputInfo.exists()) {
        sourceDirectory = inputInfo.absolutePath();
    }
}

void AudioAlignmentDialog::setDefaultOutputFile(const QString &outputFilename)
{
    const QString normalizedOutput = AudioAlignmentUtil::normalizePathForCurrentPlatform(outputFilename);
    if (normalizedOutput.isEmpty()) {
        return;
    }

    userEditedLinearOutput = true;
    ui->linearOutputLineEdit->setText(normalizedOutput);
    const QFileInfo outputInfo(normalizedOutput);
    if (!outputInfo.absolutePath().isEmpty()) {
        sourceDirectory = outputInfo.absolutePath();
    }
}

void AudioAlignmentDialog::setDefaultRfVideoSampleRate(quint32 sampleRateHz)
{
    if (!ui->rfVideoRatePresetComboBox || !ui->rfVideoSampleRateCustomSpinBox) {
        return;
    }

    if (sampleRateHz == 40000000) {
        ui->rfVideoRatePresetComboBox->setCurrentIndex(0);
    } else if (sampleRateHz == 20000000) {
        ui->rfVideoRatePresetComboBox->setCurrentIndex(1);
    } else if (sampleRateHz == 16000000) {
        ui->rfVideoRatePresetComboBox->setCurrentIndex(2);
    } else {
        ui->rfVideoRatePresetComboBox->setCurrentIndex(3);
        ui->rfVideoSampleRateCustomSpinBox->setValue(static_cast<int>(sampleRateHz));
    }
}

void AudioAlignmentDialog::setExportTrackOutputFile(const QString &outputFilename)
{
    exportTrackOutputFile = AudioAlignmentUtil::normalizePathForCurrentPlatform(outputFilename);
}

void AudioAlignmentDialog::on_jsonBrowseButton_clicked()
{
    const QString filter = tr("TBC metadata JSON (*.json);;All Files (*)");
    const QString jsonFileName = QFileDialog::getOpenFileName(this,
                                                              tr("Select metadata JSON"),
                                                              sourceDirectory,
                                                              filter);
    if (jsonFileName.isEmpty()) {
        return;
    }

    const QString normalizedJson = AudioAlignmentUtil::normalizePathForCurrentPlatform(jsonFileName);
    ui->jsonLineEdit->setText(normalizedJson);
    tryAutoDetectInputsFromJson(true);
    if (ui->statusLabel) {
        ui->statusLabel->clear();
    }

    const QFileInfo selectedInfo(normalizedJson);
    if (selectedInfo.exists()) {
        sourceDirectory = selectedInfo.absolutePath();
    }
}

void AudioAlignmentDialog::on_linearInputBrowseButton_clicked()
{
    const QString filter = tr("Audio input (*.wav *.flac);;All Files (*)");
    const QString inputFileName = QFileDialog::getOpenFileName(this,
                                                               tr("Select linear/baseband input audio"),
                                                               sourceDirectory,
                                                               filter);
    if (inputFileName.isEmpty()) {
        return;
    }

    const QString normalizedInput = AudioAlignmentUtil::normalizePathForCurrentPlatform(inputFileName);
    ui->linearInputLineEdit->setText(normalizedInput);
    updateLinearOutputFromInput(true);
    if (ui->statusLabel) {
        ui->statusLabel->clear();
    }

    const QFileInfo selectedInfo(normalizedInput);
    if (selectedInfo.exists()) {
        sourceDirectory = selectedInfo.absolutePath();
    }
}

void AudioAlignmentDialog::on_linearOutputBrowseButton_clicked()
{
    QString suggestedOutput = ui->linearOutputLineEdit->text().trimmed();
    if (suggestedOutput.isEmpty()) {
        suggestedOutput = AudioAlignmentUtil::defaultAlignedOutputPath(ui->linearInputLineEdit->text());
    }

    const QString filter = tr("Aligned audio (*.flac);;All Files (*)");
    const QString outputFileName = QFileDialog::getSaveFileName(this,
                                                                tr("Select aligned linear/baseband output"),
                                                                suggestedOutput,
                                                                filter);
    if (outputFileName.isEmpty()) {
        return;
    }

    userEditedLinearOutput = true;
    ui->linearOutputLineEdit->setText(AudioAlignmentUtil::normalizePathForCurrentPlatform(outputFileName));
    if (ui->statusLabel) {
        ui->statusLabel->clear();
    }

    const QFileInfo selectedInfo(ui->linearOutputLineEdit->text());
    if (!selectedInfo.absolutePath().isEmpty()) {
        sourceDirectory = selectedInfo.absolutePath();
    }
}

void AudioAlignmentDialog::on_hifiInputBrowseButton_clicked()
{
    const QString filter = tr("Audio input (*.wav *.flac);;All Files (*)");
    const QString inputFileName = QFileDialog::getOpenFileName(this,
                                                               tr("Select hifi input audio"),
                                                               sourceDirectory,
                                                               filter);
    if (inputFileName.isEmpty()) {
        return;
    }

    const QString normalizedInput = AudioAlignmentUtil::normalizePathForCurrentPlatform(inputFileName);
    ui->hifiInputLineEdit->setText(normalizedInput);
    updateHifiOutputFromInput(true);
    if (ui->statusLabel) {
        ui->statusLabel->clear();
    }

    const QFileInfo selectedInfo(normalizedInput);
    if (selectedInfo.exists()) {
        sourceDirectory = selectedInfo.absolutePath();
    }
}

void AudioAlignmentDialog::on_hifiOutputBrowseButton_clicked()
{
    QString suggestedOutput = ui->hifiOutputLineEdit->text().trimmed();
    if (suggestedOutput.isEmpty()) {
        suggestedOutput = AudioAlignmentUtil::defaultAlignedOutputPath(ui->hifiInputLineEdit->text());
    }

    const QString filter = tr("Aligned audio (*.flac);;All Files (*)");
    const QString outputFileName = QFileDialog::getSaveFileName(this,
                                                                tr("Select aligned hifi output"),
                                                                suggestedOutput,
                                                                filter);
    if (outputFileName.isEmpty()) {
        return;
    }

    userEditedHifiOutput = true;
    ui->hifiOutputLineEdit->setText(AudioAlignmentUtil::normalizePathForCurrentPlatform(outputFileName));
    if (ui->statusLabel) {
        ui->statusLabel->clear();
    }

    const QFileInfo selectedInfo(ui->hifiOutputLineEdit->text());
    if (!selectedInfo.absolutePath().isEmpty()) {
        sourceDirectory = selectedInfo.absolutePath();
    }
}

void AudioAlignmentDialog::on_alignButton_clicked()
{
    const QString jsonFileName = AudioAlignmentUtil::normalizePathForCurrentPlatform(ui->jsonLineEdit->text());
    QString linearInputFileName = AudioAlignmentUtil::normalizePathForCurrentPlatform(ui->linearInputLineEdit->text());
    QString linearOutputFileName = AudioAlignmentUtil::normalizePathForCurrentPlatform(ui->linearOutputLineEdit->text());
    QString hifiInputFileName = AudioAlignmentUtil::normalizePathForCurrentPlatform(ui->hifiInputLineEdit->text());
    QString hifiOutputFileName = AudioAlignmentUtil::normalizePathForCurrentPlatform(ui->hifiOutputLineEdit->text());

    if (linearOutputFileName.isEmpty() && !linearInputFileName.isEmpty()) {
        linearOutputFileName = AudioAlignmentUtil::defaultAlignedOutputPath(linearInputFileName);
    }
    if (hifiOutputFileName.isEmpty() && !hifiInputFileName.isEmpty()) {
        hifiOutputFileName = AudioAlignmentUtil::defaultAlignedOutputPath(hifiInputFileName);
    }

    if (jsonFileName.isEmpty()) {
        ui->statusLabel->setText(tr("Please choose metadata JSON."));
        return;
    }
    if (!QFileInfo::exists(jsonFileName)) {
        ui->statusLabel->setText(tr("Metadata JSON does not exist."));
        return;
    }

    const bool hasLinearTrack = !linearInputFileName.isEmpty();
    const bool hasHifiTrack = !hifiInputFileName.isEmpty();
    if (!hasLinearTrack && !hasHifiTrack) {
        ui->statusLabel->setText(tr("Please choose at least one input audio track (Linear/Baseband or HiFi)."));
        return;
    }
    if (hasLinearTrack && !QFileInfo::exists(linearInputFileName)) {
        ui->statusLabel->setText(tr("Linear/Baseband input audio file does not exist."));
        return;
    }
    if (hasHifiTrack && !QFileInfo::exists(hifiInputFileName)) {
        ui->statusLabel->setText(tr("HiFi input audio file does not exist."));
        return;
    }

    auto comparablePathForMatch = [](const QString &path) -> QString {
        const QFileInfo info(path);
        const QString canonicalPath = info.canonicalFilePath();
        if (!canonicalPath.isEmpty()) {
            return canonicalPath;
        }
        return info.absoluteFilePath();
    };

    if (hasLinearTrack && hasHifiTrack
        && comparablePathForMatch(linearOutputFileName) == comparablePathForMatch(hifiOutputFileName)) {
        ui->statusLabel->setText(tr("Linear/Baseband and HiFi outputs must be different files."));
        return;
    }

    if (hasLinearTrack && QFileInfo::exists(linearOutputFileName) && !ui->overwriteCheckBox->isChecked()) {
        ui->statusLabel->setText(tr("Linear/Baseband output exists. Enable overwrite or choose a new output path."));
        return;
    }
    if (hasHifiTrack && QFileInfo::exists(hifiOutputFileName) && !ui->overwriteCheckBox->isChecked()) {
        ui->statusLabel->setText(tr("HiFi output exists. Enable overwrite or choose a new output path."));
        return;
    }

    {
        const QSignalBlocker jsonBlocker(ui->jsonLineEdit);
        ui->jsonLineEdit->setText(jsonFileName);
    }
    {
        const QSignalBlocker linearInputBlocker(ui->linearInputLineEdit);
        ui->linearInputLineEdit->setText(linearInputFileName);
    }
    {
        const QSignalBlocker linearOutputBlocker(ui->linearOutputLineEdit);
        ui->linearOutputLineEdit->setText(linearOutputFileName);
    }
    {
        const QSignalBlocker hifiInputBlocker(ui->hifiInputLineEdit);
        ui->hifiInputLineEdit->setText(hifiInputFileName);
    }
    {
        const QSignalBlocker hifiOutputBlocker(ui->hifiOutputLineEdit);
        ui->hifiOutputLineEdit->setText(hifiOutputFileName);
    }

    QVector<AlignmentTrackRequest> trackRequests;
    trackRequests.reserve(2);
    if (hasLinearTrack) {
        AlignmentTrackRequest request;
        request.isLinearTrack = true;
        request.trackLabel = tr("Baseband");
        request.inputFile = linearInputFileName;
        request.outputFile = linearOutputFileName;
        trackRequests.push_back(request);
    }
    if (hasHifiTrack) {
        AlignmentTrackRequest request;
        request.isLinearTrack = false;
        request.trackLabel = tr("HiFi-Decode");
        request.inputFile = hifiInputFileName;
        request.outputFile = hifiOutputFileName;
        trackRequests.push_back(request);
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);
    startAlignmentRun(jsonFileName,
                      trackRequests,
                      ui->overwriteCheckBox && ui->overwriteCheckBox->isChecked(),
                      currentRfVideoSampleRateHz());
}

void AudioAlignmentDialog::on_cancelButton_clicked()
{
    if (!alignmentInProgress) {
        if (ui && ui->statusLabel) {
            ui->statusLabel->setText(tr("No alignment is currently running."));
        }
        return;
    }

    const bool alreadyRequested = alignmentCancelRequested.exchange(true, std::memory_order_relaxed);
    if (alreadyRequested) {
        if (ui && ui->statusLabel) {
            ui->statusLabel->setText(tr("Force stop already requested. Waiting for the running process to exit..."));
        }
        return;
    }

    if (ui && ui->statusLabel) {
        ui->statusLabel->setText(tr("Stopping alignment..."));
    }
    if (ui && ui->cancelButton) {
        ui->cancelButton->setEnabled(false);
        ui->cancelButton->setText(tr("Stopping..."));
    }
}

void AudioAlignmentDialog::updateLinearOutputFromInput(bool forceOutputUpdate)
{
    const QString normalizedInput = AudioAlignmentUtil::normalizePathForCurrentPlatform(ui->linearInputLineEdit->text());
    if (!normalizedInput.isEmpty() && normalizedInput != ui->linearInputLineEdit->text()) {
        const QSignalBlocker blocker(ui->linearInputLineEdit);
        ui->linearInputLineEdit->setText(normalizedInput);
    }

    if (normalizedInput.isEmpty()) {
        if (forceOutputUpdate || !userEditedLinearOutput) {
            const QSignalBlocker blocker(ui->linearOutputLineEdit);
            ui->linearOutputLineEdit->clear();
            userEditedLinearOutput = false;
        }
        return;
    }

    const QString suggestedOutput = AudioAlignmentUtil::defaultAlignedOutputPath(normalizedInput);
    if (suggestedOutput.isEmpty()) {
        return;
    }

    const bool outputEmpty = ui->linearOutputLineEdit->text().trimmed().isEmpty();
    if (forceOutputUpdate || outputEmpty || !userEditedLinearOutput) {
        const QSignalBlocker blocker(ui->linearOutputLineEdit);
        ui->linearOutputLineEdit->setText(suggestedOutput);
        userEditedLinearOutput = false;
    }
}

void AudioAlignmentDialog::updateHifiOutputFromInput(bool forceOutputUpdate)
{
    const QString normalizedInput = AudioAlignmentUtil::normalizePathForCurrentPlatform(ui->hifiInputLineEdit->text());
    if (!normalizedInput.isEmpty() && normalizedInput != ui->hifiInputLineEdit->text()) {
        const QSignalBlocker blocker(ui->hifiInputLineEdit);
        ui->hifiInputLineEdit->setText(normalizedInput);
    }

    if (normalizedInput.isEmpty()) {
        if (forceOutputUpdate || !userEditedHifiOutput) {
            const QSignalBlocker blocker(ui->hifiOutputLineEdit);
            ui->hifiOutputLineEdit->clear();
            userEditedHifiOutput = false;
        }
        return;
    }

    const QString suggestedOutput = AudioAlignmentUtil::defaultAlignedOutputPath(normalizedInput);
    if (suggestedOutput.isEmpty()) {
        return;
    }

    const bool outputEmpty = ui->hifiOutputLineEdit->text().trimmed().isEmpty();
    if (forceOutputUpdate || outputEmpty || !userEditedHifiOutput) {
        const QSignalBlocker blocker(ui->hifiOutputLineEdit);
        ui->hifiOutputLineEdit->setText(suggestedOutput);
        userEditedHifiOutput = false;
    }
}

void AudioAlignmentDialog::tryAutoDetectInputsFromJson(bool forceReplaceInputs)
{
    const bool replaceLinearInput = forceReplaceInputs || ui->linearInputLineEdit->text().trimmed().isEmpty();
    const bool replaceHifiInput = forceReplaceInputs || ui->hifiInputLineEdit->text().trimmed().isEmpty();
    if (!replaceLinearInput && !replaceHifiInput) {
        return;
    }

    QString detectedLinearInput;
    QString detectedHifiInput;

    if (replaceLinearInput) {
        const QString excludePath = replaceHifiInput ? ui->hifiInputLineEdit->text() : QString();
        detectedLinearInput = AudioAlignmentUtil::autoDetectLinearInputAudioFile(ui->jsonLineEdit->text(), excludePath);
        if (detectedLinearInput.isEmpty()) {
            detectedLinearInput = AudioAlignmentUtil::autoDetectInputAudioFile(ui->jsonLineEdit->text(), excludePath);
        }
    }
    if (replaceHifiInput) {
        const QString excludePath = !detectedLinearInput.isEmpty() ? detectedLinearInput : ui->linearInputLineEdit->text();
        detectedHifiInput = AudioAlignmentUtil::autoDetectHifiInputAudioFile(ui->jsonLineEdit->text(), excludePath);
        if (detectedHifiInput.isEmpty()) {
            detectedHifiInput = AudioAlignmentUtil::autoDetectInputAudioFile(ui->jsonLineEdit->text(), excludePath);
        }
    }

    if (replaceLinearInput) {
        const QSignalBlocker inputBlocker(ui->linearInputLineEdit);
        ui->linearInputLineEdit->setText(detectedLinearInput);
    }
    if (replaceHifiInput) {
        const QSignalBlocker inputBlocker(ui->hifiInputLineEdit);
        ui->hifiInputLineEdit->setText(detectedHifiInput);
    }

    if (replaceLinearInput) {
        updateLinearOutputFromInput(true);
    }
    if (replaceHifiInput) {
        updateHifiOutputFromInput(true);
    }

    if (!detectedHifiInput.isEmpty()) {
        const QFileInfo detectedInfo(detectedHifiInput);
        if (detectedInfo.exists()) {
            sourceDirectory = detectedInfo.absolutePath();
        }
    } else if (!detectedLinearInput.isEmpty()) {
        const QFileInfo detectedInfo(detectedLinearInput);
        if (detectedInfo.exists()) {
            sourceDirectory = detectedInfo.absolutePath();
        }
    }
}

bool AudioAlignmentDialog::writeExportTrackPayload(const QString &linearOutputFile,
                                                   const QString &hifiOutputFile,
                                                   bool includeLinearTrack,
                                                   bool includeHifiTrack,
                                                   QString *errorMessage) const
{
    if (errorMessage) {
        errorMessage->clear();
    }

    const QString payloadPath = AudioAlignmentUtil::normalizePathForCurrentPlatform(exportTrackOutputFile);
    if (payloadPath.trimmed().isEmpty()) {
        return true;
    }

    const QFileInfo payloadInfo(payloadPath);
    const QString payloadDirectory = payloadInfo.absolutePath();
    if (!payloadDirectory.isEmpty() && !QDir().mkpath(payloadDirectory)) {
        if (errorMessage) {
            *errorMessage = tr("Unable to create export track payload directory:\n%1").arg(payloadDirectory);
        }
        return false;
    }

    QJsonArray tracks;
    auto appendTrack = [&tracks](bool includeTrack,
                                 const QString &trackFilePath,
                                 const QString &trackRole,
                                 const QString &trackName) {
        if (!includeTrack) {
            return;
        }
        const QString normalizedTrackPath = AudioAlignmentUtil::normalizePathForCurrentPlatform(trackFilePath);
        if (normalizedTrackPath.isEmpty()) {
            return;
        }
        const QFileInfo trackInfo(normalizedTrackPath);
        const QString absoluteTrackPath = trackInfo.isAbsolute()
            ? trackInfo.absoluteFilePath()
            : QDir::current().absoluteFilePath(normalizedTrackPath);
        if (absoluteTrackPath.trimmed().isEmpty()) {
            return;
        }
        QJsonObject trackObject;
        trackObject.insert(QStringLiteral("role"), trackRole);
        trackObject.insert(QStringLiteral("name"), trackName);
        trackObject.insert(QStringLiteral("file"), QDir::toNativeSeparators(absoluteTrackPath));
        tracks.append(trackObject);
    };

    appendTrack(includeLinearTrack,
                linearOutputFile,
                QStringLiteral("linear"),
                tr("Baseband"));
    appendTrack(includeHifiTrack,
                hifiOutputFile,
                QStringLiteral("hifi"),
                tr("HiFi-Decode"));

    if (tracks.isEmpty()) {
        QFile::remove(payloadPath);
        return true;
    }

    QJsonObject rootObject;
    rootObject.insert(QStringLiteral("format"), QStringLiteral("ld-analyse-export-audio-tracks"));
    rootObject.insert(QStringLiteral("version"), 1);
    rootObject.insert(QStringLiteral("tracks"), tracks);

    QSaveFile payloadFile(payloadPath);
    if (!payloadFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (errorMessage) {
            *errorMessage = tr("Unable to open export track payload file for writing:\n%1").arg(payloadPath);
        }
        return false;
    }
    const QByteArray payloadData = QJsonDocument(rootObject).toJson(QJsonDocument::Compact);
    if (payloadFile.write(payloadData) != payloadData.size()) {
        if (errorMessage) {
            *errorMessage = tr("Unable to write export track payload file:\n%1").arg(payloadPath);
        }
        payloadFile.cancelWriting();
        return false;
    }
    if (!payloadFile.commit()) {
        if (errorMessage) {
            *errorMessage = tr("Unable to finalize export track payload file:\n%1").arg(payloadPath);
        }
        return false;
    }

    return true;
}

quint32 AudioAlignmentDialog::currentRfVideoSampleRateHz() const
{
    if (!ui->rfVideoRatePresetComboBox || !ui->rfVideoSampleRateCustomSpinBox) {
        return 40000000;
    }

    switch (ui->rfVideoRatePresetComboBox->currentIndex()) {
    case 1:
        return 20000000;
    case 2:
        return 16000000;
    case 3:
        return static_cast<quint32>(ui->rfVideoSampleRateCustomSpinBox->value());
    case 0:
    default:
        return 40000000;
    }
}
