/******************************************************************************
 * editsegmentswidget.h
 * tbc-analyse - TBC output analysis GUI
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Harry Munday
 *
 * This file is part of tbc-tools.
 ******************************************************************************/

#ifndef EDITSEGMENTSWIDGET_H
#define EDITSEGMENTSWIDGET_H

#include <QVector>
#include <QWidget>

class QLabel;
class QPushButton;
class QSpinBox;
class QVBoxLayout;

// One export segment: an inclusive frame range and the framing offsets
// (samples, frame lines) applied to the global active area for it
struct ExportSegment {
    qint32 start = 1;
    qint32 end = 1;
    qint32 offsetX = 0;
    qint32 offsetY = 0;
};

// Horizontal timeline of the export segments across the whole source
class SegmentTimeline : public QWidget
{
    Q_OBJECT

public:
    explicit SegmentTimeline(QWidget *parent = nullptr);
    void setState(qint32 frameCount, const QVector<ExportSegment> &segments, qint32 currentFrame,
                  qint32 pendingStart);
    QSize sizeHint() const override;

signals:
    void frameClicked(qint32 frame);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

private:
    qint32 frameCount = 0;
    QVector<ExportSegment> segments;
    qint32 currentFrame = 0;
    qint32 pendingStart = 0;
    qint32 frameAtX(int x) const;
    int xAtFrame(qint32 frame) const;
};

// Edit tab: the picture (shared with the Viewer), a segment timeline and the
// controls to make and adjust segments. Segments are exported in order and
// joined into one file; the frames between them are cut. Stored as the
// metadata's UserSegmentsJson.
class EditSegmentsWidget : public QWidget
{
    Q_OBJECT

public:
    explicit EditSegmentsWidget(QWidget *parent = nullptr);

    // Where the main window places the shared picture view
    QVBoxLayout *viewerSlot() const { return viewerLayout; }

    // Load the segments of a newly loaded source (frameCount 0 = no source)
    void setSource(qint32 frameCount, const QString &segmentsJson);
    void setCurrentFrame(qint32 frame);
    // The segment containing |frame|, or -1
    int segmentAt(qint32 frame) const;
    const ExportSegment &segment(int index) const { return segments.at(index); }
    void setSegmentOffsets(int index, qint32 offsetX, qint32 offsetY);

signals:
    // The list changed: the new UserSegmentsJson (empty when there are none)
    void segmentsChanged(const QString &segmentsJson);
    void goToFrameRequested(qint32 frame);

private:
    QVBoxLayout *viewerLayout = nullptr;
    SegmentTimeline *timeline = nullptr;
    QPushButton *startButton = nullptr;
    QPushButton *endButton = nullptr;
    QPushButton *removeButton = nullptr;
    QSpinBox *startSpin = nullptr;
    QSpinBox *endSpin = nullptr;
    QLabel *summaryLabel = nullptr;

    QVector<ExportSegment> segments;
    qint32 frameCount = 0;
    qint32 currentFrame = 0;
    qint32 pendingStart = 0;
    bool updatingControls = false;

    void segmentsEdited();
    void refresh();
};

#endif // EDITSEGMENTSWIDGET_H
