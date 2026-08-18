/******************************************************************************
 * vectorscopedialog.h
 * tbc-analyse - TBC output analysis GUI
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2022 Adam Sampson
 *
 * This file is part of tbc-tools.
 ******************************************************************************/

#ifndef VECTORSCOPEDIALOG_H
#define VECTORSCOPEDIALOG_H

#include <QAbstractButton>
#include <QButtonGroup>
#include <QGraphicsPixmapItem>
#include <QDialog>
#include <QRect>
#include <QRadioButton>
#include <QImage>

#include "componentframe.h"
#include "lddecodemetadata.h"
#include "tbcsource.h"

namespace Ui {
class VectorscopeDialog;
}
class QResizeEvent;

class VectorscopeDialog : public QDialog
{
    Q_OBJECT

public:
    explicit VectorscopeDialog(QWidget *parent = nullptr);
    ~VectorscopeDialog();

    void showTraceImage(const ComponentFrame &componentFrame, const LdDecodeMetaData::VideoParameters &videoParameters,
                        const TbcSource::ViewMode& viewMode, const bool isFirstField);
    bool isCustomAreaModeSelected() const;
    QRect customAreaRect() const;
    void setCustomAreaModeSelected(bool selected);
    void setCustomAreaRect(const QRect &areaRect);
    bool setAdvancedRenderModeSelected(bool selected);
    bool isAdvancedRenderModeSelected() const;
    bool setFullAreaModeSelected(bool selected);
    bool isFullAreaModeSelected() const;

protected:
    void resizeEvent(QResizeEvent *event) override;

signals:
    void scopeChanged();

private slots:
    void on_defocusCheckBox_clicked();
    void on_blendColorCheckBox_clicked();
    void on_multiColourCheckBox_clicked();
    void on_graticuleButtonGroup_buttonClicked(QAbstractButton *button);
    void on_fieldSelectButtonGroup_buttonClicked(QAbstractButton *button);
    void on_renderModeButtonGroup_buttonClicked(QAbstractButton *button);
    void on_areaModeButtonGroup_buttonClicked(QAbstractButton *button);

private:
    Ui::VectorscopeDialog *ui;

    QImage getTraceImage(const ComponentFrame &componentFrame, const LdDecodeMetaData::VideoParameters &videoParameters);
    void updateScopeLabelPixmap();
    void initialiseAdvancedControls();
    void updateAreaControlState(const ComponentFrame &componentFrame, const LdDecodeMetaData::VideoParameters &videoParameters);
    void applyAreaPreset();
    QImage cachedTraceImage;

    QButtonGroup *renderModeButtonGroup = nullptr;
    QRadioButton *renderModePointsRadioButton = nullptr;
    QRadioButton *renderModeDensityRadioButton = nullptr;
    QButtonGroup *areaModeButtonGroup = nullptr;
    QRadioButton *areaModeActiveRadioButton = nullptr;
    QRadioButton *areaModeFullRadioButton = nullptr;
    QRadioButton *areaModeCustomRadioButton = nullptr;

    bool hasAreaContext = false;
    qint32 areaFrameWidth = 0;
    qint32 areaFrameHeight = 0;
    qint32 activeXStart = 0;
    qint32 activeXEnd = 0;
    qint32 activeYStart = 0;
    qint32 activeYEnd = 0;
    qint32 customXStart = 0;
    qint32 customXEnd = 0;
    qint32 customYStart = 0;
    qint32 customYEnd = 0;
};

#endif // VECTORSCOPEDIALOG_H
