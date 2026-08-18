/******************************************************************************
 * efmhandlerdialog.h
 * tbc-analyse - Dedicated EFM/AC3 handling workflow GUI
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 *
 * This file is part of tbc-tools.
 ******************************************************************************/

#ifndef EFMHANDLERDIALOG_H
#define EFMHANDLERDIALOG_H

#include <QDialog>
#include <QStringList>

class QCheckBox;
class QComboBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;

class EfmHandlerDialog : public QDialog
{
    Q_OBJECT

public:
    explicit EfmHandlerDialog(QWidget *parent = nullptr);
    ~EfmHandlerDialog() override = default;

    void setSourceDirectory(const QString &directory);
    void setDefaultEfmInput(const QString &efmFilename);
    void setDefaultAc3Input(const QString &ac3Filename);
    void setSuggestedOutputBase(const QString &outputBasePath);

signals:
    void exportTracksPrepared(const QStringList &trackFiles, const QStringList &trackNames);

private slots:
    void onAddEfmInputClicked();
    void onRemoveEfmInputClicked();
    void onClearEfmInputsClicked();
    void onBrowseOutputBaseClicked();
    void onBrowseAudioOutputClicked();
    void onBrowseDataOutputClicked();
    void onBrowseAc3InputClicked();
    void onBrowseAc3OutputClicked();
    void onRunClicked();
    void onCancelClicked();

private:
    struct CommandStep {
        QString title;
        QString program;
        QStringList arguments;
    };

    void buildUi();
    void setBusy(bool enabled);
    void updateControlStates();
    QString normalizePath(const QString &path) const;
    QString defaultOutputBaseFromInputs() const;
    void refreshDerivedOutputPaths(bool forceUpdate);
    void appendStatus(const QString &text);
    void appendLog(const QString &text);
    QString formatCommand(const QString &program, const QStringList &arguments) const;
    QString resolveExternalExecutable(const QStringList &toolNames) const;
    bool runProcessStep(const CommandStep &step,
                        int stepNumberOneBased,
                        int totalSteps,
                        QString *errorMessage);
    bool runSelectedWorkflows(QString *errorMessage, QStringList *generatedAudioTracks);

    QString sourceDirectory;
    bool runInProgress = false;
    bool cancelRequested = false;
    bool userEditedOutputBase = false;
    bool userEditedAudioOutput = false;
    bool userEditedDataOutput = false;
    bool userEditedAc3Output = false;

    QGroupBox *efmGroupBox = nullptr;
    QListWidget *efmInputListWidget = nullptr;
    QPushButton *addEfmInputButton = nullptr;
    QPushButton *removeEfmInputButton = nullptr;
    QPushButton *clearEfmInputsButton = nullptr;
    QCheckBox *noTimecodesCheckBox = nullptr;
    QCheckBox *stackF2CheckBox = nullptr;
    QLineEdit *outputBaseLineEdit = nullptr;
    QPushButton *outputBaseBrowseButton = nullptr;
    QCheckBox *extractAudioCheckBox = nullptr;
    QLineEdit *audioOutputLineEdit = nullptr;
    QPushButton *audioOutputBrowseButton = nullptr;
    QCheckBox *extractDataCheckBox = nullptr;
    QLineEdit *dataOutputLineEdit = nullptr;
    QPushButton *dataOutputBrowseButton = nullptr;
    QCheckBox *outputBadSectorMapCheckBox = nullptr;

    QGroupBox *ac3GroupBox = nullptr;
    QCheckBox *decodeAc3CheckBox = nullptr;
    QLineEdit *ac3InputLineEdit = nullptr;
    QPushButton *ac3InputBrowseButton = nullptr;
    QLineEdit *ac3OutputLineEdit = nullptr;
    QPushButton *ac3OutputBrowseButton = nullptr;
    QComboBox *ac3FormatComboBox = nullptr;

    QCheckBox *loadDecodedAudioForExportCheckBox = nullptr;
    QLabel *statusLabel = nullptr;
    QProgressBar *progressBar = nullptr;
    QPlainTextEdit *logTextEdit = nullptr;
    QPushButton *runButton = nullptr;
    QPushButton *cancelButton = nullptr;
    QPushButton *closeButton = nullptr;
};

#endif // EFMHANDLERDIALOG_H
