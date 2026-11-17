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

#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
enum Column { StartColumn = 0, EndColumn, OffsetXColumn, OffsetYColumn, ColumnCount };
}

EditSegmentsWidget::EditSegmentsWidget(QWidget *parent) : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);

    auto *intro = new QLabel(tr("Segments are exported in order and joined into one file; the frames between "
                                "them are cut. X and Y offsets move the active area for that segment "
                                "(samples, and lines in steps of 2) so every segment lines up."), this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    table = new QTableWidget(0, ColumnCount, this);
    table->setHorizontalHeaderLabels({tr("Start"), tr("End"), tr("X offset"), tr("Y offset")});
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(table, 1);

    auto *buttons = new QHBoxLayout();
    addButton = new QPushButton(tr("Add segment from in/out"), this);
    removeButton = new QPushButton(tr("Remove"), this);
    upButton = new QPushButton(tr("Move up"), this);
    downButton = new QPushButton(tr("Move down"), this);
    goToButton = new QPushButton(tr("Go to start"), this);
    for (QPushButton *button : {addButton, removeButton, upButton, downButton, goToButton}) {
        buttons->addWidget(button);
    }
    buttons->addStretch();
    layout->addLayout(buttons);

    summaryLabel = new QLabel(this);
    layout->addWidget(summaryLabel);

    connect(addButton, &QPushButton::clicked, this, &EditSegmentsWidget::addSegmentRequested);
    connect(removeButton, &QPushButton::clicked, this, [this]() {
        const int row = table->currentRow();
        if (row < 0) return;
        table->removeRow(row);
        renumber();
        emitChanged();
    });
    connect(upButton, &QPushButton::clicked, this, [this]() { moveRow(table->currentRow(), table->currentRow() - 1); });
    connect(downButton, &QPushButton::clicked, this, [this]() { moveRow(table->currentRow(), table->currentRow() + 1); });
    connect(goToButton, &QPushButton::clicked, this, [this]() {
        if (table->currentRow() >= 0) emit goToFrameRequested(cellValue(table->currentRow(), StartColumn));
    });
    connect(table, &QTableWidget::currentCellChanged, this, [this]() { updateButtons(); });

    setSource(0, QString());
}

void EditSegmentsWidget::setSource(qint32 _frameCount, const QString &segmentsJson)
{
    loading = true;
    frameCount = _frameCount;
    table->setRowCount(0);
    const QJsonArray segments = QJsonDocument::fromJson(segmentsJson.toUtf8()).array();
    for (const QJsonValue &value : segments) {
        const QJsonObject segment = value.toObject();
        insertRow(table->rowCount(), segment.value(QStringLiteral("start")).toInt(),
                  segment.value(QStringLiteral("end")).toInt(), segment.value(QStringLiteral("offsetX")).toInt(),
                  segment.value(QStringLiteral("offsetY")).toInt());
    }
    renumber();
    loading = false;
    updateButtons();
}

void EditSegmentsWidget::addSegment(qint32 start, qint32 end)
{
    insertRow(table->rowCount(), start, end, 0, 0);
    renumber();
    table->setCurrentCell(table->rowCount() - 1, StartColumn);
    emitChanged();
}

void EditSegmentsWidget::insertRow(int row, qint32 start, qint32 end, qint32 offsetX, qint32 offsetY)
{
    table->insertRow(row);
    const qint32 maxFrame = qMax(1, frameCount);
    struct Cell { qint32 minimum, maximum, step, value; };
    const Cell cells[ColumnCount] = {
        {1, maxFrame, 1, qBound(1, start, maxFrame)},
        {1, maxFrame, 1, qBound(1, end, maxFrame)},
        {-400, 400, 1, offsetX},
        {-200, 200, 2, offsetY - (offsetY % 2)},
    };
    for (int column = 0; column < ColumnCount; ++column) {
        auto *spin = new QSpinBox(table);
        spin->setRange(cells[column].minimum, cells[column].maximum);
        spin->setSingleStep(cells[column].step);
        spin->setValue(cells[column].value);
        spin->setFrame(false);
        connect(spin, &QSpinBox::valueChanged, this, [this, spin, column]() {
            // Keep each segment's start <= end, and Y offsets even
            int row = -1;
            for (int r = 0; r < table->rowCount() && row < 0; ++r) {
                if (table->cellWidget(r, column) == spin) row = r;
            }
            if (row < 0) return;
            if (column == OffsetYColumn && spin->value() % 2 != 0) {
                spin->setValue(spin->value() - (spin->value() % 2));
                return;
            }
            auto *startSpin = qobject_cast<QSpinBox *>(table->cellWidget(row, StartColumn));
            auto *endSpin = qobject_cast<QSpinBox *>(table->cellWidget(row, EndColumn));
            if (startSpin && endSpin && startSpin->value() > endSpin->value()) {
                if (column == StartColumn) endSpin->setValue(startSpin->value());
                else startSpin->setValue(endSpin->value());
                return;
            }
            emitChanged();
        });
        table->setCellWidget(row, column, spin);
    }
}

void EditSegmentsWidget::moveRow(int from, int to)
{
    if (from < 0 || to < 0 || from >= table->rowCount() || to >= table->rowCount()) return;
    const qint32 values[ColumnCount] = {cellValue(from, StartColumn), cellValue(from, EndColumn),
                                        cellValue(from, OffsetXColumn), cellValue(from, OffsetYColumn)};
    loading = true;
    table->removeRow(from);
    insertRow(to, values[StartColumn], values[EndColumn], values[OffsetXColumn], values[OffsetYColumn]);
    loading = false;
    renumber();
    table->setCurrentCell(to, StartColumn);
    emitChanged();
}

void EditSegmentsWidget::renumber()
{
    QStringList labels;
    qint64 totalFrames = 0;
    for (int row = 0; row < table->rowCount(); ++row) {
        labels << tr("Segment %1").arg(row + 1);
        totalFrames += cellValue(row, EndColumn) - cellValue(row, StartColumn) + 1;
    }
    table->setVerticalHeaderLabels(labels);
    summaryLabel->setText(table->rowCount() == 0
                              ? tr("No segments: the export uses the in/out range.")
                              : tr("%n segment(s), %1 frames exported.", "", table->rowCount()).arg(totalFrames));
}

void EditSegmentsWidget::updateButtons()
{
    const int row = table->currentRow();
    addButton->setEnabled(frameCount > 0);
    removeButton->setEnabled(row >= 0);
    upButton->setEnabled(row > 0);
    downButton->setEnabled(row >= 0 && row < table->rowCount() - 1);
    goToButton->setEnabled(row >= 0);
}

void EditSegmentsWidget::emitChanged()
{
    renumber();
    updateButtons();
    if (loading) return;
    QJsonArray segments;
    for (int row = 0; row < table->rowCount(); ++row) {
        QJsonObject segment;
        segment.insert(QStringLiteral("start"), cellValue(row, StartColumn));
        segment.insert(QStringLiteral("end"), cellValue(row, EndColumn));
        if (cellValue(row, OffsetXColumn) != 0) segment.insert(QStringLiteral("offsetX"), cellValue(row, OffsetXColumn));
        if (cellValue(row, OffsetYColumn) != 0) segment.insert(QStringLiteral("offsetY"), cellValue(row, OffsetYColumn));
        segments.append(segment);
    }
    emit segmentsChanged(segments.isEmpty() ? QString()
                                            : QString::fromUtf8(QJsonDocument(segments).toJson(QJsonDocument::Compact)));
}

qint32 EditSegmentsWidget::cellValue(int row, int column) const
{
    const auto *spin = qobject_cast<QSpinBox *>(table->cellWidget(row, column));
    return spin ? spin->value() : 0;
}
