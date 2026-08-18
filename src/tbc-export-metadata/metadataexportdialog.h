/******************************************************************************
 * metadataexportdialog.h
 * tbc-export-metadata - Export ld-decode metadata into other formats
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 *
 * This file is part of tbc-tools.
 ******************************************************************************/

#ifndef METADATAEXPORTDIALOG_H
#define METADATAEXPORTDIALOG_H

#include <QDialog>

namespace Ui {
class MetadataExportDialog;
}
class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;

class MetadataExportDialog : public QDialog
{
    Q_OBJECT

public:
    struct InitialOptions {
        QString inputFile;
        bool exportVitsCsv = false;
        bool exportVbiCsv = false;
        bool exportUserMarkersTxt = false;
        bool exportUserMarkersCsv = false;
        bool exportAudacityLabels = false;
        bool exportFfmetadata = false;
        bool exportFfmpegVitc = false;
        bool exportFfmetadataVitcTimecode = true;
        bool exportClosedCaptions = false;
        qint32 ffmetadataStart = -1;
        qint32 ffmetadataLength = -1;
        bool debug = false;
        bool quiet = false;
    };

    explicit MetadataExportDialog(QWidget *parent = nullptr);
    ~MetadataExportDialog();

    void setSourceDirectory(const QString &directory);
    void setDefaultInputFile(const QString &inputFile);
    void setExportExecutablePath(const QString &executablePath);
    void setInitialOptions(const InitialOptions &options);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private slots:
    void on_inputBrowseButton_clicked();
    void on_exportButton_clicked();

private:
    QString normalizedPath(const QString &path) const;
    QString defaultOutputPath(const QString &inputPath, const QString &suffix) const;
    QString inputBaseName(const QString &inputPath) const;
    bool isJsonPath(const QString &path) const;
    bool isSupportedInputPath(const QString &path) const;
    bool exportToolSupportsOption(const QString &optionName) const;
    void updateOptionCompatibilityState();
    void updateFfmetadataControlsEnabled();

    Ui::MetadataExportDialog *ui;
    QString sourceDirectory;
    QString exportExecutablePath;
};

#endif // METADATAEXPORTDIALOG_H
