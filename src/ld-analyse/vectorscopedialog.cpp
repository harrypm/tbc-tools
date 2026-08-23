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
#include <random>

#include <QApplication>
#include <QButtonGroup>
#include <QDebug>
#include <QGroupBox>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
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

void drawTargetBox(QPainter &painter, const QPointF &centerPoint, const QColor &color)
{
    const double halfBox = kTargetBoxSizePixels / 2.0;
    const double halfCrosshair = kTargetCrosshairSizePixels / 2.0;
    const QRectF boxRect(centerPoint.x() - halfBox, centerPoint.y() - halfBox,
                         kTargetBoxSizePixels, kTargetBoxSizePixels);

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

void drawDecodeOrcStyleGraticule(QPainter &scopePainter, qint32 size, qint32 halfSize, qint32 radius,
                                 const TbcMetaData::VideoParameters &videoParameters, bool graticule75)
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
        constexpr std::array<const char *, 7> colorLabels = {"", "B", "G", "Cy", "R", "Mg", "Yl"};
        constexpr qint32 scopeScale = 65536 / 1024;
        const double percent = graticule75 ? 0.75 : 1.0;
        const double uvScale = static_cast<double>(ireRange) / static_cast<double>(scopeScale);

        for (qint32 rgb = 1; rgb < 7; rgb++) {
            const double R = percent * static_cast<double>((rgb >> 2) & 1);
            const double G = percent * static_cast<double>((rgb >> 1) & 1);
            const double B = percent * static_cast<double>(rgb & 1);

            const double U = (R * -0.147141) + (G * -0.288869) + (B * 0.436010);
            const double V = (R * 0.614975)  + (G * -0.514965) + (B * -0.100010);

            const double uScaled = U * uvScale;
            const double vScaled = V * uvScale;
            const QPointF targetPoint(static_cast<double>(halfSize) + uScaled,
                                      static_cast<double>(halfSize) - vScaled);
            const double targetMagnitude = std::hypot(uScaled, vScaled);
            const double targetAngleDegrees = (std::atan2(V, U) * 180.0) / kPi;
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

            const QString labelText = QString::fromUtf8(colorLabels[rgb]);
            const QFontMetrics metrics(labelFont);
            const qint32 textWidth = metrics.horizontalAdvance(labelText);
            const qint32 textHeight = metrics.height();
            scopePainter.drawText(static_cast<qint32>(labelPoint.x()) - (textWidth / 2),
                                  static_cast<qint32>(labelPoint.y()) + (textHeight / 4),
                                  labelText);
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
}

VectorscopeDialog::~VectorscopeDialog()
{
    delete ui;
}

void VectorscopeDialog::showTraceImage(const ComponentFrame &componentFrame, const TbcMetaData::VideoParameters &videoParameters,
                                       const TbcSource::ViewMode& viewMode, const bool isFirstField)
{
    tbcDebugStream() << "VectorscopeDialog::showTraceImage(): Called";

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


    updateAreaControlState(componentFrame, videoParameters);

    // Draw the image
    cachedTraceImage = getTraceImage(componentFrame, videoParameters);
    updateScopeLabelPixmap();

    // QT Bug workaround for some macOS versions
    #if defined(Q_OS_MACOS)
        repaint();
    #endif
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
        if (areaModeActiveRadioButton) {
            areaModeActiveRadioButton->setChecked(true);
        } else if (areaModeFullRadioButton) {
            areaModeFullRadioButton->setChecked(true);
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
    if (!renderModePointsRadioButton || !renderModeDensityRadioButton) {
        return false;
    }

    QRadioButton *targetButton = selected ? renderModeDensityRadioButton : renderModePointsRadioButton;
    if (!targetButton->isChecked()) {
        targetButton->setChecked(true);
    }
    return targetButton->isChecked();
}

bool VectorscopeDialog::isAdvancedRenderModeSelected() const
{
    if (!renderModeDensityRadioButton) {
        return false;
    }
    return renderModeDensityRadioButton->isChecked();
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
    const bool densityMode = renderModeDensityRadioButton && renderModeDensityRadioButton->isChecked();
    const bool blendColors = ui->blendColorCheckBox->isChecked();
    const bool multiColour = ui->multiColourCheckBox->isChecked();

    if (componentFrame.getWidth() <= 0 || componentFrame.getHeight() <= 0) {
        QImage emptyImage(SIZE, SIZE, QImage::Format_RGB888);
        emptyImage.fill(Qt::black);
        return emptyImage;
    }

    // Define image with width, height and format
    QImage scopeImage(SIZE, SIZE, densityMode ? QImage::Format_ARGB32 : QImage::Format_RGB888);
    QPainter scopePainter;

    // Set the background to black
    scopeImage.fill(Qt::black);

    // Attach the scope image to the painter
    scopePainter.begin(&scopeImage);

    // Blend the field colors
    if (blendColors || densityMode) {
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

    // Multi-colour colourise: colour each plotted point by its U/V position on
    // the scope canvas via the BT.601 inverse matrix (ITU-R BT.470-6 / EBU
    // Tech. 3280-E), ported from decode-orc's vectorscope_dialog Pass-2.
    // Independent of the Blend (field-colour) checkbox.
    //
    // The plotted U/V samples are scaled by ireRange in 16-bit units
    // (U_sample = U_bt601 * ireRange), so dividing the recovered canvas U/V by
    // ireRange yields the raw BT.601 U/V coefficient — exactly what decode-orc
    // computes as u_uv / kVectorscopeSignedFullScale (its amplitude_scale).
    // The BT.601 inverse is then applied with ku = 0.492111, kv = 0.877283.
    // Do NOT additionally divide by the 100%-bar magnitudes (0.436010 /
    // 0.614975): that double-normalises and warps the recovered hue.
    const qint32 ireRange = videoParameters.white16bIre - videoParameters.black16bIre;
    auto colorizePoint = [&](qint32 px, qint32 py) -> QColor {
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
        // the centre (U=V=0) gives equal components → white.
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
    };

    for (auto fieldN = startingFrame; fieldN < fieldCount; fieldN++) {
        QColor color = Qt::green;
        if (ui->fieldSelectSecondRadioButton->isChecked()) {
            color = Qt::cyan;
        } else if (ui->fieldSelectFirstRadioButton->isChecked()) {
            color = Qt::yellow;
        } else if ((blendColors || densityMode) && ui->fieldSelectAllRadioButton->isChecked()) {
            color = (fieldN == 0) ? QColor(255, 255, 0) : QColor(0, 255, 255);
        }

        if (densityMode) {
            color.setAlpha(14);
        }

        if (!multiColour) {
            scopePainter.setPen(color);
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
                    QColor pc = colorizePoint(x, y);
                    if (densityMode) pc.setAlpha(14);
                    scopePainter.setPen(pc);
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
                                    ui->graticule75RadioButton->isChecked());
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

void VectorscopeDialog::resizeEvent(QResizeEvent *event)
{
    QDialog::resizeEvent(event);
    updateScopeLabelPixmap();
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

void VectorscopeDialog::on_renderModeButtonGroup_buttonClicked(QAbstractButton *button)
{
    (void) button;
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

    QGroupBox *renderGroup = new QGroupBox(tr("Mode"), ui->frame);
    QVBoxLayout *renderLayout = new QVBoxLayout(renderGroup);
    renderModePointsRadioButton = new QRadioButton(tr("Point plot"), renderGroup);
    renderModeDensityRadioButton = new QRadioButton(tr("Density plot"), renderGroup);
    renderModePointsRadioButton->setChecked(true);
    renderLayout->addWidget(renderModePointsRadioButton);
    renderLayout->addWidget(renderModeDensityRadioButton);

    renderModeButtonGroup = new QButtonGroup(this);
    renderModeButtonGroup->addButton(renderModePointsRadioButton);
    renderModeButtonGroup->addButton(renderModeDensityRadioButton);
    connect(renderModeButtonGroup,
            QOverload<QAbstractButton *>::of(&QButtonGroup::buttonClicked),
            this,
            &VectorscopeDialog::on_renderModeButtonGroup_buttonClicked);

    QGroupBox *areaGroup = new QGroupBox(tr("Scope area"), ui->frame);
    QVBoxLayout *areaLayout = new QVBoxLayout(areaGroup);
    areaModeActiveRadioButton = new QRadioButton(tr("Active video"), areaGroup);
    areaModeFullRadioButton = new QRadioButton(tr("Full frame"), areaGroup);
    areaModeCustomRadioButton = new QRadioButton(tr("Custom"), areaGroup);
    areaModeActiveRadioButton->setChecked(true);
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

    const int graticuleLabelIndex = controlsLayout->indexOf(ui->graticuleLabel);
    const int insertIndex = graticuleLabelIndex >= 0 ? graticuleLabelIndex : controlsLayout->count();
    controlsLayout->insertWidget(insertIndex, renderGroup);
    controlsLayout->insertWidget(insertIndex + 1, areaGroup);

    const int preferredControlsWidth = qMax(renderGroup->sizeHint().width(), areaGroup->sizeHint().width()) + 24;
    ui->frame->setMinimumWidth(qMax(ui->frame->minimumWidth(), preferredControlsWidth));
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
