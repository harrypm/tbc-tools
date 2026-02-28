/******************************************************************************
 * metadataconversiondialog.h
 * ld-analyse - TBC output analysis GUI
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 *
 * This file is part of ld-decode-tools.
 ******************************************************************************/

#ifndef METADATACONVERSIONDIALOG_H
#define METADATACONVERSIONDIALOG_H

#include <QDialog>

namespace Ui {
class MetadataConversionDialog;
}

class MetadataConversionDialog : public QDialog
{
    Q_OBJECT

public:
    explicit MetadataConversionDialog(QWidget *parent = nullptr);
    ~MetadataConversionDialog();

    void setSourceDirectory(const QString &directory);
    void setDefaultInput(const QString &inputFilename);

private slots:
    void on_inputBrowseButton_clicked();
    void on_outputBrowseButton_clicked();
    void on_directionComboBox_currentIndexChanged(int index);
    void on_convertButton_clicked();

private:
    Ui::MetadataConversionDialog *ui;
    QString sourceDirectory;
};

#endif // METADATACONVERSIONDIALOG_H
