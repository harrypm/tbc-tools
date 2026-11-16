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
#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QLabel>
#include <QSlider>
#include <QSpinBox>
#include <QGraphicsPixmapItem>
#include <QDialog>
#include <QRect>
#include <QRadioButton>
#include <QScrollArea>
#include <QTabWidget>
#include <QImage>

#include "componentframe.h"
#include "composite_vectorscope.h"
#include "tbcmetadata.h"
#include "tbcsource.h"
#include "vectorscope_data.h"

#include <optional>

namespace Ui {
class VectorscopeDialog;
}
class QResizeEvent;

class VectorscopeDialog : public QDialog
{
    Q_OBJECT

public:
    explicit VectorscopeDialog(QWidget *parent = nullptr);
    ~VectorscopeDialog() override;

    void showTraceImage(const ComponentFrame &componentFrame, const TbcMetaData::VideoParameters &videoParameters,
                        const TbcSource::ViewMode& viewMode, const bool isFirstField);

    // Advanced (composite, measurement) acquisition tab: MainWindow assembles
    // the frame, runs the engine with the dialog's options and hands the
    // result back here for rendering.
    void showCompositeTrace(const std::optional<tbc::cvbs::VectorscopeData> &compositeData,
                            const TbcMetaData::VideoParameters &videoParameters,
                            const TbcSource::ViewMode& viewMode, const bool isFirstField);
    bool setAdvancedTabSelected(bool selected);
    bool isAdvancedTabSelected() const;
    tbc::cvbs::CompositeVectorscopeOptions compositeOptions() const;
    void setCompositeGeometry(qint32 frameLines);
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
    void renderModeComboBoxChanged(int index);
    void on_areaModeButtonGroup_buttonClicked(QAbstractButton *button);

    // Advanced (composite) tab controls.  Deliberately not named on_*:
    // these widgets are created in code, and an on_<object>_<signal> name
    // would also be picked up by connectSlotsByName and fire twice.
    void lineViewComboBoxChanged(int index);
    void fieldViewComboBoxChanged(int index);
    void fieldViewEndEnabledCheckBoxClicked();
    void compositeLineSpinBoxChanged();
    void traceLinesCheckBoxClicked();

private:
    Ui::VectorscopeDialog *ui;

    QImage getTraceImage(const ComponentFrame &componentFrame, const TbcMetaData::VideoParameters &videoParameters);
    QVector<double> graticuleBarPercents() const;
    QImage getCompositeTraceImage(const tbc::cvbs::VectorscopeData &compositeData,
                                  const TbcMetaData::VideoParameters &videoParameters);
    void updateFieldSelectionControls(const TbcSource::ViewMode& viewMode, const bool isFirstField);
    void updateScopeLabelPixmap();
    void updateAdvancedScopePixmap();
    void initialiseAdvancedControls();
    void setupAdvancedScopeTab();
    void updateAreaControlState(const ComponentFrame &componentFrame, const TbcMetaData::VideoParameters &videoParameters);
    void applyAreaPreset();
    void updateMeasurementReadout();
    QImage cachedTraceImage;
    QImage cachedCompositeImage;

    QComboBox *renderModeComboBox = nullptr;  // 0 = point plot, 1 = density plot
    QWidget *renderModePanel = nullptr;
    QWidget *basicIntensityPanel = nullptr;
    QSlider *basicIntensitySlider = nullptr;  // 1..10, 5 = original look
    QButtonGroup *areaModeButtonGroup = nullptr;
    QGroupBox *areaModeGroupBox = nullptr;
    QRadioButton *areaModeActiveRadioButton = nullptr;
    QRadioButton *areaModeFullRadioButton = nullptr;
    QRadioButton *areaModeCustomRadioButton = nullptr;

    // Advanced (composite) scope tab - mirrors the oscilloscope dialog's
    // Basic/Advanced tab structure so the standard scope page stays exactly
    // as it was.
    QTabWidget *scopeTabWidget = nullptr;
    QWidget *scopeOriginalTab = nullptr;
    QWidget *scopeAdvancedTab = nullptr;
    QLabel *advancedScopeLabel = nullptr;
    QLabel *advancedTitleLabel = nullptr;
    QWidget *advancedSidePanel = nullptr;
    QScrollArea *advancedScrollArea = nullptr;
    QComboBox *lineViewComboBox = nullptr;
    QComboBox *fieldViewComboBox = nullptr;
    QSpinBox *fieldViewStartSpinBox = nullptr;
    QSpinBox *fieldViewEndSpinBox = nullptr;
    QCheckBox *fieldViewEndEnabledCheckBox = nullptr;
    QCheckBox *traceLinesCheckBox = nullptr;
    QCheckBox *lineAveragingCheckBox = nullptr;
    QSlider *traceIntensitySlider = nullptr;  // 1..10, default 2
    QLabel *measurementLabel = nullptr;
    QLabel *acquisitionInfoLabel = nullptr;
    QLabel *acquisitionNoteLabel = nullptr;
    std::optional<tbc::cvbs::VectorscopeData> compositeData;
    TbcMetaData::VideoParameters compositeVideoParameters;
    bool haveCompositeRenderContext = false;
    qint32 compositeFrameLines = 0;

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
