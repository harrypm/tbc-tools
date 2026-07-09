/******************************************************************************
 * videoparametersdialog.cpp
 * ld-analyse - TBC output analysis GUI
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2022 Adam Sampson
 *
 * This file is part of ld-decode-tools.
 ******************************************************************************/

#include "videoparametersdialog.h"
#include "ui_videoparametersdialog.h"
#include <QEvent>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QString>
#include <QWheelEvent>

namespace {
struct LineDefaults {
    qint32 minActiveFrameLine;
    qint32 firstActiveFieldLine;
    qint32 lastActiveFieldLine;
    qint32 firstActiveFrameLine;
    qint32 lastActiveFrameLine;
};

LineDefaults lineDefaultsForSystem(VideoSystem system)
{
    switch (system) {
    case PAL:
        return {2, 22, 308, 44, 620};
    case PAL_M:
        return {1, 20, 263, 40, 525};
    case NTSC:
    default:
        return {1, 20, 263, 40, 525};
    }
}
} // namespace

VideoParametersDialog::VideoParametersDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::VideoParametersDialog)
{
    ui->setupUi(this);
    setWindowFlags(Qt::Window);

    const QList<QObject *> wheelTargets = {
        ui->activeVideoStartSpinBox,
        ui->activeVideoWidthSpinBox,
        ui->activeVideoStartHorizontalSlider,
        ui->activeVideoWidthHorizontalSlider,
        ui->firstActiveFieldLineSpinBox,
        ui->lastActiveFieldLineSpinBox,
        ui->firstActiveFrameLineSpinBox,
        ui->lastActiveFrameLineSpinBox,
        ui->firstActiveFieldLineHorizontalSlider,
        ui->lastActiveFieldLineHorizontalSlider,
        ui->firstActiveFrameLineHorizontalSlider,
        ui->lastActiveFrameLineHorizontalSlider,
        ui->exportBoundaryThicknessSpinBox,
        ui->exportBoundaryThicknessHorizontalSlider
    };
    for (QObject *target : wheelTargets) {
        if (target) {
            target->installEventFilter(this);
        }
    }
}

void VideoParametersDialog::setShowExportBoundary(bool enabled)
{
    if (!ui->exportBoundaryCheckBox) {
        return;
    }

    ui->exportBoundaryCheckBox->blockSignals(true);
    ui->exportBoundaryCheckBox->setChecked(enabled);
    ui->exportBoundaryCheckBox->blockSignals(false);
}

void VideoParametersDialog::setExportBoundaryThickness(int thickness)
{
    if (!ui->exportBoundaryThicknessSpinBox || !ui->exportBoundaryThicknessHorizontalSlider) {
        return;
    }

    const int clampedThickness = (thickness < 1) ? 1 : ((thickness > 8) ? 8 : thickness);
    QSignalBlocker spinBlocker(ui->exportBoundaryThicknessSpinBox);
    QSignalBlocker sliderBlocker(ui->exportBoundaryThicknessHorizontalSlider);
    ui->exportBoundaryThicknessSpinBox->setValue(clampedThickness);
    ui->exportBoundaryThicknessHorizontalSlider->setValue(clampedThickness);
}

VideoParametersDialog::~VideoParametersDialog()
{
    delete ui;
}
bool VideoParametersDialog::eventFilter(QObject *object, QEvent *event)
{
    if (event->type() == QEvent::Wheel) {
        auto *wheelEvent = static_cast<QWheelEvent *>(event);
        const int delta = wheelEvent->angleDelta().y();
        if (delta == 0) {
            return true;
        }
        const int step = (delta > 0) ? 1 : -1;
        if (auto *slider = qobject_cast<QSlider *>(object)) {
            slider->setValue(slider->value() + step);
            return true;
        }
        if (auto *spinBox = qobject_cast<QSpinBox *>(object)) {
            spinBox->setValue(spinBox->value() + step);
            return true;
        }
    }

    return QDialog::eventFilter(object, event);
}

void VideoParametersDialog::updateResultingFrameSizeLabel()
{
    const qint32 width = videoParameters.activeVideoEnd - videoParameters.activeVideoStart;
    const qint32 height = videoParameters.lastActiveFrameLine - videoParameters.firstActiveFrameLine;
    ui->resultingFrameSizeValueLabel->setText(QString("%1 x %2").arg(width).arg(height));
}

void VideoParametersDialog::setVideoParameters(const LdDecodeMetaData::VideoParameters &_videoParameters)
{
    videoParameters = _videoParameters;
    originalActiveVideoStart = videoParameters.activeVideoStart;
    originalActiveVideoWidth = videoParameters.activeVideoEnd - videoParameters.activeVideoStart;
    originalFirstActiveFieldLine = videoParameters.firstActiveFieldLine;
    originalLastActiveFieldLine = videoParameters.lastActiveFieldLine;
    originalFirstActiveFrameLine = videoParameters.firstActiveFrameLine;
    originalLastActiveFrameLine = videoParameters.lastActiveFrameLine;

    // Block signals while updating the UI to prevent spurious videoParametersChanged emissions
    ui->activeVideoWidthSpinBox->blockSignals(true);
    ui->activeVideoStartSpinBox->blockSignals(true);
    ui->activeVideoStartHorizontalSlider->blockSignals(true);
    ui->activeVideoWidthHorizontalSlider->blockSignals(true);
    ui->firstActiveFieldLineSpinBox->blockSignals(true);
    ui->lastActiveFieldLineSpinBox->blockSignals(true);
    ui->firstActiveFrameLineSpinBox->blockSignals(true);
    ui->lastActiveFrameLineSpinBox->blockSignals(true);
    ui->firstActiveFieldLineHorizontalSlider->blockSignals(true);
    ui->lastActiveFieldLineHorizontalSlider->blockSignals(true);
    ui->firstActiveFrameLineHorizontalSlider->blockSignals(true);
    ui->lastActiveFrameLineHorizontalSlider->blockSignals(true);
    ui->aspectRatio169RadioButton->blockSignals(true);
    ui->aspectRatio43RadioButton->blockSignals(true);

    // Transfer values to the dialogue
    const LineDefaults defaults = lineDefaultsForSystem(videoParameters.system);
    const qint32 activeWidth = videoParameters.activeVideoEnd - videoParameters.activeVideoStart;
    const qint32 firstFieldLine = (videoParameters.firstActiveFieldLine > 0)
                                      ? videoParameters.firstActiveFieldLine
                                      : defaults.firstActiveFieldLine;
    const qint32 lastFieldLine = (videoParameters.lastActiveFieldLine > 0)
                                     ? videoParameters.lastActiveFieldLine
                                     : defaults.lastActiveFieldLine;
    const qint32 firstFrameLine = (videoParameters.firstActiveFrameLine > 0)
                                      ? videoParameters.firstActiveFrameLine
                                      : defaults.firstActiveFrameLine;
    const qint32 lastFrameLine = (videoParameters.lastActiveFrameLine > 0)
                                     ? videoParameters.lastActiveFrameLine
                                     : defaults.lastActiveFrameLine;

    ui->activeVideoStartSpinBox->setMinimum(videoParameters.colourBurstEnd);
    ui->activeVideoStartSpinBox->setMaximum(videoParameters.fieldWidth - 1);
    ui->activeVideoStartHorizontalSlider->setMinimum(videoParameters.colourBurstEnd);
    ui->activeVideoStartHorizontalSlider->setMaximum(videoParameters.fieldWidth - 1);
    ui->activeVideoStartSpinBox->setValue(videoParameters.activeVideoStart);
    ui->activeVideoStartHorizontalSlider->setValue(videoParameters.activeVideoStart);

    const qint32 maxWidth = videoParameters.fieldWidth - videoParameters.activeVideoStart;
    ui->activeVideoWidthSpinBox->setMinimum(1);
    ui->activeVideoWidthSpinBox->setMaximum(maxWidth);
    ui->activeVideoWidthHorizontalSlider->setMinimum(1);
    ui->activeVideoWidthHorizontalSlider->setMaximum(maxWidth);
    ui->activeVideoWidthSpinBox->setValue(activeWidth);
    ui->activeVideoWidthHorizontalSlider->setValue(activeWidth);

    ui->firstActiveFieldLineSpinBox->setMinimum(1);
    ui->firstActiveFieldLineSpinBox->setMaximum(defaults.lastActiveFieldLine);
    ui->lastActiveFieldLineSpinBox->setMinimum(1);
    ui->lastActiveFieldLineSpinBox->setMaximum(defaults.lastActiveFieldLine);
    ui->firstActiveFieldLineHorizontalSlider->setMinimum(1);
    ui->firstActiveFieldLineHorizontalSlider->setMaximum(defaults.lastActiveFieldLine);
    ui->lastActiveFieldLineHorizontalSlider->setMinimum(1);
    ui->lastActiveFieldLineHorizontalSlider->setMaximum(defaults.lastActiveFieldLine);
    ui->firstActiveFieldLineSpinBox->setValue(firstFieldLine);
    ui->firstActiveFieldLineHorizontalSlider->setValue(firstFieldLine);
    ui->lastActiveFieldLineSpinBox->setValue(lastFieldLine);
    ui->lastActiveFieldLineHorizontalSlider->setValue(lastFieldLine);

    ui->firstActiveFrameLineSpinBox->setMinimum(defaults.minActiveFrameLine);
    ui->firstActiveFrameLineSpinBox->setMaximum(defaults.lastActiveFrameLine);
    ui->lastActiveFrameLineSpinBox->setMinimum(defaults.minActiveFrameLine);
    ui->lastActiveFrameLineSpinBox->setMaximum(defaults.lastActiveFrameLine);
    ui->firstActiveFrameLineHorizontalSlider->setMinimum(defaults.minActiveFrameLine);
    ui->firstActiveFrameLineHorizontalSlider->setMaximum(defaults.lastActiveFrameLine);
    ui->lastActiveFrameLineHorizontalSlider->setMinimum(defaults.minActiveFrameLine);
    ui->lastActiveFrameLineHorizontalSlider->setMaximum(defaults.lastActiveFrameLine);
    ui->firstActiveFrameLineSpinBox->setValue(firstFrameLine);
    ui->firstActiveFrameLineHorizontalSlider->setValue(firstFrameLine);
    ui->lastActiveFrameLineSpinBox->setValue(lastFrameLine);
    ui->lastActiveFrameLineHorizontalSlider->setValue(lastFrameLine);

    if (videoParameters.isWidescreen) ui->aspectRatio169RadioButton->setChecked(true);
    else ui->aspectRatio43RadioButton->setChecked(true);

    updateResultingFrameSizeLabel();

    // Unblock signals
    ui->activeVideoWidthSpinBox->blockSignals(false);
    ui->activeVideoStartSpinBox->blockSignals(false);
    ui->activeVideoStartHorizontalSlider->blockSignals(false);
    ui->activeVideoWidthHorizontalSlider->blockSignals(false);
    ui->firstActiveFieldLineSpinBox->blockSignals(false);
    ui->lastActiveFieldLineSpinBox->blockSignals(false);
    ui->firstActiveFrameLineSpinBox->blockSignals(false);
    ui->lastActiveFrameLineSpinBox->blockSignals(false);
    ui->firstActiveFieldLineHorizontalSlider->blockSignals(false);
    ui->lastActiveFieldLineHorizontalSlider->blockSignals(false);
    ui->firstActiveFrameLineHorizontalSlider->blockSignals(false);
    ui->lastActiveFrameLineHorizontalSlider->blockSignals(false);
    ui->aspectRatio169RadioButton->blockSignals(false);
    ui->aspectRatio43RadioButton->blockSignals(false);
}

// Private slots


void VideoParametersDialog::on_activeVideoStartSpinBox_valueChanged(int value)
{
    videoParameters.activeVideoStart = value;
    // prevent the width from going over the actual field width
    const qint32 maxWidth = videoParameters.fieldWidth - value;
    ui->activeVideoWidthSpinBox->setMaximum(maxWidth);
    ui->activeVideoWidthHorizontalSlider->setMaximum(maxWidth);
    if (ui->activeVideoWidthSpinBox->value() > maxWidth) {
        ui->activeVideoWidthSpinBox->setValue(maxWidth);
    }
    if (ui->activeVideoWidthHorizontalSlider->value() > maxWidth) {
        ui->activeVideoWidthHorizontalSlider->setValue(maxWidth);
    }
    videoParameters.activeVideoEnd = value + ui->activeVideoWidthSpinBox->value();
    if (ui->activeVideoStartHorizontalSlider->value() != value) {
        QSignalBlocker blocker(ui->activeVideoStartHorizontalSlider);
        ui->activeVideoStartHorizontalSlider->setValue(value);
    }
    updateResultingFrameSizeLabel();
    emit videoParametersChanged(videoParameters);
}

void VideoParametersDialog::on_activeVideoWidthSpinBox_valueChanged(int value)
{
    videoParameters.activeVideoEnd = videoParameters.activeVideoStart + value;
    if (ui->activeVideoWidthHorizontalSlider->value() != value) {
        QSignalBlocker blocker(ui->activeVideoWidthHorizontalSlider);
        ui->activeVideoWidthHorizontalSlider->setValue(value);
    }
    updateResultingFrameSizeLabel();
    emit videoParametersChanged(videoParameters);
}

void VideoParametersDialog::on_activeVideoStartResetButton_clicked()
{
    ui->activeVideoStartSpinBox->setValue(originalActiveVideoStart);
}

void VideoParametersDialog::on_activeVideoWidthResetButton_clicked()
{
    ui->activeVideoWidthSpinBox->setValue(originalActiveVideoWidth);
}
void VideoParametersDialog::on_activeVideoStartHorizontalSlider_valueChanged(int value)
{
    if (ui->activeVideoStartSpinBox->value() != value) {
        QSignalBlocker blocker(ui->activeVideoStartSpinBox);
        ui->activeVideoStartSpinBox->setValue(value);
    }
    on_activeVideoStartSpinBox_valueChanged(value);
}

void VideoParametersDialog::on_activeVideoWidthHorizontalSlider_valueChanged(int value)
{
    if (ui->activeVideoWidthSpinBox->value() != value) {
        QSignalBlocker blocker(ui->activeVideoWidthSpinBox);
        ui->activeVideoWidthSpinBox->setValue(value);
    }
    on_activeVideoWidthSpinBox_valueChanged(value);
}

void VideoParametersDialog::on_firstActiveFieldLineSpinBox_valueChanged(int value)
{
    if (ui->lastActiveFieldLineSpinBox->value() < value) {
        ui->lastActiveFieldLineSpinBox->setValue(value);
    }
    videoParameters.firstActiveFieldLine = value;
    if (ui->firstActiveFieldLineHorizontalSlider->value() != value) {
        QSignalBlocker blocker(ui->firstActiveFieldLineHorizontalSlider);
        ui->firstActiveFieldLineHorizontalSlider->setValue(value);
    }
    emit videoParametersChanged(videoParameters);
}

void VideoParametersDialog::on_lastActiveFieldLineSpinBox_valueChanged(int value)
{
    if (ui->firstActiveFieldLineSpinBox->value() > value) {
        ui->firstActiveFieldLineSpinBox->setValue(value);
    }
    videoParameters.lastActiveFieldLine = value;
    if (ui->lastActiveFieldLineHorizontalSlider->value() != value) {
        QSignalBlocker blocker(ui->lastActiveFieldLineHorizontalSlider);
        ui->lastActiveFieldLineHorizontalSlider->setValue(value);
    }
    emit videoParametersChanged(videoParameters);
}

void VideoParametersDialog::on_firstActiveFrameLineSpinBox_valueChanged(int value)
{
    if (ui->lastActiveFrameLineSpinBox->value() < value) {
        ui->lastActiveFrameLineSpinBox->setValue(value);
    }
    videoParameters.firstActiveFrameLine = value;
    if (ui->firstActiveFrameLineHorizontalSlider->value() != value) {
        QSignalBlocker blocker(ui->firstActiveFrameLineHorizontalSlider);
        ui->firstActiveFrameLineHorizontalSlider->setValue(value);
    }
    updateResultingFrameSizeLabel();
    emit videoParametersChanged(videoParameters);
}

void VideoParametersDialog::on_lastActiveFrameLineSpinBox_valueChanged(int value)
{
    if (ui->firstActiveFrameLineSpinBox->value() > value) {
        ui->firstActiveFrameLineSpinBox->setValue(value);
    }
    videoParameters.lastActiveFrameLine = value;
    if (ui->lastActiveFrameLineHorizontalSlider->value() != value) {
        QSignalBlocker blocker(ui->lastActiveFrameLineHorizontalSlider);
        ui->lastActiveFrameLineHorizontalSlider->setValue(value);
    }
    updateResultingFrameSizeLabel();
    emit videoParametersChanged(videoParameters);
}

void VideoParametersDialog::on_firstActiveFieldLineHorizontalSlider_valueChanged(int value)
{
    if (ui->firstActiveFieldLineSpinBox->value() != value) {
        QSignalBlocker blocker(ui->firstActiveFieldLineSpinBox);
        ui->firstActiveFieldLineSpinBox->setValue(value);
    }
    on_firstActiveFieldLineSpinBox_valueChanged(value);
}

void VideoParametersDialog::on_lastActiveFieldLineHorizontalSlider_valueChanged(int value)
{
    if (ui->lastActiveFieldLineSpinBox->value() != value) {
        QSignalBlocker blocker(ui->lastActiveFieldLineSpinBox);
        ui->lastActiveFieldLineSpinBox->setValue(value);
    }
    on_lastActiveFieldLineSpinBox_valueChanged(value);
}

void VideoParametersDialog::on_firstActiveFrameLineHorizontalSlider_valueChanged(int value)
{
    if (ui->firstActiveFrameLineSpinBox->value() != value) {
        QSignalBlocker blocker(ui->firstActiveFrameLineSpinBox);
        ui->firstActiveFrameLineSpinBox->setValue(value);
    }
    on_firstActiveFrameLineSpinBox_valueChanged(value);
}

void VideoParametersDialog::on_lastActiveFrameLineHorizontalSlider_valueChanged(int value)
{
    if (ui->lastActiveFrameLineSpinBox->value() != value) {
        QSignalBlocker blocker(ui->lastActiveFrameLineSpinBox);
        ui->lastActiveFrameLineSpinBox->setValue(value);
    }
    on_lastActiveFrameLineSpinBox_valueChanged(value);
}

void VideoParametersDialog::on_firstActiveFieldLineResetButton_clicked()
{
    ui->firstActiveFieldLineSpinBox->setValue(originalFirstActiveFieldLine);
}

void VideoParametersDialog::on_lastActiveFieldLineResetButton_clicked()
{
    ui->lastActiveFieldLineSpinBox->setValue(originalLastActiveFieldLine);
}

void VideoParametersDialog::on_firstActiveFrameLineResetButton_clicked()
{
    ui->firstActiveFrameLineSpinBox->setValue(originalFirstActiveFrameLine);
}

void VideoParametersDialog::on_lastActiveFrameLineResetButton_clicked()
{
    ui->lastActiveFrameLineSpinBox->setValue(originalLastActiveFrameLine);
}

void VideoParametersDialog::on_aspectRatioButtonGroup_buttonClicked(QAbstractButton *button)
{
    videoParameters.isWidescreen = (button == ui->aspectRatio169RadioButton);
    emit videoParametersChanged(videoParameters);
}

void VideoParametersDialog::on_exportBoundaryCheckBox_toggled(bool checked)
{
    emit exportBoundaryToggled(checked);
}

void VideoParametersDialog::on_exportBoundaryThicknessSpinBox_valueChanged(int value)
{
    if (ui->exportBoundaryThicknessHorizontalSlider->value() != value) {
        QSignalBlocker blocker(ui->exportBoundaryThicknessHorizontalSlider);
        ui->exportBoundaryThicknessHorizontalSlider->setValue(value);
    }
    emit exportBoundaryThicknessChanged(value);
}

void VideoParametersDialog::on_exportBoundaryThicknessHorizontalSlider_valueChanged(int value)
{
    if (ui->exportBoundaryThicknessSpinBox->value() != value) {
        QSignalBlocker blocker(ui->exportBoundaryThicknessSpinBox);
        ui->exportBoundaryThicknessSpinBox->setValue(value);
    }
    on_exportBoundaryThicknessSpinBox_valueChanged(value);
}
