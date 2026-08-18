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
#include <QPalette>
#include <QStyleFactory>
#include <QtGlobal>

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
} // namespace tbc::ui

#endif // TBC_UISTYLE_H
