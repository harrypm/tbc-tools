/******************************************************************************
 * teletextviewerdialog.cpp
 * tbc-analyse - TBC output analysis GUI
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2018-2026 Simon Inns
 * SPDX-FileCopyrightText: 2026 Harry Munday
 *
 * This file is part of tbc-tools.
 ******************************************************************************/

#include "teletextviewerdialog.h"
#include "teletextnativeviewwidget.h"
#ifdef emit
#undef emit
#endif
#include "tbc/vbi/nabts_data_group.h"
#include "tbc/vbi/nabts_packet.h"
#include "tbc/vbi/nabts_record.h"
#include "tbc/vbi/nabts_page.h"
#include "tbc/vbi/naplps_interpreter.h"
#include "tbc/vbi/naplps_raster.h"
#include "tbc/vbi/naplps_render_grid.h"
#include "tbc/vbi/teletext_decoder.h"
#include "tbc/vbi/teletext_page.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QMessageBox>
#include <QMimeData>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QRegularExpression>
#include <QImage>
#include <QImageReader>
#include <QShortcut>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QScreen>
#include <QShowEvent>
#include <QTextBrowser>
#include <QTextDocument>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QWindow>
#include <map>
namespace {
const QRegularExpression kTeletextStreamSuffixPattern(
    QStringLiteral("^t\\d\\d$"),
    QRegularExpression::CaseInsensitiveOption
);
void appendUniqueCandidate(QStringList &candidates, const QString &candidate)
{
    if (candidate.isEmpty()) {
        return;
    }
    for (const QString &existing : candidates) {
        if (existing.compare(candidate, Qt::CaseInsensitive) == 0) {
            return;
        }
    }
    candidates.append(candidate);
}

QString normalizedTeletextBaseName(const QFileInfo &pathInfo)
{
    QString baseName = pathInfo.completeBaseName();
    QString baseNameLower = baseName.toLower();
    const QStringList suffixesToStrip = {
        QStringLiteral(".tbc"),
        QStringLiteral(".ytbc"),
        QStringLiteral(".ctbc"),
        QStringLiteral(".tbcy"),
        QStringLiteral(".tbcc")
    };
    for (const QString &suffix : suffixesToStrip) {
        if (!baseNameLower.endsWith(suffix)) {
            continue;
        }
        baseName.chop(suffix.size());
        break;
    }
    return baseName;
}

bool directoryContainsHtmlPages(const QString &directoryPath)
{
    if (directoryPath.trimmed().isEmpty()) {
        return false;
    }
    const QDir directory(directoryPath);
    if (!directory.exists()) {
        return false;
    }
    const QStringList htmlPages = directory.entryList(
        QStringList() << QStringLiteral("*.html"),
        QDir::Files,
        QDir::Name | QDir::IgnoreCase
    );
    return !htmlPages.isEmpty();
}

struct TeletextInputSelection {
    QString directoryPath;
    QString htmlPageName;
    QString streamFilePath;

    bool isValid() const
    {
        return !directoryPath.isEmpty() || !streamFilePath.isEmpty();
    }
};

bool isTeletextStreamFile(const QFileInfo &pathInfo)
{
    return pathInfo.exists()
           && pathInfo.isFile()
           && kTeletextStreamSuffixPattern.match(pathInfo.suffix()).hasMatch();
}

TeletextInputSelection resolveTeletextInputFromHint(const QString &pathHint)
{
    TeletextInputSelection selection;
    const QFileInfo pathInfo(pathHint);
    if (!pathInfo.exists()) {
        return selection;
    }

    if (pathInfo.isFile() && pathInfo.suffix().compare(QStringLiteral("html"), Qt::CaseInsensitive) == 0) {
        const QDir htmlDirectory(pathInfo.absolutePath());
        if (directoryContainsHtmlPages(htmlDirectory.absolutePath())) {
            selection.directoryPath = htmlDirectory.absolutePath();
            selection.htmlPageName = pathInfo.fileName();
            return selection;
        }
    }

    if (pathInfo.isDir() && directoryContainsHtmlPages(pathInfo.absoluteFilePath())) {
        selection.directoryPath = QDir(pathInfo.absoluteFilePath()).absolutePath();
        return selection;
    }

    if (isTeletextStreamFile(pathInfo)) {
        selection.streamFilePath = pathInfo.absoluteFilePath();
        return selection;
    }

    QDir baseDirectory;
    if (pathInfo.isDir()) {
        baseDirectory = QDir(pathInfo.absoluteFilePath());
    } else {
        baseDirectory = pathInfo.absoluteDir();
    }

    QStringList candidateDirectories;
    QStringList baseNames;
    if (pathInfo.isDir()) {
        appendUniqueCandidate(baseNames, pathInfo.fileName());
    } else {
        appendUniqueCandidate(baseNames, normalizedTeletextBaseName(pathInfo));
        appendUniqueCandidate(baseNames, pathInfo.completeBaseName());
    }

    for (const QString &baseName : baseNames) {
        if (baseName.isEmpty()) {
            continue;
        }
        appendUniqueCandidate(candidateDirectories, baseDirectory.filePath(baseName + QStringLiteral("_teletext_html")));
        appendUniqueCandidate(candidateDirectories, baseDirectory.filePath(baseName + QStringLiteral(".teletext_html")));
        appendUniqueCandidate(candidateDirectories, baseDirectory.filePath(baseName + QStringLiteral("_teletext")));
    }
    appendUniqueCandidate(candidateDirectories, baseDirectory.filePath(QStringLiteral("teletext_html")));
    appendUniqueCandidate(candidateDirectories, baseDirectory.filePath(QStringLiteral("teletext")));

    for (const QString &candidate : candidateDirectories) {
        if (!directoryContainsHtmlPages(candidate)) {
            continue;
        }
        selection.directoryPath = QDir(candidate).absolutePath();
        return selection;
    }
    return selection;
}

TeletextInputSelection resolveTeletextInputFromHints(const QStringList &pathHints)
{
    TeletextInputSelection selection;
    for (const QString &pathHint : pathHints) {
        const TeletextInputSelection resolvedSelection = resolveTeletextInputFromHint(pathHint);
        if (!resolvedSelection.isValid()) {
            continue;
        }
        return resolvedSelection;
    }
    return selection;
}

bool isTeletextVendorDirectory(const QString &directoryPath)
{
    if (directoryPath.trimmed().isEmpty()) {
        return false;
    }
    const QDir directory(directoryPath);
    if (!directory.exists()) {
        return false;
    }
    return QFileInfo::exists(directory.filePath(QStringLiteral("teletext/__main__.py")))
           && QFileInfo::exists(directory.filePath(QStringLiteral("misc/teletext.css")));
}

QString resolveTeletextVendorDirectory()
{
    QStringList candidates;
#ifdef TELETEXT_VENDOR_DIR
    appendUniqueCandidate(candidates, QString::fromUtf8(TELETEXT_VENDOR_DIR));
#endif

    const QString appDirectory = QCoreApplication::applicationDirPath();
    appendUniqueCandidate(candidates, QDir(appDirectory).filePath(QStringLiteral("vendor/vhs-teletext")));
    appendUniqueCandidate(candidates, QDir(appDirectory).filePath(QStringLiteral("../vendor/vhs-teletext")));
    appendUniqueCandidate(candidates, QDir(appDirectory).filePath(QStringLiteral("../../vendor/vhs-teletext")));
    appendUniqueCandidate(candidates, QDir::current().filePath(QStringLiteral("src/ld-process-vbi/vendor/vhs-teletext")));

    for (const QString &candidate : candidates) {
        if (isTeletextVendorDirectory(candidate)) {
            return QDir::cleanPath(candidate);
        }
    }
    return QString();
}

QString resolvePythonExecutable()
{
    const QString overrideValue = qEnvironmentVariable("TELETEXT_PYTHON").trimmed();
    if (!overrideValue.isEmpty()) {
        const QString overrideExecutable = QStandardPaths::findExecutable(overrideValue);
        if (!overrideExecutable.isEmpty()) {
            return overrideExecutable;
        }
        const QFileInfo overrideInfo(overrideValue);
        if (overrideInfo.exists() && overrideInfo.isFile() && overrideInfo.isExecutable()) {
            return overrideInfo.absoluteFilePath();
        }
    }

    const QStringList candidates = {
        QStringLiteral("python3"),
        QStringLiteral("python")
    };
    for (const QString &candidate : candidates) {
        const QString executablePath = QStandardPaths::findExecutable(candidate);
        if (!executablePath.isEmpty()) {
            return executablePath;
        }
    }
    return QString();
}

bool runPythonStep(const QString &pythonExecutable,
                   const QStringList &arguments,
                   const QProcessEnvironment &environment,
                   QString *errorMessage)
{
    QProcess process;
    process.setProcessEnvironment(environment);
    process.start(pythonExecutable, arguments);

    if (!process.waitForStarted(5000)) {
        if (errorMessage) {
            *errorMessage = QObject::tr("Could not start Python teletext converter.");
        }
        return false;
    }
    if (!process.waitForFinished(120000)) {
        process.kill();
        process.waitForFinished(3000);
        if (errorMessage) {
            *errorMessage = QObject::tr("Teletext conversion timed out.");
        }
        return false;
    }

    const QString stdoutText = QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed();
    const QString stderrText = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        QString details = stderrText;
        if (details.isEmpty()) {
            details = stdoutText;
        }
        if (details.isEmpty()) {
            details = QObject::tr("unknown error");
        }
        if (errorMessage) {
            *errorMessage = QObject::tr("Teletext conversion failed: %1").arg(details);
        }
        return false;
    }


    return true;
}

bool copyFileReplacing(const QString &sourcePath, const QString &targetPath, QString *errorMessage)
{
    if (!QFileInfo::exists(sourcePath)) {
        return true;
    }
    if (QFileInfo::exists(targetPath) && !QFile::remove(targetPath)) {
        if (errorMessage) {
            *errorMessage = QObject::tr("Could not replace existing file: %1").arg(targetPath);
        }
        return false;
    }
    if (!QFile::copy(sourcePath, targetPath)) {
        if (errorMessage) {
            *errorMessage = QObject::tr("Could not copy file from %1 to %2").arg(sourcePath, targetPath);
        }
        return false;
    }
    return true;
}

void ensureCssSwitchScript(const QString &outputDirectoryPath)
{
    const QString cssSwitchPath = QDir(outputDirectoryPath).filePath(QStringLiteral("cssswitch.js"));
    if (QFileInfo::exists(cssSwitchPath)) {
        return;
    }

    QFile cssSwitchFile(cssSwitchPath);
    if (!cssSwitchFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return;
    }
    cssSwitchFile.write("function set_style_from_cookie() {\n");
    cssSwitchFile.write("    // Placeholder helper for generated teletext HTML pages.\n");
    cssSwitchFile.write("}\n");
    cssSwitchFile.close();
}

QString cacheDirectoryForTeletextStream(const QFileInfo &streamInfo)
{
    QString rootDirectory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (rootDirectory.trimmed().isEmpty()) {
        rootDirectory = QDir::tempPath();
    }

    QString streamBaseName = streamInfo.completeBaseName();
    if (streamBaseName.trimmed().isEmpty()) {
        streamBaseName = QStringLiteral("teletext");
    }
    streamBaseName.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]")), QStringLiteral("_"));

    QByteArray keyData = streamInfo.absoluteFilePath().toUtf8();
    keyData.append('|');
    keyData.append(QByteArray::number(streamInfo.size()));
    keyData.append('|');
    keyData.append(QByteArray::number(streamInfo.lastModified().toMSecsSinceEpoch()));
    keyData.append("|teletext-viewer-v2");
    const QString streamHash = QString::fromLatin1(
        QCryptographicHash::hash(keyData, QCryptographicHash::Sha1).toHex().left(16)
    );

    return QDir(rootDirectory).filePath(
        QStringLiteral("teletext_viewer_cache/%1_%2").arg(streamBaseName, streamHash)
    );
}

QString htmlEscapeTeletextCharacter(const QChar &character)
{
    if (character == QLatin1Char('&')) {
        return QStringLiteral("&amp;");
    }
    if (character == QLatin1Char('<')) {
        return QStringLiteral("&lt;");
    }
    if (character == QLatin1Char('>')) {
        return QStringLiteral("&gt;");
    }
    return QString(character);
}

QString teletextPageKey(qint32 magazine, qint32 page)
{
    return QStringLiteral("%1%2").arg(magazine).arg(page, 2, 16, QLatin1Char('0'));
}

QString teletextCodePointToString(char32_t cp)
{
    if (cp <= 0xFFFF) {
        return htmlEscapeTeletextCharacter(QChar(static_cast<ushort>(cp)));
    }
    QString text;
    text.append(QChar(QChar::highSurrogate(cp)));
    text.append(QChar(QChar::lowSurrogate(cp)));
    return text;
}

// One TeletextPageCell as HTML text. Mosaic characters (outside the 0x40-0x5F
// blast-through range) map to the Private Use Area the vendored teletext.css
// font renders — 0xEE00 for contiguous, 0xEDE0 for separated — exactly as
// vhs-teletext's parser.py does. Blast-through capitals and alpha cells map
// through the G0 set the cell resolved to (with its national option sub-set).
QString teletextSnapshotCellText(const tbc::vbi::TeletextPageCell &cell)
{
    const bool blastThrough = cell.character >= 0x40 && cell.character < 0x60;
    if (cell.mosaic && !blastThrough) {
        const char32_t base = cell.separated_mosaic ? 0xEDE0 : 0xEE00;
        return htmlEscapeTeletextCharacter(QChar(static_cast<ushort>(base + cell.character)));
    }
    const char32_t cp = tbc::vbi::teletext_g0_to_unicode(cell.character,
                                                        cell.g0_set,
                                                        cell.national_option_subset);
    return teletextCodePointToString(cp);
}

// CSS class for one cell, in the f# b# [dh] [fl] [cn] [bx|nx] form the
// vendored teletext.css expects.
QString teletextSnapshotCellCssClass(const tbc::vbi::TeletextPageCell &cell)
{
    QString cssClass = QStringLiteral("f%1 b%2")
                           .arg(static_cast<int>(cell.foreground))
                           .arg(static_cast<int>(cell.background));
    if (cell.double_height) {
        cssClass += QStringLiteral(" dh");
    }
    if (cell.flash) {
        cssClass += QStringLiteral(" fl");
    }
    if (cell.conceal) {
        cssClass += QStringLiteral(" cn");
    }
    cssClass += cell.boxed ? QStringLiteral(" bx") : QStringLiteral(" nx");
    return cssClass;
}

QString teletextSnapshotRowToHtml(
    const std::array<tbc::vbi::TeletextPageCell, tbc::vbi::TeletextPageSnapshot::kColumns> &row)
{
    QString html = QStringLiteral("<span class=\"row\">");
    QString activeClass;
    bool spanOpen = false;
    for (const tbc::vbi::TeletextPageCell &cell : row) {
        const QString cellClass = teletextSnapshotCellCssClass(cell);
        if (!spanOpen || cellClass != activeClass) {
            if (spanOpen) {
                html += QStringLiteral("</span>");
            }
            html += QStringLiteral("<span class=\"") + cellClass + QStringLiteral("\">");
            activeClass = cellClass;
            spanOpen = true;
        }
        html += teletextSnapshotCellText(cell);
    }
    if (spanOpen) {
        html += QStringLiteral("</span>");
    }
    html += QStringLiteral("</span>");
    return html;
}

bool snapshotRowHasDoubleHeight(
    const std::array<tbc::vbi::TeletextPageCell, tbc::vbi::TeletextPageSnapshot::kColumns> &row)
{
    for (const tbc::vbi::TeletextPageCell &cell : row) {
        if (cell.double_height) {
            return true;
        }
    }
    return false;
}

// A TeletextPageSnapshot as the vhs-teletext-compatible HTML the viewer shows.
// The header row (row 0) is stamped with the receiver-synthesised page label
// "P<mag><page>" a real teletext header displays, then the transmitted display
// the decoder placed at columns 8-39. A row following a double-height row is
// its lower half and is skipped, as the existing pipeline did.
QString teletextSnapshotToHtml(const tbc::vbi::TeletextPageSnapshot &snapshot)
{
    std::array<tbc::vbi::TeletextPageCell, tbc::vbi::TeletextPageSnapshot::kColumns> headerRow = snapshot.cells[0];
    const QByteArray pageLabel =
        QStringLiteral("P%1").arg(teletextPageKey(snapshot.magazine, snapshot.page_number)).toLatin1();
    for (int i = 0; i < pageLabel.size() && (3 + i) < static_cast<int>(headerRow.size()); ++i) {
        headerRow[3 + i].character = static_cast<uint8_t>(pageLabel.at(i));
        headerRow[3 + i].mosaic = false;
        headerRow[3 + i].foreground = tbc::vbi::TeletextColour::White;
        headerRow[3 + i].background = tbc::vbi::TeletextColour::Black;
    }

    QString body;
    body += QStringLiteral("<div class=\"subpage\" id=\"%1\">")
                .arg(snapshot.subcode, 4, 16, QLatin1Char('0'));
    body += teletextSnapshotRowToHtml(headerRow);

    bool previousDoubleHeight = false;
    for (int row = 1; row <= 24; ++row) {
        const bool rowDoubleHeight = snapshotRowHasDoubleHeight(snapshot.cells[row]);
        if (row > 1 && previousDoubleHeight) {
            previousDoubleHeight = rowDoubleHeight;
            continue;
        }
        body += teletextSnapshotRowToHtml(snapshot.cells[row]);
        previousDoubleHeight = rowDoubleHeight;
    }
    body += QStringLiteral("</div>");

    QString html;
    html += QStringLiteral("<html><head>");
    html += QStringLiteral("<meta http-equiv=\"Content-Type\" content=\"text/html; charset=utf-8\">");
    html += QStringLiteral("<title>Page %1</title>").arg(teletextPageKey(snapshot.magazine, snapshot.page_number));
    html += QStringLiteral("<link rel=\"stylesheet\" type=\"text/css\" href=\"teletext.css\" title=\"Default Style\"/>");
    html += QStringLiteral("<link rel=\"alternative stylesheet\" type=\"text/css\" href=\"teletext-noscanlines.css\" title=\"No Scanlines\"/>");
    html += QStringLiteral("<script type=\"text/javascript\" src=\"cssswitch.js\"></script>");
    html += QStringLiteral("</head><body onload=\"set_style_from_cookie()\">");
    html += body;
    html += QStringLiteral("</body></html>");
    return html;
}

struct NabtsRenderedPage {
    QString title;
    QString imageFileName;
    QString htmlFileName;
    QString debugText;
};

bool writeTeletextHtmlFromT42Native(const QString &streamPath,
                                    const QString &outputDirectoryPath,
                                    QString *errorMessage)
{
    QFile streamFile(streamPath);
    if (!streamFile.open(QIODevice::ReadOnly)) {
        if (errorMessage) {
            *errorMessage = QObject::tr("Could not open teletext stream file: %1").arg(streamPath);
        }
        return false;
    }

    // Decode the T42 stream into TeletextPageSnapshot values via the shared
    // tbc::vbi::TeletextDecoder, which handles MRAG routing, page assembly,
    // row squashing and Level 1 attribute resolution against the Phase 1
    // snapshot model. The resulting HTML is the vhs-teletext-compatible output
    // the viewer already renders.
    std::vector<tbc::vbi::TeletextPageSnapshot> snapshots;
    tbc::vbi::TeletextDecoder decoder;
    decoder.set_page_callback([&snapshots](const tbc::vbi::TeletextPageSnapshot &snapshot) {
        snapshots.push_back(snapshot);
    });

    while (true) {
        const QByteArray packet = streamFile.read(static_cast<qint64>(tbc::vbi::kTeletextT42Bytes));
        if (packet.size() != static_cast<int>(tbc::vbi::kTeletextT42Bytes)) {
            break;
        }
        const auto *packetData = reinterpret_cast<const uint8_t *>(packet.constData());
        decoder.add_packet(packetData, static_cast<size_t>(packet.size()));
    }
    decoder.flush();

    if (snapshots.empty()) {
        if (errorMessage) {
            *errorMessage = QObject::tr("No decodable teletext pages were found in the .tXX stream.");
        }
        return false;
    }

    // A carousel transmits a page one row per field, so each header-cycle
    // snapshot carries only a few rows. Reassemble complete pages by grouping
    // every recurring snapshot of the same (magazine, page, subpage) and, for
    // each row, taking the copy the stream repeated most (the highest
    // row_copies) — the way vhs-teletext's Service accumulates a page over
    // many fields. One page per (magazine, page) is rendered, using the lowest
    // subcode, matching the carousel behaviour the existing pipeline exhibited.
    QMap<QString, QList<int>> groupsBySubpage;
    for (int i = 0; i < snapshots.size(); ++i) {
        const tbc::vbi::TeletextPageSnapshot &snapshot = snapshots[i];
        const QString key = QStringLiteral("%1-%2-%3")
                                .arg(snapshot.magazine)
                                .arg(snapshot.page_number, 2, 16, QLatin1Char('0'))
                                .arg(snapshot.subcode, 4, 16, QLatin1Char('0'));
        groupsBySubpage[key].append(i);
    }

    QMap<QString, tbc::vbi::TeletextPageSnapshot> renderedByPage;
    for (auto it = groupsBySubpage.cbegin(); it != groupsBySubpage.cend(); ++it) {
        const tbc::vbi::TeletextPageSnapshot &first = snapshots[it.value().first()];
        const QString pageKey = teletextPageKey(first.magazine, first.page_number);
        const auto existing = renderedByPage.constFind(pageKey);
        if (existing != renderedByPage.constEnd() && first.subcode >= existing.value().subcode) {
            continue;  // a lower subcode for this page has already been rendered
        }

        tbc::vbi::TeletextPageSnapshot merged = first;
        for (int row = 0; row < tbc::vbi::TeletextPageSnapshot::kRows; ++row) {
            int bestCopies = merged.row_copies[row];
            for (int idx : it.value()) {
                const tbc::vbi::TeletextPageSnapshot &candidate = snapshots[idx];
                if (candidate.row_received[row] && candidate.row_copies[row] > bestCopies) {
                    merged.cells[row] = candidate.cells[row];
                    merged.row_received[row] = true;
                    merged.row_copies[row] = candidate.row_copies[row];
                    bestCopies = candidate.row_copies[row];
                }
            }
        }
        renderedByPage.insert(pageKey, merged);
    }

    // Render one HTML page per (magazine, page). A page that never gathered
    // more than its header row is noise rather than a transmitted page, so it
    // is dropped — real carousel pages accumulate many rows across a capture
    // this long.
    bool wroteAny = false;
    for (auto it = renderedByPage.cbegin(); it != renderedByPage.cend(); ++it) {
        int receivedRows = 0;
        for (int row = 1; row < tbc::vbi::TeletextPageSnapshot::kRows; ++row) {
            if (it.value().row_received[row]) {
                ++receivedRows;
            }
        }
        if (receivedRows < 2) {
            continue;
        }

        const QString pageHtmlPath = QDir(outputDirectoryPath).filePath(it.key() + QStringLiteral(".html"));
        QFile pageFile(pageHtmlPath);
        if (!pageFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            if (errorMessage) {
                *errorMessage = QObject::tr("Could not write teletext HTML page: %1").arg(pageHtmlPath);
            }
            return false;
        }
        pageFile.write(teletextSnapshotToHtml(it.value()).toUtf8());
        pageFile.close();
        wroteAny = true;
    }

    if (!wroteAny) {
        if (errorMessage) {
            *errorMessage = QObject::tr("No decodable teletext pages were found in the .tXX stream.");
        }
        return false;
    }

    return true;
}

QColor nabtsColourToQColor(const tbc::vbi::NabtsColour &colour)
{
    if (colour.transparent) {
        return QColor(0, 0, 0, 0);
    }

    const auto to8Bit = [](quint8 value) {
        return static_cast<qint32>((static_cast<qint32>(value) * 255 + 3) / 7);
    };
    return QColor(to8Bit(colour.red), to8Bit(colour.green), to8Bit(colour.blue), 255);
}

tbc::vbi::NabtsColour decodeNabtsDirectColourFromIncrementalSpec(quint8 spec)
{
    const quint8 sixBits = spec & 0x3F;
    const quint8 greenBits = static_cast<quint8>((((sixBits >> 5) & 0x01) << 1) | ((sixBits >> 2) & 0x01));
    const quint8 redBits = static_cast<quint8>((((sixBits >> 4) & 0x01) << 1) | ((sixBits >> 1) & 0x01));
    const quint8 blueBits = static_cast<quint8>((((sixBits >> 3) & 0x01) << 1) | (sixBits & 0x01));
    const auto scaleToThreeBits = [](quint8 twoBits) {
        return static_cast<quint8>((static_cast<qint32>(twoBits) * 7 + 1) / 3);
    };

    tbc::vbi::NabtsColour colour;
    colour.green = scaleToThreeBits(greenBits);
    colour.red = scaleToThreeBits(redBits);
    colour.blue = scaleToThreeBits(blueBits);
    colour.transparent = false;
    return colour;
}

const tbc::vbi::NabtsTextureMask *nabtsTextureMaskForPattern(
    tbc::vbi::NabtsTexturePattern pattern,
    const tbc::vbi::NabtsPageSnapshot &snapshot)
{
    using tbc::vbi::NabtsTexturePattern;
    switch (pattern) {
    case NabtsTexturePattern::kMaskA:
        return &snapshot.texture_masks[0];
    case NabtsTexturePattern::kMaskB:
        return &snapshot.texture_masks[1];
    case NabtsTexturePattern::kMaskC:
        return &snapshot.texture_masks[2];
    case NabtsTexturePattern::kMaskD:
        return &snapshot.texture_masks[3];
    default:
        return nullptr;
    }
}

tbc::vbi::NaplpsInk nabtsHighlightInkForPrimitive(const tbc::vbi::NabtsPrimitive &primitive)
{
    using tbc::vbi::NabtsColourMode;

    tbc::vbi::NaplpsInk highlight;
    if (primitive.colour_mode == NabtsColourMode::kMappedWithBackground
        && primitive.background_map_address >= 0) {
        highlight = tbc::vbi::naplps_background_ink_of(primitive);
    } else {
        highlight.colour = tbc::vbi::kNabtsNominalBlack;
    }
    return highlight;
}

tbc::vbi::NaplpsInk nabtsIncrementalInkForSpec(quint8 spec,
                                               const tbc::vbi::NabtsPrimitive &primitive,
                                               const tbc::vbi::NabtsPageSnapshot &snapshot)
{
    using tbc::vbi::NabtsColourMode;

    tbc::vbi::NaplpsInk ink;
    ink.blinking = primitive.blinking;
    ink.blink_to = primitive.blink_to;
    ink.blink_to_map_address = primitive.blink_to_map_address;

    if (primitive.colour_mode == NabtsColourMode::kDirect) {
        ink.colour = decodeNabtsDirectColourFromIncrementalSpec(spec);
        ink.colour_map_address = -1;
        return ink;
    }

    const qint32 mapAddress = static_cast<qint32>((spec >> 2) & 0x0F);
    ink.colour_map_address = static_cast<int16_t>(mapAddress);
    ink.colour = snapshot.colour_map[mapAddress];
    return ink;
}

void maybeHighlightFilledPrimitive(const tbc::vbi::NabtsPrimitive &primitive,
                                  tbc::vbi::NaplpsRasteriser &rasteriser)
{
    using tbc::vbi::NabtsPoint;
    using tbc::vbi::NabtsPrimitiveKind;
    if (!primitive.highlighted) {
        return;
    }

    const tbc::vbi::NaplpsInk highlightInk = nabtsHighlightInkForPrimitive(primitive);
    switch (primitive.kind) {
    case NabtsPrimitiveKind::kArc: {
        const std::vector<NabtsPoint> outline = rasteriser.arc_polyline(primitive.points);
        rasteriser.highlight_path(outline, primitive.logical_pel, highlightInk, false);
        break;
    }
    case NabtsPrimitiveKind::kPolygon:
        rasteriser.highlight_path(primitive.points, primitive.logical_pel, highlightInk, true);
        break;
    case NabtsPrimitiveKind::kRectangle: {
        const NabtsPoint origin = primitive.origin;
        const NabtsPoint far{origin.x + primitive.size.dx, origin.y + primitive.size.dy};
        const std::vector<NabtsPoint> corners = {
            origin,
            NabtsPoint{far.x, origin.y},
            far,
            NabtsPoint{origin.x, far.y}
        };
        rasteriser.highlight_path(corners, primitive.logical_pel, highlightInk, true);
        break;
    }
    default:
        break;
    }
}

void rasteriseNabtsPrimitive(const tbc::vbi::NabtsPrimitive &primitive,
                             const tbc::vbi::NabtsPageSnapshot &snapshot,
                             tbc::vbi::NaplpsRasteriser &rasteriser)
{
    using tbc::vbi::NabtsPoint;
    using tbc::vbi::NabtsPrimitiveKind;

    const tbc::vbi::NaplpsInk ink = tbc::vbi::naplps_ink_of(primitive);
    const tbc::vbi::NaplpsInk backgroundInk = tbc::vbi::naplps_background_ink_of(primitive);
    const tbc::vbi::NaplpsInk *backgroundInkPtr =
        primitive.colour_mode == tbc::vbi::NabtsColourMode::kMappedWithBackground
            ? &backgroundInk
            : nullptr;
    const tbc::vbi::NabtsTextureMask *textureMask =
        nabtsTextureMaskForPattern(primitive.texture_pattern, snapshot);

    switch (primitive.kind) {
    case NabtsPrimitiveKind::kPoint:
        if (!primitive.points.empty()) {
            rasteriser.stamp_pel(primitive.points.front(), primitive.logical_pel, ink);
        }
        break;
    case NabtsPrimitiveKind::kCharacter:
        rasteriser.deposit_character(primitive, snapshot.drcs, ink, backgroundInkPtr);
        break;
    case NabtsPrimitiveKind::kLine:
        rasteriser.stroke_path(primitive.points, primitive.logical_pel, primitive.line_texture, ink);
        break;
    case NabtsPrimitiveKind::kArc: {
        const std::vector<NabtsPoint> outline = rasteriser.arc_polyline(primitive.points);
        if (primitive.filled) {
            rasteriser.fill_path(outline, primitive.logical_pel, primitive.texture_pattern,
                                 primitive.texture_mask_size, textureMask, ink);
            maybeHighlightFilledPrimitive(primitive, rasteriser);
        } else {
            rasteriser.stroke_path(outline, primitive.logical_pel, primitive.line_texture, ink);
        }
        break;
    }
    case NabtsPrimitiveKind::kPolygon:
        if (primitive.filled) {
            rasteriser.fill_path(primitive.points, primitive.logical_pel, primitive.texture_pattern,
                                 primitive.texture_mask_size, textureMask, ink);
            maybeHighlightFilledPrimitive(primitive, rasteriser);
        } else {
            rasteriser.stroke_path(primitive.points, primitive.logical_pel, primitive.line_texture, ink);
        }
        break;
    case NabtsPrimitiveKind::kRectangle: {
        const NabtsPoint origin = primitive.origin;
        const NabtsPoint far{origin.x + primitive.size.dx, origin.y + primitive.size.dy};
        const std::vector<NabtsPoint> corners = {
            origin,
            NabtsPoint{far.x, origin.y},
            far,
            NabtsPoint{origin.x, far.y}
        };
        if (primitive.filled) {
            rasteriser.fill_path(corners, primitive.logical_pel, primitive.texture_pattern,
                                 primitive.texture_mask_size, textureMask, ink);
            maybeHighlightFilledPrimitive(primitive, rasteriser);
        } else {
            rasteriser.stroke_path(corners, primitive.logical_pel, primitive.line_texture, ink, true);
        }
        break;
    }
    case NabtsPrimitiveKind::kIncrementalPoints: {
        std::vector<tbc::vbi::NaplpsInk> colours;
        colours.reserve(primitive.incremental_colours.size());
        for (quint8 spec : primitive.incremental_colours) {
            colours.push_back(nabtsIncrementalInkForSpec(spec, primitive, snapshot));
        }
        rasteriser.deposit_colour_run(primitive.origin, primitive.size, primitive.logical_pel, colours);
        break;
    }
    }
}

QImage renderNabtsSnapshotToImage(const tbc::vbi::NabtsPageSnapshot &snapshot,
                                  tbc::vbi::NaplpsRenderMode renderMode)
{
    const tbc::vbi::NaplpsRenderGrid grid = tbc::vbi::naplps_render_grid(renderMode);
    if (grid.width <= 0 || grid.height <= 0) {
        return QImage();
    }

    tbc::vbi::NaplpsCellSurface surface(grid);
    tbc::vbi::NaplpsRasteriser rasteriser(surface, tbc::vbi::NaplpsGridMapping(grid));
    for (const tbc::vbi::NabtsPrimitive &primitive : snapshot.primitives) {
        rasteriseNabtsPrimitive(primitive, snapshot, rasteriser);
    }

    QImage image(grid.width, grid.height, QImage::Format_ARGB32);
    image.fill(Qt::black);
    for (qint32 row = 0; row < surface.height(); ++row) {
        for (qint32 column = 0; column < surface.width(); ++column) {
            const tbc::vbi::NaplpsCell &cell = surface.at(column, row);
            if (!cell.painted) {
                continue;
            }
            const QColor colour = nabtsColourToQColor(cell.colour);
            image.setPixelColor(column, grid.height - 1 - row,
                                colour.alpha() == 0 ? QColor(Qt::black) : colour);
        }
    }
    return image;
}

QString buildNabtsHtmlPage(const QString &title,
                           const QString &imageFileName,
                           const QString &debugText,
                           const QString &previousPageHref,
                           const QString &nextPageHref)
{
    QString html;
    html += QStringLiteral("<html><head>");
    html += QStringLiteral("<meta http-equiv=\"Content-Type\" content=\"text/html; charset=utf-8\">");
    html += QStringLiteral("<meta name=\"teletext-format\" content=\"nabts\">");
    html += QStringLiteral("<title>%1</title>").arg(title.toHtmlEscaped());
    html += QStringLiteral("</head><body style=\"background:#000;margin:0;padding:0;display:flex;align-items:flex-start;justify-content:center;\">");
    html += QStringLiteral("<div id=\"nabts-canvas\" style=\"position:relative;display:inline-block;line-height:0;\">");
    if (!previousPageHref.trimmed().isEmpty()) {
        html += QStringLiteral("<a id=\"nabts-prev\" href=\"%1\" title=\"Previous page\" style=\"position:absolute;left:0;top:0;width:50%%;height:100%%;display:block;z-index:5;background:transparent;text-decoration:none;\"></a>")
                    .arg(previousPageHref.toHtmlEscaped());
    }
    if (!nextPageHref.trimmed().isEmpty()) {
        html += QStringLiteral("<a id=\"nabts-next\" href=\"%1\" title=\"Next page\" style=\"position:absolute;right:0;top:0;width:50%%;height:100%%;display:block;z-index:5;background:transparent;text-decoration:none;\"></a>")
                    .arg(nextPageHref.toHtmlEscaped());
    }
    html += QStringLiteral("<img src=\"%1\" style=\"display:block;image-rendering:pixelated;max-width:100%%;height:auto;background:#000;\"/>")
                .arg(imageFileName.toHtmlEscaped());
    html += QStringLiteral("</div>");
    if (!debugText.trimmed().isEmpty()) {
        QString escapedDebug = debugText.toHtmlEscaped();
        escapedDebug.replace(QStringLiteral("\n"), QStringLiteral("<br/>"));
        html += QStringLiteral("<pre id=\"nabts-debug\" class=\"nabts-debug\" style=\"display:none;color:#ddd;background:#000;margin:12px;font-family:Consolas,monospace;font-size:12px;line-height:1.35;white-space:pre-wrap;\">%1</pre>")
                    .arg(escapedDebug);
    }
    html += QStringLiteral("</body></html>");
    return html;
}

bool writeNabtsHtmlFromT33Native(const QString &streamPath,
                                 const QString &outputDirectoryPath,
                                 QString *errorMessage)
{
    QFile streamFile(streamPath);
    if (!streamFile.open(QIODevice::ReadOnly)) {
        if (errorMessage) {
            *errorMessage = QObject::tr("Could not open NABTS stream file: %1").arg(streamPath);
        }
        return false;
    }

    const QByteArray streamBytes = streamFile.readAll();
    if (streamBytes.size() < static_cast<qint64>(tbc::vbi::kNabtsPacketBytes)) {
        if (errorMessage) {
            *errorMessage = QObject::tr("NABTS stream is too short to contain any packets.");
        }
        return false;
    }

    const tbc::vbi::NaplpsRenderMode renderMode = tbc::vbi::NaplpsRenderMode::kTwice;
    const tbc::vbi::NaplpsRenderGrid renderGrid = tbc::vbi::naplps_render_grid(renderMode);

    tbc::vbi::NabtsRecordAssembler recordAssembler;
    tbc::vbi::NabtsGroupAssembler groupAssembler;
    std::map<uint16_t, tbc::vbi::NaplpsInterpreter> channelInterpreters;
    std::map<uint16_t, bool> channelHasDisplay;
    std::vector<NabtsRenderedPage> renderedPages;

    qint32 renderedPageCount = 0;
    qint32 seenPresentationMessages = 0;

    recordAssembler.set_message_callback([&](const tbc::vbi::NabtsMessage &message) {
        if (!tbc::vbi::nabts_type_is_presentation(message.type)) {
            return;
        }
        ++seenPresentationMessages;

        auto interpreterIt = channelInterpreters.find(message.channel);
        if (interpreterIt == channelInterpreters.end()) {
            interpreterIt = channelInterpreters.emplace(message.channel, tbc::vbi::NaplpsInterpreter(renderGrid)).first;
            channelHasDisplay[message.channel] = false;
        }

        tbc::vbi::NaplpsInterpreter &interpreter = interpreterIt->second;
        if (message.classification.caption) {
            interpreter.apply_caption_state();
        }

        const bool keepDisplay = message.classification.more && channelHasDisplay[message.channel];
        tbc::vbi::NabtsPageSnapshot snapshot = interpreter.run(message.data, keepDisplay);
        channelHasDisplay[message.channel] = channelHasDisplay[message.channel] || keepDisplay || !snapshot.empty();
        if (message.classification.support_record || snapshot.empty()) {
            return;
        }

        const QImage image = renderNabtsSnapshotToImage(snapshot, renderMode);
        if (image.isNull()) {
            return;
        }

        ++renderedPageCount;
        const QString channelText = QString::number(message.channel, 16).rightJustified(3, QLatin1Char('0')).toUpper();
        const QString addressText = QString::fromStdString(message.address.text()).toUpper();
        const QString pageStem = QStringLiteral("record-%1-ch%2-addr%3-v%4")
                                     .arg(renderedPageCount, 5, 10, QLatin1Char('0'))
                                     .arg(channelText)
                                     .arg(addressText)
                                     .arg(message.version, 2, 16, QLatin1Char('0'));
        const QString imageFileName = pageStem + QStringLiteral(".png");
        const QString htmlFileName = pageStem + QStringLiteral(".html");
        const QString imagePath = QDir(outputDirectoryPath).filePath(imageFileName);
        if (!image.save(imagePath)) {
            return;
        }

        const QString debugText = QStringLiteral("Channel: 0x%1\nAddress: %2\nVersion: 0x%3\nType: %4\nPrimitives: %5\nText: %6")
                                      .arg(channelText)
                                      .arg(addressText)
                                      .arg(QString::number(message.version, 16).rightJustified(2, QLatin1Char('0')).toUpper())
                                      .arg(message.type)
                                      .arg(snapshot.primitives.size())
                                      .arg(QString::fromStdString(tbc::vbi::nabts_page_text(snapshot)));
        const QString title = QStringLiteral("NABTS %1").arg(pageStem.toUpper());
        renderedPages.push_back({title, imageFileName, htmlFileName, debugText});
    });

    groupAssembler.set_group_callback([&](const tbc::vbi::NabtsDataGroup &group) {
        recordAssembler.add_group(group);
    });

    const qint64 packetCount = streamBytes.size() / static_cast<qint64>(tbc::vbi::kNabtsPacketBytes);
    for (qint64 packetIndex = 0; packetIndex < packetCount; ++packetIndex) {
        const qint64 offset = packetIndex * static_cast<qint64>(tbc::vbi::kNabtsPacketBytes);
        const auto *packetData = reinterpret_cast<const uint8_t *>(streamBytes.constData() + offset);
        const tbc::vbi::NabtsPacket packet =
            tbc::vbi::nabts_decode_packet(packetData, tbc::vbi::kNabtsPacketBytes, nullptr);
        groupAssembler.add_packet(packet);
    }

    groupAssembler.flush();
    recordAssembler.flush();

    if (renderedPageCount == 0) {
        if (errorMessage) {
            if (seenPresentationMessages == 0) {
                *errorMessage = QObject::tr("No NABTS presentation records were found in the .t33 stream.");
            } else {
                *errorMessage = QObject::tr("NABTS records were decoded, but no renderable pages were produced.");
            }
        }
        return false;
    }

    for (size_t pageIndex = 0; pageIndex < renderedPages.size(); ++pageIndex) {
        const NabtsRenderedPage &page = renderedPages[pageIndex];
        const NabtsRenderedPage &previousPage =
            renderedPages[(pageIndex + renderedPages.size() - 1) % renderedPages.size()];
        const NabtsRenderedPage &nextPage =
            renderedPages[(pageIndex + 1) % renderedPages.size()];
        const QString html = buildNabtsHtmlPage(page.title,
                                                page.imageFileName,
                                                page.debugText,
                                                previousPage.htmlFileName,
                                                nextPage.htmlFileName);

        QFile htmlFile(QDir(outputDirectoryPath).filePath(page.htmlFileName));
        if (!htmlFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            if (errorMessage) {
                *errorMessage = QObject::tr("Could not write NABTS HTML page: %1")
                                    .arg(QDir(outputDirectoryPath).filePath(page.htmlFileName));
            }
            return false;
        }
        htmlFile.write(html.toUtf8());
        htmlFile.close();
    }

    return true;
}

bool convertTeletextStreamToHtmlDirectory(const QString &streamPath,
                                          QString *outputDirectoryPath,
                                          QString *errorMessage,
                                          bool forceRegenerateNabtsCache)
{
    const QFileInfo streamInfo(streamPath);
    if (!isTeletextStreamFile(streamInfo)) {
        if (errorMessage) {
            *errorMessage = QObject::tr("Selected file is not a .tXX teletext stream: %1").arg(streamPath);
        }
        return false;
    }

    const bool isNabtsT33Stream =
        streamInfo.suffix().compare(QStringLiteral("t33"), Qt::CaseInsensitive) == 0;

    QString vendorDirectory;
    QString pythonExecutable;
    if (!isNabtsT33Stream) {
        vendorDirectory = resolveTeletextVendorDirectory();
        if (vendorDirectory.isEmpty()) {
            if (errorMessage) {
                *errorMessage = QObject::tr("Could not locate vendored vhs-teletext runtime directory.");
            }
            return false;
        }
        pythonExecutable = resolvePythonExecutable();
    }

    const QString outputPath = QDir::cleanPath(cacheDirectoryForTeletextStream(streamInfo));
    QDir outputDirectory(outputPath);
    if (!outputDirectory.exists() && !outputDirectory.mkpath(QStringLiteral("."))) {
        if (errorMessage) {
            *errorMessage = QObject::tr("Could not create teletext output directory: %1").arg(outputPath);
        }
        return false;
    }
    ensureCssSwitchScript(outputPath);

    if (isNabtsT33Stream) {
        const bool hasCachedHtml = directoryContainsHtmlPages(outputPath);
        if (forceRegenerateNabtsCache || !hasCachedHtml) {
            if (forceRegenerateNabtsCache) {
                const QStringList generatedArtifacts = outputDirectory.entryList(
                    QStringList() << QStringLiteral("*.html") << QStringLiteral("*.png"),
                    QDir::Files,
                    QDir::Name | QDir::IgnoreCase
                );
                for (const QString &artifactFileName : generatedArtifacts) {
                    outputDirectory.remove(artifactFileName);
                }
            }
            if (!writeNabtsHtmlFromT33Native(streamInfo.absoluteFilePath(), outputPath, errorMessage)) {
                return false;
            }
        }
    } else if (!directoryContainsHtmlPages(outputPath)) {
        // Decode on-device via the shared tbc::vbi::TeletextDecoder first, so a
        // WST .t34/.t42 stream needs no Python runtime. The vendored
        // vhs-teletext remains as a fallback for material the native Level 1
        // decoder does not yet cover, and is still the source of the CSS/font
        // assets copied below.
        QString nativeConversionError;
        const bool convertedByNative = writeTeletextHtmlFromT42Native(
            streamInfo.absoluteFilePath(), outputPath, &nativeConversionError);

        if (!convertedByNative) {
            QString pythonConversionError;
            bool convertedByPython = false;

            if (!pythonExecutable.isEmpty()) {
                QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
                const QString existingPythonPath = environment.value(QStringLiteral("PYTHONPATH"));
                if (existingPythonPath.trimmed().isEmpty()) {
                    environment.insert(QStringLiteral("PYTHONPATH"), vendorDirectory);
                } else {
                    environment.insert(
                        QStringLiteral("PYTHONPATH"),
                        vendorDirectory + QDir::listSeparator() + existingPythonPath
                    );
                }

                const QString htmlExportScript = QStringLiteral(R"PY(
import sys
import types

try:
    import tqdm  # noqa: F401
except ModuleNotFoundError:
    tqdm_module = types.ModuleType("tqdm")
    tqdm_module.tqdm = lambda iterable=None, **kwargs: iterable
    sys.modules["tqdm"] = tqdm_module

from teletext.file import FileChunker
from teletext.packet import Packet
from teletext.service import Service

stream_path = sys.argv[1]
output_directory = sys.argv[2]

with open(stream_path, "rb") as stream_file:
    packet_stream = (Packet(data, number) for number, data in FileChunker(stream_file, 42))
    service = Service.from_packets((packet for packet in packet_stream if not packet.is_padding()))
service.to_html(output_directory, None, None)
)PY");
                const QStringList htmlArguments = {
                    QStringLiteral("-c"),
                    htmlExportScript,
                    streamInfo.absoluteFilePath(),
                    outputPath
                };
                convertedByPython = runPythonStep(
                    pythonExecutable,
                    htmlArguments,
                    environment,
                    &pythonConversionError
                );
            } else {
                pythonConversionError = QObject::tr("Python interpreter was not found.");
            }

            if (!convertedByPython) {
                if (errorMessage) {
                    *errorMessage = QObject::tr("Teletext conversion failed. Native error: %1 | Python fallback error: %2")
                                        .arg(nativeConversionError.isEmpty()
                                                 ? QObject::tr("unknown error")
                                                 : nativeConversionError,
                                             pythonConversionError.isEmpty()
                                                 ? QObject::tr("unknown error")
                                                 : pythonConversionError);
                }
                return false;
            }
        }
    }

    if (!isNabtsT33Stream) {
        if (!copyFileReplacing(QDir(vendorDirectory).filePath(QStringLiteral("misc/teletext.css")),
                               outputDirectory.filePath(QStringLiteral("teletext.css")),
                               errorMessage)) {
            return false;
        }
        if (!copyFileReplacing(QDir(vendorDirectory).filePath(QStringLiteral("misc/teletext-noscanlines.css")),
                               outputDirectory.filePath(QStringLiteral("teletext-noscanlines.css")),
                               errorMessage)) {
            return false;
        }
        if (!copyFileReplacing(QDir(vendorDirectory).filePath(QStringLiteral("misc/teletext2.ttf")),
                               outputDirectory.filePath(QStringLiteral("teletext2.ttf")),
                               errorMessage)) {
            return false;
        }
        if (!copyFileReplacing(QDir(vendorDirectory).filePath(QStringLiteral("misc/teletext4.ttf")),
                               outputDirectory.filePath(QStringLiteral("teletext4.ttf")),
                               errorMessage)) {
            return false;
        }
    }

    if (!directoryContainsHtmlPages(outputPath)) {
        if (errorMessage) {
            *errorMessage = QObject::tr("Teletext conversion completed, but no HTML pages were generated.");
        }
        return false;
    }

    if (outputDirectoryPath) {
        *outputDirectoryPath = outputPath;
    }
    return true;
}

QStringList droppedLocalFiles(const QMimeData *mimeData)
{
    QStringList filePaths;
    if (!mimeData || !mimeData->hasUrls()) {
        return filePaths;
    }

    for (const QUrl &url : mimeData->urls()) {
        if (!url.isLocalFile()) {
            continue;
        }
        appendUniqueCandidate(filePaths, url.toLocalFile());
    }
    return filePaths;
}

QString qtTeletextCompatibilityStyle()
{
    return QStringLiteral(
        "<style id=\"qt-teletext-compat\">"
        "body { background: black !important; margin: 0 !important; padding: 0 !important; }"
        ".subpage {"
        "  float: none !important;"
        "  display: inline-block !important;"
        "  white-space: pre !important;"
        "  border: 0 !important;"
        "  text-shadow: none !important;"
        "  filter: none !important;"
        "  font-family: teletext2, \"Courier New\", monospace !important;"
        "  font-size: 20px !important;"
        "  line-height: 1.0 !important;"
        "  min-width: 40ch !important;"
        "  min-height: 25em !important;"
        "}"
        ".row { display: block !important; white-space: pre !important; }"
        ".dh {"
        "  font-family: teletext4, teletext2, \"Courier New\", monospace !important;"
        "  font-size: 200% !important;"
        "  line-height: 1.0 !important;"
        "}"
        "</style>");
}

QString normalizedTeletextHtmlForQt(QString htmlContent)
{
    if (htmlContent.contains(QStringLiteral("qt-teletext-compat"), Qt::CaseInsensitive)) {
        return htmlContent;
    }

    static const QRegularExpression rowBoundaryPattern(
        QStringLiteral("</span>\\s*<span\\s+class=\"row\">"),
        QRegularExpression::CaseInsensitiveOption
    );
    htmlContent.replace(rowBoundaryPattern, QStringLiteral("</span><br/><span class=\"row\">"));

    const QString styleBlock = qtTeletextCompatibilityStyle();
    const qint32 headCloseIndex = htmlContent.indexOf(QStringLiteral("</head>"), 0, Qt::CaseInsensitive);
    if (headCloseIndex >= 0) {
        htmlContent.insert(headCloseIndex, styleBlock);
    } else {
        htmlContent.prepend(QStringLiteral("<head>") + styleBlock + QStringLiteral("</head>"));
    }
    return htmlContent;
}

QString firstImageSourceFromHtml(const QString &htmlContent)
{
    const qint32 imageStart = htmlContent.indexOf(QStringLiteral("<img"), 0, Qt::CaseInsensitive);
    if (imageStart < 0) {
        return QString();
    }

    const qint32 srcMarker = htmlContent.indexOf(QStringLiteral("src="), imageStart, Qt::CaseInsensitive);
    if (srcMarker < 0) {
        return QString();
    }

    qint32 valueStart = srcMarker + 4;
    while (valueStart < htmlContent.size() && htmlContent.at(valueStart).isSpace()) {
        ++valueStart;
    }
    if (valueStart >= htmlContent.size()) {
        return QString();
    }

    const QChar quote = htmlContent.at(valueStart);
    if (quote == QLatin1Char('"') || quote == QLatin1Char('\'')) {
        const qint32 valueEnd = htmlContent.indexOf(quote, valueStart + 1);
        if (valueEnd <= valueStart) {
            return QString();
        }
        return htmlContent.mid(valueStart + 1, valueEnd - valueStart - 1).trimmed();
    }

    qint32 valueEnd = valueStart;
    while (valueEnd < htmlContent.size()
           && !htmlContent.at(valueEnd).isSpace()
           && htmlContent.at(valueEnd) != QLatin1Char('>')) {
        ++valueEnd;
    }
    if (valueEnd <= valueStart) {
        return QString();
    }
    return htmlContent.mid(valueStart, valueEnd - valueStart).trimmed();
}

QString nabtsDebugInnerHtmlFromPage(const QString &htmlContent)
{
    qint32 markerIndex = htmlContent.indexOf(QStringLiteral("id=\"nabts-debug\""), 0, Qt::CaseInsensitive);
    if (markerIndex < 0) {
        markerIndex = htmlContent.indexOf(QStringLiteral("class=\"nabts-debug\""), 0, Qt::CaseInsensitive);
    }
    if (markerIndex >= 0) {
        const qint32 preStart = htmlContent.lastIndexOf(QStringLiteral("<pre"), markerIndex, Qt::CaseInsensitive);
        if (preStart >= 0) {
            const qint32 contentStart = htmlContent.indexOf(QLatin1Char('>'), preStart);
            if (contentStart >= 0) {
                const qint32 preEnd = htmlContent.indexOf(QStringLiteral("</pre>"), contentStart, Qt::CaseInsensitive);
                if (preEnd > contentStart) {
                    return htmlContent.mid(contentStart + 1, preEnd - contentStart - 1).trimmed();
                }
            }
        }
    }

    const qint32 imageStart = htmlContent.indexOf(QStringLiteral("<img"), 0, Qt::CaseInsensitive);
    qint32 scanStart = imageStart >= 0 ? imageStart : 0;
    while (scanStart >= 0 && scanStart < htmlContent.size()) {
        const qint32 divStart = htmlContent.indexOf(QStringLiteral("<div"), scanStart, Qt::CaseInsensitive);
        if (divStart < 0) {
            break;
        }
        const qint32 divOpenEnd = htmlContent.indexOf(QLatin1Char('>'), divStart);
        if (divOpenEnd < 0) {
            break;
        }
        const QString divOpenTag = htmlContent.mid(divStart, divOpenEnd - divStart + 1);
        const bool looksLikeDebugBlock =
            divOpenTag.contains(QStringLiteral("Consolas"), Qt::CaseInsensitive)
            || divOpenTag.contains(QStringLiteral("monospace"), Qt::CaseInsensitive)
            || divOpenTag.contains(QStringLiteral("nabts-debug"), Qt::CaseInsensitive);
        if (looksLikeDebugBlock) {
            const qint32 divEnd = htmlContent.indexOf(QStringLiteral("</div>"), divOpenEnd, Qt::CaseInsensitive);
            if (divEnd > divOpenEnd) {
                return htmlContent.mid(divOpenEnd + 1, divEnd - divOpenEnd - 1).trimmed();
            }
            break;
        }
        scanStart = divOpenEnd + 1;
    }
    return QString();
}

QString nabtsCanvasHtml(const QString &imageSource,
                        const QString &previousPageHref,
                        const QString &nextPageHref)
{
    QString html = QStringLiteral("<div id=\"nabts-canvas\" style=\"position:relative;display:inline-block;line-height:0;\">");
    if (!previousPageHref.trimmed().isEmpty()) {
        html += QStringLiteral("<a id=\"nabts-prev\" href=\"%1\" title=\"Previous page\" style=\"position:absolute;left:0;top:0;width:50%%;height:100%%;display:block;z-index:5;background:transparent;text-decoration:none;\"></a>")
                    .arg(previousPageHref.toHtmlEscaped());
    }
    if (!nextPageHref.trimmed().isEmpty()) {
        html += QStringLiteral("<a id=\"nabts-next\" href=\"%1\" title=\"Next page\" style=\"position:absolute;right:0;top:0;width:50%%;height:100%%;display:block;z-index:5;background:transparent;text-decoration:none;\"></a>")
                    .arg(nextPageHref.toHtmlEscaped());
    }
    html += QStringLiteral("<img src=\"%1\" style=\"display:block;image-rendering:pixelated;max-width:100%%;height:auto;background:#000;\"/>")
                .arg(imageSource.toHtmlEscaped());
    html += QStringLiteral("</div>");
    return html;
}

QString buildNabtsViewerHtmlPage(const QString &title,
                                 const QString &imageSource,
                                 const QString &previousPageHref,
                                 const QString &nextPageHref,
                                 const QString &debugInnerHtml,
                                 bool showDebug)
{
    const QString canvasHtml = nabtsCanvasHtml(imageSource, previousPageHref, nextPageHref);
    QString bodyHtml;
    if (showDebug) {
        bodyHtml += QStringLiteral("<table cellspacing=\"0\" cellpadding=\"0\" style=\"border-collapse:collapse;\"><tr>");
        bodyHtml += QStringLiteral("<td valign=\"top\" style=\"padding:0;margin:0;\">") + canvasHtml + QStringLiteral("</td>");
        bodyHtml += QStringLiteral("<td valign=\"top\" style=\"padding-left:12px;width:320px;min-width:320px;max-width:320px;\">");
        bodyHtml += QStringLiteral("<div style=\"color:#ddd;background:#111;padding:8px;margin:0;font-family:Consolas,monospace;font-size:12px;line-height:1.35;white-space:normal;\">");
        bodyHtml += debugInnerHtml.isEmpty()
            ? QStringLiteral("Debug info unavailable for this page.")
            : debugInnerHtml;
        bodyHtml += QStringLiteral("</div></td></tr></table>");
    } else {
        bodyHtml = canvasHtml;
    }

    QString html;
    html += QStringLiteral("<html><head>");
    html += QStringLiteral("<meta http-equiv=\"Content-Type\" content=\"text/html; charset=utf-8\">");
    html += QStringLiteral("<meta name=\"teletext-format\" content=\"nabts\">");
    html += QStringLiteral("<title>%1</title>").arg(title.toHtmlEscaped());
    html += QStringLiteral("</head><body style=\"background:#000;margin:0;padding:0;\">");
    html += bodyHtml;
    html += QStringLiteral("</body></html>");
    return html;
}

QSize nabtsImageSizeForPage(const QString &imageSource, const QString &pagePath)
{
    if (imageSource.trimmed().isEmpty()) {
        return QSize();
    }

    QUrl imageUrl(imageSource);
    if (imageUrl.isRelative()) {
        imageUrl = QUrl::fromLocalFile(pagePath).resolved(imageUrl);
    }
    if (!imageUrl.isLocalFile()) {
        return QSize();
    }

    QImageReader reader(imageUrl.toLocalFile());
    QSize imageSize = reader.size();
    if (imageSize.isValid()) {
        return imageSize;
    }

    const QImage image(imageUrl.toLocalFile());
    return image.size();
}
} // namespace

TeletextViewerDialog::TeletextViewerDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Teletext Viewer"));
    setModal(false);
    setAttribute(Qt::WA_DeleteOnClose, false);
    setMinimumSize(520, 420);
    setAcceptDrops(true);

    auto *mainLayout = new QVBoxLayout(this);

    auto *directoryLayout = new QHBoxLayout();
    directoryLayout->addWidget(new QLabel(tr("Directory:"), this));
    directoryLineEdit = new QLineEdit(this);
    directoryLineEdit->setReadOnly(true);
    directoryLayout->addWidget(directoryLineEdit, 1);
    browseDirectoryButton = new QPushButton(tr("Browse HTML..."), this);
    browseStreamButton = new QPushButton(tr("Open .tXX..."), this);
    refreshListButton = new QPushButton(tr("Refresh List"), this);
    directoryLayout->addWidget(browseDirectoryButton);
    directoryLayout->addWidget(browseStreamButton);
    directoryLayout->addWidget(refreshListButton);
    mainLayout->addLayout(directoryLayout);

    auto *pageLayout = new QHBoxLayout();
    pageLayout->addWidget(new QLabel(tr("Page:"), this));
    pageComboBox = new QComboBox(this);
    pageComboBox->setMinimumContentsLength(10);
    pageLayout->addWidget(pageComboBox, 1);
    refreshPageButton = new QPushButton(tr("Refresh Page"), this);
    openInBrowserButton = new QPushButton(tr("Open in Browser"), this);
    pageLayout->addWidget(refreshPageButton);
    pageLayout->addWidget(openInBrowserButton);
    mainLayout->addLayout(pageLayout);

    auto *optionsLayout = new QHBoxLayout();
    autoRefreshCheckBox = new QCheckBox(tr("Live refresh"), this);
    autoRefreshCheckBox->setChecked(true);
    optionsLayout->addWidget(autoRefreshCheckBox);
    optionsLayout->addSpacing(12);
    optionsLayout->addWidget(new QLabel(tr("Renderer:"), this));
    rendererComboBox = new QComboBox(this);
    rendererComboBox->addItem(tr("HTML"));
    rendererComboBox->addItem(tr("Native"));
    optionsLayout->addWidget(rendererComboBox);
    flashAnimationCheckBox = new QCheckBox(tr("Animate flash"), this);
    flashAnimationCheckBox->setChecked(true);
    flashAnimationCheckBox->setEnabled(false);
    optionsLayout->addWidget(flashAnimationCheckBox);
    debugInfoCheckBox = new QCheckBox(tr("Debug info"), this);
    debugInfoCheckBox->setChecked(false);
    debugInfoCheckBox->setEnabled(false);
    optionsLayout->addWidget(debugInfoCheckBox);
    rebuildNabtsCacheCheckBox = new QCheckBox(tr("Rebuild .t33 cache"), this);
    rebuildNabtsCacheCheckBox->setChecked(false);
    optionsLayout->addWidget(rebuildNabtsCacheCheckBox);
    optionsLayout->addStretch(1);
    mainLayout->addLayout(optionsLayout);
    viewerStack = new QStackedWidget(this);
    pageViewer = new QTextBrowser(viewerStack);
    pageViewer->document()->setDocumentMargin(0);
    pageViewer->setOpenExternalLinks(false);
    pageViewer->setOpenLinks(false);
    nativePageViewer = new TeletextNativeViewWidget(viewerStack);
    nativePageViewer->setPlaceholderText(tr("No teletext page loaded"));
    nativePageViewer->setAnimationsEnabled(true);
    viewerStack->addWidget(pageViewer);
    viewerStack->addWidget(nativePageViewer);
    viewerStack->setCurrentWidget(pageViewer);
    mainLayout->addWidget(viewerStack, 1);

    refreshTimer = new QTimer(this);
    refreshTimer->setInterval(1000);
    refreshTimer->setSingleShot(false);

    connect(browseDirectoryButton, &QPushButton::clicked,
            this, &TeletextViewerDialog::browseForDirectory);
    connect(browseStreamButton, &QPushButton::clicked,
            this, &TeletextViewerDialog::browseForTeletextStream);
    connect(refreshListButton, &QPushButton::clicked,
            this, &TeletextViewerDialog::refreshPageList);
    connect(pageComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &TeletextViewerDialog::loadSelectedPage);
    connect(refreshPageButton, &QPushButton::clicked,
            this, &TeletextViewerDialog::loadSelectedPage);
    connect(pageViewer, &QTextBrowser::anchorClicked,
            this, &TeletextViewerDialog::handlePageLinkClicked);
    connect(openInBrowserButton, &QPushButton::clicked,
            this, &TeletextViewerDialog::openSelectedPageInBrowser);
    connect(autoRefreshCheckBox, &QCheckBox::toggled,
            this, &TeletextViewerDialog::setAutoRefreshEnabled);
    connect(refreshTimer, &QTimer::timeout,
            this, &TeletextViewerDialog::handlePeriodicRefresh);
    connect(rendererComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) { setNativeRendererEnabled(index == 1); });
    connect(flashAnimationCheckBox, &QCheckBox::toggled,
            this, &TeletextViewerDialog::setFlashAnimationEnabled);
    connect(debugInfoCheckBox, &QCheckBox::toggled,
            this, &TeletextViewerDialog::setDebugInfoEnabled);
    auto *previousPageShortcut = new QShortcut(QKeySequence(Qt::Key_PageUp), this);
    previousPageShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(previousPageShortcut, &QShortcut::activated, this, [this]() {
        cyclePageSelection(-1);
    });
    auto *nextPageShortcut = new QShortcut(QKeySequence(Qt::Key_PageDown), this);
    nextPageShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(nextPageShortcut, &QShortcut::activated, this, [this]() {
        cyclePageSelection(1);
    });
    const QList<QWidget *> dropTargets = {
        directoryLineEdit,
        pageComboBox,
        pageViewer,
        nativePageViewer,
        viewerStack
    };
    for (QWidget *dropTarget : dropTargets) {
        if (!dropTarget) {
            continue;
        }
        dropTarget->setAcceptDrops(true);
        dropTarget->installEventFilter(this);
    }
    if (pageViewer && pageViewer->viewport()) {
        pageViewer->viewport()->setAcceptDrops(true);
        pageViewer->viewport()->installEventFilter(this);
    }

    setAutoRefreshEnabled(autoRefreshCheckBox->isChecked());
    setNativeRendererEnabled(false);
}

void TeletextViewerDialog::setDirectory(const QString &directoryPath)
{
    const QString normalizedPath = QDir::cleanPath(directoryPath.trimmed());
    if (normalizedPath.isEmpty()) {
        return;
    }

    currentDirectoryPath = normalizedPath;
    autoWindowSizePending = true;
    directoryLineEdit->setText(currentDirectoryPath);
    if (nativePageViewer) {
        nativePageViewer->setAssetDirectory(currentDirectoryPath);
    }
    refreshPageList();
}

QString TeletextViewerDialog::directory() const
{
    return currentDirectoryPath;
}

bool TeletextViewerDialog::openTeletextStream(const QString &streamPath, QString *errorMessage)
{
    return openTeletextStreamPath(streamPath, errorMessage);
}

bool TeletextViewerDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (!event) {
        return QDialog::eventFilter(watched, event);
    }

    if (event->type() == QEvent::DragEnter) {
        auto *dragEnterEvent = static_cast<QDragEnterEvent *>(event);
        if (canAcceptDrop(dragEnterEvent->mimeData())) {
            dragEnterEvent->acceptProposedAction();
            return true;
        }
    } else if (event->type() == QEvent::DragMove) {
        auto *dragMoveEvent = static_cast<QDragMoveEvent *>(event);
        if (canAcceptDrop(dragMoveEvent->mimeData())) {
            dragMoveEvent->acceptProposedAction();
            return true;
        }
    } else if (event->type() == QEvent::Drop) {
        auto *dropEvent = static_cast<QDropEvent *>(event);
        if (handleDrop(dropEvent->mimeData())) {
            dropEvent->acceptProposedAction();
            return true;
        }
    } else if (event->type() == QEvent::Wheel) {
        const bool navigationTarget = watched == pageComboBox
                                      || watched == pageViewer
                                      || watched == nativePageViewer
                                      || watched == viewerStack
                                      || (pageViewer && watched == pageViewer->viewport());
        if (navigationTarget) {
            auto *wheelEvent = static_cast<QWheelEvent *>(event);
            const int wheelDeltaY = wheelEvent->angleDelta().y();
            if (wheelDeltaY != 0) {
                const int direction = wheelDeltaY > 0 ? -1 : 1;
                const int stepCount = qMax(1, qAbs(wheelDeltaY) / 120);
                bool pageChanged = false;
                for (int step = 0; step < stepCount; ++step) {
                    if (cyclePageSelection(direction)) {
                        pageChanged = true;
                    }
                }
                if (pageChanged) {
                    wheelEvent->accept();
                    return true;
                }
            }
        }
    }

    return QDialog::eventFilter(watched, event);
}

void TeletextViewerDialog::dragEnterEvent(QDragEnterEvent *event)
{
    if (!event) {
        return;
    }
    if (canAcceptDrop(event->mimeData())) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void TeletextViewerDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    if (!lastLoadedPagePath.isEmpty()) {
        autoSizeWindowForCurrentRenderer();
        autoWindowSizePending = false;
    }
}

void TeletextViewerDialog::dragMoveEvent(QDragMoveEvent *event)
{
    if (!event) {
        return;
    }
    if (canAcceptDrop(event->mimeData())) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void TeletextViewerDialog::dropEvent(QDropEvent *event)
{
    if (!event) {
        return;
    }
    if (handleDrop(event->mimeData())) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void TeletextViewerDialog::browseForDirectory()
{
    const QString startPath = currentDirectoryPath.isEmpty() ? QDir::homePath() : currentDirectoryPath;
    const QString selectedDirectory = QFileDialog::getExistingDirectory(
        this,
        tr("Select teletext HTML directory"),
        startPath,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks
    );
    if (selectedDirectory.isEmpty()) {
        return;
    }

    setDirectory(selectedDirectory);
}
void TeletextViewerDialog::browseForTeletextStream()
{
    const QString startPath = currentDirectoryPath.isEmpty() ? QDir::homePath() : currentDirectoryPath;
    const QString selectedStreamFile = QFileDialog::getOpenFileName(
        this,
        tr("Select teletext stream file"),
        startPath,
        tr("Teletext stream files (*.t??);;All Files (*)")
    );
    if (selectedStreamFile.isEmpty()) {
        return;
    }

    QString errorMessage;
    if (!openTeletextStreamPath(selectedStreamFile, &errorMessage)) {
        QMessageBox::warning(this, tr("Teletext open failed"),
                             errorMessage.isEmpty()
                                 ? tr("Could not open the selected teletext stream.")
                                 : errorMessage);
    }
}

bool TeletextViewerDialog::openTeletextStreamPath(const QString &streamPath, QString *errorMessage)
{
    QString outputDirectoryPath;
    const bool forceRegenerateNabtsCache =
        rebuildNabtsCacheCheckBox && rebuildNabtsCacheCheckBox->isChecked();
    if (!convertTeletextStreamToHtmlDirectory(streamPath,
                                              &outputDirectoryPath,
                                              errorMessage,
                                              forceRegenerateNabtsCache)) {
        return false;
    }
    setDirectory(outputDirectoryPath);
    return true;
}

void TeletextViewerDialog::refreshPageList()
{
    pageComboBox->blockSignals(true);

    const QString previousSelection = pageComboBox->currentText();
    pageComboBox->clear();

    if (!directoryContainsHtml()) {
        pageComboBox->blockSignals(false);
        pageViewer->setHtml(tr("<html><body><p>No teletext HTML pages were found in this directory.</p></body></html>"));
        if (nativePageViewer) {
            nativePageViewer->clearPage();
        }
        if (debugInfoCheckBox) {
            const bool wasBlocked = debugInfoCheckBox->blockSignals(true);
            debugInfoCheckBox->setChecked(false);
            debugInfoCheckBox->setEnabled(false);
            debugInfoCheckBox->blockSignals(wasBlocked);
        }
        setWindowTitle(tr("Teletext Viewer"));
        lastLoadedPagePath.clear();
        lastLoadedPageModified = QDateTime();
        lastLoadedPageIsNabts = false;
        lastLoadedNabtsDebugVisible = false;
        lastLoadedNabtsImageSize = QSize();
        return;
    }

    const QDir directory(currentDirectoryPath);
    const QStringList htmlPages = directory.entryList(
        QStringList() << QStringLiteral("*.html"),
        QDir::Files,
        QDir::Name | QDir::IgnoreCase
    );
    pageComboBox->addItems(htmlPages);

    qint32 selectedIndex = htmlPages.indexOf(previousSelection);
    if (selectedIndex < 0) {
        selectedIndex = htmlPages.indexOf(QStringLiteral("100.html"));
    }
    if (selectedIndex < 0 && !htmlPages.isEmpty()) {
        selectedIndex = 0;
    }
    if (selectedIndex >= 0) {
        pageComboBox->setCurrentIndex(selectedIndex);
    }

    pageComboBox->blockSignals(false);
    loadSelectedPage();
}

void TeletextViewerDialog::loadSelectedPage()
{
    const QString pagePath = selectedPagePath();
    if (pagePath.isEmpty()) {
        return;
    }

    const QFileInfo pageInfo(pagePath);
    if (!pageInfo.exists()) {
        return;
    }

    if (lastLoadedPagePath == pagePath && lastLoadedPageModified == pageInfo.lastModified()) {
        if (autoWindowSizePending) {
            autoSizeWindowForCurrentRenderer();
            autoWindowSizePending = false;
        }
        return;
    }

    QFile pageFile(pagePath);
    if (!pageFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }

    const QString rawPageHtml = QString::fromUtf8(pageFile.readAll());
    const bool hasNabtsMetaTag = rawPageHtml.contains(
        QStringLiteral("<meta name=\"teletext-format\" content=\"nabts\">"),
        Qt::CaseInsensitive
    );
    const bool isNabtsRecordFile = pageInfo.fileName().startsWith(QStringLiteral("record-"), Qt::CaseInsensitive);
    const bool isNabtsHtml = hasNabtsMetaTag || isNabtsRecordFile;
    const bool debugSupported = isNabtsHtml;

    if (debugInfoCheckBox) {
        debugInfoCheckBox->setEnabled(debugSupported);
        if (!debugSupported && debugInfoCheckBox->isChecked()) {
            const bool wasBlocked = debugInfoCheckBox->blockSignals(true);
            debugInfoCheckBox->setChecked(false);
            debugInfoCheckBox->blockSignals(wasBlocked);
        }
    }

    QString renderSourceHtml = rawPageHtml;
    lastLoadedPageIsNabts = isNabtsHtml;
    lastLoadedNabtsDebugVisible = false;
    lastLoadedNabtsImageSize = QSize();
    if (isNabtsHtml) {
        QString previousPageHref;
        QString nextPageHref;
        if (pageComboBox && pageComboBox->count() > 1) {
            const int pageCount = pageComboBox->count();
            int currentIndex = pageComboBox->currentIndex();
            if (currentIndex < 0 || currentIndex >= pageCount) {
                currentIndex = 0;
            }
            const int previousIndex = (currentIndex + pageCount - 1) % pageCount;
            const int nextIndex = (currentIndex + 1) % pageCount;
            previousPageHref = pageComboBox->itemText(previousIndex);
            nextPageHref = pageComboBox->itemText(nextIndex);
        }
        const QString imageSource = firstImageSourceFromHtml(rawPageHtml);
        const QString debugInnerHtml = nabtsDebugInnerHtmlFromPage(rawPageHtml);
        const bool showDebug = debugSupported
                               && debugInfoCheckBox
                               && debugInfoCheckBox->isChecked();
        if (!imageSource.isEmpty()) {
            renderSourceHtml = buildNabtsViewerHtmlPage(pageInfo.completeBaseName(),
                                                        imageSource,
                                                        previousPageHref,
                                                        nextPageHref,
                                                        debugInnerHtml,
                                                        showDebug);
            lastLoadedNabtsImageSize = nabtsImageSizeForPage(imageSource, pagePath);
            lastLoadedNabtsDebugVisible = showDebug;
        }
    }

    QString pageHtml = isNabtsHtml ? renderSourceHtml : normalizedTeletextHtmlForQt(renderSourceHtml);
    pageViewer->setSearchPaths(QStringList() << currentDirectoryPath);
    pageViewer->document()->setBaseUrl(QUrl::fromLocalFile(pagePath));
    pageViewer->setHtml(pageHtml);
    if (nativePageViewer) {
        nativePageViewer->setAssetDirectory(currentDirectoryPath);
        const QVector<TeletextNativeViewWidget::Row> nativeRows =
            TeletextNativeViewWidget::parseHtmlPage(rawPageHtml);
        if (nativeRows.isEmpty()) {
            nativePageViewer->clearPage();
        } else {
            nativePageViewer->setPage(nativeRows);
        }
    }

    lastLoadedPagePath = pagePath;
    lastLoadedPageModified = pageInfo.lastModified();
    setWindowTitle(tr("Teletext Viewer - %1").arg(pageInfo.fileName()));
    if (isNabtsHtml || autoWindowSizePending) {
        autoSizeWindowForCurrentRenderer();
        autoWindowSizePending = false;
    }
}

void TeletextViewerDialog::handlePageLinkClicked(const QUrl &linkUrl)
{
    if (!linkUrl.isValid()) {
        return;
    }

    if (linkUrl.path().isEmpty() && !linkUrl.fragment().isEmpty()) {
        pageViewer->scrollToAnchor(linkUrl.fragment());
        return;
    }

    QUrl resolvedUrl = linkUrl;
    if (resolvedUrl.isRelative() && !lastLoadedPagePath.isEmpty()) {
        resolvedUrl = QUrl::fromLocalFile(lastLoadedPagePath).resolved(linkUrl);
    }

    if (resolvedUrl.isLocalFile()) {
        const QFileInfo linkedFileInfo(QDir::cleanPath(resolvedUrl.toLocalFile()));
        if (linkedFileInfo.exists() &&
            linkedFileInfo.isFile() &&
            linkedFileInfo.suffix().compare(QStringLiteral("html"), Qt::CaseInsensitive) == 0) {
            if (currentDirectoryPath.compare(linkedFileInfo.absolutePath(), Qt::CaseInsensitive) != 0) {
                setDirectory(linkedFileInfo.absolutePath());
            }

            const qint32 linkedPageIndex = pageComboBox->findText(
                linkedFileInfo.fileName(),
                Qt::MatchFixedString
            );
            if (linkedPageIndex >= 0) {
                if (pageComboBox->currentIndex() != linkedPageIndex) {
                    pageComboBox->setCurrentIndex(linkedPageIndex);
                } else {
                    loadSelectedPage();
                }
                return;
            }
        }
    }

    QDesktopServices::openUrl(resolvedUrl);
}

void TeletextViewerDialog::openSelectedPageInBrowser()
{
    const QString pagePath = selectedPagePath();
    if (pagePath.isEmpty()) {
        return;
    }

    const QUrl pageUrl = QUrl::fromLocalFile(pagePath);
    if (!QDesktopServices::openUrl(pageUrl)) {
        QMessageBox::warning(this, tr("Warning"),
                             tr("Could not open teletext page in browser:\n%1").arg(pagePath));
    }
}

void TeletextViewerDialog::setAutoRefreshEnabled(bool enabled)
{
    if (enabled) {
        refreshTimer->start();
    } else {
        refreshTimer->stop();
    }
}

void TeletextViewerDialog::handlePeriodicRefresh()
{
    refreshPageList();
}

void TeletextViewerDialog::setNativeRendererEnabled(bool enabled)
{
    if (!viewerStack || !pageViewer || !nativePageViewer) {
        return;
    }
    viewerStack->setCurrentWidget(enabled ? static_cast<QWidget *>(nativePageViewer)
                                          : static_cast<QWidget *>(pageViewer));
    if (flashAnimationCheckBox) {
        flashAnimationCheckBox->setEnabled(enabled);
    }
    if (!lastLoadedPagePath.isEmpty()) {
        autoSizeWindowForCurrentRenderer();
    }
}

void TeletextViewerDialog::setFlashAnimationEnabled(bool enabled)
{
    if (!nativePageViewer) {
        return;
    }
    nativePageViewer->setAnimationsEnabled(enabled);
}

void TeletextViewerDialog::setDebugInfoEnabled(bool enabled)
{
    Q_UNUSED(enabled)
    lastLoadedPageModified = QDateTime();
    loadSelectedPage();
}

bool TeletextViewerDialog::cyclePageSelection(int direction)
{
    if (!pageComboBox || direction == 0) {
        return false;
    }

    const int pageCount = pageComboBox->count();
    if (pageCount <= 1) {
        return false;
    }

    int currentIndex = pageComboBox->currentIndex();
    if (currentIndex < 0) {
        currentIndex = 0;
    }

    const int normalizedDirection = direction > 0 ? 1 : -1;
    int nextIndex = (currentIndex + normalizedDirection) % pageCount;
    if (nextIndex < 0) {
        nextIndex += pageCount;
    }
    if (nextIndex == currentIndex) {
        return false;
    }

    pageComboBox->setCurrentIndex(nextIndex);
    return true;
}

bool TeletextViewerDialog::canAcceptDrop(const QMimeData *mimeData) const
{
    const QStringList filePaths = droppedLocalFiles(mimeData);
    if (filePaths.isEmpty()) {
        return false;
    }
    const TeletextInputSelection selection = resolveTeletextInputFromHints(filePaths);
    return selection.isValid();
}

bool TeletextViewerDialog::handleDrop(const QMimeData *mimeData)
{
    const QStringList filePaths = droppedLocalFiles(mimeData);
    if (filePaths.isEmpty()) {
        return false;
    }

    const TeletextInputSelection selection = resolveTeletextInputFromHints(filePaths);
    if (!selection.isValid()) {
        return false;
    }

    if (!selection.streamFilePath.isEmpty()) {
        QString errorMessage;
        if (!openTeletextStreamPath(selection.streamFilePath, &errorMessage)) {
            QMessageBox::warning(this, tr("Teletext open failed"),
                                 errorMessage.isEmpty()
                                     ? tr("Could not open the dropped .tXX teletext stream.")
                                     : errorMessage);
            return false;
        }
        return true;
    }

    QString droppedHtmlPageName;
    for (const QString &filePath : filePaths) {
        const QFileInfo fileInfo(filePath);
        if (!fileInfo.exists() || !fileInfo.isFile()) {
            continue;
        }
        if (fileInfo.suffix().compare(QStringLiteral("html"), Qt::CaseInsensitive) != 0) {
            continue;
        }
        if (fileInfo.absolutePath().compare(selection.directoryPath, Qt::CaseInsensitive) == 0) {
            droppedHtmlPageName = fileInfo.fileName();
            break;
        }
    }
    if (droppedHtmlPageName.isEmpty() && !selection.htmlPageName.isEmpty()) {
        droppedHtmlPageName = selection.htmlPageName;
    }
    setDirectory(selection.directoryPath);
    if (!droppedHtmlPageName.isEmpty()) {
        const qint32 pageIndex = pageComboBox->findText(droppedHtmlPageName, Qt::MatchFixedString);
        if (pageIndex >= 0 && pageComboBox->currentIndex() != pageIndex) {
            pageComboBox->setCurrentIndex(pageIndex);
        } else if (pageIndex >= 0) {
            loadSelectedPage();
        }
    }
    return true;
}

bool TeletextViewerDialog::directoryContainsHtml() const
{
    return directoryContainsHtmlPages(currentDirectoryPath);
}

void TeletextViewerDialog::autoSizeWindowForCurrentRenderer()
{
    const bool nativeRendererActive = viewerStack
                                      && viewerStack->currentWidget() == nativePageViewer;
    QSize targetViewerSize;
    if (nativeRendererActive && nativePageViewer) {
        targetViewerSize = nativePageViewer->sizeHint();
    } else if (lastLoadedPageIsNabts && lastLoadedNabtsImageSize.isValid()) {
        const int horizontalPadding = 12;
        const int verticalPadding = 12;
        const int debugGap = lastLoadedNabtsDebugVisible ? 12 : 0;
        const int debugWidth = lastLoadedNabtsDebugVisible ? 320 : 0;
        targetViewerSize = QSize(lastLoadedNabtsImageSize.width() + horizontalPadding + debugGap + debugWidth,
                                 lastLoadedNabtsImageSize.height() + verticalPadding);
    } else if (pageViewer && pageViewer->document()) {
        pageViewer->document()->adjustSize();
        targetViewerSize = pageViewer->document()->size().toSize();
    }
    if (targetViewerSize.width() <= 0 || targetViewerSize.height() <= 0) {
        targetViewerSize = nativeRendererActive ? QSize(640, 480) : QSize(640, 420);
    }
    targetViewerSize += QSize(24, 24);

    QSize targetSize = size();
    if (viewerStack) {
        const QSize currentViewerSize = viewerStack->size();
        targetSize += QSize(targetViewerSize.width() - currentViewerSize.width(),
                            targetViewerSize.height() - currentViewerSize.height());
    } else {
        targetSize = targetViewerSize;
    }
    targetSize = targetSize.expandedTo(minimumSize());

    QRect availableGeometry;
    if (QScreen *screen = windowHandle() ? windowHandle()->screen() : QGuiApplication::primaryScreen()) {
        availableGeometry = screen->availableGeometry();
        const QSize maxSize(static_cast<qint32>(availableGeometry.width() * 0.96),
                            static_cast<qint32>(availableGeometry.height() * 0.96));
        targetSize.setWidth(qMin(targetSize.width(), maxSize.width()));
        targetSize.setHeight(qMin(targetSize.height(), maxSize.height()));
    }

    resize(targetSize);
}

QString TeletextViewerDialog::selectedPagePath() const
{
    if (currentDirectoryPath.trimmed().isEmpty()) {
        return QString();
    }
    if (pageComboBox->currentIndex() < 0) {
        return QString();
    }

    return QDir(currentDirectoryPath).filePath(pageComboBox->currentText());
}
