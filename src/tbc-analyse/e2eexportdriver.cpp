/************************************************************************

    e2eexportdriver.cpp

    Headless end-to-end driver for the tbc-analyse Export tab.
    See e2eexportdriver.h.

    This file is part of tbc-tools.

    tbc-tools is free software: you can redistribute it and/or
    modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation, either version 3 of the
    License, or (at your option) any later version.

************************************************************************/

#include "e2eexportdriver.h"

#include <QAbstractButton>
#include <QApplication>
#include <QComboBox>
#include <QFile>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextStream>

#include "exportdialog.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

namespace {

// Log lines that mean the GUI silently degraded or failed to reach the
// exporter. Any of these fails a run unless the spec allow-lists it.
const QStringList kDefaultForbiddenLogPatterns = {
    QStringLiteral("not supported by the detected tbc-video-export"),
    QStringLiteral("tbc-video-export not found"),
    QStringLiteral("Failed to"),
    QStringLiteral("failed"),
    QStringLiteral("timed out"),
};

} // namespace

E2EExportDriver::E2EExportDriver(QWidget *mainWindowParam, Mode modeParam,
                                 const QString &specOrOutputPath, QObject *parent)
    : QObject(parent), mainWindow(mainWindowParam), mode(modeParam), path(specOrOutputPath)
{
    timer.setInterval(100);
    connect(&timer, &QTimer::timeout, this, &E2EExportDriver::tick);
}

void E2EExportDriver::start()
{
    if (mode == Mode::Export) {
        QFile specFile(path);
        if (!specFile.open(QIODevice::ReadOnly)) {
            QTextStream(stderr) << "e2e: cannot read spec " << path << "\n";
            QCoreApplication::exit(2);
            return;
        }
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(specFile.readAll(), &parseError);
        if (!doc.isObject()) {
            QTextStream(stderr) << "e2e: invalid spec JSON: " << parseError.errorString() << "\n";
            QCoreApplication::exit(2);
            return;
        }
        spec = doc.object();
        timeoutMs = static_cast<qint64>(spec.value(QStringLiteral("timeout_seconds")).toDouble(900.0) * 1000.0);
    }
    clock.start();
    timer.start();

    // Last-resort watchdog: if the event loop itself is ever blocked (so the
    // driver's own timeout cannot fire), exit with a diagnosable message
    // instead of hanging until the harness kills the process.
    const qint64 hardLimitMs = timeoutMs + 20000;
    std::thread([hardLimitMs]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(hardLimitMs));
        std::fprintf(stderr, "e2e: FAIL: watchdog: event loop blocked for %lld s (modal or hang)\n",
                     static_cast<long long>(hardLimitMs / 1000));
        std::fflush(nullptr);
        std::_Exit(3);
    }).detach();
}

void E2EExportDriver::handleModal()
{
    QWidget *modal = QApplication::activeModalWidget();
    if (!modal || modal == mainWindow) {
        return;
    }

    QJsonObject record;
    record.insert(QStringLiteral("title"), modal->windowTitle());
    record.insert(QStringLiteral("class"), QString::fromLatin1(modal->metaObject()->className()));
    record.insert(QStringLiteral("state"), state == State::WaitingForSource ? QStringLiteral("startup")
                                                                            : QStringLiteral("export"));

    auto *box = qobject_cast<QMessageBox *>(modal);
    if (!box) {
        // A file picker or custom dialog would block a real user too: the
        // spec must set paths via widgets, so anything else is a failure.
        record.insert(QStringLiteral("action"), QStringLiteral("rejected"));
        modals.append(record);
        ++modalErrors;
        if (auto *dlg = qobject_cast<QDialog *>(modal)) {
            dlg->reject();
        } else {
            modal->close();
        }
        return;
    }

    record.insert(QStringLiteral("text"), box->text());
    record.insert(QStringLiteral("informative"), box->informativeText());
    QAbstractButton *answer = nullptr;
    // Yes/No boxes are confirmations (overwrite, lossy ProRes) whatever their
    // icon; plain warnings/errors are failures.
    if (box->icon() == QMessageBox::Question || box->button(QMessageBox::Yes)) {
        // Never accept startup prompts (plugin installs, updates); export
        // prompts (overwrite) follow the spec.
        const bool yes = state != State::WaitingForSource
                         && spec.value(QStringLiteral("answer_questions")).toString(QStringLiteral("yes"))
                                == QStringLiteral("yes");
        answer = box->button(yes ? QMessageBox::Yes : QMessageBox::No);
        record.insert(QStringLiteral("action"), yes ? QStringLiteral("yes") : QStringLiteral("no"));
    } else {
        if (box->icon() == QMessageBox::Warning || box->icon() == QMessageBox::Critical) {
            ++modalErrors;
        }
        answer = box->button(QMessageBox::Ok);
        record.insert(QStringLiteral("action"), QStringLiteral("ok"));
    }
    modals.append(record);
    if (!answer) {
        answer = box->defaultButton() ? static_cast<QAbstractButton *>(box->defaultButton())
                                      : box->escapeButton();
    }
    if (answer) {
        answer->click();
    } else {
        box->done(0);
    }
}

bool E2EExportDriver::applySpec(QString *errorMessage)
{
    const QJsonObject advanced = spec.value(QStringLiteral("advanced")).toObject();
    if (!advanced.isEmpty()) {
        ExportAdvancedOptions options;
        options.exportMetadata = advanced.value(QStringLiteral("exportMetadata")).toBool(options.exportMetadata);
        options.disableMetadataEmbedding =
            advanced.value(QStringLiteral("disableMetadataEmbedding")).toBool(options.disableMetadataEmbedding);
        options.letterboxCrop = advanced.value(QStringLiteral("letterboxCrop")).toBool(options.letterboxCrop);
        options.forceAnamorphic = advanced.value(QStringLiteral("forceAnamorphic")).toBool(options.forceAnamorphic);
        options.reverseFieldOrder =
            advanced.value(QStringLiteral("reverseFieldOrder")).toBool(options.reverseFieldOrder);
        options.twoStep = advanced.value(QStringLiteral("twoStep")).toBool(options.twoStep);
        options.outputPadding = advanced.value(QStringLiteral("outputPadding")).toInt(options.outputPadding);
        options.threads = advanced.value(QStringLiteral("threads")).toInt(options.threads);
        dialog->setExportAdvancedOptions(options);
    }

    // Custom JSON profile set, loaded the way MainWindow restores it from
    // the saved configuration (the Load button only opens a file picker).
    const QString profileConfig = spec.value(QStringLiteral("profile_config")).toString();
    if (!profileConfig.isEmpty()) {
        dialog->setExportProfileConfigPreference(true, profileConfig);
        QCoreApplication::processEvents();
    }

    // Ordered list: later controls are often repopulated by earlier ones
    // (profile -> container/bit depth, framing -> output resolution).
    const QJsonArray widgets = spec.value(QStringLiteral("widgets")).toArray();
    for (const QJsonValue &entry : widgets) {
        const QJsonObject item = entry.toObject();
        const QString name = item.value(QStringLiteral("name")).toString();
        const QJsonValue value = item.value(QStringLiteral("value"));
        QTextStream(stderr) << "e2e: set " << name << " = "
                            << QString::fromUtf8(QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact))
                            << Qt::endl;
        QWidget *widget = dialog->findChild<QWidget *>(name);
        if (!widget) {
            *errorMessage = QStringLiteral("no Export-tab widget named '%1'").arg(name);
            return false;
        }
        if (!widget->isEnabled()) {
            *errorMessage = QStringLiteral("widget '%1' is disabled; a user could not set it").arg(name);
            return false;
        }

        if (auto *combo = qobject_cast<QComboBox *>(widget)) {
            const QString wanted = value.toString();
            int index = -1;
            for (int i = 0; i < combo->count() && index < 0; ++i) {
                if (combo->itemData(i).toString() == wanted) {
                    index = i;
                }
            }
            if (index < 0) {
                index = combo->findText(wanted);
            }
            if (index < 0) {
                QStringList available;
                for (int i = 0; i < combo->count(); ++i) {
                    available << QStringLiteral("%1 [%2]").arg(combo->itemText(i), combo->itemData(i).toString());
                }
                *errorMessage = QStringLiteral("combo '%1' has no item '%2' (available: %3)")
                                    .arg(name, wanted, available.join(QStringLiteral(", ")));
                return false;
            }
            const auto *model = qobject_cast<QStandardItemModel *>(combo->model());
            if (model && model->item(index) && !model->item(index)->isEnabled()) {
                *errorMessage = QStringLiteral("combo '%1' item '%2' is disabled; a user could not pick it")
                                    .arg(name, wanted);
                return false;
            }
            combo->setCurrentIndex(index);
        } else if (auto *spin = qobject_cast<QSpinBox *>(widget)) {
            const int wanted = value.toInt();
            if (wanted < spin->minimum() || wanted > spin->maximum()) {
                *errorMessage = QStringLiteral("spin box '%1' value %2 outside [%3, %4]")
                                    .arg(name).arg(wanted).arg(spin->minimum()).arg(spin->maximum());
                return false;
            }
            spin->setValue(wanted);
            Q_EMIT spin->editingFinished();
        } else if (auto *line = qobject_cast<QLineEdit *>(widget)) {
            line->setText(value.toString());
            Q_EMIT line->textEdited(line->text());
            Q_EMIT line->editingFinished();
        } else if (auto *button = qobject_cast<QAbstractButton *>(widget); button && button->isCheckable()) {
            if (button->isChecked() != value.toBool()) {
                button->click();
            }
        } else {
            *errorMessage = QStringLiteral("widget '%1' (%2) is not a settable control")
                                .arg(name, QString::fromLatin1(widget->metaObject()->className()));
            return false;
        }
        QCoreApplication::processEvents();
    }
    return true;
}

QJsonObject E2EExportDriver::describeControls() const
{
    QJsonObject controls;
    for (QComboBox *combo : dialog->findChildren<QComboBox *>()) {
        if (combo->objectName().isEmpty()) {
            continue;
        }
        QJsonArray items;
        const auto *model = qobject_cast<QStandardItemModel *>(combo->model());
        for (int i = 0; i < combo->count(); ++i) {
            const bool itemEnabled = !model || !model->item(i) || model->item(i)->isEnabled();
            items.append(QJsonObject{{QStringLiteral("text"), combo->itemText(i)},
                                     {QStringLiteral("data"), combo->itemData(i).toString()},
                                     {QStringLiteral("enabled"), itemEnabled}});
        }
        controls.insert(combo->objectName(),
                        QJsonObject{{QStringLiteral("type"), QStringLiteral("combo")},
                                    {QStringLiteral("enabled"), combo->isEnabled()},
                                    {QStringLiteral("current"), combo->itemData(combo->currentIndex()).toString()},
                                    {QStringLiteral("items"), items}});
    }
    for (QAbstractButton *button : dialog->findChildren<QAbstractButton *>()) {
        if (button->objectName().isEmpty() || !button->isCheckable()) {
            continue;
        }
        controls.insert(button->objectName(),
                        QJsonObject{{QStringLiteral("type"), QStringLiteral("check")},
                                    {QStringLiteral("enabled"), button->isEnabled()},
                                    {QStringLiteral("checked"), button->isChecked()}});
    }
    for (QSpinBox *spin : dialog->findChildren<QSpinBox *>()) {
        if (spin->objectName().isEmpty()) {
            continue;
        }
        controls.insert(spin->objectName(),
                        QJsonObject{{QStringLiteral("type"), QStringLiteral("spin")},
                                    {QStringLiteral("enabled"), spin->isEnabled()},
                                    {QStringLiteral("value"), spin->value()},
                                    {QStringLiteral("minimum"), spin->minimum()},
                                    {QStringLiteral("maximum"), spin->maximum()}});
    }
    for (QLineEdit *line : dialog->findChildren<QLineEdit *>()) {
        if (line->objectName().isEmpty()) {
            continue;
        }
        controls.insert(line->objectName(),
                        QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                                    {QStringLiteral("enabled"), line->isEnabled()},
                                    {QStringLiteral("value"), line->text()}});
    }
    return controls;
}

void E2EExportDriver::tick()
{
    handleModal();
    if (state == State::Done) {
        return;
    }
    if (clock.elapsed() > timeoutMs) {
        finish(false, QStringLiteral("timed out after %1 s").arg(timeoutMs / 1000));
        return;
    }

    if (state == State::WaitingForSource) {
        if (!dialog) {
            dialog = mainWindow->findChild<ExportDialog *>();
        }
        auto *exportButton = dialog ? dialog->findChild<QPushButton *>(QStringLiteral("exportButton")) : nullptr;
        if (!exportButton || !exportButton->isEnabled()) {
            return;
        }

        if (mode == Mode::ListOptions) {
            QFile out(path);
            if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                finish(false, QStringLiteral("cannot write %1").arg(path));
                return;
            }
            out.write(QJsonDocument(describeControls()).toJson());
            out.close();
            state = State::Done;
            timer.stop();
            QCoreApplication::exit(0);
            return;
        }

        const auto captureFrom = [](QPlainTextEdit *edit, QStringList *sink) {
            if (!edit) {
                return;
            }
            QObject::connect(edit, &QPlainTextEdit::textChanged, edit, [edit, sink]() {
                const QString line = edit->document()->lastBlock().text().trimmed();
                if (!line.isEmpty() && (sink->isEmpty() || sink->constLast() != line)) {
                    sink->append(line);
                }
            });
        };
        captureFrom(dialog->findChild<QPlainTextEdit *>(QStringLiteral("logTextEdit")), &capturedLog);
        captureFrom(dialog->findChild<QPlainTextEdit *>(QStringLiteral("proxyLogTextEdit")), &capturedProxyLog);

        QTextStream(stderr) << "e2e: source loaded, applying spec" << Qt::endl;
        state = State::Running;
        // Applying widgets (which pumps events) and the Export click (which
        // runs the whole pre-launch flow synchronously: option probing,
        // config dump, modal prompts) both run from a separate event. Qt
        // never re-delivers a timer event while that timer's handler is
        // running, so doing either inside tick() would freeze the driver as
        // soon as a modal opened (seen on a fresh macOS runner while setting
        // widgets). Ticks keep firing inside their nested loops to answer
        // modals, but must not conclude early (clickInProgress).
        clickInProgress = true;
        QTimer::singleShot(0, this, [this, exportButton]() {
            QString applyError;
            if (!applySpec(&applyError)) {
                finish(false, applyError);
                return;
            }
            if (spec.value(QStringLiteral("list_only")).toBool()) {
                // Report the controls as these widget values leave them
                // (choices that depend on the profile/framing) without
                // exporting.
                finish(modalErrors == 0, QString());
                return;
            }
            QTextStream(stderr) << "e2e: clicking Export" << Qt::endl;
            exportButton->click();
            clickInProgress = false;
            QTextStream(stderr) << "e2e: Export click returned" << Qt::endl;
        });
        return;
    }

    auto *cancelButton = dialog ? dialog->findChild<QPushButton *>(QStringLiteral("cancelButton")) : nullptr;
    if (clickInProgress || (cancelButton && cancelButton->isEnabled())) {
        return;
    }

    // Run finished (or never started). Judge it from what the user sees.
    const QString log = (capturedLog + capturedProxyLog).join(QLatin1Char('\n'));
    const auto *statusLabel = dialog->findChild<QLabel *>(QStringLiteral("statusLabel"));
    const QString status = statusLabel ? statusLabel->text().trimmed() : QString();

    QStringList forbidden = kDefaultForbiddenLogPatterns;
    for (const QJsonValue &pattern : spec.value(QStringLiteral("forbid_log_patterns")).toArray()) {
        forbidden << pattern.toString();
    }
    QStringList allowed;
    for (const QJsonValue &pattern : spec.value(QStringLiteral("allow_log_patterns")).toArray()) {
        allowed << pattern.toString();
    }
    QStringList problems;
    for (const QString &line : log.split(QLatin1Char('\n'))) {
        bool isAllowed = false;
        for (const QString &pattern : allowed) {
            isAllowed = isAllowed || line.contains(pattern, Qt::CaseInsensitive);
        }
        if (isAllowed) {
            continue;
        }
        for (const QString &pattern : forbidden) {
            if (line.contains(pattern, Qt::CaseInsensitive)) {
                problems << QStringLiteral("log: %1").arg(line.trimmed());
                break;
            }
        }
    }
    for (const QJsonValue &needle : spec.value(QStringLiteral("expect_log_contains")).toArray()) {
        if (!log.contains(needle.toString())) {
            problems << QStringLiteral("log is missing expected text '%1'").arg(needle.toString());
        }
    }
    if (status != QStringLiteral("Export complete.")) {
        problems << QStringLiteral("status is '%1', expected 'Export complete.'").arg(status);
    }
    if (modalErrors > 0) {
        problems << QStringLiteral("%1 warning/error dialog(s) were shown").arg(modalErrors);
    }
    finish(problems.isEmpty(), problems.join(QStringLiteral("\n")));
}

void E2EExportDriver::finish(bool ok, const QString &reason)
{
    state = State::Done;
    timer.stop();

    QJsonObject result;
    result.insert(QStringLiteral("ok"), ok);
    result.insert(QStringLiteral("reason"), reason);
    result.insert(QStringLiteral("modals"), modals);
    result.insert(QStringLiteral("elapsed_ms"), static_cast<double>(clock.elapsed()));
    if (dialog) {
        result.insert(QStringLiteral("log"), capturedLog.join(QLatin1Char('\n')));
        result.insert(QStringLiteral("proxy_log"), capturedProxyLog.join(QLatin1Char('\n')));
        if (auto *status = dialog->findChild<QLabel *>(QStringLiteral("statusLabel"))) {
            result.insert(QStringLiteral("status"), status->text());
        }
        result.insert(QStringLiteral("controls"), describeControls());
    }

    const QString resultPath = spec.value(QStringLiteral("result")).toString();
    if (!resultPath.isEmpty()) {
        QFile out(resultPath);
        if (out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            out.write(QJsonDocument(result).toJson());
        }
    }
    QTextStream(stderr) << "e2e: " << (ok ? "PASS" : "FAIL") << (reason.isEmpty() ? QString() : ": " + reason)
                        << Qt::endl;
    if (clickInProgress) {
        // Still inside the Export click's nested event loop, which
        // QCoreApplication::exit() cannot end.
        std::fflush(nullptr);
        std::_Exit(ok ? 0 : 1);
    }
    QCoreApplication::exit(ok ? 0 : 1);
}
