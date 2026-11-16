/******************************************************************************
 * processvbidialog.h
 * tbc-process-vbi - standalone VBI/VITS processing GUI
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Harry Munday
 *
 * This file is part of tbc-tools.
 ******************************************************************************/

#ifndef PROCESSVBIDIALOG_H
#define PROCESSVBIDIALOG_H

#include <QDialog>
#include <QString>

#include "tbc/vbiprocessingoptions.h"

class QCheckBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QSpinBox;

class ProcessVbiDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ProcessVbiDialog(QWidget *parent = nullptr);
    ~ProcessVbiDialog() override = default;

    // CLI prefill setters (used by processvbi-main.cpp).
    void setSourceDirectory(const QString &directory);
    void setDefaultInputTbc(const QString &tbcFilename);
    void setDefaultOutputMetadata(const QString &metadataFilename);

private slots:
    void onBrowseInputTbcClicked();
    void onBrowseOutputMetadataClicked();
    void onBrowseTeletextDirClicked();
    void onTeletextToggled(bool checked);
    void onRunClicked();
    void onCancelClicked();

private:
    void buildUi();
    void setBusy(bool enabled);
    void updateControlStates();
    QString normalizePath(const QString &path) const;
    void appendStatus(const QString &text);
    void appendLog(const QString &text);
    QString formatCommand(const QString &program, const QStringList &arguments) const;
    QString resolveLdProcessVbi() const;
    bool toolSupportsOption(const QString &toolPath, const QString &option) const;
    QStringList buildToolArguments(const QString &toolPath, const VbiProcessingOptions &opts) const;
    bool runProcessStep(const QString &program, const QStringList &arguments, QString *errorMessage);

    QString sourceDirectory;
    bool runInProgress = false;
    bool cancelRequested = false;

    QLineEdit *inputTbcLineEdit = nullptr;
    QPushButton *inputTbcBrowseButton = nullptr;
    QLineEdit *outputMetadataLineEdit = nullptr;
    QPushButton *outputMetadataBrowseButton = nullptr;

    QCheckBox *vbiCoreCheckBox = nullptr;
    QCheckBox *ntscCheckBox = nullptr;
    QCheckBox *vitcCheckBox = nullptr;
    QCheckBox *closedCaptionsCheckBox = nullptr;
    QCheckBox *teletextCheckBox = nullptr;
    QCheckBox *vitsCheckBox = nullptr;

    QGroupBox *teletextGroupBox = nullptr;
    QLineEdit *teletextHtmlDirLineEdit = nullptr;
    QLineEdit *teletextTapeFormatLineEdit = nullptr;
    QSpinBox *teletextMinDuplicatesSpinBox = nullptr;

    QLabel *statusLabel = nullptr;
    QProgressBar *progressBar = nullptr;
    QPlainTextEdit *logTextEdit = nullptr;
    QPushButton *runButton = nullptr;
    QPushButton *cancelButton = nullptr;
    QPushButton *closeButton = nullptr;

    // Cached capability flags for the resolved tbc-process-vbi (populated lazily).
    mutable QString cachedToolPath;
    mutable QString cachedHelpText;
};

#endif // PROCESSVBIDIALOG_H
