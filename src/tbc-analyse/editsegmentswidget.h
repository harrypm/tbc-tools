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

#include <QWidget>

class QLabel;
class QPushButton;
class QTableWidget;

// Edit tab: an ordered list of export segments (start/end frames), each with
// framing offsets from the global active area. tbc-video-export exports the
// segments in order and joins them into one file; the gaps are cut. Stored as
// the metadata's UserSegmentsJson.
class EditSegmentsWidget : public QWidget
{
    Q_OBJECT

public:
    explicit EditSegmentsWidget(QWidget *parent = nullptr);

    // Load the segments of a newly loaded source (frameCount 0 = no source)
    void setSource(qint32 frameCount, const QString &segmentsJson);
    // Append a segment covering [start, end]
    void addSegment(qint32 start, qint32 end);

signals:
    // The list changed: the new UserSegmentsJson (empty when there are none)
    void segmentsChanged(const QString &segmentsJson);
    // Ask for a segment from the current export in/out points
    void addSegmentRequested();
    void goToFrameRequested(qint32 frame);

private:
    QTableWidget *table = nullptr;
    QPushButton *addButton = nullptr;
    QPushButton *removeButton = nullptr;
    QPushButton *upButton = nullptr;
    QPushButton *downButton = nullptr;
    QPushButton *goToButton = nullptr;
    QLabel *summaryLabel = nullptr;
    qint32 frameCount = 0;
    bool loading = false;

    void insertRow(int row, qint32 start, qint32 end, qint32 offsetX, qint32 offsetY);
    void moveRow(int from, int to);
    void renumber();
    void updateButtons();
    void emitChanged();
    qint32 cellValue(int row, int column) const;
};

#endif // EDITSEGMENTSWIDGET_H
