/******************************************************************************
 * oscilloscopedialog.h
 * tbc-analyse - TBC output analysis GUI
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2018-2025 Simon Inns
 *
 * This file is part of tbc-tools.
 ******************************************************************************/

#ifndef OSCILLOSCOPEDIALOG_H
#define OSCILLOSCOPEDIALOG_H

#include <QDialog>
#include <QGraphicsPixmapItem>
#include <QPainter>
#include <QDebug>
#include <QMouseEvent>
#include <QTabWidget>
#include <QLabel>
#include <QPushButton>

#include "tbcsource.h"
#include "plotwidget.h"

namespace Ui {
class OscilloscopeDialog;
}

class OscilloscopeDialog : public QDialog
{
    Q_OBJECT

public:
    explicit OscilloscopeDialog(QWidget *parent = nullptr);
    ~OscilloscopeDialog();

    void showTraceImage(TbcSource::ScanLineData scanLineData, qint32 xCoord, qint32 yCoord, qint32 frameWidth, qint32 frameHeight, bool bothSources);
    bool setAdvancedTabSelected(bool selected);
    bool isAdvancedTabSelected() const;
    void setVisible(bool visible) override;

signals:
    void scopeCoordsChanged(qint32 xCoord, qint32 yCoord);
    void scopeLevelSelect(qint32 value);

private slots:
    void on_previousPushButton_clicked();
    void on_nextPushButton_clicked();
    void on_fieldToggleButton_clicked();
    void on_xCoordSpinBox_valueChanged(int arg1);
    void on_yCoordSpinBox_valueChanged(int arg1);
    void on_YCcheckBox_clicked();
    void on_YcheckBox_clicked();
    void on_CcheckBox_clicked();
    void on_dropoutsCheckBox_clicked();

    void mousePressEvent(QMouseEvent *event);
    void mouseMoveEvent(QMouseEvent *event);
    void resizeEvent(QResizeEvent *event);

private:
    Ui::OscilloscopeDialog *ui;
    qint32 maximumX;
    qint32 maximumY;
    qint32 scopeWidth;
    qint32 fieldWidth;  // Original field width in samples
    qint32 lastScopeX;
    qint32 lastScopeY;
    
    // Cache the last scan line data for regenerating on resize
    TbcSource::ScanLineData cachedScanLineData;
    bool hasCachedData;
    bool bothSourcesMode;
    qint32 cachedPictureDot;
    
    // Advanced decode-orc style tab (keeps original scope intact)
    QTabWidget *scopeTabWidget;
    QWidget *scopeOriginalTab;
    QWidget *scopeAdvancedTab;
    PlotWidget *advancedPlotWidget;
    PlotSeries *advancedCompositeSeries;
    PlotSeries *advancedYSeries;
    PlotSeries *advancedCSeries;
    PlotMarker *advancedSampleMarker;
    QLabel *advancedSampleInfoLabel;
    QWidget *advancedSidePanel;
    QPushButton *fieldToggleButton;
    bool firstShowDone = false;

    QImage getFieldLineTraceImage(TbcSource::ScanLineData scanLineData, qint32 pictureDot, bool bothSources, qint32 scopeHeight, qint32 scopeWidth);
    void setupAdvancedScopeTab();
    void updateAdvancedScope(const TbcSource::ScanLineData &scanLineData, qint32 pictureDot, bool bothSources);
    void updateAdvancedSampleMarker(qint32 pictureDot);
    double sampleToMillivolts(qint32 sampleValue, const TbcSource::ScanLineData &scanLineData) const;
    double sampleToIre(qint32 sampleValue, const TbcSource::ScanLineData &scanLineData) const;
    void updateAdvancedInfoLabel(qint32 pictureDot, const TbcSource::ScanLineData &scanLineData);
    void mouseLevelSelect(qint32 oY);
    void mousePictureDotSelect(qint32 oX);
};

#endif // OSCILLOSCOPEDIALOG_H
