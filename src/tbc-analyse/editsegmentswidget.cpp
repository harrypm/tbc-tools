/******************************************************************************
 * editsegmentswidget.cpp
 * tbc-analyse - TBC output analysis GUI
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Harry Munday
 *
 * This file is part of tbc-tools.
 ******************************************************************************/

#include "editsegmentswidget.h"

#include <algorithm>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

// --- SegmentTimeline -------------------------------------------------------

SegmentTimeline::SegmentTimeline(QWidget *parent) : QWidget(parent)
{
    setMinimumHeight(34);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setCursor(Qt::PointingHandCursor);
}

QSize SegmentTimeline::sizeHint() const
{
    return QSize(400, 34);
}

void SegmentTimeline::setState(qint32 _frameCount, const QVector<ExportSegment> &_segments, qint32 _currentFrame,
                               qint32 _pendingStart)
{
    frameCount = _frameCount;
    segments = _segments;
    currentFrame = _currentFrame;
    pendingStart = _pendingStart;
    update();
}

int SegmentTimeline::xAtFrame(qint32 frame) const
{
    if (frameCount <= 1) return 0;
    return static_cast<int>(static_cast<qint64>(frame - 1) * (width() - 1) / (frameCount - 1));
}

qint32 SegmentTimeline::frameAtX(int x) const
{
    if (frameCount <= 1 || width() <= 1) return 1;
    return qBound(1, 1 + static_cast<qint32>(static_cast<qint64>(x) * (frameCount - 1) / (width() - 1)), frameCount);
}

void SegmentTimeline::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    const QRect bar = rect().adjusted(0, 4, -1, -4);
    // Everything outside a segment is cut: draw it as the dark background
    painter.fillRect(bar, palette().color(QPalette::Base).darker(130));
    if (frameCount <= 0) return;

    const QColor colours[2] = {QColor(70, 140, 220), QColor(60, 180, 120)};
    QFont font = painter.font();
    font.setBold(true);
    painter.setFont(font);
    for (int index = 0; index < segments.size(); ++index) {
        const ExportSegment &segment = segments.at(index);
        const QRect block(QPoint(xAtFrame(segment.start), bar.top()), QPoint(qMax(xAtFrame(segment.start) + 2,
                                                                                    xAtFrame(segment.end)), bar.bottom()));
        const bool current = currentFrame >= segment.start && currentFrame <= segment.end;
        QColor colour = colours[index % 2];
        painter.fillRect(block, current ? colour.lighter(130) : colour);
        painter.setPen(current ? QPen(Qt::white, 2) : QPen(colour.darker(150), 1));
        painter.drawRect(block.adjusted(0, 0, -1, -1));
        if (block.width() > 14) {
            painter.setPen(Qt::white);
            painter.drawText(block, Qt::AlignCenter, QString::number(index + 1));
        }
    }

    if (pendingStart > 0) {
        painter.setPen(QPen(QColor(255, 220, 0), 2, Qt::DashLine));
        painter.drawLine(xAtFrame(pendingStart), rect().top(), xAtFrame(pendingStart), rect().bottom());
    }
    painter.setPen(QPen(QColor(230, 40, 40), 2));
    painter.drawLine(xAtFrame(currentFrame), rect().top(), xAtFrame(currentFrame), rect().bottom());
}

void SegmentTimeline::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && frameCount > 0) emit frameClicked(frameAtX(event->position().toPoint().x()));
}

void SegmentTimeline::mouseMoveEvent(QMouseEvent *event)
{
    if ((event->buttons() & Qt::LeftButton) && frameCount > 0) emit frameClicked(frameAtX(event->position().toPoint().x()));
}

// --- EditSegmentsWidget ----------------------------------------------------

EditSegmentsWidget::EditSegmentsWidget(QWidget *parent) : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    // The shared picture view goes here while the Edit tab is shown
    viewerLayout = new QVBoxLayout();
    viewerLayout->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(viewerLayout, 1);

    timeline = new SegmentTimeline(this);
    layout->addWidget(timeline);

    auto *controls = new QHBoxLayout();
    startButton = new QPushButton(tr("Segment start"), this);
    startButton->setToolTip(tr("Start a segment at the current frame"));
    endButton = new QPushButton(tr("Segment end"), this);
    endButton->setToolTip(tr("End the segment at the current frame"));
    removeButton = new QPushButton(tr("Remove"), this);
    removeButton->setToolTip(tr("Remove the segment at the current frame"));
    controls->addWidget(startButton);
    controls->addWidget(endButton);
    controls->addWidget(removeButton);
    controls->addSpacing(16);

    // Fine adjustment of the segment under the playhead
    const auto addSpin = [this, controls](const QString &label, qint32 minimum, qint32 maximum, qint32 step) {
        controls->addWidget(new QLabel(label, this));
        auto *spin = new QSpinBox(this);
        spin->setRange(minimum, maximum);
        spin->setSingleStep(step);
        controls->addWidget(spin);
        return spin;
    };
    startSpin = addSpin(tr("Start"), 1, 1, 1);
    endSpin = addSpin(tr("End"), 1, 1, 1);
    offsetXSpin = addSpin(tr("X offset"), -400, 400, 1);
    offsetYSpin = addSpin(tr("Y offset"), -200, 200, 2);
    offsetXSpin->setToolTip(tr("Move the active area sideways for this segment (samples)"));
    offsetYSpin->setToolTip(tr("Move the active area up or down for this segment (lines, even: an odd shift swaps the field order)"));
    controls->addStretch();
    summaryLabel = new QLabel(this);
    controls->addWidget(summaryLabel);
    layout->addLayout(controls);

    connect(timeline, &SegmentTimeline::frameClicked, this, &EditSegmentsWidget::goToFrameRequested);
    connect(startButton, &QPushButton::clicked, this, [this]() {
        pendingStart = currentFrame;
        refresh();
    });
    connect(endButton, &QPushButton::clicked, this, [this]() {
        if (pendingStart <= 0) return;
        ExportSegment segment;
        segment.start = qMin(pendingStart, currentFrame);
        segment.end = qMax(pendingStart, currentFrame);
        segments.append(segment);
        pendingStart = 0;
        segmentsEdited();
    });
    connect(removeButton, &QPushButton::clicked, this, [this]() {
        const int index = segmentAt(currentFrame);
        if (index < 0) return;
        segments.removeAt(index);
        segmentsEdited();
    });
    for (QSpinBox *spin : {startSpin, endSpin, offsetXSpin, offsetYSpin}) {
        connect(spin, &QSpinBox::valueChanged, this, [this, spin]() {
            const int index = segmentAt(currentFrame);
            if (updatingControls || index < 0) return;
            if (spin == offsetYSpin && spin->value() % 2 != 0) {
                spin->setValue(spin->value() - (spin->value() % 2));
                return;
            }
            ExportSegment &segment = segments[index];
            segment.start = qMin(startSpin->value(), endSpin->value());
            segment.end = qMax(startSpin->value(), endSpin->value());
            segment.offsetX = offsetXSpin->value();
            segment.offsetY = offsetYSpin->value();
            segmentsEdited();
            // Keep editing this segment if its range no longer covers the playhead
            if (segmentAt(currentFrame) < 0) emit goToFrameRequested(segment.start);
        });
    }

    setSource(0, QString());
}

void EditSegmentsWidget::setSource(qint32 _frameCount, const QString &segmentsJson)
{
    frameCount = _frameCount;
    pendingStart = 0;
    segments.clear();
    for (const QJsonValue &value : QJsonDocument::fromJson(segmentsJson.toUtf8()).array()) {
        const QJsonObject object = value.toObject();
        ExportSegment segment;
        segment.start = object.value(QStringLiteral("start")).toInt();
        segment.end = object.value(QStringLiteral("end")).toInt();
        segment.offsetX = object.value(QStringLiteral("offsetX")).toInt();
        segment.offsetY = object.value(QStringLiteral("offsetY")).toInt();
        if (segment.start >= 1 && segment.end >= segment.start) segments.append(segment);
    }
    refresh();
}

void EditSegmentsWidget::setCurrentFrame(qint32 frame)
{
    if (currentFrame == frame) return;
    currentFrame = frame;
    refresh();
}

int EditSegmentsWidget::segmentAt(qint32 frame) const
{
    for (int index = 0; index < segments.size(); ++index) {
        if (frame >= segments.at(index).start && frame <= segments.at(index).end) return index;
    }
    return -1;
}

void EditSegmentsWidget::setSegmentOffsets(int index, qint32 offsetX, qint32 offsetY)
{
    if (index < 0 || index >= segments.size()) return;
    offsetY -= offsetY % 2;
    if (segments.at(index).offsetX == offsetX && segments.at(index).offsetY == offsetY) return;
    segments[index].offsetX = offsetX;
    segments[index].offsetY = offsetY;
    segmentsEdited();
}

void EditSegmentsWidget::segmentsEdited()
{
    // Timeline order is the export order
    std::stable_sort(segments.begin(), segments.end(),
                     [](const ExportSegment &a, const ExportSegment &b) { return a.start < b.start; });
    refresh();
    QJsonArray array;
    for (const ExportSegment &segment : segments) {
        QJsonObject object;
        object.insert(QStringLiteral("start"), segment.start);
        object.insert(QStringLiteral("end"), segment.end);
        if (segment.offsetX != 0) object.insert(QStringLiteral("offsetX"), segment.offsetX);
        if (segment.offsetY != 0) object.insert(QStringLiteral("offsetY"), segment.offsetY);
        array.append(object);
    }
    emit segmentsChanged(array.isEmpty() ? QString()
                                         : QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact)));
}

void EditSegmentsWidget::refresh()
{
    timeline->setState(frameCount, segments, currentFrame, pendingStart);

    const int index = segmentAt(currentFrame);
    startButton->setEnabled(frameCount > 0);
    endButton->setEnabled(pendingStart > 0);
    removeButton->setEnabled(index >= 0);

    updatingControls = true;
    for (QSpinBox *spin : {startSpin, endSpin, offsetXSpin, offsetYSpin}) spin->setEnabled(index >= 0);
    startSpin->setRange(1, qMax(1, frameCount));
    endSpin->setRange(1, qMax(1, frameCount));
    if (index >= 0) {
        const ExportSegment &segment = segments.at(index);
        startSpin->setValue(segment.start);
        endSpin->setValue(segment.end);
        offsetXSpin->setValue(segment.offsetX);
        offsetYSpin->setValue(segment.offsetY);
    }
    updatingControls = false;

    qint64 totalFrames = 0;
    for (const ExportSegment &segment : segments) totalFrames += segment.end - segment.start + 1;
    if (segments.isEmpty()) {
        summaryLabel->setText(pendingStart > 0 ? tr("Segment starts at %1").arg(pendingStart)
                                               : tr("No segments: the export uses the in/out range"));
    } else {
        summaryLabel->setText((index >= 0 ? tr("Segment %1 of %2").arg(index + 1).arg(segments.size())
                                          : tr("%n segment(s)", "", segments.size()))
                              + tr(", %1 frames exported").arg(totalFrames));
    }
}
