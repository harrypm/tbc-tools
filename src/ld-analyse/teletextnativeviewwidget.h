/******************************************************************************
 * teletextnativeviewwidget.h
 * tbc-analyse - TBC output analysis GUI
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Harry Munday
 *
 * This file is part of tbc-tools.
 ******************************************************************************/

#ifndef TELETEXTNATIVEVIEWWIDGET_H
#define TELETEXTNATIVEVIEWWIDGET_H
#include <QColor>

#include <QTimer>
#include <QVector>
#include <QWidget>
class QPainter;
class QRectF;

class TeletextNativeViewWidget : public QWidget
{
public:
    struct Cell {
        QChar character = QChar(u' ');
        quint8 foreground = 7;
        quint8 background = 0;
        bool doubleHeight = false;
        bool doubleHeightLower = false;
        bool flash = false;
        bool conceal = false;
        bool boxed = false;
        bool mosaic = false;
        bool mosaicSeparated = false;
        quint8 mosaicPattern = 0;
    };

    using Row = QVector<Cell>;

    explicit TeletextNativeViewWidget(QWidget *parent = nullptr);

    void setPage(const QVector<Row> &rows);
    void clearPage();
    bool hasPage() const { return !rows_.isEmpty(); }

    void setAssetDirectory(const QString &directoryPath);

    void setAnimationsEnabled(bool enabled);
    bool animationsEnabled() const { return animations_enabled_; }

    void setPlaceholderText(const QString &text);

    static QVector<Row> parseHtmlPage(const QString &htmlContent);

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    static QColor teletextColour(quint8 colourIndex);
    static bool decodeMosaicCharacter(const QChar &character, quint8 *pattern, bool *separated);
    static bool isVisiblyFlashingCell(const Cell &cell);
    static void paintMosaicCell(QPainter &painter, const QRectF &cellRect, quint8 pattern,
                                bool separated, const QColor &foreground);
    static Row padOrTrimRow(const Row &row, qint32 targetColumns);
    static QString resolveFontFamily(const QString &fontPath, const QString &fallbackFamily);

    void refreshFlashTimer();

    QVector<Row> rows_;
    QString placeholder_text_;
    QString asset_directory_;
    QString teletext_font_family_;
    QString teletext_double_font_family_;
    bool animations_enabled_ = true;
    bool flash_lit_ = true;
    bool has_flashing_cells_ = false;
    QTimer flash_timer_;
};

#endif // TELETEXTNATIVEVIEWWIDGET_H
