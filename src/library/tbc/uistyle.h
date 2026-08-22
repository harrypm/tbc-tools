/******************************************************************************
 * uistyle.h
 * Shared Qt UI style helpers for ld-decode GUI tools
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 *
 * This file is part of tbc-tools.
 ******************************************************************************/

#ifndef TBC_UISTYLE_H
#define TBC_UISTYLE_H

#include <QApplication>
#include <QColor>
#include <QMargins>
#include <QPalette>
#include <QRect>
#include <QScreen>
#include <QStyleFactory>
#include <QtGlobal>
#include <QWidget>
#include <QWindow>

namespace tbc::ui {
inline qreal paletteContrastDistance(const QColor &first, const QColor &second)
{
    return qAbs(first.lightnessF() - second.lightnessF());
}

inline QColor preferredInputTextColor(bool darkBase)
{
    return darkBase ? QColor(0xF5, 0xF7, 0xFA) : QColor(0x16, 0x18, 0x1C);
}

inline QColor preferredPlaceholderColor(bool darkBase)
{
    return darkBase ? QColor(0xD0, 0xD4, 0xD9) : QColor(0x5F, 0x63, 0x68);
}

inline QColor preferredHighlightedTextColor(bool darkHighlight)
{
    return darkHighlight ? QColor(0xF5, 0xF7, 0xFA) : QColor(0x20, 0x21, 0x24);
}
inline void normalizeUnsupportedStyleOverrideToFusion()
{
    const QByteArray styleOverride = qgetenv("QT_STYLE_OVERRIDE").trimmed();
    if (styleOverride.isEmpty()) {
        return;
    }

    const QString requestedStyle = QString::fromLocal8Bit(styleOverride);
    const QStringList availableStyles = QStyleFactory::keys();
    const bool styleSupported = availableStyles.contains(requestedStyle, Qt::CaseInsensitive);
    if (!styleSupported && availableStyles.contains(QStringLiteral("Fusion"), Qt::CaseInsensitive)) {
        qputenv("QT_STYLE_OVERRIDE", QByteArrayLiteral("Fusion"));
    }
}

inline void applyFusionStyleIfAvailable(QApplication &application)
{
    if (QStyleFactory::keys().contains(QStringLiteral("Fusion"), Qt::CaseInsensitive)) {
        application.setStyle(QStringLiteral("Fusion"));
    }
}

inline void enforceInputWidgetContrast(QApplication &application)
{
    QPalette palette = application.palette();
    const QColor inputBackground = palette.color(QPalette::Base);
    const bool darkBase = inputBackground.lightnessF() < 0.5;

    QColor inputText = palette.color(QPalette::Text);
    if (paletteContrastDistance(inputBackground, inputText) < 0.45) {
        inputText = preferredInputTextColor(darkBase);
    }

    QColor inputPlaceholder = palette.color(QPalette::PlaceholderText);
    if (paletteContrastDistance(inputBackground, inputPlaceholder) < 0.2) {
        inputPlaceholder = preferredPlaceholderColor(darkBase);
    }

    const QColor inputHighlight = palette.color(QPalette::Highlight);
    const bool darkHighlight = inputHighlight.lightnessF() < 0.5;
    QColor inputHighlightedText = palette.color(QPalette::HighlightedText);
    if (paletteContrastDistance(inputHighlight, inputHighlightedText) < 0.45) {
        inputHighlightedText = preferredHighlightedTextColor(darkHighlight);
    }

    palette.setColor(QPalette::Text, inputText);
    palette.setColor(QPalette::PlaceholderText, inputPlaceholder);
    palette.setColor(QPalette::HighlightedText, inputHighlightedText);
    palette.setColor(QPalette::Disabled, QPalette::Text, darkBase ? QColor(0xAA, 0xAF, 0xB5) : QColor(0x6B, 0x72, 0x80));
    palette.setColor(QPalette::Disabled, QPalette::PlaceholderText, darkBase ? QColor(0x8D, 0x93, 0x99) : QColor(0x9A, 0xA0, 0xA6));
    application.setPalette(palette);

    const QString guardMarker = QStringLiteral("TBC_INPUT_CONTRAST_GUARD");
    if (application.styleSheet().contains(guardMarker)) {
        return;
    }

    QString styleSheet = application.styleSheet();
    if (!styleSheet.isEmpty()) {
        styleSheet.append(QLatin1Char('\n'));
    }

    styleSheet.append(QStringLiteral(
        "/* %1 */"
        "QLineEdit,"
        "QTextEdit,"
        "QPlainTextEdit {"
        "  color: palette(text);"
        "  selection-color: palette(highlighted-text);"
        "  selection-background-color: palette(highlight);"
        "}").arg(guardMarker));

    application.setStyleSheet(styleSheet);
}

// Center a top-level sub-window over its parent widget (typically the main
// analyse window). Call this from a show-path override (e.g. setVisible or a
// show-event filter) so the placement is applied before the window manager
// maps the window.
//
// QWidget::move() positions the window *frame* on most platforms, while
// QWidget::width()/height() report the client area. Centering the client area
// without accounting for the frame margins leaves the window shifted right and
// down by the left/top frame margins, so we subtract them. The result is
// clamped to the available geometry of the window's screen.
inline void centerDialogOverParent(QWidget *dialog)
{
    if (!dialog) {
        return;
    }

    QWidget *parent = dialog->parentWidget();
    QRect referenceRect;
    if (parent) {
        referenceRect = parent->geometry();
    } else if (QScreen *screen = dialog->screen()) {
        referenceRect = screen->availableGeometry();
    } else {
        return;
    }

    // Force the native handle so the window-frame margins are known.
    if (!dialog->windowHandle()) {
        dialog->winId();
    }

    int leftMargin = 0;
    int topMargin = 0;
    if (QWindow *handle = dialog->windowHandle()) {
        const QMargins margins = handle->frameMargins();
        leftMargin = margins.left();
        topMargin = margins.top();
    }

    const int width = dialog->width();
    const int height = dialog->height();
    int x = referenceRect.x() + (referenceRect.width() - width) / 2 - leftMargin;
    int y = referenceRect.y() + (referenceRect.height() - height) / 2 - topMargin;

    if (QScreen *screen = dialog->screen()) {
        const QRect avail = screen->availableGeometry();
        const int maxX = avail.left() + qMax(0, avail.width() - width);
        const int maxY = avail.top() + qMax(0, avail.height() - height);
        x = qBound(avail.left(), x, maxX);
        y = qBound(avail.top(), y, maxY);
    }

    dialog->move(x, y);
}
} // namespace tbc::ui

#endif // TBC_UISTYLE_H
