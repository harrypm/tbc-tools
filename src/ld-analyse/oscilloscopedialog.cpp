/******************************************************************************
 * oscilloscopedialog.cpp
 * tbc-analyse - TBC output analysis GUI
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2018-2025 Simon Inns
 *
 * This file is part of tbc-tools.
 ******************************************************************************/

#include "oscilloscopedialog.h"
#include "ui_oscilloscopedialog.h"
#include "tbc/logging.h"

#include <cassert>
#include <QVBoxLayout>
#include <QSignalBlocker>
#include <QtMath>
#include <limits>

OscilloscopeDialog::OscilloscopeDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::OscilloscopeDialog)
{
    ui->setupUi(this);
    setWindowFlags(Qt::Window);

    maximumX = 1135;
    maximumY = 625;
    lastScopeX = 0;
    lastScopeY = 0;
    scopeWidth = 0;
    fieldWidth = 0;
    hasCachedData = false;
    bothSourcesMode = false;
    cachedPictureDot = 0;
    scopeTabWidget = nullptr;
    scopeOriginalTab = nullptr;
    scopeAdvancedTab = nullptr;
    advancedPlotWidget = nullptr;
    advancedCompositeSeries = nullptr;
    advancedYSeries = nullptr;
    advancedCSeries = nullptr;
    advancedSampleMarker = nullptr;
    advancedSampleInfoLabel = nullptr;
    advancedSidePanel = nullptr;
    fieldToggleButton = nullptr;

    // Configure the GUI
    ui->xCoordSpinBox->setMinimum(0);
    ui->xCoordSpinBox->setMaximum(maximumX - 1);
    ui->yCoordSpinBox->setMinimum(0);
    ui->yCoordSpinBox->setMaximum(maximumY - 1);

    ui->previousPushButton->setText(tr("Line Up"));
    ui->previousPushButton->setToolTip(tr("Move to previous line"));
    ui->previousPushButton->setAutoRepeat(true);
    ui->previousPushButton->setAutoRepeatInterval(50);

    ui->nextPushButton->setText(tr("Line Down"));
    ui->nextPushButton->setToolTip(tr("Move to next line"));
    ui->nextPushButton->setAutoRepeat(true);
    ui->nextPushButton->setAutoRepeatInterval(50);

    ui->previousPushButton->setFocusPolicy(Qt::NoFocus);
    ui->nextPushButton->setFocusPolicy(Qt::NoFocus);

    setupAdvancedScopeTab();
}

OscilloscopeDialog::~OscilloscopeDialog()
{
    delete ui;
}

bool OscilloscopeDialog::setAdvancedTabSelected(bool selected)
{
    if (!scopeTabWidget || !scopeOriginalTab || !scopeAdvancedTab) {
        return false;
    }

    QWidget *targetTab = selected ? scopeAdvancedTab : scopeOriginalTab;
    if (scopeTabWidget->currentWidget() != targetTab) {
        scopeTabWidget->setCurrentWidget(targetTab);
    }
    return scopeTabWidget->currentWidget() == targetTab;
}

bool OscilloscopeDialog::isAdvancedTabSelected() const
{
    if (!scopeTabWidget || !scopeAdvancedTab) {
        return false;
    }
    return scopeTabWidget->currentWidget() == scopeAdvancedTab;
}

void OscilloscopeDialog::setupAdvancedScopeTab()
{
    if (!ui || !ui->horizontalLayout || !ui->scopeLabel) {
        return;
    }

    scopeTabWidget = new QTabWidget(this);
    scopeOriginalTab = new QWidget(scopeTabWidget);
    scopeAdvancedTab = new QWidget(scopeTabWidget);

    auto *originalLayout = new QVBoxLayout(scopeOriginalTab);
    originalLayout->setContentsMargins(0, 0, 0, 0);
    originalLayout->setSpacing(0);

    auto *advancedLayout = new QVBoxLayout(scopeAdvancedTab);
    advancedLayout->setContentsMargins(0, 0, 0, 0);
    advancedLayout->setSpacing(0);

    ui->horizontalLayout->removeWidget(ui->scopeLabel);
    ui->scopeLabel->setParent(scopeOriginalTab);
    originalLayout->addWidget(ui->scopeLabel);

    advancedPlotWidget = new PlotWidget(scopeAdvancedTab);
    advancedPlotWidget->setAxisTitle(Qt::Horizontal, tr("Time (µs)"));
    advancedPlotWidget->setAxisTitle(Qt::Vertical, tr("mV (millivolts)"));
    advancedPlotWidget->setGridEnabled(true);
    advancedPlotWidget->setLegendEnabled(true);
    advancedPlotWidget->setZoomEnabled(true);
    advancedPlotWidget->setPanEnabled(true);
    advancedPlotWidget->setYAxisIntegerLabels(false);
    advancedLayout->addWidget(advancedPlotWidget, 1);

    advancedSidePanel = new QWidget(ui->frame);
    auto *advancedControls = new QVBoxLayout(advancedSidePanel);
    advancedControls->setContentsMargins(0, 0, 0, 0);
    advancedControls->setSpacing(6);
    advancedSampleInfoLabel = new QLabel(advancedSidePanel);
    advancedSampleInfoLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    if (ui->standardLineLabel) {
        advancedSampleInfoLabel->setFont(ui->standardLineLabel->font());
    } else if (ui->fieldLineLabel) {
        advancedSampleInfoLabel->setFont(ui->fieldLineLabel->font());
    }
    advancedControls->addWidget(advancedSampleInfoLabel, 1);
    advancedControls->addStretch();

    if (ui->verticalLayout) {
        if (!fieldToggleButton) {
            fieldToggleButton = new QPushButton(tr("Field 1/2"), ui->frame);
            fieldToggleButton->setToolTip(tr("Toggle field 1/2"));
            if (ui->previousPushButton) {
                fieldToggleButton->setSizePolicy(ui->previousPushButton->sizePolicy());
                fieldToggleButton->setMinimumSize(ui->previousPushButton->minimumSize());
                fieldToggleButton->setMaximumSize(ui->previousPushButton->maximumSize());
            }
            fieldToggleButton->setFocusPolicy(Qt::NoFocus);
            connect(fieldToggleButton, &QPushButton::clicked, this, &OscilloscopeDialog::on_fieldToggleButton_clicked);
            ui->verticalLayout->insertWidget(2, fieldToggleButton);
        }
        if (ui->verticalSpacer) {
            ui->verticalLayout->removeItem(ui->verticalSpacer);
        }
        ui->verticalLayout->addWidget(advancedSidePanel);
        if (ui->verticalSpacer) {
            ui->verticalLayout->addItem(ui->verticalSpacer);
        }
    }

    scopeTabWidget->addTab(scopeOriginalTab, tr("Basic"));
    scopeTabWidget->addTab(scopeAdvancedTab, tr("Advanced"));
    ui->horizontalLayout->insertWidget(0, scopeTabWidget, 1);

    const auto updateDropoutVisibility = [this](int index) {
        if (!ui || !ui->dropoutsCheckBox) {
            return;
        }
        const bool isAdvanced = (scopeTabWidget && scopeTabWidget->widget(index) == scopeAdvancedTab);
        ui->dropoutsCheckBox->setVisible(!isAdvanced);
        if (advancedSidePanel) {
            advancedSidePanel->setVisible(isAdvanced);
        }
    };
    connect(scopeTabWidget, &QTabWidget::currentChanged, this, updateDropoutVisibility);
    updateDropoutVisibility(scopeTabWidget->currentIndex());

    const auto onAdvancedPlotPositionChanged = [this](const QPointF &dataPoint) {
        if (!hasCachedData || cachedScanLineData.fieldWidth < 1) {
            return;
        }
        const double usPerSample = (cachedScanLineData.sampleRate > 0.0)
            ? (1000000.0 / cachedScanLineData.sampleRate)
            : 1.0;
        const double samplePos = usPerSample > 0.0 ? (dataPoint.x() / usPerSample) : dataPoint.x();
        const qint32 newX = qBound(0, qRound(samplePos), cachedScanLineData.fieldWidth - 1);
        if (newX == lastScopeX) {
            updateAdvancedSampleMarker(newX);
            updateAdvancedInfoLabel(newX, cachedScanLineData);
            return;
        }

        lastScopeX = newX;
        {
            const QSignalBlocker blocker(ui->xCoordSpinBox);
            ui->xCoordSpinBox->setValue(lastScopeX);
        }
        updateAdvancedSampleMarker(lastScopeX);
        updateAdvancedInfoLabel(lastScopeX, cachedScanLineData);
        emit scopeCoordsChanged(lastScopeX, lastScopeY);
    };

    connect(advancedPlotWidget, &PlotWidget::plotClicked, this, onAdvancedPlotPositionChanged);
    connect(advancedPlotWidget, &PlotWidget::plotDragged, this, onAdvancedPlotPositionChanged);
}

double OscilloscopeDialog::sampleToMillivolts(qint32 sampleValue, const TbcSource::ScanLineData &scanLineData) const
{
    const qint32 levelSpan = scanLineData.whiteIre - scanLineData.blackIre;
    if (levelSpan == 0) {
        return static_cast<double>(sampleValue);
    }

    const bool ntscLike = scanLineData.systemDescription.contains(QStringLiteral("NTSC"), Qt::CaseInsensitive)
            || scanLineData.systemDescription.contains(QStringLiteral("PAL-M"), Qt::CaseInsensitive);
    const double ireToMv = ntscLike ? 7.143 : 7.0;
    const double ire = (static_cast<double>(sampleValue) - static_cast<double>(scanLineData.blackIre))
            * 100.0 / static_cast<double>(levelSpan);
    return ire * ireToMv;
}

double OscilloscopeDialog::sampleToIre(qint32 sampleValue, const TbcSource::ScanLineData &scanLineData) const
{
    const qint32 levelSpan = scanLineData.whiteIre - scanLineData.blackIre;
    if (levelSpan == 0) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return (static_cast<double>(sampleValue) - static_cast<double>(scanLineData.blackIre))
            * 100.0 / static_cast<double>(levelSpan);
}

void OscilloscopeDialog::updateAdvancedInfoLabel(qint32 pictureDot, const TbcSource::ScanLineData &scanLineData)
{
    if (!advancedSampleInfoLabel) {
        return;
    }
    if (scanLineData.fieldWidth < 1 || scanLineData.composite.isEmpty()) {
        advancedSampleInfoLabel->setText(QString());
        return;
    }

    const qint32 clampedDot = qBound(0, pictureDot, scanLineData.fieldWidth - 1);
    const double usPerSample = (scanLineData.sampleRate > 0.0)
        ? (1000000.0 / scanLineData.sampleRate)
        : 1.0;
    const double timeUs = static_cast<double>(clampedDot) * usPerSample;

    const bool showYC = ui->YCcheckBox && ui->YCcheckBox->isChecked();
    const bool showY = ui->YcheckBox && ui->YcheckBox->isChecked();
    const bool showC = ui->CcheckBox && ui->CcheckBox->isChecked();

    qint32 sampleValue = scanLineData.composite[clampedDot];
    if (showY && clampedDot < scanLineData.luma.size()) {
        sampleValue = scanLineData.luma[clampedDot];
    } else if (showC) {
        if (scanLineData.chroma.size() == scanLineData.fieldWidth) {
            sampleValue = scanLineData.chroma[clampedDot];
        } else if (clampedDot < scanLineData.luma.size()) {
            sampleValue = scanLineData.composite[clampedDot] - scanLineData.luma[clampedDot];
        }
    } else if (showYC) {
        sampleValue = scanLineData.composite[clampedDot];
    }

    const double mvValue = sampleToMillivolts(sampleValue, scanLineData);
    const double ireValue = sampleToIre(sampleValue, scanLineData);

    QString infoText = tr("Time: %1 µs").arg(timeUs, 0, 'f', 3);
    infoText += tr("\nSample: %1").arg(clampedDot);
    infoText += tr("\nmV: %1").arg(mvValue, 0, 'f', 1);
    if (qIsFinite(ireValue)) {
        infoText += tr("\nIRE: %1").arg(ireValue, 0, 'f', 1);
    }

    advancedSampleInfoLabel->setText(infoText);
}

void OscilloscopeDialog::updateAdvancedSampleMarker(qint32 pictureDot)
{
    if (!advancedPlotWidget || !hasCachedData || cachedScanLineData.fieldWidth < 1) {
        return;
    }

    if (advancedSampleMarker) {
        advancedPlotWidget->removeMarker(advancedSampleMarker);
        advancedSampleMarker = nullptr;
    }

    const qint32 clampedDot = qBound(0, pictureDot, cachedScanLineData.fieldWidth - 1);
    const double usPerSample = (cachedScanLineData.sampleRate > 0.0)
        ? (1000000.0 / cachedScanLineData.sampleRate)
        : 1.0;
    const double timeUs = static_cast<double>(clampedDot) * usPerSample;
    advancedSampleMarker = advancedPlotWidget->addMarker();
    advancedSampleMarker->setStyle(PlotMarker::VLine);
    advancedSampleMarker->setPosition(QPointF(timeUs, 0.0));
    advancedSampleMarker->setPen(QPen(QColor(0, 255, 0), 2));
}

void OscilloscopeDialog::updateAdvancedScope(const TbcSource::ScanLineData &scanLineData, qint32 pictureDot, bool bothSources)
{
    if (!advancedPlotWidget) {
        return;
    }
    if (scanLineData.fieldWidth < 1 || scanLineData.composite.isEmpty()) {
        // PlotWidget::showNoDataMessage() deletes every series and marker owned
        // by the plot widget. Forget our cached pointers so the next valid
        // update recreates them instead of dereferencing freed memory
        // (GitHub issue #28: access violation when the Line Scope stays open
        // across a no-data -> valid-data transition during a view-mode switch).
        advancedPlotWidget->showNoDataMessage(tr("No data available for this line"));
        advancedCompositeSeries = nullptr;
        advancedYSeries = nullptr;
        advancedCSeries = nullptr;
        advancedSampleMarker = nullptr;
        return;
    }

    advancedPlotWidget->clearNoDataMessage();

    if (!advancedCompositeSeries) {
        advancedCompositeSeries = advancedPlotWidget->addSeries(tr("Composite"));
        advancedCompositeSeries->setPen(QPen(QColor(100, 200, 255), 1));
    }
    if (!advancedYSeries) {
        advancedYSeries = advancedPlotWidget->addSeries(tr("Luma (Y)"));
        advancedYSeries->setPen(QPen(QColor(255, 255, 100), 1));
    }
    if (!advancedCSeries) {
        advancedCSeries = advancedPlotWidget->addSeries(tr("Chroma (C)"));
        advancedCSeries->setPen(QPen(QColor(120, 160, 255), 1));
    }

    const bool showYC = ui->YCcheckBox->isChecked();
    const bool showY = ui->YcheckBox->isChecked();
    const bool showC = ui->CcheckBox->isChecked();

    QVector<qint32> chromaData(scanLineData.fieldWidth);
    if (showC) {
        if (bothSources && scanLineData.chroma.size() == scanLineData.fieldWidth) {
            chromaData = scanLineData.chroma;
        } else {
            for (qint32 i = 0; i < scanLineData.fieldWidth; i++) {
                chromaData[i] = scanLineData.composite[i] - scanLineData.luma[i];
            }
        }
    }

    const double usPerSample = (scanLineData.sampleRate > 0.0)
        ? (1000000.0 / scanLineData.sampleRate)
        : 1.0;

    QVector<QPointF> compositePoints;
    QVector<QPointF> yPoints;
    QVector<QPointF> cPoints;
    compositePoints.reserve(scanLineData.fieldWidth);
    yPoints.reserve(scanLineData.fieldWidth);
    cPoints.reserve(scanLineData.fieldWidth);

    double dataMinMv = 0.0;
    double dataMaxMv = 0.0;
    bool firstValue = true;
    auto trackMinMax = [&firstValue, &dataMinMv, &dataMaxMv](double value) {
        if (firstValue) {
            dataMinMv = value;
            dataMaxMv = value;
            firstValue = false;
        } else {
            dataMinMv = qMin(dataMinMv, value);
            dataMaxMv = qMax(dataMaxMv, value);
        }
    };

    for (qint32 i = 0; i < scanLineData.fieldWidth; i++) {
        const double timeUs = static_cast<double>(i) * usPerSample;
        if (showYC) {
            const double mv = sampleToMillivolts(scanLineData.composite[i], scanLineData);
            compositePoints.append(QPointF(timeUs, mv));
            trackMinMax(mv);
        }
        if (showY && i < scanLineData.luma.size()) {
            const double mv = sampleToMillivolts(scanLineData.luma[i], scanLineData);
            yPoints.append(QPointF(timeUs, mv));
            trackMinMax(mv);
        }
        if (showC && i < chromaData.size()) {
            const double mv = sampleToMillivolts(chromaData[i], scanLineData);
            cPoints.append(QPointF(timeUs, mv));
            trackMinMax(mv);
        }
    }

    advancedCompositeSeries->setVisible(showYC);
    advancedCompositeSeries->setData(compositePoints);
    advancedYSeries->setVisible(showY);
    advancedYSeries->setData(yPoints);
    advancedCSeries->setVisible(showC);
    advancedCSeries->setData(cPoints);

    const int visibleSeries = static_cast<int>(showYC) + static_cast<int>(showY) + static_cast<int>(showC);
    advancedPlotWidget->setLegendEnabled(visibleSeries > 1);
    const bool ntscLike = scanLineData.systemDescription.contains(QStringLiteral("NTSC"), Qt::CaseInsensitive)
            || scanLineData.systemDescription.contains(QStringLiteral("PAL-M"), Qt::CaseInsensitive);
    const double ireToMv = ntscLike ? 7.143 : 7.0;
    constexpr double mvTickStep = 100.0;
    constexpr double ireMinLimit = -40.0;
    constexpr double ireMaxLimit = 120.0;
    const double mvMinLimit = ireMinLimit * ireToMv;
    const double mvMaxLimit = qMin(900.0, ireMaxLimit * ireToMv);

    // Use fixed decode-orc style limits to keep real-world scaling consistent
    const double minMv = mvMinLimit;
    const double maxMv = mvMaxLimit;

    const double maxTimeUs = qMax(1.0, static_cast<double>(scanLineData.fieldWidth - 1)) * usPerSample;
    advancedPlotWidget->setAxisRange(Qt::Horizontal, 0.0, maxTimeUs);
    advancedPlotWidget->setAxisRange(Qt::Vertical, minMv, maxMv);
    advancedPlotWidget->setAxisAutoScale(Qt::Horizontal, false);
    advancedPlotWidget->setAxisAutoScale(Qt::Vertical, false);
    advancedPlotWidget->setAxisTickStep(Qt::Horizontal, 2.0, 0.0);
    advancedPlotWidget->setAxisTickStep(Qt::Vertical, mvTickStep, 0.0);
    advancedPlotWidget->setSecondaryYAxisEnabled(true);
    advancedPlotWidget->setSecondaryYAxisTitle(tr("IRE"));
    const double minIreRange = ireMinLimit;
    const double maxIreRange = ireMaxLimit;
    advancedPlotWidget->setSecondaryYAxisRange(minIreRange, maxIreRange);
    advancedPlotWidget->setSecondaryYAxisTickStep(20.0, 0.0);

    advancedPlotWidget->clearMarkers();
    advancedSampleMarker = nullptr;

    if (scanLineData.colourBurstStart >= 0 && scanLineData.colourBurstEnd >= 0) {
        auto *burstStart = advancedPlotWidget->addMarker();
        burstStart->setStyle(PlotMarker::VLine);
        burstStart->setPosition(QPointF(static_cast<double>(scanLineData.colourBurstStart) * usPerSample, 0.0));
        burstStart->setPen(QPen(QColor(0, 255, 255), 1, Qt::DashLine));

        auto *burstEnd = advancedPlotWidget->addMarker();
        burstEnd->setStyle(PlotMarker::VLine);
        burstEnd->setPosition(QPointF(static_cast<double>(scanLineData.colourBurstEnd) * usPerSample, 0.0));
        burstEnd->setPen(QPen(QColor(0, 255, 255), 1, Qt::DashLine));
    }

    if (scanLineData.activeVideoStart >= 0 && scanLineData.activeVideoEnd >= 0) {
        auto *activeStart = advancedPlotWidget->addMarker();
        activeStart->setStyle(PlotMarker::VLine);
        activeStart->setPosition(QPointF(static_cast<double>(scanLineData.activeVideoStart) * usPerSample, 0.0));
        activeStart->setPen(QPen(QColor(255, 255, 100), 1, Qt::DashLine));

        auto *activeEnd = advancedPlotWidget->addMarker();
        activeEnd->setStyle(PlotMarker::VLine);
        activeEnd->setPosition(QPointF(static_cast<double>(scanLineData.activeVideoEnd) * usPerSample, 0.0));
        activeEnd->setPen(QPen(QColor(255, 255, 100), 1, Qt::DashLine));
    }

    updateAdvancedSampleMarker(pictureDot);
    updateAdvancedInfoLabel(pictureDot, scanLineData);
    advancedPlotWidget->replot();
}

void OscilloscopeDialog::showTraceImage(TbcSource::ScanLineData scanLineData, qint32 xCoord, qint32 yCoord, qint32 frameWidth, qint32 frameHeight, bool bothSources)
{
    tbcDebugStream() << "OscilloscopeDialog::showTraceImage(): Called with xCoord =" << xCoord << "and yCoord =" << yCoord;

    // Validate scan line data before doing anything
    if (scanLineData.fieldWidth < 1 || scanLineData.composite.empty()) {
        tbcDebugStream() << "OscilloscopeDialog::showTraceImage(): Invalid scan line data, skipping - fieldWidth:" << scanLineData.fieldWidth;
        if (advancedPlotWidget) {
            // PlotWidget::showNoDataMessage() deletes every series and marker
            // owned by the plot widget. Forget our cached pointers so the next
            // valid update recreates them instead of dereferencing freed memory
            // (GitHub issue #28: access violation when the Line Scope stays
            // open across a no-data -> valid-data transition during a view-mode
            // switch).
            advancedPlotWidget->showNoDataMessage(tr("No data available for this line"));
            advancedCompositeSeries = nullptr;
            advancedYSeries = nullptr;
            advancedCSeries = nullptr;
            advancedSampleMarker = nullptr;
        }
        if (advancedSampleInfoLabel) {
            advancedSampleInfoLabel->setText(QString());
        }
        return;
    }

    // Calculate optimal scope dimensions based on widget size (use widget size or reasonable defaults)
    qint32 widgetHeight = ui->scopeLabel->height();
    qint32 widgetWidth = ui->scopeLabel->width();
    qint32 targetHeight = (widgetHeight > 100) ? widgetHeight : 512;
    qint32 targetWidth = (widgetWidth > 100) ? widgetWidth : 910;
    // Clamp to reasonable bounds
    targetHeight = qBound(256, targetHeight, 2048);
    targetWidth = qBound(256, targetWidth, 4096);
    
    // Validate dimensions before creating the image
    if (targetHeight < 1 || targetWidth < 1) {
        tbcDebugStream() << "OscilloscopeDialog::showTraceImage(): Invalid dimensions, skipping - width:" << targetWidth << "height:" << targetHeight;
        return;
    }
    
    // Store coordinates (only after validation passes)
    maximumX = frameWidth;
    maximumY = frameHeight;
    lastScopeX = xCoord;
    lastScopeY = yCoord;
    cachedPictureDot = xCoord;
    
    // Cache the scan line data for resize events (only after validation passes)
    cachedScanLineData = scanLineData;
    hasCachedData = true;
    bothSourcesMode = bothSources;

    // Get the raw field data for the selected line
    QImage traceImage = getFieldLineTraceImage(scanLineData, lastScopeX, bothSources, targetHeight, targetWidth);

    // Add the QImage to the QLabel in the dialogue
    ui->scopeLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    ui->scopeLabel->setAlignment(Qt::AlignCenter);
    ui->scopeLabel->setScaledContents(false);
    ui->scopeLabel->setPixmap(QPixmap::fromImage(traceImage));
    ui->scopeLabel->update(); // Force Qt to redraw the widget

    // Update the X coordinate spinbox
    ui->xCoordSpinBox->setMaximum(maximumX - 1);
    ui->xCoordSpinBox->setValue(lastScopeX);

    // Update the Y coordinate spinbox
    ui->yCoordSpinBox->setMaximum(maximumY - 1);
    ui->yCoordSpinBox->setValue(lastScopeY);

    // Update the line number displays
    ui->standardLineLabel->setText(QString("%1 line %2")
                                   .arg(scanLineData.systemDescription)
                                   .arg(scanLineData.lineNumber.standard()));
    ui->fieldLineLabel->setText(QString("Field %1 line %2")
                                        .arg(scanLineData.lineNumber.isFirstField() ? "1" : "2")
                                        .arg(scanLineData.lineNumber.field1()));

    // Update decode-orc style advanced view
    updateAdvancedScope(scanLineData, cachedPictureDot, bothSources);

    // QT Bug workaround for some macOS versions
    #if defined(Q_OS_MACOS)
    	repaint();
    #endif
}

QImage OscilloscopeDialog::getFieldLineTraceImage(TbcSource::ScanLineData scanLineData, qint32 pictureDot, bool bothSources, qint32 scopeHeight, qint32 scopeWidth)
{
    // Validate dimensions before creating any images
    if (scopeWidth < 1 || scopeHeight < 1 || scanLineData.fieldWidth < 1) {
        qWarning() << "OscilloscopeDialog::getFieldLineTraceImage(): Invalid dimensions - scopeWidth:" << scopeWidth 
                   << "scopeHeight:" << scopeHeight << "fieldWidth:" << scanLineData.fieldWidth;
        // Return a minimal valid blank image
        QImage blankImage(256, 256, QImage::Format_RGB888);
        blankImage.fill(Qt::black);
        return blankImage;
    }
    
    // Ensure we have valid data
    if (scanLineData.composite.empty()) {
        qWarning() << "Did not get valid data for the requested field!";
        // Return blank image with correct size
        QImage blankImage(scopeWidth, scopeHeight, QImage::Format_RGB888);
        blankImage.fill(Qt::black);
        return blankImage;
    }
    
    // Get the display settings from the UI
    bool showYC = ui->YCcheckBox->isChecked();
    bool showY = ui->YcheckBox->isChecked();
    bool showC = ui->CcheckBox->isChecked();
    bool showDropouts = ui->dropoutsCheckBox->isChecked();

    // Calculate x-axis scale factor to fit the widget width
    double xScale = static_cast<double>(scopeWidth) / static_cast<double>(scanLineData.fieldWidth);
    this->scopeWidth = scopeWidth; // Store for mouse coordinate calculations
    this->fieldWidth = scanLineData.fieldWidth; // Store original field width for mouse coordinate conversion

    qint32 scopeScale = 65536 / scopeHeight;

    // Define image with width, height and format
    QImage scopeImage(scopeWidth, scopeHeight, QImage::Format_RGB888);
    QPainter scopePainter;
    assert(scanLineData.composite.size() == scanLineData.fieldWidth);
    assert(scanLineData.luma.size() == scanLineData.fieldWidth);

    // Set the background to black
    scopeImage.fill(Qt::black);

    // Attach the scope image to the painter
    scopePainter.begin(&scopeImage);

    // Add the black and white levels
    // Note: For PAL this should be black at 64 and white at 211

    // Scale to 512 pixel height
    qint32 blackIre = scopeHeight - (scanLineData.blackIre / scopeScale);
    qint32 whiteIre = scopeHeight - (scanLineData.whiteIre / scopeScale);
    qint32 midPointIre = scanLineData.blackIre + ((scanLineData.whiteIre - scanLineData.blackIre) / 2);
    midPointIre = scopeHeight - (midPointIre / scopeScale);

    scopePainter.setPen(Qt::white);
    scopePainter.drawLine(0, blackIre, scopeWidth, blackIre);
    scopePainter.drawLine(0, whiteIre, scopeWidth, whiteIre);

    // If showing C - draw the IRE mid-point
    if (showC) {
        scopePainter.setPen(Qt::gray);
        scopePainter.drawLine(0, midPointIre, scopeWidth, midPointIre);
    }

    // Draw the indicator lines (scaled to image width)
    scopePainter.setPen(Qt::blue);
    scopePainter.drawLine(scanLineData.colourBurstStart * xScale, 0, scanLineData.colourBurstStart * xScale, scopeHeight);
    scopePainter.drawLine(scanLineData.colourBurstEnd * xScale, 0, scanLineData.colourBurstEnd * xScale, scopeHeight);
    scopePainter.setPen(Qt::cyan);
    scopePainter.drawLine(scanLineData.activeVideoStart * xScale, 0, scanLineData.activeVideoStart * xScale, scopeHeight);
    scopePainter.drawLine(scanLineData.activeVideoEnd * xScale, 0, scanLineData.activeVideoEnd * xScale, scopeHeight);

    // Get the signal data
    const QVector<qint32> &signalDataYC = scanLineData.composite; // Composite - luma (Y) and chroma (C) combined
    const QVector<bool> &dropOutYC = scanLineData.isDropout; // Drop out locations within YC data
    const QVector<qint32> &signalDataY = scanLineData.luma; // Luma (Y) only
    QVector<qint32> signalDataC(scanLineData.fieldWidth); // Chroma (C) only

    if (showC) {
        if (!bothSources) {
            for (qint32 i = 0; i < scanLineData.fieldWidth; i++) {
                signalDataC[i] = signalDataYC[i] - signalDataY[i];
            }
        } else {
            signalDataC = scanLineData.chroma;
        }
    }

    // Draw the scope image
    qint32 lastSignalLevelYC = 0;
    qint32 lastXPositionScaled = 0;
    if (showYC) {
        for (qint32 xPosition = 0; xPosition < scanLineData.fieldWidth; xPosition++) {
            qint32 xPositionScaled = xPosition * xScale;
            // Scale (to 0-scopeHeight) and invert
            qint32 signalLevelYC = scopeHeight - (signalDataYC[xPosition] / scopeScale);

            if (xPosition != 0) {
                // Non-active video area YC is yellow, active is white
                if (!showY && !showC) scopePainter.setPen(Qt::white); else scopePainter.setPen(Qt::darkGray);
                if (xPosition < scanLineData.colourBurstEnd || xPosition > scanLineData.activeVideoEnd) scopePainter.setPen(Qt::yellow);

                // Highlight dropouts
                if (dropOutYC[xPosition] && showDropouts) scopePainter.setPen(Qt::red);

                // Draw a line from the last YC signal to the current one
                scopePainter.drawLine(lastXPositionScaled, lastSignalLevelYC, xPositionScaled, signalLevelYC);
            }

            // Remember the current signal's level and position
            lastSignalLevelYC = signalLevelYC;
            lastXPositionScaled = xPositionScaled;
        }
    }

    // Draw the Y/C traces, for the active region only
    qint32 lastSignalLevelY = 0;
    qint32 lastSignalLevelC = 0;
    qint32 lastXPosScaled = scanLineData.activeVideoStart * xScale;
    if (scanLineData.isActiveLine) {
        for (qint32 xPosition = scanLineData.activeVideoStart; xPosition < scanLineData.activeVideoEnd; xPosition++) {
            qint32 xPosScaled = xPosition * xScale;
            
            if (showC) {
                // Scale (to 0-scopeHeight) and invert
                qint32 signalLevelC = (scopeHeight - (signalDataC[xPosition] / scopeScale)) - (scopeHeight - midPointIre);

                if (xPosition != scanLineData.activeVideoStart) {
                    // Draw a line from the last Y signal to the current one (signal green, out of range in yellow)
                    if (signalLevelC > blackIre || signalLevelC < whiteIre) scopePainter.setPen(Qt::yellow);
                    else scopePainter.setPen(Qt::green);
                    scopePainter.drawLine(lastXPosScaled, lastSignalLevelC, xPosScaled, signalLevelC);
                }

                // Remember the current signal's level
                lastSignalLevelC = signalLevelC;
            }

            if (showY) {
                // Scale (to 0-scopeHeight) and invert
                qint32 signalLevelY = scopeHeight - (signalDataY[xPosition] / scopeScale);

                if (xPosition != scanLineData.activeVideoStart) {
                    // Draw a line from the last Y signal to the current one (signal white, out of range in red)
                    if (signalLevelY > blackIre || signalLevelY < whiteIre) scopePainter.setPen(Qt::red);
                    else scopePainter.setPen(Qt::white);
                    scopePainter.drawLine(lastXPosScaled, lastSignalLevelY, xPosScaled, signalLevelY);
                }

                // Remember the current signal's level
                lastSignalLevelY = signalLevelY;
            }
            
            lastXPosScaled = xPosScaled;
        }
    }

    // Draw the picture dot position line (scaled to image width)
    scopePainter.setPen(QColor(0, 255, 0, 127));
    qint32 pictureDotScaled = pictureDot * xScale;
    scopePainter.drawLine(pictureDotScaled, 0, pictureDotScaled, scopeHeight);

    // Return the QImage
    scopePainter.end();
    return scopeImage;
}

// GUI signal handlers ------------------------------------------------------------------------------------------------

void OscilloscopeDialog::on_previousPushButton_clicked()
{
    if (ui->yCoordSpinBox->value() != 0) {
        emit scopeCoordsChanged(lastScopeX, ui->yCoordSpinBox->value() - 1);
    }
}

void OscilloscopeDialog::on_nextPushButton_clicked()
{
    if (ui->yCoordSpinBox->value() < maximumY - 1) {
        emit scopeCoordsChanged(lastScopeX, ui->yCoordSpinBox->value() + 1);
    }
}

void OscilloscopeDialog::on_fieldToggleButton_clicked()
{
    if (!ui || !ui->yCoordSpinBox) {
        return;
    }

    qint32 current = lastScopeY;
    qint32 target = current;

    if ((current % 2) == 0) {
        if (current + 1 < maximumY) {
            target = current + 1;
        } else if (current - 1 >= 0) {
            target = current - 1;
        }
    } else {
        if (current - 1 >= 0) {
            target = current - 1;
        } else if (current + 1 < maximumY) {
            target = current + 1;
        }
    }

    if (target != current) {
        ui->yCoordSpinBox->setValue(target);
    }
}

void OscilloscopeDialog::on_xCoordSpinBox_valueChanged(int arg1)
{
    (void)arg1;
    if (ui->xCoordSpinBox->value() != lastScopeX) {
        cachedPictureDot = ui->xCoordSpinBox->value();
        emit scopeCoordsChanged(ui->xCoordSpinBox->value(), lastScopeY);
    }
}

void OscilloscopeDialog::on_yCoordSpinBox_valueChanged(int arg1)
{
    (void)arg1;
    if (ui->yCoordSpinBox->value() != lastScopeY)
        emit scopeCoordsChanged(lastScopeX, ui->yCoordSpinBox->value() );
}

void OscilloscopeDialog::on_YCcheckBox_clicked()
{
    emit scopeCoordsChanged(lastScopeX, lastScopeY);
}

void OscilloscopeDialog::on_YcheckBox_clicked()
{
    emit scopeCoordsChanged(lastScopeX, lastScopeY);
}

void OscilloscopeDialog::on_CcheckBox_clicked()
{
    emit scopeCoordsChanged(lastScopeX, lastScopeY);
}

void OscilloscopeDialog::on_dropoutsCheckBox_clicked()
{
    emit scopeCoordsChanged(lastScopeX, lastScopeY);
}

// Mouse press event handler
void OscilloscopeDialog::mousePressEvent(QMouseEvent *event)
{
    // Get the mouse position relative to our scene
    QPoint origin = ui->scopeLabel->mapFromGlobal(QCursor::pos());

    // Check that the mouse click is within bounds of the current picture
    qint32 oX = origin.x();
    qint32 oY = origin.y();

    if (oX + 1 >= 0 &&
            oY >= 0 &&
            oX + 1 <= ui->scopeLabel->width() &&
            oY <= ui->scopeLabel->height()) {

        // Is shift held down?
        if (event->modifiers().testFlag(Qt::ShiftModifier)) {
            mouseLevelSelect(oY);
        } else {
            mousePictureDotSelect(oX);
        }
        event->accept();
    }
}

// Mouse drag event handler
void OscilloscopeDialog::mouseMoveEvent(QMouseEvent *event)
{
    // Handle this the same way as a click
    mousePressEvent(event);
}

// Handle a click on the scope with shift held down, to select a level
void OscilloscopeDialog::mouseLevelSelect(qint32 oY)
{
    double unscaledYR = (65536.0 / static_cast<double>(ui->scopeLabel->height())) * static_cast<double>(oY);

    qint32 level = qBound(0, 65535 - static_cast<qint32>(unscaledYR), 65535);
    emit scopeLevelSelect(level);
}

// Handle a click on the scope without shift held down, to select a sample
void OscilloscopeDialog::mousePictureDotSelect(qint32 oX)
{
    // Convert from widget pixel coordinates to image pixel coordinates
    // Since setScaledContents is false, this is 1:1 when image fits, but might differ if centered
    double imageX = oX;
    
    // Convert from image pixel coordinates to field sample coordinates
    // The image width (scopeWidth) represents the full field width (fieldWidth)
    double unscaledXR = (static_cast<double>(fieldWidth) / static_cast<double>(scopeWidth)) * imageX;

    qint32 unscaledX = static_cast<qint32>(unscaledXR);
    if (unscaledX > fieldWidth - 1) unscaledX = fieldWidth - 1;
    if (unscaledX < 0) unscaledX = 0;

    // Remember the last dot selected
    lastScopeX = unscaledX;
    cachedPictureDot = unscaledX;
    updateAdvancedSampleMarker(cachedPictureDot);
    if (hasCachedData) {
        updateAdvancedInfoLabel(cachedPictureDot, cachedScanLineData);
    }

    emit scopeCoordsChanged(lastScopeX, lastScopeY);
}

// Resize event handler - regenerate the scope image when dialog is resized
void OscilloscopeDialog::resizeEvent(QResizeEvent *event)
{
    QDialog::resizeEvent(event);
    
    // Regenerate the scope image with the new size if we have cached data
    // Also verify that the cached data has valid dimensions before regenerating
    if (hasCachedData && cachedScanLineData.fieldWidth > 0 && !cachedScanLineData.composite.empty()) {
        showTraceImage(cachedScanLineData, lastScopeX, lastScopeY, maximumX, maximumY, bothSourcesMode);
    }
}

// Apply the scan-line scope's first-open defaults (reference start size,
// Advanced tab, YC + Y traces). Centering over the main window is handled
// centrally by MainWindow's Show-event filter via tbc::ui::centerDialogOverParent.
void OscilloscopeDialog::setVisible(bool visible)
{
    if (visible && !isVisible() && !firstShowDone) {
        resize(1028, 390);
        setAdvancedTabSelected(true);
        if (ui->YCcheckBox) {
            ui->YCcheckBox->setChecked(true);
        }
        if (ui->YcheckBox) {
            ui->YcheckBox->setChecked(true);
        }
        firstShowDone = true;
    }
    QDialog::setVisible(visible);
}
