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
#include <QGuiApplication>
#include <QMargins>
#include <QPalette>
#include <QRect>
#include <QScreen>
#include <QStyleFactory>
#include <QStyle>
#include <QStyleHints>
#include <QtGlobal>
#include <QTimer>
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
    if (!QStyleFactory::keys().contains(QStringLiteral("Fusion"), Qt::CaseInsensitive)) {
        return;
    }
    const QString currentStyleName =
        application.style() ? application.style()->objectName() : QString();
    if (currentStyleName.compare(QStringLiteral("Fusion"), Qt::CaseInsensitive) == 0) {
        return;
    }
    application.setStyle(QStringLiteral("Fusion"));
}

// Stock dark Fusion palette (neutral grey/black, legacy blue Highlight).
// Mirrors the complete palette previously in ld-lds-converter/main.cpp so all
// GUI tools share one authoritative dark theme. Keeps Base/Highlight values
// unchanged to preserve the established theme per the project AGENTS.md.
inline QPalette stockDarkPalette()
{
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(53, 53, 53));
    palette.setColor(QPalette::WindowText, QColor(255, 255, 255));
    palette.setColor(QPalette::Base, QColor(25, 25, 25));
    palette.setColor(QPalette::AlternateBase, QColor(64, 64, 64));
    palette.setColor(QPalette::ToolTipBase, QColor(53, 53, 53));
    palette.setColor(QPalette::ToolTipText, QColor(255, 255, 255));
    palette.setColor(QPalette::Text, QColor(255, 255, 255));
    palette.setColor(QPalette::Button, QColor(53, 53, 53));
    palette.setColor(QPalette::ButtonText, QColor(255, 255, 255));
    palette.setColor(QPalette::BrightText, QColor(0xFF, 0x55, 0x55));
    palette.setColor(QPalette::Highlight, QColor(42, 130, 218));
    palette.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
    palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor(160, 160, 160));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor(160, 160, 160));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(160, 160, 160));
    palette.setColor(QPalette::Disabled, QPalette::Highlight, QColor(80, 80, 80));
    palette.setColor(QPalette::Disabled, QPalette::HighlightedText, QColor(255, 255, 255));
    return palette;
}

// Stock light Fusion palette — manual opt-in via --light-theme. Same role
// coverage as the dark palette so the contrast guard and plot tokens resolve
// consistently regardless of which stock theme is active.
inline QPalette stockLightPalette()
{
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(239, 239, 239));
    palette.setColor(QPalette::WindowText, QColor(0, 0, 0));
    palette.setColor(QPalette::Base, QColor(255, 255, 255));
    palette.setColor(QPalette::AlternateBase, QColor(245, 245, 245));
    palette.setColor(QPalette::ToolTipBase, QColor(255, 255, 255));
    palette.setColor(QPalette::ToolTipText, QColor(0, 0, 0));
    palette.setColor(QPalette::Text, QColor(0, 0, 0));
    palette.setColor(QPalette::Button, QColor(239, 239, 239));
    palette.setColor(QPalette::ButtonText, QColor(0, 0, 0));
    palette.setColor(QPalette::BrightText, QColor(0xFF, 0x55, 0x55));
    palette.setColor(QPalette::Highlight, QColor(42, 130, 218));
    palette.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
    palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor(120, 120, 120));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor(120, 120, 120));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(120, 120, 120));
    palette.setColor(QPalette::Disabled, QPalette::Highlight, QColor(200, 200, 200));
    palette.setColor(QPalette::Disabled, QPalette::HighlightedText, QColor(0, 0, 0));
    return palette;
}

// Must be called before QApplication construction. Disables Qt's tracking of
// desktop palette/font changes (defense-in-depth against the macOS appearance
// switchover; the authoritative re-assert lives in ThemedApplication::event()).
inline void prepareStockThemeEnvironment()
{
    QGuiApplication::setDesktopSettingsAware(false);
    normalizeUnsupportedStyleOverrideToFusion();
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

// ThemedApplication pins the stock Fusion palette (dark by default, light via
// applyStockLightTheme()) and re-asserts it whenever the platform signals an
// application palette change — e.g. the macOS scheduled Dark Mode switchover.
// Without this, Qt re-reads the system palette on the switchover and overwrites
// the manually-applied dark palette, breaking plot/input-widget theming mid-run.
//
// The re-assert is deferred with QTimer::singleShot(0, ...) so it runs *after*
// QApplication's default ApplicationPaletteChange propagation to top-level
// windows/widgets; re-applying the palette synchronously inside event() can
// recurse (setPalette -> ApplicationPaletteChange -> setPalette ...) and crash.
// On Qt >= 6.8, setColorScheme() asks the platform to override the system color
// scheme and ignore its changes (honored on macOS).
class ThemedApplication : public QApplication
{
public:
    explicit ThemedApplication(int &argc, char **argv)
        : QApplication(argc, argv)
    {
    }

    void applyStockDarkTheme() { applyStockTheme(true); }
    void applyStockLightTheme() { applyStockTheme(false); }

protected:
    bool event(QEvent *event) override
    {
        if (event && event->type() == QEvent::ApplicationPaletteChange) {
            const bool result = QApplication::event(event);
            if (!m_reasserting) {
                m_reasserting = true;
                QTimer::singleShot(0, this, [this]() {
                    QApplication::setPalette(m_reassertDark ? stockDarkPalette()
                                                             : stockLightPalette());
                    enforceInputWidgetContrast(*this);
                    m_reasserting = false;
                });
            }
            return result;
        }
        return QApplication::event(event);
    }

private:
    void applyStockTheme(bool dark)
    {
        m_reassertDark = dark;
        // Set the isDarkTheme property BEFORE setPalette(): setPalette()
        // synchronously propagates PaletteChange to every widget, and
        // custom-painted widgets (PlotWidget::changeEvent -> updateTheme() ->
        // isDarkTheme()) read this property during that propagation. If it's
        // still the previous value, they repaint with the stale theme and the
        // switch appears to need a second click.
        setProperty("isDarkTheme", dark);
        applyFusionStyleIfAvailable(*this);
        setPalette(dark ? stockDarkPalette() : stockLightPalette());
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
        styleHints()->setColorScheme(dark ? Qt::ColorScheme::Dark : Qt::ColorScheme::Light);
#endif
        enforceInputWidgetContrast(*this);

        // Force every custom-painted widget (scopes/analysis plots that read
        // the theme in paintEvent) to repaint immediately so the switch is
        // complete in a single pass. setPalette() already propagates a
        // PaletteChange (which QWidget forwards to changeEvent -> update),
        // but this guarantees no widget is left showing the old theme. No-op
        // at startup when no widgets exist yet.
        const auto topLevels = QApplication::topLevelWidgets();
        for (QWidget *top : topLevels) {
            top->update();
            const auto children = top->findChildren<QWidget *>();
            for (QWidget *child : children) {
                child->update();
            }
        }

        // Deferred second pass: a direct (non-system) setPalette() can leave
        // some custom-painted widgets (QGraphicsView/scene-based plots, scopes
        // that cache the theme) partially resolved until a second event. The
        // macOS-switchover path already re-asserts the palette via a deferred
        // timer and that completes in one pass; mirror that here so a menu
        // click switches fully without needing a second click. Re-apply the
        // stock palette + contrast guard on the next loop tick, then force a
        // repaint of every widget. Harmless at startup (same palette, no
        // widgets yet). m_reasserting prevents the event() override from
        // stacking another deferred re-assert on top of this one.
        m_reasserting = true;
        QTimer::singleShot(0, this, [this]() {
            QApplication::setPalette(m_reassertDark ? stockDarkPalette()
                                                     : stockLightPalette());
            enforceInputWidgetContrast(*this);
            const auto tops = QApplication::topLevelWidgets();
            for (QWidget *top : tops) {
                top->update();
                const auto kids = top->findChildren<QWidget *>();
                for (QWidget *kid : kids) {
                    kid->update();
                }
            }
            m_reasserting = false;
        });
    }

    bool m_reassertDark = true;
    bool m_reasserting = false;
};

// Access the running ThemedApplication (nullptr if the QCoreApplication is not
// a ThemedApplication, e.g. in non-GUI tools). Uses dynamic_cast because
// ThemedApplication has no Q_OBJECT, so qobject_cast is unavailable.
inline ThemedApplication *themedApplicationInstance()
{
    return dynamic_cast<ThemedApplication *>(QCoreApplication::instance());
}

// Apply the stock dark/light preset to the running GUI app. Goes through
// ThemedApplication so the switchover re-assert state (m_reassertDark) tracks
// the user's choice. No-op if the app is not a ThemedApplication.
inline void applyStockDarkThemeToApp()
{
    if (auto *app = themedApplicationInstance()) {
        app->applyStockDarkTheme();
    }
}

inline void applyStockLightThemeToApp()
{
    if (auto *app = themedApplicationInstance()) {
        app->applyStockLightTheme();
    }
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
