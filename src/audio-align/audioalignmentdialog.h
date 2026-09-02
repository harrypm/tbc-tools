/******************************************************************************
 * audioalignmentdialog.h
 * tbc-audio-align - Audio alignment GUI for ld-decode
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 *
 * This file is part of tbc-tools.
 ******************************************************************************/

#ifndef AUDIOALIGNMENTDIALOG_H
#define AUDIOALIGNMENTDIALOG_H

#include <QDialog>
#include <QtGlobal>
#include <QVector>
#include <QStringList>
#include <atomic>

namespace Ui {
class AudioAlignmentDialog;
}
class QCloseEvent;
class QThread;

class AudioAlignmentDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AudioAlignmentDialog(QWidget *parent = nullptr);
    ~AudioAlignmentDialog();

    void setSourceDirectory(const QString &directory);
    void setDefaultJson(const QString &jsonFilename);
    void setDefaultInputFile(const QString &inputFilename);
    void setDefaultOutputFile(const QString &outputFilename);
    void setDefaultRfVideoSampleRate(quint32 sampleRateHz);
    void setExportTrackOutputFile(const QString &outputFilename);
signals:
    void exportTracksPrepared(const QStringList &trackFiles, const QStringList &trackNames);
protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void on_jsonBrowseButton_clicked();
    void on_linearInputBrowseButton_clicked();
    void on_linearOutputBrowseButton_clicked();
    void on_hifiInputBrowseButton_clicked();
    void on_hifiOutputBrowseButton_clicked();
    void on_alignButton_clicked();
    void on_cancelButton_clicked();

private:
    struct AlignmentTrackRequest {
        bool isLinearTrack = false;
        QString trackLabel;
        QString inputFile;
        QString outputFile;
        bool convertMonoToStereo = true;
    };
    struct AlignmentRunResult {
        bool success = false;
        bool cancelled = false;
        bool linearAligned = false;
        bool hifiAligned = false;
        QString linearOutputFile;
        QString hifiOutputFile;
        QString failureTrackLabel;
        QString errorMessage;
    };
    void setAlignmentUiBusy(bool busy);
    void startAlignmentRun(const QString &jsonFileName,
                           const QVector<AlignmentTrackRequest> &trackRequests,
                           bool overwriteOutput,
                           quint32 rfVideoSampleRateHz);
    void finishAlignmentRun(const AlignmentRunResult &result);
    void updateLinearOutputFromInput(bool forceOutputUpdate);
    void updateHifiOutputFromInput(bool forceOutputUpdate);
    void tryAutoDetectInputsFromJson(bool forceReplaceInputs);
    void applyRfSourceRateFromJson(const QString &jsonFilename);
    bool writeExportTrackPayload(const QString &linearOutputFile,
                                 const QString &hifiOutputFile,
                                 bool includeLinearTrack,
                                 bool includeHifiTrack,
                                 QString *errorMessage) const;
    quint32 currentRfVideoSampleRateHz() const;
    Ui::AudioAlignmentDialog *ui;
    QString sourceDirectory;
    QString exportTrackOutputFile;
    bool userEditedLinearOutput = false;
    bool userEditedHifiOutput = false;
    bool alignmentInProgress = false;
    std::atomic_bool alignmentCancelRequested{false};
    QThread *alignmentWorkerThread = nullptr;
};

#endif // AUDIOALIGNMENTDIALOG_H
