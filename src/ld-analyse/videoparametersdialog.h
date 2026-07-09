/******************************************************************************
 * videoparametersdialog.h
 * ld-analyse - TBC output analysis GUI
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2022 Adam Sampson
 *
 * This file is part of ld-decode-tools.
 ******************************************************************************/

#ifndef VIDEOPARAMETERSDIALOG_H
#define VIDEOPARAMETERSDIALOG_H

#include <QAbstractButton>
#include <QDialog>

#include "lddecodemetadata.h"

namespace Ui {
class VideoParametersDialog;
}

class VideoParametersDialog : public QDialog
{
    Q_OBJECT

public:
    explicit VideoParametersDialog(QWidget *parent = nullptr);
    ~VideoParametersDialog();

    void setVideoParameters(const LdDecodeMetaData::VideoParameters &videoParameters);
    void setShowExportBoundary(bool enabled);
    void setExportBoundaryThickness(int thickness);

signals:
    void videoParametersChanged(const LdDecodeMetaData::VideoParameters &videoParameters);
    void exportBoundaryToggled(bool enabled);
    void exportBoundaryThicknessChanged(int thickness);
protected:
    bool eventFilter(QObject *object, QEvent *event) override;

private slots:
    void on_activeVideoStartSpinBox_valueChanged(int value);
    void on_activeVideoWidthSpinBox_valueChanged(int value);
    void on_activeVideoStartResetButton_clicked();
    void on_activeVideoWidthResetButton_clicked();
    void on_activeVideoStartHorizontalSlider_valueChanged(int value);
    void on_activeVideoWidthHorizontalSlider_valueChanged(int value);

    void on_firstActiveFieldLineSpinBox_valueChanged(int value);
    void on_lastActiveFieldLineSpinBox_valueChanged(int value);
    void on_firstActiveFrameLineSpinBox_valueChanged(int value);
    void on_lastActiveFrameLineSpinBox_valueChanged(int value);
    void on_firstActiveFieldLineHorizontalSlider_valueChanged(int value);
    void on_lastActiveFieldLineHorizontalSlider_valueChanged(int value);
    void on_firstActiveFrameLineHorizontalSlider_valueChanged(int value);
    void on_lastActiveFrameLineHorizontalSlider_valueChanged(int value);
    void on_firstActiveFieldLineResetButton_clicked();
    void on_lastActiveFieldLineResetButton_clicked();
    void on_firstActiveFrameLineResetButton_clicked();
    void on_lastActiveFrameLineResetButton_clicked();

    void on_aspectRatioButtonGroup_buttonClicked(QAbstractButton *button);
    void on_exportBoundaryCheckBox_toggled(bool checked);
    void on_exportBoundaryThicknessSpinBox_valueChanged(int value);
    void on_exportBoundaryThicknessHorizontalSlider_valueChanged(int value);

private:
    void updateResultingFrameSizeLabel();

    Ui::VideoParametersDialog *ui;
    LdDecodeMetaData::VideoParameters videoParameters;
    qint32 originalActiveVideoStart = -1;
    qint32 originalActiveVideoWidth = -1;
    qint32 originalFirstActiveFieldLine = -1;
    qint32 originalLastActiveFieldLine = -1;
    qint32 originalFirstActiveFrameLine = -1;
    qint32 originalLastActiveFrameLine = -1;
};

#endif // VIDEOPARAMETERSDIALOG_H
