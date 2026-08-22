/******************************************************************************
 * teletextnativeviewwidget.cpp
 * tbc-analyse - TBC output analysis GUI
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Harry Munday
 *
 * This file is part of tbc-tools.
 ******************************************************************************/

#include "teletextnativeviewwidget.h"

#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QHash>
#include <QHideEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QRegularExpression>
#include <QShowEvent>
#include <QTextDocument>
#include <QXmlStreamReader>

namespace {
constexpr qint32 kTeletextColumns = 40;
constexpr qint32 kTeletextRows = 25;
constexpr qreal kCellAspectWidth = 12.0;
constexpr qreal kCellAspectHeight = 20.0;
constexpr qint32 kFlashIntervalMs = 500;

struct CellStyle {
    quint8 foreground = 7;
    quint8 background = 0;
    bool doubleHeight = false;
    bool flash = false;
    bool conceal = false;
    bool boxed = false;
};

QString extractSubpageFragment(const QString &htmlContent)
{
    static const QRegularExpression subpageExpression(
        QStringLiteral("<div\\s+class\\s*=\\s*\"subpage\"[^>]*>.*?</div>"),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption
    );
    const QRegularExpressionMatch subpageMatch = subpageExpression.match(htmlContent);
    if (subpageMatch.hasMatch()) {
        return subpageMatch.captured(0);
    }
    return htmlContent;
}

bool decodeMosaicCharacterImpl(const QChar &character, quint8 *pattern, bool *separated)
{
    if (!pattern || !separated) {
        return false;
    }

    const char32_t codePoint = static_cast<char32_t>(character.unicode());
    quint8 code = 0;
    bool isSeparated = false;

    if ((codePoint >= 0xEE00 && codePoint <= 0xEE1F)
        || (codePoint >= 0xEE40 && codePoint <= 0xEE5F)) {
        code = static_cast<quint8>(codePoint - 0xEDE0);
        isSeparated = true;
    } else if ((codePoint >= 0xEE20 && codePoint <= 0xEE3F)
               || (codePoint >= 0xEE60 && codePoint <= 0xEE7F)) {
        code = static_cast<quint8>(codePoint - 0xEE00);
        isSeparated = false;
    } else {
        return false;
    }

    *pattern = static_cast<quint8>((code & 0x1F) | ((code >> 1) & 0x20));
    *separated = isSeparated;
    return true;
}

bool isVisiblyFlashingCellImpl(const TeletextNativeViewWidget::Cell &cell)
{
    if (!cell.flash || cell.doubleHeightLower || cell.conceal) {
        return false;
    }
    if (cell.mosaic) {
        return cell.mosaicPattern != 0;
    }
    return !cell.character.isSpace();
}

void paintMosaicCellImpl(QPainter &painter, const QRectF &cellRect, quint8 pattern,
                         bool separated, const QColor &foreground)
{
    if (pattern == 0) {
        return;
    }

    const qreal blockWidth = cellRect.width() / 2.0;
    const qreal blockHeight = cellRect.height() / 3.0;
    const qreal insetX = separated ? blockWidth / 6.0 : 0.0;
    const qreal insetY = separated ? blockHeight / 6.0 : 0.0;

    for (qint32 block = 0; block < 6; ++block) {
        if ((pattern & (1U << block)) == 0) {
            continue;
        }
        const qint32 blockColumn = block % 2;
        const qint32 blockRow = block / 2;
        QRectF blockRect(cellRect.left() + blockColumn * blockWidth,
                         cellRect.top() + blockRow * blockHeight,
                         blockWidth,
                         blockHeight);
        blockRect.adjust(insetX, insetY, -insetX, -insetY);
        painter.fillRect(blockRect, foreground);
    }
}

CellStyle styleFromClassAttribute(const CellStyle &baseStyle, const QString &classAttribute)
{
    CellStyle style = baseStyle;
    const QStringList tokens = classAttribute.split(QRegularExpression(QStringLiteral("\\s+")),
                                                    Qt::SkipEmptyParts);
    for (const QString &tokenRaw : tokens) {
        const QString token = tokenRaw.trimmed().toLower();
        if (token.size() >= 2 && token.startsWith(QLatin1Char('f'))) {
            bool ok = false;
            const qint32 value = token.mid(1).toInt(&ok);
            if (ok && value >= 0 && value <= 7) {
                style.foreground = static_cast<quint8>(value);
            }
            continue;
        }
        if (token.size() >= 2 && token.startsWith(QLatin1Char('b'))) {
            bool ok = false;
            const qint32 value = token.mid(1).toInt(&ok);
            if (ok && value >= 0 && value <= 7) {
                style.background = static_cast<quint8>(value);
            }
            continue;
        }
        if (token == QStringLiteral("dh")) {
            style.doubleHeight = true;
            continue;
        }
        if (token == QStringLiteral("fl")) {
            style.flash = true;
            continue;
        }
        if (token == QStringLiteral("cn")) {
            style.conceal = true;
            continue;
        }
        if (token == QStringLiteral("bx")) {
            style.boxed = true;
            continue;
        }
        if (token == QStringLiteral("nx")) {
            style.boxed = false;
            continue;
        }
    }
    return style;
}

TeletextNativeViewWidget::Cell makeCell(const QChar &character, const CellStyle &style)
{
    TeletextNativeViewWidget::Cell cell;
    cell.character = character;
    cell.foreground = style.foreground;
    cell.background = style.background;
    cell.doubleHeight = style.doubleHeight;
    cell.flash = style.flash;
    cell.conceal = style.conceal;
    cell.boxed = style.boxed;
    return cell;
}

TeletextNativeViewWidget::Row defaultTeletextRow(qint32 columns)
{
    TeletextNativeViewWidget::Row row;
    row.reserve(columns);
    for (qint32 i = 0; i < columns; ++i) {
        row.append(TeletextNativeViewWidget::Cell());
    }
    return row;
}
} // namespace

bool TeletextNativeViewWidget::decodeMosaicCharacter(const QChar &character,
                                                     quint8 *pattern, bool *separated)
{
    return decodeMosaicCharacterImpl(character, pattern, separated);
}

bool TeletextNativeViewWidget::isVisiblyFlashingCell(const Cell &cell)
{
    return isVisiblyFlashingCellImpl(cell);
}

void TeletextNativeViewWidget::paintMosaicCell(QPainter &painter, const QRectF &cellRect,
                                               quint8 pattern, bool separated,
                                               const QColor &foreground)
{
    paintMosaicCellImpl(painter, cellRect, pattern, separated, foreground);
}

TeletextNativeViewWidget::TeletextNativeViewWidget(QWidget *parent)
    : QWidget(parent),
      placeholder_text_(QObject::tr("No teletext page loaded"))
{
    setMinimumSize(480, 320);
    setAutoFillBackground(false);

    flash_timer_.setInterval(kFlashIntervalMs);
    flash_timer_.setSingleShot(false);
    connect(&flash_timer_, &QTimer::timeout, this, [this]() {
        flash_lit_ = !flash_lit_;
        update();
    });

    teletext_font_family_ = QStringLiteral("Courier New");
    teletext_double_font_family_ = teletext_font_family_;
}

void TeletextNativeViewWidget::setPage(const QVector<Row> &rows)
{
    rows_.clear();
    rows_.reserve(kTeletextRows);

    has_flashing_cells_ = false;
    for (qint32 rowIndex = 0; rowIndex < rows.size() && rowIndex < kTeletextRows; ++rowIndex) {
        rows_.append(padOrTrimRow(rows.at(rowIndex), kTeletextColumns));
    }

    while (rows_.size() < kTeletextRows) {
        rows_.append(defaultTeletextRow(kTeletextColumns));
    }

    for (qint32 rowIndex = 0; rowIndex < rows_.size(); ++rowIndex) {
        Row &row = rows_[rowIndex];
        for (qint32 columnIndex = 0; columnIndex < row.size(); ++columnIndex) {
            Cell &cell = row[columnIndex];
            quint8 mosaicPattern = 0;
            bool mosaicSeparated = false;
            if (decodeMosaicCharacter(cell.character, &mosaicPattern, &mosaicSeparated)) {
                cell.mosaic = true;
                cell.mosaicSeparated = mosaicSeparated;
                cell.mosaicPattern = mosaicPattern;
                cell.character = QChar(u' ');
            }
        }
    }

    QVector<bool> isLowerRow(kTeletextRows, false);
    for (qint32 rowIndex = 0; rowIndex + 1 < rows_.size(); ++rowIndex) {
        if (isLowerRow.at(rowIndex)) {
            continue;
        }
        const Row &originRow = rows_.at(rowIndex);
        bool rowHasDoubleHeight = false;
        for (const Cell &cell : originRow) {
            if (cell.doubleHeight) {
                rowHasDoubleHeight = true;
                break;
            }
        }
        if (!rowHasDoubleHeight) {
            continue;
        }

        const qint32 lowerRowIndex = rowIndex + 1;
        isLowerRow[lowerRowIndex] = true;
        Row &lowerRow = rows_[lowerRowIndex];
        const qint32 columnCount = qMin(originRow.size(), lowerRow.size());
        for (qint32 columnIndex = 0; columnIndex < columnCount; ++columnIndex) {
            Cell lowerCell;
            lowerCell.background = originRow.at(columnIndex).background;
            lowerCell.doubleHeightLower = true;
            lowerCell.boxed = originRow.at(columnIndex).boxed;
            lowerRow[columnIndex] = lowerCell;
        }
    }

    has_flashing_cells_ = false;
    for (const Row &row : rows_) {
        for (const Cell &cell : row) {
            if (!isVisiblyFlashingCell(cell)) {
                continue;
            }
            has_flashing_cells_ = true;
            break;
        }
        if (has_flashing_cells_) {
            break;
        }
    }

    refreshFlashTimer();
    update();
}

void TeletextNativeViewWidget::clearPage()
{
    rows_.clear();
    has_flashing_cells_ = false;
    refreshFlashTimer();
    update();
}

void TeletextNativeViewWidget::setAssetDirectory(const QString &directoryPath)
{
    const QString normalizedPath = QDir::cleanPath(directoryPath.trimmed());
    if (asset_directory_ == normalizedPath) {
        return;
    }
    asset_directory_ = normalizedPath;

    const QString fallbackFamily = QStringLiteral("Courier New");
    const QString teletext2Path = QDir(asset_directory_).filePath(QStringLiteral("teletext2.ttf"));
    const QString teletext4Path = QDir(asset_directory_).filePath(QStringLiteral("teletext4.ttf"));
    teletext_font_family_ = resolveFontFamily(teletext2Path, fallbackFamily);
    teletext_double_font_family_ = resolveFontFamily(teletext4Path, teletext_font_family_);
    update();
}

void TeletextNativeViewWidget::setAnimationsEnabled(bool enabled)
{
    if (animations_enabled_ == enabled) {
        return;
    }
    animations_enabled_ = enabled;
    refreshFlashTimer();
    update();
}

void TeletextNativeViewWidget::setPlaceholderText(const QString &text)
{
    placeholder_text_ = text;
    update();
}

QVector<TeletextNativeViewWidget::Row> TeletextNativeViewWidget::parseHtmlPage(const QString &htmlContent)
{
    QVector<Row> parsedRows;
    const QString subpageFragment = extractSubpageFragment(htmlContent);
    if (subpageFragment.trimmed().isEmpty()) {
        return parsedRows;
    }

    QXmlStreamReader xmlReader(QStringLiteral("<root>%1</root>").arg(subpageFragment));
    QVector<CellStyle> styleStack;
    styleStack.reserve(32);
    styleStack.append(CellStyle());

    QVector<bool> spanRowStack;
    spanRowStack.reserve(32);

    Row currentRow;
    bool inRow = false;
    auto flushCurrentRow = [&]() {
        if (!inRow) {
            return;
        }
        parsedRows.append(padOrTrimRow(currentRow, kTeletextColumns));
        currentRow.clear();
        inRow = false;
    };

    while (!xmlReader.atEnd()) {
        xmlReader.readNext();
        if (xmlReader.isStartElement()) {
            const QString name = xmlReader.name().toString();
            if (name.compare(QStringLiteral("span"), Qt::CaseInsensitive) == 0) {
                const QString classAttribute = xmlReader.attributes().value(QStringLiteral("class")).toString();
                const QStringList classTokens = classAttribute.split(
                    QRegularExpression(QStringLiteral("\\s+")),
                    Qt::SkipEmptyParts
                );
                const bool isRowSpan = classTokens.contains(QStringLiteral("row"), Qt::CaseInsensitive);

                CellStyle nextStyle = styleStack.isEmpty() ? CellStyle() : styleStack.constLast();
                if (!isRowSpan) {
                    nextStyle = styleFromClassAttribute(nextStyle, classAttribute);
                }
                styleStack.append(nextStyle);
                spanRowStack.append(isRowSpan);
                if (isRowSpan) {
                    flushCurrentRow();
                    inRow = true;
                }
            }
            continue;
        }

        if (xmlReader.isEndElement()) {
            const QString name = xmlReader.name().toString();
            if (name.compare(QStringLiteral("span"), Qt::CaseInsensitive) == 0) {
                bool wasRowSpan = false;
                if (!spanRowStack.isEmpty()) {
                    wasRowSpan = spanRowStack.takeLast();
                }
                if (styleStack.size() > 1) {
                    styleStack.removeLast();
                }
                if (wasRowSpan) {
                    flushCurrentRow();
                }
            }
            continue;
        }

        if (xmlReader.isCharacters() && inRow) {
            const QString text = xmlReader.text().toString();
            if (text.isEmpty()) {
                continue;
            }
            const CellStyle style = styleStack.isEmpty() ? CellStyle() : styleStack.constLast();
            for (const QChar &character : text) {
                if (character == QLatin1Char('\r') || character == QLatin1Char('\n')) {
                    continue;
                }
                currentRow.append(makeCell(character, style));
            }
        }
    }
    flushCurrentRow();

    if (xmlReader.hasError()) {
        parsedRows.clear();
    }

    if (parsedRows.isEmpty()) {
        QTextDocument plainDocument;
        plainDocument.setHtml(htmlContent);
        const QStringList plainLines = plainDocument.toPlainText().split(QLatin1Char('\n'));
        for (const QString &line : plainLines) {
            if (line.trimmed().isEmpty()) {
                continue;
            }
            Row row;
            row.reserve(kTeletextColumns);
            for (const QChar &character : line) {
                if (character == QLatin1Char('\r') || character == QLatin1Char('\n')) {
                    continue;
                }
                row.append(makeCell(character, CellStyle()));
                if (row.size() >= kTeletextColumns) {
                    break;
                }
            }
            parsedRows.append(padOrTrimRow(row, kTeletextColumns));
            if (parsedRows.size() >= kTeletextRows) {
                break;
            }
        }
    }

    while (parsedRows.size() < kTeletextRows) {
        parsedRows.append(defaultTeletextRow(kTeletextColumns));
    }
    if (parsedRows.size() > kTeletextRows) {
        parsedRows.resize(kTeletextRows);
    }

    return parsedRows;
}

void TeletextNativeViewWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)

    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);

    if (rows_.isEmpty()) {
        painter.setPen(Qt::gray);
        painter.drawText(rect(), Qt::AlignCenter, placeholder_text_);
        return;
    }

    const qreal pageAspect = (kTeletextColumns * kCellAspectWidth) / (kTeletextRows * kCellAspectHeight);
    qreal pageWidth = width();
    qreal pageHeight = pageWidth / pageAspect;
    if (pageHeight > height()) {
        pageHeight = height();
        pageWidth = pageHeight * pageAspect;
    }

    const QRectF pageRect((width() - pageWidth) / 2.0,
                          (height() - pageHeight) / 2.0,
                          pageWidth,
                          pageHeight);

    const qreal cellWidth = pageRect.width() / kTeletextColumns;
    const qreal cellHeight = pageRect.height() / kTeletextRows;

    auto cellRect = [&pageRect, cellWidth, cellHeight](qint32 rowIndex, qint32 columnIndex) {
        return QRectF(pageRect.left() + columnIndex * cellWidth,
                      pageRect.top() + rowIndex * cellHeight,
                      cellWidth,
                      cellHeight);
    };

    for (qint32 rowIndex = 0; rowIndex < rows_.size(); ++rowIndex) {
        const Row &row = rows_.at(rowIndex);
        for (qint32 columnIndex = 0; columnIndex < row.size(); ++columnIndex) {
            const Cell &cell = row.at(columnIndex);
            if (cell.doubleHeightLower) {
                continue;
            }
            painter.fillRect(cellRect(rowIndex, columnIndex), teletextColour(cell.background));
        }
    }
    painter.setClipRect(pageRect);

    QFont regularFont(teletext_font_family_);
    regularFont.setStyleHint(QFont::Monospace);
    regularFont.setPixelSize(qMax(1, static_cast<qint32>(cellHeight * 0.95)));

    QFont doubleHeightFont(teletext_double_font_family_);
    doubleHeightFont.setStyleHint(QFont::Monospace);
    doubleHeightFont.setPixelSize(qMax(1, static_cast<qint32>(cellHeight * 1.85)));

    painter.setRenderHint(QPainter::TextAntialiasing, false);

    for (qint32 rowIndex = 0; rowIndex < rows_.size(); ++rowIndex) {
        const Row &row = rows_.at(rowIndex);
        for (qint32 columnIndex = 0; columnIndex < row.size(); ++columnIndex) {
            const Cell &cell = row.at(columnIndex);
            if (cell.conceal) {
                continue;
            }
            if (cell.flash && animations_enabled_ && !flash_lit_) {
                continue;
            }

            QRectF drawRect = cellRect(rowIndex, columnIndex);
            if (cell.doubleHeight) {
                drawRect.setHeight(qMin(pageRect.bottom() - drawRect.top(), cellHeight * 2.0));
            }
            if (cell.mosaic) {
                paintMosaicCell(painter, drawRect, cell.mosaicPattern, cell.mosaicSeparated,
                                teletextColour(cell.foreground));
                continue;
            }
            if (cell.character.isSpace()) {
                continue;
            }

            if (cell.doubleHeight) {
                painter.setFont(doubleHeightFont);
            } else {
                painter.setFont(regularFont);
            }
            painter.setPen(teletextColour(cell.foreground));
            painter.drawText(drawRect, Qt::AlignHCenter | Qt::AlignVCenter, QString(cell.character));
        }
    }
}

void TeletextNativeViewWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    refreshFlashTimer();
}

void TeletextNativeViewWidget::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    refreshFlashTimer();
}

QColor TeletextNativeViewWidget::teletextColour(quint8 colourIndex)
{
    switch (colourIndex) {
    case 0:
        return QColor(0, 0, 0);
    case 1:
        return QColor(255, 0, 0);
    case 2:
        return QColor(0, 255, 0);
    case 3:
        return QColor(255, 255, 0);
    case 4:
        return QColor(0, 0, 255);
    case 5:
        return QColor(255, 0, 255);
    case 6:
        return QColor(0, 255, 255);
    case 7:
    default:
        return QColor(255, 255, 255);
    }
}

TeletextNativeViewWidget::Row TeletextNativeViewWidget::padOrTrimRow(const Row &row, qint32 targetColumns)
{
    Row normalizedRow = row;
    if (normalizedRow.size() > targetColumns) {
        normalizedRow.resize(targetColumns);
    } else {
        while (normalizedRow.size() < targetColumns) {
            normalizedRow.append(Cell());
        }
    }
    return normalizedRow;
}

QString TeletextNativeViewWidget::resolveFontFamily(const QString &fontPath, const QString &fallbackFamily)
{
    static QHash<QString, QString> fontFamilyCache;
    const QString normalizedPath = QDir::cleanPath(fontPath.trimmed());
    if (normalizedPath.isEmpty() || !QFileInfo::exists(normalizedPath)) {
        return fallbackFamily;
    }
    if (fontFamilyCache.contains(normalizedPath)) {
        return fontFamilyCache.value(normalizedPath);
    }

    const qint32 fontId = QFontDatabase::addApplicationFont(normalizedPath);
    QString resolvedFamily = fallbackFamily;
    if (fontId >= 0) {
        const QStringList fontFamilies = QFontDatabase::applicationFontFamilies(fontId);
        if (!fontFamilies.isEmpty()) {
            resolvedFamily = fontFamilies.first();
        }
    }
    fontFamilyCache.insert(normalizedPath, resolvedFamily);
    return resolvedFamily;
}

void TeletextNativeViewWidget::refreshFlashTimer()
{
    const bool shouldAnimate = animations_enabled_ && has_flashing_cells_ && isVisible();
    if (shouldAnimate) {
        if (!flash_timer_.isActive()) {
            flash_timer_.start();
        }
    } else {
        flash_timer_.stop();
        flash_lit_ = true;
    }
}
