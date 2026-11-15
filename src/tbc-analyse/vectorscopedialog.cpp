/******************************************************************************
 * vectorscopedialog.cpp
 * tbc-analyse - TBC output analysis GUI
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2018-2025 Simon Inns
 * SPDX-FileCopyrightText: 2022 Adam Sampson
 *
 * This file is part of tbc-tools.
 ******************************************************************************/

#include "vectorscopedialog.h"
#include "ui_vectorscopedialog.h"
#include "tbc/logging.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <random>

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QDebug>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QScrollArea>
#include <QStringList>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QResizeEvent>

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kMajorMarkerLengthPixels = 18.0;
constexpr double kMinorMarkerLengthPixels = 10.0;
constexpr double kIqLabelOffsetPixels = 22.0;
constexpr double kColorLabelOffsetPixels = 48.0;
constexpr double kZoneHalfAngleDegrees = 13.0;
constexpr double kZoneHalfRadialSpanPercent = 0.14;
constexpr double kTargetBoxSizePixels = 42.0;
constexpr double kTargetCrosshairSizePixels = 22.0;

constexpr double kNtscIAxisDegrees = 147.0;
constexpr double kNtscNegIAxisDegrees = -33.0;
constexpr double kNtscQAxisDegrees = 57.0;
constexpr double kNtscNegQAxisDegrees = -123.0;
constexpr double kIqLabelAngularOffsetDegrees = 4.0;

// Colour-burst box geometry.  ITU-R BT.470-6 Table 2 item 2.16: the PAL burst
// swings +/-45 degrees about the -U axis with the V-switch; SMPTE 170M-2004
// section 8.4: the NTSC burst sits on the -U axis.
constexpr double kPalBurstVPositiveDegrees = 135.0;
constexpr double kPalBurstVNegativeDegrees = 225.0;
constexpr double kNtscBurstDegrees = 180.0;

// Nominal colour-burst peak amplitude as a fraction of the blanking-to-white
// range.  EBU Tech. 3280-E section 1.2: PAL burst is 300 mV peak-to-peak on a
// 700 mV luminance range.  SMPTE 170M-2004 section 8.4: NTSC burst is 40 IRE
// peak-to-peak on a 100 IRE range.
constexpr double kPalBurstAmplitudeFraction = 150.0 / 700.0;
constexpr double kNtscBurstAmplitudeFraction = 0.20;

constexpr double kBurstBoxSizePixels = 30.0;
constexpr double kBurstCrosshairSizePixels = 16.0;

QPointF pointFromStandardDegrees(double angleDegrees, double magnitude, qint32 halfSize)
{
    const double theta = (angleDegrees * kPi) / 180.0;
    const double x = static_cast<double>(halfSize) + (magnitude * std::cos(theta));
    const double y = static_cast<double>(halfSize) - (magnitude * std::sin(theta));
    return QPointF(x, y);
}

QColor vectorscopeTargetColor(qint32 rgb)
{
    switch (rgb) {
    case 1: return QColor(70, 150, 255, 120);  // Blue
    case 2: return QColor(70, 230, 120, 120);  // Green
    case 3: return QColor(90, 235, 235, 120);  // Cyan
    case 4: return QColor(255, 90, 90, 120);   // Red
    case 5: return QColor(230, 90, 230, 120);  // Magenta
    case 6: return QColor(245, 215, 80, 120);  // Yellow
    default: return QColor(255, 255, 255, 120);
    }
}

// Colourise a plotted point by its U/V position on the scope canvas via the
// BT.601 inverse matrix (ITU-R BT.470-6 / EBU Tech. 3280-E), ported from
// decode-orc's vectorscope dialog Pass-2.  Shared by the decoded and
// composite render paths: it recovers the BT.601 U/V coefficient from the
// canvas position, so it is agnostic to the units each path plots in (the
// decoded path scales U/V by ireRange in 16-bit units; the composite path by
// the system's blanking-to-white range in U10 units).
//
// Do NOT additionally divide by the 100%-bar magnitudes (0.436010 /
// 0.614975): that double-normalises and warps the recovered hue.
QColor colorizeScopePoint(qint32 px, qint32 py, qint32 ireRange)
{
    constexpr qint32 HALF_SIZE = 512;
    constexpr qint32 SCALE = 65536 / 1024;
    if (ireRange <= 0) return Qt::green;
    const double u_units = static_cast<double>((px - HALF_SIZE) * SCALE);
    const double v_units = -static_cast<double>((py - HALF_SIZE) * SCALE);
    // Recover the BT.601 U/V coefficient (U_bt601, V_bt601).
    const double u_n = u_units / ireRange;
    const double v_n = v_units / ireRange;
    // BT.601 inverse at Y=0.5:  B-Y = U/ku, R-Y = V/kv, G from luminance.
    const double r_raw = 0.5 + v_n / 0.877283;
    const double b_raw = 0.5 + u_n / 0.492111;
    const double g_raw = (0.5 - 0.299 * r_raw - 0.114 * b_raw) / 0.587;
    double r_c = std::clamp(r_raw, 0.0, 1.0);
    double g_c = std::clamp(g_raw, 0.0, 1.0);
    double b_c = std::clamp(b_raw, 0.0, 1.0);
    // Max-component normalise so the hue direction is always vivid;
    // the centre (U=V=0) gives equal components -> white.
    const double max_c = std::max({r_c, g_c, b_c});
    if (max_c > 0.001) {
        r_c /= max_c;
        g_c /= max_c;
        b_c /= max_c;
    } else {
        r_c = g_c = b_c = 1.0;
    }
    return QColor(static_cast<int>(r_c * 255), static_cast<int>(g_c * 255),
                  static_cast<int>(b_c * 255));
}

void drawReferenceAxis(QPainter &painter, qint32 halfSize, double angleDegrees, double innerMagnitude, double outerMagnitude)
{
    painter.drawLine(pointFromStandardDegrees(angleDegrees, innerMagnitude, halfSize),
                     pointFromStandardDegrees(angleDegrees, outerMagnitude, halfSize));
}

void drawCircleMarkers(QPainter &painter, qint32 size, qint32 halfSize, qint32 radius)
{
    const double outerRadius = static_cast<double>(radius);
    for (qint32 degrees = 0; degrees < 360; degrees += 2) {
        const bool isMajor = (degrees % 10) == 0;
        const double markerLength = isMajor ? kMajorMarkerLengthPixels : kMinorMarkerLengthPixels;
        const double theta = (static_cast<double>(degrees) * kPi) / 180.0;
        const double x1 = static_cast<double>(halfSize) + ((outerRadius - markerLength) * std::cos(theta));
        const double y1 = static_cast<double>(halfSize) - ((outerRadius - markerLength) * std::sin(theta));
        const double x2 = static_cast<double>(halfSize) + (outerRadius * std::cos(theta));
        const double y2 = static_cast<double>(halfSize) - (outerRadius * std::sin(theta));
        painter.setPen(QPen(Qt::white, isMajor ? 2.0 : 1.0));
        painter.drawLine(QPointF(x1, y1), QPointF(x2, y2));
    }

    painter.setPen(QPen(Qt::white, 1.0));
    painter.drawEllipse(0, 0, size - 1, size - 1);
}

void drawNtscIqLabels(QPainter &painter, qint32 halfSize, qint32 radius)
{
    struct AxisLabel {
        const char *text;
        double angleDegrees;
        double labelOffsetDegrees;
    };
    constexpr AxisLabel axisLabels[] = {
        {"I", kNtscIAxisDegrees, kIqLabelAngularOffsetDegrees},
        {"Q", kNtscQAxisDegrees, -kIqLabelAngularOffsetDegrees},
        {"-I", kNtscNegIAxisDegrees, -kIqLabelAngularOffsetDegrees},
        {"-Q", kNtscNegQAxisDegrees, kIqLabelAngularOffsetDegrees},
    };

    QFont labelFont = painter.font();
    labelFont.setPointSize(24);
    labelFont.setBold(true);
    painter.setFont(labelFont);
    painter.setPen(QPen(QColor(200, 200, 200), 1));

    const double labelRadius = static_cast<double>(radius) - kIqLabelOffsetPixels;
    for (const AxisLabel &axisLabel : axisLabels) {
        const QPointF centre = pointFromStandardDegrees(axisLabel.angleDegrees + axisLabel.labelOffsetDegrees,
                                                        labelRadius,
                                                        halfSize);
        const QString text = QString::fromUtf8(axisLabel.text);
        const QFontMetrics metrics(labelFont);
        const QRect textRect = metrics.boundingRect(text);
        painter.drawText(static_cast<qint32>(centre.x()) - (textRect.width() / 2),
                         static_cast<qint32>(centre.y()) + (textRect.height() / 3),
                         text);
    }
}

void drawColorZone(QPainter &painter, qint32 halfSize, qint32 radius, double angleDegrees, double magnitude, const QColor &color)
{
    const double zoneHalfAngleRadians = (kZoneHalfAngleDegrees * kPi) / 180.0;
    const double radialSpan = magnitude * kZoneHalfRadialSpanPercent;
    const double innerRadius = std::max(0.0, magnitude - radialSpan);
    const double outerRadius = std::min(static_cast<double>(radius), magnitude + radialSpan);
    const double angleRadians = (angleDegrees * kPi) / 180.0;

    QPainterPath zonePath;
    zonePath.moveTo(pointFromStandardDegrees(angleDegrees - kZoneHalfAngleDegrees, innerRadius, halfSize));
    for (qint32 step = 0; step <= 12; ++step) {
        const double t = static_cast<double>(step) / 12.0;
        const double arcRadians = angleRadians - zoneHalfAngleRadians + (t * zoneHalfAngleRadians * 2.0);
        const double arcDegrees = (arcRadians * 180.0) / kPi;
        zonePath.lineTo(pointFromStandardDegrees(arcDegrees, outerRadius, halfSize));
    }
    for (qint32 step = 12; step >= 0; --step) {
        const double t = static_cast<double>(step) / 12.0;
        const double arcRadians = angleRadians - zoneHalfAngleRadians + (t * zoneHalfAngleRadians * 2.0);
        const double arcDegrees = (arcRadians * 180.0) / kPi;
        zonePath.lineTo(pointFromStandardDegrees(arcDegrees, innerRadius, halfSize));
    }
    zonePath.closeSubpath();

    QColor fillColor = color;
    fillColor.setAlpha(52);
    QColor outlineColor = color;
    outlineColor.setAlpha(180);
    painter.save();
    painter.setPen(QPen(outlineColor, 1));
    painter.setBrush(fillColor);
    painter.drawPath(zonePath);
    painter.restore();
}

void drawTargetBox(QPainter &painter, const QPointF &centerPoint, const QColor &color,
                   double boxSize = kTargetBoxSizePixels,
                   double crosshairSize = kTargetCrosshairSizePixels)
{
    const double halfBox = boxSize / 2.0;
    const double halfCrosshair = crosshairSize / 2.0;
    const QRectF boxRect(centerPoint.x() - halfBox, centerPoint.y() - halfBox,
                         boxSize, boxSize);

    QColor outlineColor = color;
    outlineColor.setAlpha(220);
    painter.save();
    painter.setPen(QPen(outlineColor, 2));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(boxRect);
    painter.drawLine(QPointF(centerPoint.x() - halfCrosshair, centerPoint.y()),
                     QPointF(centerPoint.x() + halfCrosshair, centerPoint.y()));
    painter.drawLine(QPointF(centerPoint.x(), centerPoint.y() - halfCrosshair),
                     QPointF(centerPoint.x(), centerPoint.y() + halfCrosshair));
    painter.restore();
}

// Draw the shared graticule.  |compositeMode| selects the measurement
// graticule drawn over the Advanced tab's composite acquisition; the standard
// page always passes false and gets exactly the graticule it always had plus
// the colour-burst boxes.
void drawDecodeOrcStyleGraticule(QPainter &scopePainter, qint32 size, qint32 halfSize, qint32 radius,
                                 const TbcMetaData::VideoParameters &videoParameters, bool graticule75,
                                 bool compositeMode)
{
    scopePainter.save();
    scopePainter.setRenderHint(QPainter::Antialiasing, true);
    scopePainter.setPen(QPen(Qt::white, 1.0));

    // Draw center cross hairs and main circle/tick markers.
    scopePainter.drawLine(halfSize, 0, halfSize, size - 1);
    scopePainter.drawLine(0, halfSize, size - 1, halfSize);
    drawCircleMarkers(scopePainter, size, halfSize, radius);

    // NTSC I/Q reference axes + labels.
    if (videoParameters.system == NTSC) {
        const double innerMagnitude = 0.2 * static_cast<double>(radius);
        const double outerMagnitude = static_cast<double>(radius);
        drawReferenceAxis(scopePainter, halfSize, kNtscIAxisDegrees, innerMagnitude, outerMagnitude);
        drawReferenceAxis(scopePainter, halfSize, kNtscNegIAxisDegrees, innerMagnitude, outerMagnitude);
        drawReferenceAxis(scopePainter, halfSize, kNtscQAxisDegrees, innerMagnitude, outerMagnitude);
        drawReferenceAxis(scopePainter, halfSize, kNtscNegQAxisDegrees, innerMagnitude, outerMagnitude);
        drawNtscIqLabels(scopePainter, halfSize, radius);
    }

    // Draw color-bar target zones and boxes.
    const qint32 ireRange = videoParameters.white16bIre - videoParameters.black16bIre;
    if (ireRange > 0) {
        constexpr std::array<const char *, 7> colorLabelsUpper = {"", "B", "G", "Cy", "R", "Mg", "Yl"};
        constexpr std::array<const char *, 7> colorLabelsLower = {"", "b", "g", "cy", "r", "mg", "yl"};
        constexpr qint32 scopeScale = 65536 / 1024;
        const double percent = graticule75 ? 0.75 : 1.0;
        const double uvScale = static_cast<double>(ireRange) / static_cast<double>(scopeScale);

        // A composite (measurement) acquisition does not undo the PAL
        // V-switch, so its graticule carries two mirrored sets of targets -
        // upper case for the +V line phase, lower case for -V (ITU-R BT.470-6
        // Table 2 item 2.16).  A blanking-referenced composite measurement
        // also sees the NTSC/PAL_M encoder's 0.925 chroma scale, which the
        // decoded acquisition's picture-excursion normalisation hides
        // (SMPTE 170M-2004 section 10).
        const bool mirrorTargets = compositeMode
                                   && (videoParameters.system == PAL
                                       || videoParameters.system == PAL_M);
        const double chromaScale = (compositeMode
                                    && (videoParameters.system == NTSC
                                        || videoParameters.system == PAL_M))
                                       ? 0.925
                                       : 1.0;

        for (qint32 phasePass = 0; phasePass < 2; phasePass++) {
            const bool mirror = (phasePass == 1);
            if (mirror && !mirrorTargets) continue;

            for (qint32 rgb = 1; rgb < 7; rgb++) {
                const double R = percent * static_cast<double>((rgb >> 2) & 1);
                const double G = percent * static_cast<double>((rgb >> 1) & 1);
                const double B = percent * static_cast<double>(rgb & 1);

                const double U = (R * -0.147141) + (G * -0.288869) + (B * 0.436010);
                const double V = (R * 0.614975)  + (G * -0.514965) + (B * -0.100010);
                const double plotV = mirror ? -V : V;

                const double uScaled = U * uvScale * chromaScale;
                const double vScaled = plotV * uvScale * chromaScale;
                const QPointF targetPoint(static_cast<double>(halfSize) + uScaled,
                                          static_cast<double>(halfSize) - vScaled);
                const double targetMagnitude = std::hypot(uScaled, vScaled);
                const double targetAngleDegrees = (std::atan2(plotV, U) * 180.0) / kPi;
                const QColor targetColor = vectorscopeTargetColor(rgb);

                drawColorZone(scopePainter, halfSize, radius, targetAngleDegrees, targetMagnitude, targetColor);
                drawTargetBox(scopePainter, targetPoint, targetColor);

                const QPointF labelPoint = pointFromStandardDegrees(targetAngleDegrees,
                                                                    targetMagnitude + kColorLabelOffsetPixels,
                                                                    halfSize);
                QFont labelFont = scopePainter.font();
                labelFont.setPointSize(14);
                labelFont.setBold(true);
                scopePainter.setFont(labelFont);

                QColor labelColor = targetColor;
                labelColor.setAlpha(255);
                scopePainter.setPen(QPen(labelColor, 1.0));

                const QString labelText = QString::fromUtf8(mirror ? colorLabelsLower[rgb]
                                                                   : colorLabelsUpper[rgb]);
                const QFontMetrics metrics(labelFont);
                const qint32 textWidth = metrics.horizontalAdvance(labelText);
                const qint32 textHeight = metrics.height();
                scopePainter.drawText(static_cast<qint32>(labelPoint.x()) - (textWidth / 2),
                                      static_cast<qint32>(labelPoint.y()) + (textHeight / 4),
                                      labelText);
            }
        }

        // Colour-burst target boxes: PAL carries two, swung +/-45 degrees
        // about the -U axis with the V-switch (ITU-R BT.470-6 Table 2 item
        // 2.16); NTSC one on the -U axis (SMPTE 170M-2004 section 8.4).  PAL_M
        // swings with PAL but takes its burst amplitude from the NTSC levels
        // (ITU-R BT.1700-1 Annex 1 Part B).  SECAM/MESECAM carry FM chroma and
        // have no burst to box.
        if (videoParameters.system == PAL || videoParameters.system == NTSC
            || videoParameters.system == PAL_M) {
            const bool isPal = (videoParameters.system == PAL);
            const double burstFraction = isPal ? kPalBurstAmplitudeFraction
                                               : kNtscBurstAmplitudeFraction;
            // The graticule's scale maps one U10-domain chroma unit to one
            // pixel, so the box radius is the burst's peak amplitude in U10
            // units: its fraction of the system's blanking-to-white range.
            const double levelRangeU10 =
                static_cast<double>(tbc::cvbs::cvbs_white(videoParameters.system)
                                    - tbc::cvbs::cvbs_blanking(videoParameters.system));
            const double burstMagnitude = burstFraction * levelRangeU10;
            const QColor burstBoxColor(220, 220, 220);
            if (isPal) {
                drawTargetBox(scopePainter,
                              pointFromStandardDegrees(kPalBurstVPositiveDegrees, burstMagnitude, halfSize),
                              burstBoxColor, kBurstBoxSizePixels, kBurstCrosshairSizePixels);
                drawTargetBox(scopePainter,
                              pointFromStandardDegrees(kPalBurstVNegativeDegrees, burstMagnitude, halfSize),
                              burstBoxColor, kBurstBoxSizePixels, kBurstCrosshairSizePixels);
            } else {
                drawTargetBox(scopePainter,
                              pointFromStandardDegrees(kNtscBurstDegrees, burstMagnitude, halfSize),
                              burstBoxColor, kBurstBoxSizePixels, kBurstCrosshairSizePixels);
            }
        }
    }

    scopePainter.restore();
}

} // namespace

VectorscopeDialog::VectorscopeDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::VectorscopeDialog)
{
    ui->setupUi(this);
    setWindowFlags(Qt::Window);
    ui->scopeLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    ui->scopeLabel->setMinimumSize(QSize(1, 1));
    ui->scopeLabel->setAlignment(Qt::AlignCenter);
    ui->scopeLabel->setScaledContents(false);

    // Set up field selection colors
    QColor firstFieldColor = QColor(255, 255, 0);   // Yellow for first field
    QColor secondFieldColor = QColor(0, 255, 255);  // Cyan for second field

    ui->fieldSelectFirstRadioButton->setStyleSheet(
        QString("color: %1;").arg(firstFieldColor.name()));
    ui->fieldSelectSecondRadioButton->setStyleSheet(
        QString("color: %1;").arg(secondFieldColor.name()));

    initialiseAdvancedControls();
    setupAdvancedScopeTab();
}

VectorscopeDialog::~VectorscopeDialog()
{
    delete ui;
}

void VectorscopeDialog::showTraceImage(const ComponentFrame &componentFrame, const TbcMetaData::VideoParameters &videoParameters,
                                       const TbcSource::ViewMode& viewMode, const bool isFirstField)
{
    tbcDebugStream() << "VectorscopeDialog::showTraceImage(): Called";

    updateFieldSelectionControls(viewMode, isFirstField);
    updateAreaControlState(componentFrame, videoParameters);

    // Draw the image
    cachedTraceImage = getTraceImage(componentFrame, videoParameters);
    updateScopeLabelPixmap();

    // QT Bug workaround for some macOS versions
    #if defined(Q_OS_MACOS)
        repaint();
    #endif
}

void VectorscopeDialog::updateFieldSelectionControls(const TbcSource::ViewMode& viewMode, const bool isFirstField)
{
    // Set/enable/disable controls based on view
    switch (viewMode) {
        case TbcSource::ViewMode::FRAME_VIEW:
        case TbcSource::ViewMode::SPLIT_VIEW:
            ui->fieldSelectAllRadioButton->setEnabled(true);
            ui->fieldSelectFirstRadioButton->setEnabled(true);
            ui->fieldSelectSecondRadioButton->setEnabled(true);
            ui->blendColorCheckBox->setEnabled(true);
            ui->multiColourCheckBox->setEnabled(true);
            break;

        case TbcSource::ViewMode::FIELD_VIEW:
            ui->fieldSelectAllRadioButton->setEnabled(false);
            ui->blendColorCheckBox->setEnabled(false);
            ui->blendColorCheckBox->setChecked(false);
            // Multi-colour is position-based hue, so it stays useful for a
            // single field — leave it enabled in field view.

            if (isFirstField) {
                ui->fieldSelectFirstRadioButton->setEnabled(true);
                ui->fieldSelectSecondRadioButton->setEnabled(false);
                ui->fieldSelectFirstRadioButton->setChecked(true);
            } else {
                ui->fieldSelectFirstRadioButton->setEnabled(false);
                ui->fieldSelectSecondRadioButton->setEnabled(true);
                ui->fieldSelectSecondRadioButton->setChecked(true);
            }
            break;
    }
}

bool VectorscopeDialog::isCustomAreaModeSelected() const
{
    return areaModeCustomRadioButton && areaModeCustomRadioButton->isChecked();
}

QRect VectorscopeDialog::customAreaRect() const
{
    if (!hasAreaContext || areaFrameWidth <= 0 || areaFrameHeight <= 0) {
        return QRect();
    }

    return QRect(customXStart,
                 customYStart,
                 (customXEnd - customXStart) + 1,
                 (customYEnd - customYStart) + 1);
}

void VectorscopeDialog::setCustomAreaModeSelected(bool selected)
{
    if (!areaModeCustomRadioButton) {
        return;
    }
    const bool wasCustomSelected = areaModeCustomRadioButton->isChecked();
    if (selected) {
        if (!wasCustomSelected) {
            areaModeCustomRadioButton->setChecked(true);
        }
    } else if (wasCustomSelected) {
        if (areaModeFullRadioButton) {
            areaModeFullRadioButton->setChecked(true);
        } else if (areaModeActiveRadioButton) {
            areaModeActiveRadioButton->setChecked(true);
        } else {
            areaModeCustomRadioButton->setChecked(false);
        }
        applyAreaPreset();
    }

    if (areaModeCustomRadioButton->isChecked() != wasCustomSelected) {
        emit scopeChanged();
    }
}

void VectorscopeDialog::setCustomAreaRect(const QRect &areaRect)
{
    if (!hasAreaContext || areaFrameWidth <= 0 || areaFrameHeight <= 0 || !areaModeCustomRadioButton) {
        return;
    }

    const bool wasCustomModeSelected = areaModeCustomRadioButton->isChecked();

    const QRect frameBounds(0, 0, areaFrameWidth, areaFrameHeight);
    const QRect clampedRect = areaRect.normalized().intersected(frameBounds);
    if (clampedRect.width() <= 0 || clampedRect.height() <= 0) {
        return;
    }

    const qint32 newCustomXStart = clampedRect.left();
    const qint32 newCustomXEnd = clampedRect.right();
    const qint32 newCustomYStart = clampedRect.top();
    const qint32 newCustomYEnd = clampedRect.bottom();

    const bool changed = (newCustomXStart != customXStart)
        || (newCustomXEnd != customXEnd)
        || (newCustomYStart != customYStart)
        || (newCustomYEnd != customYEnd);

    customXStart = newCustomXStart;
    customXEnd = newCustomXEnd;
    customYStart = newCustomYStart;
    customYEnd = newCustomYEnd;

    if (!wasCustomModeSelected) {
        areaModeCustomRadioButton->setChecked(true);
    }
    if (changed || !wasCustomModeSelected) {
        emit scopeChanged();
    }
}

bool VectorscopeDialog::setAdvancedRenderModeSelected(bool selected)
{
    if (!renderModeComboBox) {
        return false;
    }

    const int targetIndex = selected ? 1 : 0;
    if (renderModeComboBox->currentIndex() != targetIndex) {
        renderModeComboBox->setCurrentIndex(targetIndex);
    }
    return renderModeComboBox->currentIndex() == targetIndex;
}

bool VectorscopeDialog::isAdvancedRenderModeSelected() const
{
    if (!renderModeComboBox) {
        return false;
    }
    return renderModeComboBox->currentIndex() == 1;
}

bool VectorscopeDialog::setFullAreaModeSelected(bool selected)
{
    if (!areaModeActiveRadioButton || !areaModeFullRadioButton) {
        return false;
    }

    QRadioButton *targetButton = selected ? areaModeFullRadioButton : areaModeActiveRadioButton;
    const bool wasChecked = targetButton->isChecked();
    if (!wasChecked) {
        targetButton->setChecked(true);
    }
    applyAreaPreset();
    if (!wasChecked && targetButton->isChecked()) {
        emit scopeChanged();
    }
    return targetButton->isChecked();
}

bool VectorscopeDialog::isFullAreaModeSelected() const
{
    if (!areaModeFullRadioButton) {
        return false;
    }
    return areaModeFullRadioButton->isChecked();
}

QImage VectorscopeDialog::getTraceImage(const ComponentFrame &componentFrame, const TbcMetaData::VideoParameters &videoParameters)
{
    // Scope size and scale
    constexpr qint32 SIZE = 1024;
    constexpr qint32 SCALE = 65536 / SIZE;
    constexpr qint32 HALF_SIZE = SIZE / 2;
    const bool densityMode = isAdvancedRenderModeSelected();
    const bool blendColors = ui->blendColorCheckBox->isChecked();
    const bool multiColour = ui->multiColourCheckBox->isChecked();

    // Intensity (1..10, 5 = the original look).  Density plot: the additive
    // per-point alpha (14 at 5) scales by 2^((i - 5) / 1.5).  Point plot:
    // below 5 the points turn translucent and additive, so sparse areas dim
    // while dense vectors stay bright; above 5 they are drawn 2 px wide and
    // additive, a brighter, bolder trace.
    const int intensity = basicIntensitySlider ? basicIntensitySlider->value() : 5;
    const double intensityScale = std::pow(2.0, (intensity - 5) / 1.5);
    const int densityAlpha = qBound(1, static_cast<int>(std::lround(14.0 * intensityScale)), 255);
    const int pointAlpha = qBound(1, static_cast<int>(std::lround(255.0 * qMin(1.0, intensityScale))), 255);
    const bool boldPoints = !densityMode && intensity > 5;

    if (componentFrame.getWidth() <= 0 || componentFrame.getHeight() <= 0) {
        QImage emptyImage(SIZE, SIZE, QImage::Format_RGB888);
        emptyImage.fill(Qt::black);
        return emptyImage;
    }

    // Define image with width, height and format
    const bool additivePoints = densityMode || pointAlpha < 255 || boldPoints;
    QImage scopeImage(SIZE, SIZE, additivePoints ? QImage::Format_ARGB32 : QImage::Format_RGB888);
    QPainter scopePainter;

    // Set the background to black
    scopeImage.fill(Qt::black);

    // Attach the scope image to the painter
    scopePainter.begin(&scopeImage);

    // Blend the field colors (and build up translucent or bold points)
    if (blendColors || additivePoints) {
        scopePainter.setCompositionMode(QPainter::CompositionMode_Plus);
    }

    // Initialise a cheap, predictable random number generator, for defocussing
    std::minstd_rand randomEngine(12345);
    std::normal_distribution<double> normalDist(0.0, 100.0);

    const bool defocus = ui->defocusCheckBox->isChecked();

    // Skip second field if first only is selected
    qint32 fieldCount = !ui->fieldSelectFirstRadioButton->isChecked() ? 2 : 1;

    // Skip first field if second only is selected
    qint32 startingFrame = !ui->fieldSelectSecondRadioButton->isChecked() ? 0 : 1;

    qint32 xStart = qBound<qint32>(0, videoParameters.activeVideoStart, componentFrame.getWidth() - 1);
    qint32 xEndInclusive = qBound<qint32>(xStart, videoParameters.activeVideoEnd - 1, componentFrame.getWidth() - 1);
    qint32 yStart = qBound<qint32>(0, videoParameters.firstActiveFrameLine, componentFrame.getHeight() - 1);
    qint32 yEndInclusive = qBound<qint32>(yStart, videoParameters.lastActiveFrameLine - 1, componentFrame.getHeight() - 1);

    if (areaModeFullRadioButton && areaModeFullRadioButton->isChecked()) {
        xStart = 0;
        xEndInclusive = componentFrame.getWidth() - 1;
        yStart = 0;
        yEndInclusive = componentFrame.getHeight() - 1;
    } else if (areaModeCustomRadioButton && areaModeCustomRadioButton->isChecked()) {
        xStart = qBound<qint32>(0, customXStart, componentFrame.getWidth() - 1);
        xEndInclusive = qBound<qint32>(xStart, customXEnd, componentFrame.getWidth() - 1);
        yStart = qBound<qint32>(0, customYStart, componentFrame.getHeight() - 1);
        yEndInclusive = qBound<qint32>(yStart, customYEnd, componentFrame.getHeight() - 1);
    }

    const qint32 xEnd = xEndInclusive + 1;
    const qint32 yEnd = yEndInclusive + 1;

    // Multi-colour colourise: see colorizeScopePoint() above - shared with
    // the composite render path.
    const qint32 ireRange = videoParameters.white16bIre - videoParameters.black16bIre;

    for (auto fieldN = startingFrame; fieldN < fieldCount; fieldN++) {
        QColor color = Qt::green;
        if (ui->fieldSelectSecondRadioButton->isChecked()) {
            color = Qt::cyan;
        } else if (ui->fieldSelectFirstRadioButton->isChecked()) {
            color = Qt::yellow;
        } else if ((blendColors || densityMode) && ui->fieldSelectAllRadioButton->isChecked()) {
            color = (fieldN == 0) ? QColor(255, 255, 0) : QColor(0, 255, 255);
        }

        color.setAlpha(densityMode ? densityAlpha : pointAlpha);

        if (!multiColour) {
            scopePainter.setPen(QPen(color, boldPoints ? 2 : 1));
        }

        // For each sample in the selected area, plot its U/V values on the chart
        for (qint32 lineNumber = yStart; lineNumber < yEnd; lineNumber++) {
            if ((lineNumber % 2) != fieldN) {
                continue;
            }

            const auto &uLine = componentFrame.u(lineNumber);
            const auto &vLine = componentFrame.v(lineNumber);
            for (qint32 xPosition = xStart; xPosition < xEnd; xPosition++) {
                // If defocussing, add a random (but normally-distributed) value to U/V
                double uOffset = defocus ? normalDist(randomEngine) : 0.0;
                double vOffset = defocus ? normalDist(randomEngine) : 0.0;

                // On a real vectorscope, U is positive to the right, and V is positive *upwards*
                qint32 x = HALF_SIZE + (static_cast<qint32>(uLine[xPosition] + uOffset) / SCALE);
                qint32 y = HALF_SIZE - (static_cast<qint32>(vLine[xPosition] + vOffset) / SCALE);

                if (multiColour) {
                    QColor pc = colorizeScopePoint(x, y, ireRange);
                    pc.setAlpha(densityMode ? densityAlpha : pointAlpha);
                    scopePainter.setPen(QPen(pc, boldPoints ? 2 : 1));
                }

                scopePainter.drawPoint(x, y);
            }
        }
    }

    // Overlay the graticule, unless it's disabled
    if (!ui->graticuleNoneRadioButton->isChecked()) {
        scopePainter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        drawDecodeOrcStyleGraticule(scopePainter,
                                    SIZE,
                                    HALF_SIZE,
                                    HALF_SIZE - 1,
                                    videoParameters,
                                    ui->graticule75RadioButton->isChecked(),
                                    false);
    }

    // Return the QImage
    scopePainter.end();
    return scopeImage;
}

void VectorscopeDialog::updateScopeLabelPixmap()
{
    if (!ui || !ui->scopeLabel) {
        return;
    }
    if (cachedTraceImage.isNull()) {
        ui->scopeLabel->setPixmap(QPixmap());
        return;
    }

    const QSize targetSize = ui->scopeLabel->size().expandedTo(QSize(1, 1));
    const QPixmap scaledPixmap = QPixmap::fromImage(cachedTraceImage).scaled(
        targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    ui->scopeLabel->setPixmap(scaledPixmap);
}

void VectorscopeDialog::updateAdvancedScopePixmap()
{
    if (!advancedScopeLabel) {
        return;
    }
    if (cachedCompositeImage.isNull()) {
        advancedScopeLabel->setPixmap(QPixmap());
        return;
    }

    const QSize targetSize = advancedScopeLabel->size().expandedTo(QSize(1, 1));
    const QPixmap scaledPixmap = QPixmap::fromImage(cachedCompositeImage).scaled(
        targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    advancedScopeLabel->setPixmap(scaledPixmap);
}

void VectorscopeDialog::resizeEvent(QResizeEvent *event)
{
    QDialog::resizeEvent(event);
    updateScopeLabelPixmap();
    updateAdvancedScopePixmap();
}

// GUI signal handlers ------------------------------------------------------------------------------------------------

void VectorscopeDialog::on_defocusCheckBox_clicked()
{
    emit scopeChanged();
}

void VectorscopeDialog::on_blendColorCheckBox_clicked()
{
    emit scopeChanged();
}

void VectorscopeDialog::on_multiColourCheckBox_clicked()
{
    emit scopeChanged();
}

void VectorscopeDialog::on_graticuleButtonGroup_buttonClicked(QAbstractButton *button)
{
    (void) button;
    emit scopeChanged();
}

void VectorscopeDialog::on_fieldSelectButtonGroup_buttonClicked(QAbstractButton *button)
{
    (void) button;
    emit scopeChanged();
}

void VectorscopeDialog::renderModeComboBoxChanged(int index)
{
    (void) index;
    emit scopeChanged();
}

void VectorscopeDialog::on_areaModeButtonGroup_buttonClicked(QAbstractButton *button)
{
    if (button == areaModeActiveRadioButton || button == areaModeFullRadioButton) {
        applyAreaPreset();
    }

    emit scopeChanged();
}

void VectorscopeDialog::initialiseAdvancedControls()
{
    QVBoxLayout *controlsLayout = qobject_cast<QVBoxLayout *>(ui->frame->layout());
    if (!controlsLayout) {
        return;
    }

    // Render mode as a caption + dropdown, matching the form's plain
    // "Field:" / "Graticule:" labels rather than a framed group.
    QWidget *renderGroup = new QWidget(ui->frame);
    QVBoxLayout *renderLayout = new QVBoxLayout(renderGroup);
    renderLayout->setContentsMargins(0, 0, 0, 0);
    renderLayout->setSpacing(2);
    renderLayout->addWidget(new QLabel(tr("Mode:"), renderGroup));
    renderModeComboBox = new QComboBox(renderGroup);
    renderModeComboBox->addItem(tr("Point plot"));
    renderModeComboBox->addItem(tr("Density plot"));
    renderModeComboBox->setCurrentIndex(0);
    renderLayout->addWidget(renderModeComboBox);
    connect(renderModeComboBox, &QComboBox::currentIndexChanged,
            this, &VectorscopeDialog::renderModeComboBoxChanged);

    // Basic-scope trace intensity for both plot modes (the Advanced tab has
    // its own, tuned to the composite trace).  5 is the original look.
    basicIntensityPanel = new QWidget(ui->frame);
    QVBoxLayout *basicIntensityLayout = new QVBoxLayout(basicIntensityPanel);
    basicIntensityLayout->setContentsMargins(0, 0, 0, 0);
    basicIntensityLayout->setSpacing(2);
    basicIntensityLayout->addWidget(new QLabel(tr("Intensity:"), basicIntensityPanel));
    basicIntensitySlider = new QSlider(Qt::Horizontal, basicIntensityPanel);
    basicIntensitySlider->setRange(1, 10);
    basicIntensitySlider->setValue(5);
    basicIntensitySlider->setToolTip(tr("Trace brightness for point and density plots"));
    basicIntensityLayout->addWidget(basicIntensitySlider);
    connect(basicIntensitySlider, &QSlider::valueChanged, this, &VectorscopeDialog::scopeChanged);

    QGroupBox *areaGroup = new QGroupBox(tr("Scope area"), ui->frame);
    QVBoxLayout *areaLayout = new QVBoxLayout(areaGroup);
    areaModeActiveRadioButton = new QRadioButton(tr("Active video"), areaGroup);
    areaModeFullRadioButton = new QRadioButton(tr("Full frame"), areaGroup);
    areaModeCustomRadioButton = new QRadioButton(tr("Custom"), areaGroup);
    // Full frame by default: it takes in the back porch, so the burst lands
    // on the graticule's burst boxes.
    areaModeFullRadioButton->setChecked(true);
    areaLayout->addWidget(areaModeActiveRadioButton);
    areaLayout->addWidget(areaModeFullRadioButton);
    areaLayout->addWidget(areaModeCustomRadioButton);

    areaModeButtonGroup = new QButtonGroup(this);
    areaModeButtonGroup->addButton(areaModeActiveRadioButton);
    areaModeButtonGroup->addButton(areaModeFullRadioButton);
    areaModeButtonGroup->addButton(areaModeCustomRadioButton);
    connect(areaModeButtonGroup,
            QOverload<QAbstractButton *>::of(&QButtonGroup::buttonClicked),
            this,
            &VectorscopeDialog::on_areaModeButtonGroup_buttonClicked);
    QLabel *customAreaHintLabel = new QLabel(tr("Custom is set from the main-view selection tool."), areaGroup);
    customAreaHintLabel->setWordWrap(true);
    areaLayout->addWidget(customAreaHintLabel);

    renderModePanel = renderGroup;
    areaModeGroupBox = areaGroup;

    const int graticuleLabelIndex = controlsLayout->indexOf(ui->graticuleLabel);
    const int insertIndex = graticuleLabelIndex >= 0 ? graticuleLabelIndex : controlsLayout->count();
    controlsLayout->insertWidget(insertIndex, renderGroup);
    controlsLayout->insertWidget(insertIndex + 1, basicIntensityPanel);
    controlsLayout->insertWidget(insertIndex + 2, areaGroup);

    const int preferredControlsWidth = qMax(renderGroup->sizeHint().width(), areaGroup->sizeHint().width()) + 24;
    ui->frame->setMinimumWidth(qMax(ui->frame->minimumWidth(), preferredControlsWidth));
}

// Advanced (composite) scope tab --------------------------------------------------------------------------------------------

// Build the Basic/Advanced tab structure, mirroring the oscilloscope dialog:
// the original scope widget moves onto the Basic tab unchanged, and the
// Advanced tab carries the composite (measurement) vectorscope with its own
// scope view and controls.  The shared control panel on the right switches
// between the standard-only controls and the composite side panel with the
// tab, so neither page's options displace the other's.
void VectorscopeDialog::setupAdvancedScopeTab()
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

    advancedScopeLabel = new QLabel(scopeAdvancedTab);
    advancedScopeLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    advancedScopeLabel->setMinimumSize(QSize(1, 1));
    advancedScopeLabel->setAlignment(Qt::AlignCenter);
    advancedScopeLabel->setScaledContents(false);
    advancedLayout->addWidget(advancedScopeLabel);

    // Composite side panel in the shared control frame: line view, field
    // view, trace controls and the measurement readouts.
    advancedSidePanel = new QWidget(ui->frame);
    auto *advancedControls = new QVBoxLayout(advancedSidePanel);
    advancedControls->setContentsMargins(0, 0, 0, 0);
    advancedControls->setSpacing(2);

    // Flat bold captions rather than group-box frames: each section holds a
    // single control, so a frame only adds border and padding.
    const auto addCaption = [this, advancedControls](const QString &text, bool spaced) {
        if (spaced) {
            advancedControls->addSpacing(6);
        }
        auto *caption = new QLabel(text, advancedSidePanel);
        QFont captionFont = caption->font();
        captionFont.setBold(true);
        caption->setFont(captionFont);
        advancedControls->addWidget(caption);
    };

    // Line view: which part of each line the composite acquisition samples.
    addCaption(tr("Line view"), false);
    lineViewComboBox = new QComboBox(advancedSidePanel);
    lineViewComboBox->addItem(tr("Active line"));
    lineViewComboBox->addItem(tr("Whole line"));
    lineViewComboBox->addItem(tr("Burst only"));
    lineViewComboBox->setCurrentIndex(1);
    connect(lineViewComboBox, &QComboBox::currentIndexChanged,
            this, &VectorscopeDialog::lineViewComboBoxChanged);
    advancedControls->addWidget(lineViewComboBox);

    // Field view: which lines down the frame the composite acquisition
    // samples.  The spin boxes carry 1-based interlaced frame lines and are
    // enabled only by the Selected line(s) entry.
    addCaption(tr("Field view"), true);
    fieldViewComboBox = new QComboBox(advancedSidePanel);
    fieldViewComboBox->addItem(tr("Active field"));
    fieldViewComboBox->addItem(tr("Whole field"));
    fieldViewComboBox->addItem(tr("Selected line(s)"));
    connect(fieldViewComboBox, &QComboBox::currentIndexChanged,
            this, &VectorscopeDialog::fieldViewComboBoxChanged);
    advancedControls->addWidget(fieldViewComboBox);
    QHBoxLayout *fieldViewRangeLayout = new QHBoxLayout();
    fieldViewRangeLayout->setContentsMargins(0, 0, 0, 0);
    fieldViewRangeLayout->setSpacing(4);
    fieldViewStartSpinBox = new QSpinBox(advancedSidePanel);
    fieldViewEndEnabledCheckBox = new QCheckBox(tr("to"), advancedSidePanel);
    fieldViewEndSpinBox = new QSpinBox(advancedSidePanel);
    fieldViewStartSpinBox->setEnabled(false);
    fieldViewEndEnabledCheckBox->setEnabled(false);
    fieldViewEndSpinBox->setEnabled(false);
    fieldViewRangeLayout->addWidget(fieldViewStartSpinBox, 1);
    fieldViewRangeLayout->addWidget(fieldViewEndEnabledCheckBox);
    fieldViewRangeLayout->addWidget(fieldViewEndSpinBox, 1);
    advancedControls->addLayout(fieldViewRangeLayout);
    connect(fieldViewEndEnabledCheckBox, &QCheckBox::clicked,
            this, &VectorscopeDialog::fieldViewEndEnabledCheckBoxClicked);
    connect(fieldViewStartSpinBox, &QSpinBox::valueChanged,
            this, &VectorscopeDialog::compositeLineSpinBoxChanged);
    connect(fieldViewEndSpinBox, &QSpinBox::valueChanged,
            this, &VectorscopeDialog::compositeLineSpinBoxChanged);

    // Composite trace display: draw the beam path between consecutive
    // samples (on by default, as a CRT scope shows it).  No gain control - the dwell-brightness law runs at a
    // fixed intensity (see getCompositeTraceImage()).
    advancedControls->addSpacing(6);
    traceLinesCheckBox = new QCheckBox(tr("Draw trace lines"), advancedSidePanel);
    traceLinesCheckBox->setChecked(true);
    connect(traceLinesCheckBox, &QCheckBox::clicked,
            this, &VectorscopeDialog::traceLinesCheckBoxClicked);
    advancedControls->addWidget(traceLinesCheckBox);

    // Vertical chroma averaging across same-phase lines, as the decoded
    // scope's 2D decoder does: clean vectors on noisy sources.  Display
    // only - the measurements stay on the raw lines.
    lineAveragingCheckBox = new QCheckBox(tr("Line averaging"), advancedSidePanel);
    lineAveragingCheckBox->setChecked(true);
    lineAveragingCheckBox->setToolTip(
        tr("Average each line with its same-phase neighbours, like a 2D decoder (display only)"));
    connect(lineAveragingCheckBox, &QCheckBox::clicked, this, &VectorscopeDialog::scopeChanged);
    advancedControls->addWidget(lineAveragingCheckBox);

    // Trace intensity for both plot modes (render-only, like the trace
    // lines).  Default 2: compact vectors with the transitions still visible,
    // close to the decoded scope's look; higher values bring out the faint
    // parts of the trace.
    addCaption(tr("Intensity"), true);
    traceIntensitySlider = new QSlider(Qt::Horizontal, advancedSidePanel);
    traceIntensitySlider->setRange(1, 10);
    traceIntensitySlider->setValue(2);
    traceIntensitySlider->setToolTip(tr("Trace brightness for point and density plots"));
    connect(traceIntensitySlider, &QSlider::valueChanged,
            this, &VectorscopeDialog::traceLinesCheckBoxClicked);
    advancedControls->addWidget(traceIntensitySlider);

    // Measurement readout (composite acquisition only), one reading per line.
    addCaption(tr("Measurements"), true);
    measurementLabel = new QLabel(tr("No data"), advancedSidePanel);
    measurementLabel->setWordWrap(true);
    acquisitionInfoLabel = new QLabel(advancedSidePanel);
    acquisitionInfoLabel->setWordWrap(true);
    acquisitionInfoLabel->setVisible(false);
    advancedControls->addWidget(measurementLabel);
    advancedControls->addWidget(acquisitionInfoLabel);

    // Note for sources the composite acquisition cannot demodulate.
    acquisitionNoteLabel = new QLabel(
        tr("Composite acquisition needs PAL, NTSC or PAL-M (SECAM carries FM chroma)."),
        advancedSidePanel);
    acquisitionNoteLabel->setWordWrap(true);
    acquisitionNoteLabel->setVisible(false);
    advancedControls->addWidget(acquisitionNoteLabel);

    advancedControls->addStretch();

    // The composite panel can be taller than the shared control frame, so it
    // lives in a scroll area that swaps in at the Scope-area group's slot:
    // the standard controls keep their places on the Basic tab, and the
    // composite controls scroll rather than overflow the dialog on the
    // Advanced tab.
    advancedScrollArea = new QScrollArea(ui->frame);
    advancedScrollArea->setWidget(advancedSidePanel);
    advancedScrollArea->setWidgetResizable(true);
    advancedScrollArea->setFrameShape(QFrame::NoFrame);
    advancedScrollArea->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    advancedScrollArea->setVisible(false);

    // Stretch 1 so the panel, not the form's spacers, takes the spare height
    // on the Advanced tab.  Hidden widgets are ignored by the layout, so the
    // Basic tab keeps its original spacing.
    if (ui->verticalLayout) {
        const int areaIndex = areaModeGroupBox ? ui->verticalLayout->indexOf(areaModeGroupBox) : -1;
        ui->verticalLayout->insertWidget(areaIndex, advancedScrollArea, 1);
    }

    // The panel is wider than the standard controls (word-wrapped readouts);
    // make sure the frame accommodates it so nothing is clipped.
    ui->frame->setMinimumWidth(qMax(ui->frame->minimumWidth(),
                                    advancedSidePanel->sizeHint().width() + 24));

    scopeTabWidget->addTab(scopeOriginalTab, tr("Basic"));
    scopeTabWidget->addTab(scopeAdvancedTab, tr("Advanced"));
    ui->horizontalLayout->insertWidget(0, scopeTabWidget, 1);

    // Switch the shared control panel between the standard-only controls and
    // the composite side panel with the tab, and let MainWindow know so the
    // scope data path follows the selection.
    const auto updateTabVisibility = [this](int index) {
        const bool isAdvanced = (scopeTabWidget && scopeTabWidget->widget(index) == scopeAdvancedTab);
        if (advancedScrollArea) {
            advancedScrollArea->setVisible(isAdvanced);
        }
        // The Scope-area group belongs to the decoded scope (the composite
        // acquisition has its own Line/Field view controls); the Mode group
        // is shared: point plot scatters the composite samples, density plot
        // renders the dwell-brightness accumulation of a measurement
        // instrument.
        if (areaModeGroupBox) {
            areaModeGroupBox->setVisible(!isAdvanced);
        }
        if (ui->blendColorCheckBox) {
            ui->blendColorCheckBox->setVisible(!isAdvanced);
        }
        if (basicIntensityPanel) {
            basicIntensityPanel->setVisible(!isAdvanced);
        }
        // The form's 40 px expanding spacers would be dead space above the
        // composite panel; close them to a small gap on the Advanced tab and
        // restore the .ui values on the Basic tab.
        for (QSpacerItem *spacer : {ui->verticalSpacer, ui->verticalSpacer_2}) {
            if (!spacer) {
                continue;
            }
            if (isAdvanced) {
                spacer->changeSize(20, 6, QSizePolicy::Minimum, QSizePolicy::Fixed);
            } else {
                spacer->changeSize(20, 40, QSizePolicy::Minimum, QSizePolicy::Expanding);
            }
        }
        if (ui->verticalLayout) {
            ui->verticalLayout->invalidate();
        }
    };
    connect(scopeTabWidget, &QTabWidget::currentChanged, this, [this, updateTabVisibility](int index) {
        updateTabVisibility(index);
        emit scopeChanged();
    });
    updateTabVisibility(scopeTabWidget->currentIndex());
}

bool VectorscopeDialog::setAdvancedTabSelected(bool selected)
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

bool VectorscopeDialog::isAdvancedTabSelected() const
{
    if (!scopeTabWidget || !scopeAdvancedTab) {
        return false;
    }
    return scopeTabWidget->currentWidget() == scopeAdvancedTab;
}

void VectorscopeDialog::setCompositeGeometry(qint32 frameLines)
{
    compositeFrameLines = qMax<qint32>(1, frameLines);
    if (fieldViewStartSpinBox) {
        fieldViewStartSpinBox->setRange(1, compositeFrameLines);
    }
    if (fieldViewEndSpinBox) {
        fieldViewEndSpinBox->setRange(1, compositeFrameLines);
    }
}

tbc::cvbs::CompositeVectorscopeOptions VectorscopeDialog::compositeOptions() const
{
    tbc::cvbs::CompositeVectorscopeOptions options;
    switch (lineViewComboBox ? lineViewComboBox->currentIndex() : 1) {
        case 0:
            options.window = tbc::cvbs::VectorscopeSampleWindow::ActiveLine;
            break;
        case 2:
            options.window = tbc::cvbs::VectorscopeSampleWindow::BurstOnly;
            break;
        default:
            options.window = tbc::cvbs::VectorscopeSampleWindow::WholeLine;
            break;
    }

    options.vertical_average = lineAveragingCheckBox && lineAveragingCheckBox->isChecked();

    const int fieldIndex = fieldViewComboBox ? fieldViewComboBox->currentIndex() : 0;
    const bool selectedLines = (fieldIndex == 2);
    options.active_lines_only = (fieldIndex == 0);

    // The spin boxes show 1-based interlaced frame lines; the engine takes
    // the 0-based inclusive range.
    options.first_line = fieldViewStartSpinBox
                             ? static_cast<quint32>(qMax<qint32>(1, fieldViewStartSpinBox->value()) - 1)
                             : 0;
    if (selectedLines) {
        if (fieldViewEndEnabledCheckBox && fieldViewEndEnabledCheckBox->isChecked()
            && fieldViewEndSpinBox) {
            options.last_line = static_cast<quint32>(qMax<qint32>(1, fieldViewEndSpinBox->value()) - 1);
        } else {
            options.last_line = options.first_line;  // the start line on its own
        }
    } else {
        options.last_line = 0;  // to the last line of the frame
    }
    return options;
}

void VectorscopeDialog::showCompositeTrace(const std::optional<tbc::cvbs::VectorscopeData> &data,
                                           const TbcMetaData::VideoParameters &videoParameters,
                                           const TbcSource::ViewMode& viewMode, const bool isFirstField)
{
    tbcDebugStream() << "VectorscopeDialog::showCompositeTrace(): Called";
    compositeData = data;
    compositeVideoParameters = videoParameters;
    haveCompositeRenderContext = data.has_value();

    updateFieldSelectionControls(viewMode, isFirstField);

    if (acquisitionNoteLabel) {
        acquisitionNoteLabel->setVisible(!(videoParameters.system == PAL
                                           || videoParameters.system == NTSC
                                           || videoParameters.system == PAL_M));
    }

    if (!compositeData || compositeData->samples.empty()) {
        cachedCompositeImage = QImage();
        if (measurementLabel) {
            measurementLabel->setText(tr("No composite data"));
        }
        if (acquisitionInfoLabel) {
            acquisitionInfoLabel->setText(QString());
            acquisitionInfoLabel->setVisible(false);
        }
        updateAdvancedScopePixmap();
        return;
    }

    cachedCompositeImage = getCompositeTraceImage(*compositeData, videoParameters);
    updateMeasurementReadout();
    updateAdvancedScopePixmap();

    // QT Bug workaround for some macOS versions
    #if defined(Q_OS_MACOS)
        repaint();
    #endif
}

QImage VectorscopeDialog::getCompositeTraceImage(const tbc::cvbs::VectorscopeData &compositeData,
                                                 const TbcMetaData::VideoParameters &videoParameters)
{
    constexpr qint32 SIZE = 1024;
    constexpr qint32 HALF_SIZE = SIZE / 2;
    const bool densityMode = isAdvancedRenderModeSelected();
    const bool multiColour = ui->multiColourCheckBox->isChecked();
    const bool defocus = ui->defocusCheckBox->isChecked();
    const bool drawTrace = traceLinesCheckBox && traceLinesCheckBox->isChecked();
    // Trace intensity from the panel slider (1..10, default 2; 5 matches the
    // earlier fixed brightness).  Density
    // plot: the brightness law's exponent is 1 / intensity, so higher values
    // lift the faint transits towards the vectors' brightness.  Point plot:
    // the brightness per unit of dwell doubles every two steps.
    const int traceIntensity = traceIntensitySlider ? traceIntensitySlider->value() : 2;
    const double pointIntensityScale = std::pow(2.0, (traceIntensity - 5) / 2.0);

    // Composite samples are normalised to +/-32767 over the system's
    // blanking-to-white range, which this graticule maps one U10 chroma unit
    // to one pixel.
    const double levelRangeU10 = static_cast<double>(tbc::cvbs::cvbs_white(videoParameters.system)
                                                     - tbc::cvbs::cvbs_blanking(videoParameters.system));
    const double compositeToPixels = levelRangeU10 / 32767.0;
    const qint32 ireRange = videoParameters.white16bIre - videoParameters.black16bIre;

    // Initialise a cheap, predictable random number generator, for defocussing
    std::minstd_rand randomEngine(12345);
    std::normal_distribution<double> normalDist(0.0, 100.0);

    const bool firstFieldOnly = ui->fieldSelectFirstRadioButton->isChecked();
    const bool secondFieldOnly = ui->fieldSelectSecondRadioButton->isChecked();

    QImage scopeImage(SIZE, SIZE, QImage::Format_RGB32);
    scopeImage.fill(Qt::black);

    // A CRT scope's brightness is proportional to how long the beam dwells
    // on a point.  The trace is accumulated as dwell into per-class grids:
    // active picture of field 1 (yellow) and field 2 (cyan), burst (white)
    // and the rest of the line (dim grey).
    //
    // With trace lines on, each sample period is drawn as the beam's path
    // from the previous sample of the same line to this one, the period's
    // dwell spread evenly along that path.  A colour-bar vector, where the
    // beam rests for the width of the bar, then reads bright while the
    // transition to the next bar is a thin, dim line - the classic
    // vectorscope star rather than a cloud of points.  With trace lines off
    // each sample deposits its whole dwell on its own pixel.
    enum DwellGrid {
        kGridPictureField1 = 0,
        kGridPictureField2 = 1,
        kGridBurst = 2,
        kGridBlanking = 3,
        kGridCount = 4
    };
    std::vector<float> grids[kGridCount];
    for (int grid = 0; grid < kGridCount; ++grid) {
        grids[grid].assign(static_cast<size_t>(SIZE) * SIZE, 0.0f);
    }
    float maxDwell[kGridCount] = {0.0f, 0.0f, 0.0f, 0.0f};

    const auto deposit = [&](int grid, double x, double y, float dwell) {
        const qint32 px = static_cast<qint32>(std::lround(x));
        const qint32 py = static_cast<qint32>(std::lround(y));
        if (px < 0 || px >= SIZE || py < 0 || py >= SIZE) {
            return;
        }
        float &cell = grids[grid][static_cast<size_t>(py) * SIZE + px];
        cell += dwell;
        maxDwell[grid] = qMax(maxDwell[grid], cell);
    };

    bool havePrevious = false;
    double previousX = 0.0;
    double previousY = 0.0;
    quint16 previousLine = 0;
    quint8 previousField = 0;
    for (const tbc::cvbs::UVSample &sample : compositeData.samples) {
        if ((firstFieldOnly && sample.field_id != 0)
            || (secondFieldOnly && sample.field_id != 1)) {
            havePrevious = false;
            continue;
        }

        double u = sample.u;
        double v = sample.v;
        if (defocus) {
            u += normalDist(randomEngine);
            v += normalDist(randomEngine);
        }
        const double x = HALF_SIZE + (u * compositeToPixels);
        const double y = HALF_SIZE - (v * compositeToPixels);

        int grid = kGridBlanking;
        if (sample.sample_class == tbc::cvbs::VectorscopeSampleClass::Burst) {
            grid = kGridBurst;
        } else if (sample.sample_class == tbc::cvbs::VectorscopeSampleClass::Picture) {
            grid = (sample.field_id == 0) ? kGridPictureField1 : kGridPictureField2;
        }

        // Join only samples of the same line: the stride and the line ends
        // are where the beam is blanked on a real scope.
        if (drawTrace && havePrevious && previousLine == sample.line_number
            && previousField == sample.field_id) {
            const double dx = x - previousX;
            const double dy = y - previousY;
            const int steps = qMax(1, static_cast<int>(std::ceil(qMax(std::abs(dx), std::abs(dy)))));
            const float dwell = 1.0f / static_cast<float>(steps);
            for (int step = 1; step <= steps; ++step) {
                const double t = static_cast<double>(step) / steps;
                deposit(grid, previousX + (dx * t), previousY + (dy * t), dwell);
            }
        } else {
            deposit(grid, x, y, 1.0f);
        }

        previousX = x;
        previousY = y;
        previousLine = sample.line_number;
        previousField = sample.field_id;
        havePrevious = true;
    }

    // Brightness laws.  Density plot: (dwell / max) ^ (1 / intensity) per
    // class, so the vectors reach full brightness and the transits stay faint
    // but visible.  Point plot: a brightness per unit of dwell that saturates
    // within a few samples, as the additive point scatter did, scaled by the
    // intensity.
    const double kPointBrightnessPerDwell = (110.0 / 255.0) * pointIntensityScale;
    const double exponent = 1.0 / static_cast<double>(traceIntensity);
    const auto brightness = [&](float dwell, int grid) -> double {
        if (dwell <= 0.0f) {
            return 0.0;
        }
        if (densityMode) {
            return (maxDwell[grid] > 0.0f) ? std::pow(dwell / maxDwell[grid], exponent) : 0.0;
        }
        const double perDwell = (grid == kGridBlanking) ? (kPointBrightnessPerDwell / 2.0)
                                                         : kPointBrightnessPerDwell;
        return qMin(1.0, perDwell * dwell);
    };

    // Compose the grids additively, as a phosphor sums the beam's visits:
    // the burst in white, the active picture in the field colours (or
    // colourised by position), and the rest of the line dim.
    for (qint32 y = 0; y < SIZE; ++y) {
        QRgb *scanLine = reinterpret_cast<QRgb *>(scopeImage.scanLine(y));
        for (qint32 x = 0; x < SIZE; ++x) {
            const size_t index = static_cast<size_t>(y) * SIZE + x;
            double red = 0.0;
            double green = 0.0;
            double blue = 0.0;

            const double picture1 = brightness(grids[kGridPictureField1][index], kGridPictureField1);
            if (picture1 > 0.0) {
                const QColor colour = multiColour ? colorizeScopePoint(x, y, ireRange)
                                                  : QColor(255, 255, 0);
                red += picture1 * colour.red();
                green += picture1 * colour.green();
                blue += picture1 * colour.blue();
            }
            const double picture2 = brightness(grids[kGridPictureField2][index], kGridPictureField2);
            if (picture2 > 0.0) {
                const QColor colour = multiColour ? colorizeScopePoint(x, y, ireRange)
                                                  : QColor(0, 255, 255);
                red += picture2 * colour.red();
                green += picture2 * colour.green();
                blue += picture2 * colour.blue();
            }
            const double burst = brightness(grids[kGridBurst][index], kGridBurst);
            if (burst > 0.0) {
                red += burst * 235.0;
                green += burst * 235.0;
                blue += burst * 235.0;
            }
            const double blanking = brightness(grids[kGridBlanking][index], kGridBlanking);
            if (blanking > 0.0) {
                red += blanking * 110.0;
                green += blanking * 110.0;
                blue += blanking * 110.0;
            }

            scanLine[x] = qRgb(qMin(255, static_cast<int>(red)),
                               qMin(255, static_cast<int>(green)),
                               qMin(255, static_cast<int>(blue)));
        }
    }

    QPainter scopePainter(&scopeImage);

    // Overlay the graticule, unless it's disabled
    if (!ui->graticuleNoneRadioButton->isChecked()) {
        scopePainter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        drawDecodeOrcStyleGraticule(scopePainter,
                                    SIZE,
                                    HALF_SIZE,
                                    HALF_SIZE - 1,
                                    videoParameters,
                                    ui->graticule75RadioButton->isChecked(),
                                    true);
    }

    scopePainter.end();
    return scopeImage;
}

void VectorscopeDialog::updateMeasurementReadout()
{
    if (!measurementLabel || !compositeData) {
        return;
    }

    const tbc::cvbs::VectorscopeMeasurements &m = compositeData->measurements;
    if (!m.valid) {
        measurementLabel->setText(tr("No burst reference recovered"));
    } else {
        QStringList readouts;
        readouts << tr("Burst: %1 IRE (%2%)")
                        .arg(QString::number(m.burst_amplitude_ire, 'f', 2),
                             QString::number(m.burst_amplitude_percent, 'f', 1));
        readouts << tr("Jitter: %1°").arg(QString::number(m.burst_phase_jitter_degrees, 'f', 2));
        readouts << tr("Lines: %1").arg(m.burst_line_count);
        if (compositeData->system == PAL || compositeData->system == PAL_M) {
            readouts << tr("V-switch split err: %1°")
                            .arg(QString::number(m.burst_phase_split_error_degrees, 'f', 2));
        }
        readouts << tr("Chroma/burst: %1").arg(QString::number(m.chroma_to_burst_ratio, 'f', 2));
        measurementLabel->setText(readouts.join(QLatin1Char('\n')));
    }

    if (acquisitionInfoLabel) {
        QString windowName;
        switch (compositeData->sample_window) {
            case tbc::cvbs::VectorscopeSampleWindow::BurstOnly: windowName = tr("Burst only"); break;
            case tbc::cvbs::VectorscopeSampleWindow::ActiveLine: windowName = tr("Active line"); break;
            case tbc::cvbs::VectorscopeSampleWindow::WholeLine: windowName = tr("Whole line"); break;
        }
        const QString strideInfo = (compositeData->sample_stride > 1)
                                       ? tr(", stride %1").arg(compositeData->sample_stride)
                                       : QString();
        acquisitionInfoLabel->setText(tr("%1%2\nFrame lines %3–%4")
                                          .arg(windowName, strideInfo)
                                          .arg(compositeData->first_line + 1)
                                          .arg(compositeData->last_line + 1));
        acquisitionInfoLabel->setVisible(true);
    }
}

void VectorscopeDialog::lineViewComboBoxChanged(int index)
{
    (void) index;
    emit scopeChanged();
}

void VectorscopeDialog::fieldViewComboBoxChanged(int index)
{
    // The line-range spin boxes apply only to the Selected line(s) entry.
    const bool rangeEnabled = (index == 2);
    if (fieldViewStartSpinBox) {
        fieldViewStartSpinBox->setEnabled(rangeEnabled);
    }
    if (fieldViewEndEnabledCheckBox) {
        fieldViewEndEnabledCheckBox->setEnabled(rangeEnabled);
    }
    if (fieldViewEndSpinBox) {
        fieldViewEndSpinBox->setEnabled(rangeEnabled && fieldViewEndEnabledCheckBox
                                        && fieldViewEndEnabledCheckBox->isChecked());
    }
    emit scopeChanged();
}

void VectorscopeDialog::fieldViewEndEnabledCheckBoxClicked()
{
    if (fieldViewEndSpinBox && fieldViewEndEnabledCheckBox) {
        fieldViewEndSpinBox->setEnabled(fieldViewEndEnabledCheckBox->isEnabled()
                                        && fieldViewEndEnabledCheckBox->isChecked());
    }
    emit scopeChanged();
}

void VectorscopeDialog::compositeLineSpinBoxChanged()
{
    emit scopeChanged();
}

void VectorscopeDialog::traceLinesCheckBoxClicked()
{
    // Trace lines and intensity are render-only: re-render locally rather
    // than re-running the acquisition.
    if (isAdvancedTabSelected() && compositeData && haveCompositeRenderContext) {
        cachedCompositeImage = getCompositeTraceImage(*compositeData, compositeVideoParameters);
        updateAdvancedScopePixmap();
        return;
    }
    emit scopeChanged();
}

void VectorscopeDialog::updateAreaControlState(const ComponentFrame &componentFrame, const TbcMetaData::VideoParameters &videoParameters)
{

    const qint32 frameWidth = componentFrame.getWidth();
    const qint32 frameHeight = componentFrame.getHeight();
    if (frameWidth <= 0 || frameHeight <= 0) {
        return;
    }

    const bool frameSizeChanged = !hasAreaContext
                                  || frameWidth != areaFrameWidth
                                  || frameHeight != areaFrameHeight;

    areaFrameWidth = frameWidth;
    areaFrameHeight = frameHeight;
    hasAreaContext = true;

    activeXStart = qBound<qint32>(0, videoParameters.activeVideoStart, frameWidth - 1);
    activeXEnd = qBound<qint32>(activeXStart, videoParameters.activeVideoEnd - 1, frameWidth - 1);
    activeYStart = qBound<qint32>(0, videoParameters.firstActiveFrameLine, frameHeight - 1);
    activeYEnd = qBound<qint32>(activeYStart, videoParameters.lastActiveFrameLine - 1, frameHeight - 1);
    if (frameSizeChanged) {
        customXStart = activeXStart;
        customXEnd = activeXEnd;
        customYStart = activeYStart;
        customYEnd = activeYEnd;
    }

    customXStart = qBound<qint32>(0, customXStart, frameWidth - 1);
    customXEnd = qBound<qint32>(0, customXEnd, frameWidth - 1);
    customYStart = qBound<qint32>(0, customYStart, frameHeight - 1);
    customYEnd = qBound<qint32>(0, customYEnd, frameHeight - 1);

    if (customXEnd < customXStart) {
        customXEnd = customXStart;
    }
    if (customYEnd < customYStart) {
        customYEnd = customYStart;
    }

    if (frameSizeChanged || (areaModeActiveRadioButton && areaModeActiveRadioButton->isChecked())
        || (areaModeFullRadioButton && areaModeFullRadioButton->isChecked())) {
        applyAreaPreset();
    }
}

void VectorscopeDialog::applyAreaPreset()
{
    if (!hasAreaContext) {
        return;
    }

    if (areaModeFullRadioButton && areaModeFullRadioButton->isChecked()) {
        customXStart = 0;
        customXEnd = areaFrameWidth - 1;
        customYStart = 0;
        customYEnd = areaFrameHeight - 1;
        return;
    }

    if (areaModeActiveRadioButton && areaModeActiveRadioButton->isChecked()) {
        customXStart = activeXStart;
        customXEnd = activeXEnd;
        customYStart = activeYStart;
        customYEnd = activeYEnd;
    }
}
