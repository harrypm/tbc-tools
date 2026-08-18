/******************************************************************************
 * efmhandlerdialog.cpp
 * tbc-analyse - Dedicated EFM/AC3 handling workflow GUI
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 *
 * This file is part of tbc-tools.
 ******************************************************************************/

#include "efmhandlerdialog.h"

#include <QApplication>
#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSizePolicy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QVBoxLayout>

namespace {
void appendUniqueCaseInsensitive(QStringList *list, const QString &value)
{
    if (!list || value.trimmed().isEmpty()) {
        return;
    }
    for (const QString &existing : *list) {
        if (existing.compare(value, Qt::CaseInsensitive) == 0) {
            return;
        }
    }
    list->append(value);
}

bool isRunnableFile(const QString &candidatePath)
{
    const QFileInfo candidateInfo(candidatePath);
#if defined(Q_OS_WIN)
    return candidateInfo.exists() && candidateInfo.isFile();
#else
    return candidateInfo.exists() && candidateInfo.isFile() && candidateInfo.isExecutable();
#endif
}

QString quoteForShell(const QString &arg)
{
    if (arg.isEmpty()) {
        return QStringLiteral("''");
    }
    QString escaped = arg;
    escaped.replace(QStringLiteral("'"), QStringLiteral("'\"'\"'"));
    return QStringLiteral("'%1'").arg(escaped);
}

QString chooseStartDirectory(const QString &preferredDirectory)
{
    const QString normalizedPreferred = QDir::cleanPath(preferredDirectory.trimmed());
    if (!normalizedPreferred.isEmpty() && QFileInfo::exists(normalizedPreferred)) {
        return normalizedPreferred;
    }
    return QDir::homePath();
}
} // namespace

EfmHandlerDialog::EfmHandlerDialog(QWidget *parent) :
    QDialog(parent)
{
    buildUi();
    setBusy(false);
    appendStatus(tr("Ready."));
}

void EfmHandlerDialog::setSourceDirectory(const QString &directory)
{
    const QString normalizedDirectory = QDir::cleanPath(directory.trimmed());
    if (normalizedDirectory.isEmpty()) {
        return;
    }
    sourceDirectory = normalizedDirectory;
}

void EfmHandlerDialog::setDefaultEfmInput(const QString &efmFilename)
{
    const QString normalizedInput = normalizePath(efmFilename);
    if (normalizedInput.isEmpty()) {
        return;
    }
    const QFileInfo inputInfo(normalizedInput);
    const QString absoluteInput = inputInfo.absoluteFilePath();
    if (!inputInfo.exists() || !inputInfo.isFile()) {
        return;
    }

    for (int index = 0; index < efmInputListWidget->count(); ++index) {
        QListWidgetItem *item = efmInputListWidget->item(index);
        if (item && item->text().compare(absoluteInput, Qt::CaseInsensitive) == 0) {
            return;
        }
    }

    efmInputListWidget->addItem(absoluteInput);
    sourceDirectory = inputInfo.absolutePath();
    if (!userEditedOutputBase || outputBaseLineEdit->text().trimmed().isEmpty()) {
        outputBaseLineEdit->setText(defaultOutputBaseFromInputs());
        userEditedOutputBase = false;
        refreshDerivedOutputPaths(false);
    }
    updateControlStates();
}
void EfmHandlerDialog::setDefaultAc3Input(const QString &ac3Filename)
{
    if (runInProgress) {
        return;
    }

    const QString normalizedInput = normalizePath(ac3Filename);
    if (normalizedInput.isEmpty()) {
        return;
    }

    const QFileInfo inputInfo(normalizedInput);
    const QString absoluteInput = inputInfo.absoluteFilePath();
    if (!inputInfo.exists() || !inputInfo.isFile()) {
        return;
    }

    const QString currentAc3Input = normalizePath(ac3InputLineEdit ? ac3InputLineEdit->text() : QString());
    if (!currentAc3Input.isEmpty()) {
        const QFileInfo currentInfo(currentAc3Input);
        if (currentInfo.absoluteFilePath().compare(absoluteInput, Qt::CaseInsensitive) == 0) {
            return;
        }
    }

    if (ac3InputLineEdit && ac3InputLineEdit->text().trimmed().isEmpty()) {
        ac3InputLineEdit->setText(absoluteInput);
        if (decodeAc3CheckBox && !decodeAc3CheckBox->isChecked()) {
            decodeAc3CheckBox->setChecked(true);
        }
        sourceDirectory = inputInfo.absolutePath();

        if (ac3OutputLineEdit
            && (!userEditedAc3Output || ac3OutputLineEdit->text().trimmed().isEmpty())) {
            ac3OutputLineEdit->setText(
                QDir(inputInfo.absolutePath()).filePath(inputInfo.completeBaseName() + QStringLiteral(".ac3")));
            userEditedAc3Output = false;
        }
        updateControlStates();
    }
}

void EfmHandlerDialog::setSuggestedOutputBase(const QString &outputBasePath)
{
    if (runInProgress) {
        return;
    }

    const QString normalizedBasePath = normalizePath(outputBasePath);
    if (normalizedBasePath.isEmpty()) {
        return;
    }

    if (outputBaseLineEdit
        && (!userEditedOutputBase || outputBaseLineEdit->text().trimmed().isEmpty())) {
        outputBaseLineEdit->setText(normalizedBasePath);
        userEditedOutputBase = false;
        refreshDerivedOutputPaths(false);
    }
}

void EfmHandlerDialog::buildUi()
{
    setWindowTitle(tr("EFM-Handler"));
    resize(860, 730);
    setMinimumSize(860, 690);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(8);
    mainLayout->setSizeConstraint(QLayout::SetMinimumSize);

    efmGroupBox = new QGroupBox(tr("EFM pipeline"), this);
    QGridLayout *efmLayout = new QGridLayout(efmGroupBox);
    efmLayout->setHorizontalSpacing(6);
    efmLayout->setVerticalSpacing(6);
    efmLayout->setColumnStretch(1, 1);
    efmLayout->setColumnStretch(2, 1);


    noTimecodesCheckBox = new QCheckBox(tr("Input EFM has no timecodes (--no-timecodes)"), efmGroupBox);
    noTimecodesCheckBox->setToolTip(tr("Used when running efm-decoder-f2 (--no-timecodes)."));
    efmLayout->addWidget(noTimecodesCheckBox, 0, 0, 1, 4);

    stackF2CheckBox = new QCheckBox(tr("Stack multiple sources before D24"), efmGroupBox);
    stackF2CheckBox->setChecked(true);
    stackF2CheckBox->setToolTip(tr("Only applies when two or more EFM inputs are present."));
    efmLayout->addWidget(stackF2CheckBox, 1, 0, 1, 4);

    QLabel *outputBaseLabel = new QLabel(tr("Output base"), efmGroupBox);
    efmLayout->addWidget(outputBaseLabel, 2, 0, 1, 1);
    outputBaseLineEdit = new QLineEdit(efmGroupBox);
    outputBaseLineEdit->setPlaceholderText(tr("/path/to/output_base (used for .d24 default outputs)"));
    efmLayout->addWidget(outputBaseLineEdit, 2, 1, 1, 2);
    outputBaseBrowseButton = new QPushButton(tr("Browse..."), efmGroupBox);
    efmLayout->addWidget(outputBaseBrowseButton, 2, 3, 1, 1);

    extractAudioCheckBox = new QCheckBox(tr("Extract audio"), efmGroupBox);
    extractAudioCheckBox->setChecked(true);
    efmLayout->addWidget(extractAudioCheckBox, 3, 0, 1, 1);
    audioOutputLineEdit = new QLineEdit(efmGroupBox);
    audioOutputLineEdit->setPlaceholderText(tr("Decoded audio output (.wav)"));
    efmLayout->addWidget(audioOutputLineEdit, 3, 1, 1, 2);
    audioOutputBrowseButton = new QPushButton(tr("Browse..."), efmGroupBox);
    efmLayout->addWidget(audioOutputBrowseButton, 3, 3, 1, 1);

    extractDataCheckBox = new QCheckBox(tr("Extract data"), efmGroupBox);
    extractDataCheckBox->setChecked(true);
    efmLayout->addWidget(extractDataCheckBox, 4, 0, 1, 1);
    dataOutputLineEdit = new QLineEdit(efmGroupBox);
    dataOutputLineEdit->setPlaceholderText(tr("Decoded data output (.bin/.dat)"));
    efmLayout->addWidget(dataOutputLineEdit, 4, 1, 1, 2);
    dataOutputBrowseButton = new QPushButton(tr("Browse..."), efmGroupBox);
    efmLayout->addWidget(dataOutputBrowseButton, 4, 3, 1, 1);

    outputBadSectorMapCheckBox = new QCheckBox(tr("Write bad-sector map metadata (.bsm)"), efmGroupBox);
    outputBadSectorMapCheckBox->setChecked(true);
    efmLayout->addWidget(outputBadSectorMapCheckBox, 5, 1, 1, 3);

    QGroupBox *efmInputsSubBox = new QGroupBox(tr("EFM inputs"), efmGroupBox);
    efmInputsSubBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    efmInputsSubBox->setMinimumHeight(120);
    QGridLayout *efmInputsLayout = new QGridLayout(efmInputsSubBox);
    efmInputsLayout->setHorizontalSpacing(6);
    efmInputsLayout->setVerticalSpacing(6);
    efmInputsLayout->setColumnStretch(0, 1);

    efmInputListWidget = new QListWidget(efmInputsSubBox);
    efmInputListWidget->setSelectionMode(QAbstractItemView::ExtendedSelection);
    efmInputListWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    efmInputListWidget->setMinimumWidth(0);
    efmInputListWidget->setMinimumHeight(84);
    efmInputsLayout->addWidget(efmInputListWidget, 0, 0, 1, 1);

    QVBoxLayout *efmInputButtonsLayout = new QVBoxLayout();
    efmInputButtonsLayout->setContentsMargins(0, 0, 0, 0);
    efmInputButtonsLayout->setSpacing(6);
    efmInputButtonsLayout->setAlignment(Qt::AlignTop);
    addEfmInputButton = new QPushButton(tr("Add..."), efmInputsSubBox);
    removeEfmInputButton = new QPushButton(tr("Remove"), efmInputsSubBox);
    clearEfmInputsButton = new QPushButton(tr("Clear"), efmInputsSubBox);
    efmInputButtonsLayout->addWidget(addEfmInputButton);
    efmInputButtonsLayout->addWidget(removeEfmInputButton);
    efmInputButtonsLayout->addWidget(clearEfmInputsButton);
    efmInputsLayout->addLayout(efmInputButtonsLayout, 0, 1, 1, 1);

    efmLayout->addWidget(efmInputsSubBox, 6, 0, 1, 4);

    const QSizePolicy browseButtonPolicy = outputBaseBrowseButton->sizePolicy();
    const int standardButtonWidth = qMax(outputBaseBrowseButton->sizeHint().width(),
                                         qMax(audioOutputBrowseButton->sizeHint().width(),
                                              dataOutputBrowseButton->sizeHint().width()));
    const int standardButtonHeight = qMax(outputBaseBrowseButton->sizeHint().height(),
                                          qMax(audioOutputBrowseButton->sizeHint().height(),
                                               dataOutputBrowseButton->sizeHint().height()));
    const auto normalizeSidebarButton = [browseButtonPolicy, standardButtonWidth, standardButtonHeight](QPushButton *button) {
        if (!button) {
            return;
        }
        button->setSizePolicy(browseButtonPolicy);
        button->setFixedSize(standardButtonWidth, standardButtonHeight);
    };
    normalizeSidebarButton(addEfmInputButton);
    normalizeSidebarButton(removeEfmInputButton);
    normalizeSidebarButton(clearEfmInputsButton);
    efmInputsLayout->setColumnMinimumWidth(1, standardButtonWidth);

    mainLayout->addWidget(efmGroupBox);

    ac3GroupBox = new QGroupBox(tr("AC3 decoder"), this);
    QGridLayout *ac3Layout = new QGridLayout(ac3GroupBox);
    ac3Layout->setHorizontalSpacing(6);
    ac3Layout->setVerticalSpacing(6);

    decodeAc3CheckBox = new QCheckBox(tr("Decode AC3 symbols"), ac3GroupBox);
    decodeAc3CheckBox->setChecked(false);
    ac3Layout->addWidget(decodeAc3CheckBox, 0, 0, 1, 4);

    QLabel *ac3InputLabel = new QLabel(tr("AC3 input"), ac3GroupBox);
    ac3Layout->addWidget(ac3InputLabel, 1, 0, 1, 1);
    ac3InputLineEdit = new QLineEdit(ac3GroupBox);
    ac3InputLineEdit->setPlaceholderText(tr("Input symbols file for ac3-decoder"));
    ac3Layout->addWidget(ac3InputLineEdit, 1, 1, 1, 2);
    ac3InputBrowseButton = new QPushButton(tr("Browse..."), ac3GroupBox);
    ac3Layout->addWidget(ac3InputBrowseButton, 1, 3, 1, 1);

    QLabel *ac3OutputLabel = new QLabel(tr("AC3 output"), ac3GroupBox);
    ac3Layout->addWidget(ac3OutputLabel, 2, 0, 1, 1);
    ac3OutputLineEdit = new QLineEdit(ac3GroupBox);
    ac3OutputLineEdit->setPlaceholderText(tr("Output AC3 bitstream file (.ac3)"));
    ac3Layout->addWidget(ac3OutputLineEdit, 2, 1, 1, 2);
    ac3OutputBrowseButton = new QPushButton(tr("Browse..."), ac3GroupBox);
    ac3Layout->addWidget(ac3OutputBrowseButton, 2, 3, 1, 1);

    QLabel *ac3FormatLabel = new QLabel(tr("Input format"), ac3GroupBox);
    ac3Layout->addWidget(ac3FormatLabel, 3, 0, 1, 1);
    ac3FormatComboBox = new QComboBox(ac3GroupBox);
    ac3FormatComboBox->addItem(tr("Auto"), QStringLiteral("auto"));
    ac3FormatComboBox->addItem(tr("Raw (symbol bytes 0..3)"), QStringLiteral("raw"));
    ac3FormatComboBox->addItem(tr("ASCII (characters 0..3)"), QStringLiteral("ascii"));
    ac3Layout->addWidget(ac3FormatComboBox, 3, 1, 1, 2);

    mainLayout->addWidget(ac3GroupBox);

    loadDecodedAudioForExportCheckBox = new QCheckBox(tr("After run, load decoded EFM audio into Export tracks"), this);
    loadDecodedAudioForExportCheckBox->setChecked(true);
    mainLayout->addWidget(loadDecodedAudioForExportCheckBox);

    statusLabel = new QLabel(this);
    statusLabel->setWordWrap(true);
    mainLayout->addWidget(statusLabel);

    progressBar = new QProgressBar(this);
    progressBar->setRange(0, 1);
    progressBar->setValue(0);
    mainLayout->addWidget(progressBar);

    logTextEdit = new QPlainTextEdit(this);
    logTextEdit->setReadOnly(true);
    logTextEdit->setMinimumHeight(96);
    logTextEdit->setMaximumHeight(140);
    QFont logFont = logTextEdit->font();
    logFont.setStyleHint(QFont::TypeWriter);
    logTextEdit->setFont(logFont);
    mainLayout->addWidget(logTextEdit);

    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch(1);
    runButton = new QPushButton(tr("Run selected"), this);
    cancelButton = new QPushButton(tr("Cancel"), this);
    closeButton = new QPushButton(tr("Close"), this);
    buttonLayout->addWidget(runButton);
    buttonLayout->addWidget(cancelButton);
    buttonLayout->addWidget(closeButton);
    mainLayout->addLayout(buttonLayout);

    connect(addEfmInputButton, &QPushButton::clicked, this, &EfmHandlerDialog::onAddEfmInputClicked);
    connect(removeEfmInputButton, &QPushButton::clicked, this, &EfmHandlerDialog::onRemoveEfmInputClicked);
    connect(clearEfmInputsButton, &QPushButton::clicked, this, &EfmHandlerDialog::onClearEfmInputsClicked);
    connect(outputBaseBrowseButton, &QPushButton::clicked, this, &EfmHandlerDialog::onBrowseOutputBaseClicked);
    connect(audioOutputBrowseButton, &QPushButton::clicked, this, &EfmHandlerDialog::onBrowseAudioOutputClicked);
    connect(dataOutputBrowseButton, &QPushButton::clicked, this, &EfmHandlerDialog::onBrowseDataOutputClicked);
    connect(ac3InputBrowseButton, &QPushButton::clicked, this, &EfmHandlerDialog::onBrowseAc3InputClicked);
    connect(ac3OutputBrowseButton, &QPushButton::clicked, this, &EfmHandlerDialog::onBrowseAc3OutputClicked);
    connect(runButton, &QPushButton::clicked, this, &EfmHandlerDialog::onRunClicked);
    connect(cancelButton, &QPushButton::clicked, this, &EfmHandlerDialog::onCancelClicked);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::close);

    connect(outputBaseLineEdit, &QLineEdit::textEdited, this, [this]() {
        userEditedOutputBase = true;
        refreshDerivedOutputPaths(false);
    });
    connect(outputBaseLineEdit, &QLineEdit::textChanged, this, [this]() {
        if (!runInProgress) {
            refreshDerivedOutputPaths(false);
        }
    });
    connect(audioOutputLineEdit, &QLineEdit::textEdited, this, [this]() {
        userEditedAudioOutput = true;
    });
    connect(dataOutputLineEdit, &QLineEdit::textEdited, this, [this]() {
        userEditedDataOutput = true;
    });
    connect(ac3OutputLineEdit, &QLineEdit::textEdited, this, [this]() {
        userEditedAc3Output = true;
    });
    connect(ac3InputLineEdit, &QLineEdit::textChanged, this, [this]() {
        if (runInProgress) {
            return;
        }
        const QFileInfo inputInfo(normalizePath(ac3InputLineEdit->text()));
        if ((!userEditedAc3Output || ac3OutputLineEdit->text().trimmed().isEmpty()) && inputInfo.exists()) {
            ac3OutputLineEdit->setText(
                QDir(inputInfo.absolutePath()).filePath(inputInfo.completeBaseName() + QStringLiteral(".ac3")));
        }
    });

    auto stateUpdater = [this]() { updateControlStates(); };
    connect(extractAudioCheckBox, &QCheckBox::toggled, this, stateUpdater);
    connect(extractDataCheckBox, &QCheckBox::toggled, this, stateUpdater);
    connect(decodeAc3CheckBox, &QCheckBox::toggled, this, stateUpdater);
    connect(efmInputListWidget, &QListWidget::itemSelectionChanged, this, stateUpdater);
    connect(efmInputListWidget, &QListWidget::itemSelectionChanged, this, [this]() {
        if (!userEditedOutputBase || outputBaseLineEdit->text().trimmed().isEmpty()) {
            outputBaseLineEdit->setText(defaultOutputBaseFromInputs());
            userEditedOutputBase = false;
            refreshDerivedOutputPaths(false);
        }
    });

    refreshDerivedOutputPaths(true);
    updateControlStates();
}

void EfmHandlerDialog::setBusy(bool enabled)
{
    runInProgress = enabled;

    if (enabled) {
        cancelRequested = false;
    }

    if (efmGroupBox) {
        efmGroupBox->setEnabled(!enabled);
    }
    if (ac3GroupBox) {
        ac3GroupBox->setEnabled(!enabled);
    }
    if (loadDecodedAudioForExportCheckBox) {
        loadDecodedAudioForExportCheckBox->setEnabled(!enabled);
    }
    if (runButton) {
        runButton->setEnabled(!enabled);
    }
    if (closeButton) {
        closeButton->setEnabled(!enabled);
    }
    if (cancelButton) {
        cancelButton->setEnabled(enabled);
    }
    if (!enabled) {
        updateControlStates();
    }
}

void EfmHandlerDialog::updateControlStates()
{
    const bool hasEfmInputs = (efmInputListWidget && efmInputListWidget->count() > 0);
    const bool multipleEfmInputs = (efmInputListWidget && efmInputListWidget->count() > 1);
    const bool runDataExtraction = extractDataCheckBox && extractDataCheckBox->isChecked();
    const bool runAudioExtraction = extractAudioCheckBox && extractAudioCheckBox->isChecked();
    const bool runAc3Decode = decodeAc3CheckBox && decodeAc3CheckBox->isChecked();

    if (removeEfmInputButton) {
        removeEfmInputButton->setEnabled(hasEfmInputs && !efmInputListWidget->selectedItems().isEmpty());
    }
    if (clearEfmInputsButton) {
        clearEfmInputsButton->setEnabled(hasEfmInputs);
    }
    Q_UNUSED(multipleEfmInputs);
    if (outputBaseLineEdit) {
        outputBaseLineEdit->setEnabled(hasEfmInputs);
    }
    if (outputBaseBrowseButton) {
        outputBaseBrowseButton->setEnabled(hasEfmInputs);
    }
    if (extractAudioCheckBox) {
        extractAudioCheckBox->setEnabled(hasEfmInputs);
    }
    if (audioOutputLineEdit) {
        audioOutputLineEdit->setEnabled(hasEfmInputs && runAudioExtraction);
    }
    if (audioOutputBrowseButton) {
        audioOutputBrowseButton->setEnabled(hasEfmInputs && runAudioExtraction);
    }
    if (extractDataCheckBox) {
        extractDataCheckBox->setEnabled(hasEfmInputs);
    }
    if (dataOutputLineEdit) {
        dataOutputLineEdit->setEnabled(hasEfmInputs && runDataExtraction);
    }
    if (dataOutputBrowseButton) {
        dataOutputBrowseButton->setEnabled(hasEfmInputs && runDataExtraction);
    }
    if (outputBadSectorMapCheckBox) {
        outputBadSectorMapCheckBox->setEnabled(hasEfmInputs && runDataExtraction);
    }

    if (ac3InputLineEdit) {
        ac3InputLineEdit->setEnabled(runAc3Decode);
    }
    if (ac3InputBrowseButton) {
        ac3InputBrowseButton->setEnabled(runAc3Decode);
    }
    if (ac3OutputLineEdit) {
        ac3OutputLineEdit->setEnabled(runAc3Decode);
    }
    if (ac3OutputBrowseButton) {
        ac3OutputBrowseButton->setEnabled(runAc3Decode);
    }
    if (ac3FormatComboBox) {
        ac3FormatComboBox->setEnabled(runAc3Decode);
    }

    if (!runInProgress && runButton) {
        runButton->setEnabled(hasEfmInputs || runAc3Decode);
    }
}

QString EfmHandlerDialog::normalizePath(const QString &path) const
{
    QString normalized = path.trimmed();
    normalized.replace(QLatin1Char('\\'), QLatin1Char('/'));
    if (normalized.isEmpty()) {
        return QString();
    }
    return QDir::cleanPath(normalized);
}

QString EfmHandlerDialog::defaultOutputBaseFromInputs() const
{
    if (!efmInputListWidget || efmInputListWidget->count() == 0) {
        return QString();
    }
    QListWidgetItem *firstItem = efmInputListWidget->item(0);
    if (!firstItem) {
        return QString();
    }
    const QFileInfo inputInfo(normalizePath(firstItem->text()));
    if (!inputInfo.exists()) {
        return QString();
    }
    QString baseName = inputInfo.completeBaseName();
    if (baseName.isEmpty()) {
        baseName = QStringLiteral("efm_output");
    }
    return QDir(inputInfo.absolutePath()).filePath(baseName);
}

void EfmHandlerDialog::refreshDerivedOutputPaths(bool forceUpdate)
{
    const QString outputBase = normalizePath(outputBaseLineEdit ? outputBaseLineEdit->text() : QString());
    if (!outputBase.isEmpty()) {
        if (audioOutputLineEdit && (forceUpdate || !userEditedAudioOutput || audioOutputLineEdit->text().trimmed().isEmpty())) {
            audioOutputLineEdit->setText(outputBase + QStringLiteral(".wav"));
            if (forceUpdate) {
                userEditedAudioOutput = false;
            }
        }
        if (dataOutputLineEdit && (forceUpdate || !userEditedDataOutput || dataOutputLineEdit->text().trimmed().isEmpty())) {
            dataOutputLineEdit->setText(outputBase + QStringLiteral(".bin"));
            if (forceUpdate) {
                userEditedDataOutput = false;
            }
        }
    }

    const QFileInfo ac3InputInfo(normalizePath(ac3InputLineEdit ? ac3InputLineEdit->text() : QString()));
    if (ac3OutputLineEdit && ac3InputInfo.exists()) {
        if (forceUpdate || !userEditedAc3Output || ac3OutputLineEdit->text().trimmed().isEmpty()) {
            ac3OutputLineEdit->setText(
                QDir(ac3InputInfo.absolutePath()).filePath(ac3InputInfo.completeBaseName() + QStringLiteral(".ac3")));
            if (forceUpdate) {
                userEditedAc3Output = false;
            }
        }
    }
}

void EfmHandlerDialog::appendStatus(const QString &text)
{
    if (statusLabel) {
        statusLabel->setText(text);
    }
}

void EfmHandlerDialog::appendLog(const QString &text)
{
    if (!logTextEdit) {
        return;
    }
    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    logTextEdit->appendPlainText(QStringLiteral("[%1] %2").arg(timestamp, text));
    QScrollBar *scrollBar = logTextEdit->verticalScrollBar();
    if (scrollBar) {
        scrollBar->setValue(scrollBar->maximum());
    }
}

QString EfmHandlerDialog::formatCommand(const QString &program, const QStringList &arguments) const
{
    QStringList commandParts;
    commandParts << quoteForShell(program);
    for (const QString &argument : arguments) {
        commandParts << quoteForShell(argument);
    }
    return commandParts.join(QLatin1Char(' '));
}

QString EfmHandlerDialog::resolveExternalExecutable(const QStringList &toolNames) const
{
    if (toolNames.isEmpty()) {
        return QString();
    }

    QStringList candidateNames;
    for (const QString &toolName : toolNames) {
        appendUniqueCaseInsensitive(&candidateNames, toolName.trimmed());
#if defined(Q_OS_WIN)
        if (!toolName.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive)) {
            appendUniqueCaseInsensitive(&candidateNames, toolName.trimmed() + QStringLiteral(".exe"));
        }
#endif
    }

    QStringList searchRoots;
    const QString appDir = QCoreApplication::applicationDirPath();
    appendUniqueCaseInsensitive(&searchRoots, appDir);
    appendUniqueCaseInsensitive(&searchRoots, QDir(appDir).filePath(QStringLiteral(".")));
    appendUniqueCaseInsensitive(&searchRoots, QDir(appDir).filePath(QStringLiteral("..")));
    appendUniqueCaseInsensitive(&searchRoots, QDir(appDir).filePath(QStringLiteral("../bin")));
    appendUniqueCaseInsensitive(&searchRoots, QDir(appDir).filePath(QStringLiteral("../../bin")));
    appendUniqueCaseInsensitive(&searchRoots, QDir(appDir).filePath(QStringLiteral("../../../bin")));
    appendUniqueCaseInsensitive(&searchRoots, QDir::currentPath());
    appendUniqueCaseInsensitive(&searchRoots, QDir(QDir::currentPath()).filePath(QStringLiteral("bin")));
    appendUniqueCaseInsensitive(&searchRoots, QDir(QDir::currentPath()).filePath(QStringLiteral("build/bin")));
    appendUniqueCaseInsensitive(&searchRoots, QDir(QDir::currentPath()).filePath(QStringLiteral("../build/bin")));

    for (const QString &root : searchRoots) {
        if (root.isEmpty()) {
            continue;
        }
        const QDir rootDir(root);
        for (const QString &candidateName : candidateNames) {
            const QString candidatePath = rootDir.filePath(candidateName);
            if (isRunnableFile(candidatePath)) {
                return candidatePath;
            }
        }
    }

    for (const QString &candidateName : candidateNames) {
        const QString fromPath = QStandardPaths::findExecutable(candidateName);
        if (!fromPath.isEmpty() && isRunnableFile(fromPath)) {
            return fromPath;
        }
    }

    return QString();
}

bool EfmHandlerDialog::runProcessStep(const CommandStep &step,
                                      int stepNumberOneBased,
                                      int totalSteps,
                                      QString *errorMessage)
{
    if (errorMessage) {
        errorMessage->clear();
    }

    appendStatus(tr("Running step %1/%2: %3").arg(stepNumberOneBased).arg(totalSteps).arg(step.title));
    appendLog(tr("----- Step %1/%2: %3 -----").arg(stepNumberOneBased).arg(totalSteps).arg(step.title));
    appendLog(QStringLiteral("$ %1").arg(formatCommand(step.program, step.arguments)));

    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(step.program, step.arguments);

    if (!process.waitForStarted(5000)) {
        if (errorMessage) {
            *errorMessage = tr("Unable to start tool: %1").arg(step.program);
        }
        return false;
    }

    QByteArray pendingOutputBuffer;
    QString lastOutputLine;
    bool terminateSent = false;
    QElapsedTimer cancelTimer;

    auto consumeOutputChunk = [&](const QByteArray &chunk) {
        if (chunk.isEmpty()) {
            return;
        }

        QByteArray normalized = chunk;
        normalized.replace('\r', '\n');
        pendingOutputBuffer.append(normalized);

        qsizetype lineBreakIndex = -1;
        while ((lineBreakIndex = pendingOutputBuffer.indexOf('\n')) >= 0) {
            QByteArray lineBytes = pendingOutputBuffer.left(lineBreakIndex);
            pendingOutputBuffer.remove(0, lineBreakIndex + 1);
            const QString lineText = QString::fromLocal8Bit(lineBytes).trimmed();
            if (lineText.isEmpty()) {
                continue;
            }
            lastOutputLine = lineText;
            appendLog(lineText);
        }
    };

    while (process.state() != QProcess::NotRunning) {
        if (cancelRequested) {
            if (!terminateSent) {
                process.terminate();
                terminateSent = true;
                cancelTimer.start();
            } else if (cancelTimer.isValid() && cancelTimer.elapsed() > 2000) {
                process.kill();
            }
        }

        process.waitForReadyRead(100);
        consumeOutputChunk(process.readAllStandardOutput());
        QCoreApplication::processEvents();
    }

    consumeOutputChunk(process.readAllStandardOutput());
    if (!pendingOutputBuffer.trimmed().isEmpty()) {
        const QString trailingLine = QString::fromLocal8Bit(pendingOutputBuffer).trimmed();
        if (!trailingLine.isEmpty()) {
            lastOutputLine = trailingLine;
            appendLog(trailingLine);
        }
    }

    if (cancelRequested) {
        if (errorMessage) {
            *errorMessage = tr("Cancelled by user.");
        }
        return false;
    }

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (errorMessage) {
            *errorMessage = !lastOutputLine.isEmpty()
                                ? lastOutputLine
                                : tr("%1 failed with exit code %2.").arg(step.title).arg(process.exitCode());
        }
        return false;
    }

    appendLog(tr("Completed: %1").arg(step.title));
    return true;
}

bool EfmHandlerDialog::runSelectedWorkflows(QString *errorMessage, QStringList *generatedAudioTracks)
{
    if (errorMessage) {
        errorMessage->clear();
    }
    if (generatedAudioTracks) {
        generatedAudioTracks->clear();
    }

    const bool runAc3 = decodeAc3CheckBox && decodeAc3CheckBox->isChecked();
    const bool hasEfmInputs = efmInputListWidget && efmInputListWidget->count() > 0;
    const bool runEfmAudio = extractAudioCheckBox && extractAudioCheckBox->isChecked();
    const bool runEfmData = extractDataCheckBox && extractDataCheckBox->isChecked();

    if (!hasEfmInputs && !runAc3) {
        if (errorMessage) {
            *errorMessage = tr("Add at least one EFM input or enable AC3 decoding.");
        }
        return false;
    }

    QStringList efmInputs;
    if (hasEfmInputs) {
        for (int index = 0; index < efmInputListWidget->count(); ++index) {
            QListWidgetItem *item = efmInputListWidget->item(index);
            if (!item) {
                continue;
            }
            const QString inputPath = QFileInfo(normalizePath(item->text())).absoluteFilePath();
            if (inputPath.isEmpty()) {
                continue;
            }
            if (!QFileInfo::exists(inputPath) || !QFileInfo(inputPath).isFile()) {
                if (errorMessage) {
                    *errorMessage = tr("EFM input does not exist:\n%1").arg(inputPath);
                }
                return false;
            }
            efmInputs << inputPath;
        }
    }

    if (hasEfmInputs && efmInputs.isEmpty()) {
        if (errorMessage) {
            *errorMessage = tr("No valid EFM input files were provided.");
        }
        return false;
    }

    const QString outputBase = QFileInfo(normalizePath(outputBaseLineEdit ? outputBaseLineEdit->text() : QString()))
                                   .absoluteFilePath();
    const QString audioOutputPath = QFileInfo(normalizePath(audioOutputLineEdit ? audioOutputLineEdit->text() : QString()))
                                        .absoluteFilePath();
    const QString dataOutputPath = QFileInfo(normalizePath(dataOutputLineEdit ? dataOutputLineEdit->text() : QString()))
                                       .absoluteFilePath();

    if (hasEfmInputs && outputBase.trimmed().isEmpty()) {
        if (errorMessage) {
            *errorMessage = tr("Please select an EFM output base path.");
        }
        return false;
    }
    if (runEfmAudio && audioOutputPath.trimmed().isEmpty()) {
        if (errorMessage) {
            *errorMessage = tr("Please select an audio output file path.");
        }
        return false;
    }
    if (runEfmData && dataOutputPath.trimmed().isEmpty()) {
        if (errorMessage) {
            *errorMessage = tr("Please select a data output file path.");
        }
        return false;
    }
    if (efmInputs.size() > 1 && (!stackF2CheckBox || !stackF2CheckBox->isChecked())) {
        if (errorMessage) {
            *errorMessage = tr("Multiple EFM inputs require \"Stack multiple sources before D24\".");
        }
        return false;
    }

    const QString ac3InputPath = QFileInfo(normalizePath(ac3InputLineEdit ? ac3InputLineEdit->text() : QString()))
                                     .absoluteFilePath();
    const QString ac3OutputPath = QFileInfo(normalizePath(ac3OutputLineEdit ? ac3OutputLineEdit->text() : QString()))
                                      .absoluteFilePath();
    const QString ac3Format = ac3FormatComboBox
                                  ? ac3FormatComboBox->currentData().toString().trimmed().toLower()
                                  : QStringLiteral("auto");
    if (runAc3) {
        if (ac3InputPath.trimmed().isEmpty() || !QFileInfo::exists(ac3InputPath)) {
            if (errorMessage) {
                *errorMessage = tr("Please select a valid AC3 input symbols file.");
            }
            return false;
        }
        if (ac3OutputPath.trimmed().isEmpty()) {
            if (errorMessage) {
                *errorMessage = tr("Please select an AC3 output file path.");
            }
            return false;
        }
    }

    QString efmF2Tool;
    QString efmStackTool;
    QString efmD24Tool;
    QString efmAudioTool;
    QString efmDataTool;
    QString ac3Tool;

    if (!efmInputs.isEmpty()) {
        efmF2Tool = resolveExternalExecutable({QStringLiteral("efm-decoder-f2")});
        efmD24Tool = resolveExternalExecutable({QStringLiteral("efm-decoder-d24")});
        if (efmF2Tool.isEmpty() || efmD24Tool.isEmpty()) {
            if (errorMessage) {
                *errorMessage = tr("efm-decoder-f2 and/or efm-decoder-d24 were not found.");
            }
            return false;
        }
        if (efmInputs.size() > 1) {
            efmStackTool = resolveExternalExecutable({QStringLiteral("efm-stacker-f2")});
            if (efmStackTool.isEmpty()) {
                if (errorMessage) {
                    *errorMessage = tr("efm-stacker-f2 was not found.");
                }
                return false;
            }
        }
        if (runEfmAudio) {
            efmAudioTool = resolveExternalExecutable({QStringLiteral("efm-decoder-audio")});
            if (efmAudioTool.isEmpty()) {
                if (errorMessage) {
                    *errorMessage = tr("efm-decoder-audio was not found.");
                }
                return false;
            }
        }
        if (runEfmData) {
            efmDataTool = resolveExternalExecutable({QStringLiteral("efm-decoder-data")});
            if (efmDataTool.isEmpty()) {
                if (errorMessage) {
                    *errorMessage = tr("efm-decoder-data was not found.");
                }
                return false;
            }
        }
    }

    if (runAc3) {
        ac3Tool = resolveExternalExecutable({QStringLiteral("ac3-decoder")});
        if (ac3Tool.isEmpty()) {
            if (errorMessage) {
                *errorMessage = tr("ac3-decoder was not found.");
            }
            return false;
        }
    }

    QTemporaryDir tempDirectory;
    if (!efmInputs.isEmpty() && !tempDirectory.isValid()) {
        if (errorMessage) {
            *errorMessage = tr("Unable to create a temporary directory for EFM intermediate files.");
        }
        return false;
    }

    QList<CommandStep> commandSteps;
    QStringList f2Outputs;
    QString selectedF2Input;
    QString d24OutputPath;

    if (!efmInputs.isEmpty()) {
        for (int inputIndex = 0; inputIndex < efmInputs.size(); ++inputIndex) {
            const QString efmInputPath = efmInputs.at(inputIndex);
            const QString f2OutputPath = QDir(tempDirectory.path()).filePath(
                QStringLiteral("efm_input_%1.f2").arg(inputIndex + 1, 2, 10, QChar('0')));
            QStringList f2Arguments;
            if (noTimecodesCheckBox && noTimecodesCheckBox->isChecked()) {
                f2Arguments << QStringLiteral("--no-timecodes");
            }
            f2Arguments << efmInputPath << f2OutputPath;
            commandSteps.append({tr("EFM to F2 (%1/%2)").arg(inputIndex + 1).arg(efmInputs.size()),
                                 efmF2Tool,
                                 f2Arguments});
            f2Outputs << f2OutputPath;
        }

        if (f2Outputs.size() > 1) {
            selectedF2Input = QDir(tempDirectory.path()).filePath(QStringLiteral("efm_stacked.f2"));
            QStringList stackArguments = f2Outputs;
            stackArguments << selectedF2Input;
            commandSteps.append({tr("Stack F2 sources"), efmStackTool, stackArguments});
        } else {
            selectedF2Input = f2Outputs.constFirst();
        }

        d24OutputPath = outputBase + QStringLiteral(".d24");
        commandSteps.append({tr("F2 to D24"), efmD24Tool, {selectedF2Input, d24OutputPath}});

        if (runEfmAudio) {
            commandSteps.append({tr("D24 to audio"), efmAudioTool, {d24OutputPath, audioOutputPath}});
        }
        if (runEfmData) {
            QStringList dataArguments;
            if (outputBadSectorMapCheckBox && outputBadSectorMapCheckBox->isChecked()) {
                dataArguments << QStringLiteral("--output-metadata");
            }
            dataArguments << d24OutputPath << dataOutputPath;
            commandSteps.append({tr("D24 to data"), efmDataTool, dataArguments});
        }
    }

    if (runAc3) {
        QStringList ac3Arguments;
        if (ac3Format != QStringLiteral("auto")) {
            ac3Arguments << QStringLiteral("--format") << ac3Format;
        }
        ac3Arguments << ac3InputPath << ac3OutputPath;
        commandSteps.append({tr("AC3 symbol decode"), ac3Tool, ac3Arguments});
    }

    if (commandSteps.isEmpty()) {
        if (errorMessage) {
            *errorMessage = tr("No runnable workflow was selected.");
        }
        return false;
    }

    if (progressBar) {
        progressBar->setRange(0, commandSteps.size());
        progressBar->setValue(0);
    }

    for (int commandIndex = 0; commandIndex < commandSteps.size(); ++commandIndex) {
        QString stepError;
        if (!runProcessStep(commandSteps.at(commandIndex), commandIndex + 1, commandSteps.size(), &stepError)) {
            if (errorMessage) {
                *errorMessage = stepError;
            }
            return false;
        }
        if (progressBar) {
            progressBar->setValue(commandIndex + 1);
        }
    }

    if (generatedAudioTracks && runEfmAudio) {
        generatedAudioTracks->append(QFileInfo(audioOutputPath).absoluteFilePath());
    }

    return true;
}

void EfmHandlerDialog::onAddEfmInputClicked()
{
    const QString startDirectory = chooseStartDirectory(sourceDirectory);
    const QStringList selectedFiles = QFileDialog::getOpenFileNames(
        this,
        tr("Select EFM input files"),
        startDirectory,
        tr("EFM files (*.efm);;All Files (*)"));
    if (selectedFiles.isEmpty()) {
        return;
    }

    bool addedAny = false;
    for (const QString &selectedFile : selectedFiles) {
        const QString normalizedPath = QFileInfo(normalizePath(selectedFile)).absoluteFilePath();
        if (normalizedPath.isEmpty() || !QFileInfo::exists(normalizedPath)) {
            continue;
        }
        bool alreadyListed = false;
        for (int index = 0; index < efmInputListWidget->count(); ++index) {
            QListWidgetItem *item = efmInputListWidget->item(index);
            if (item && item->text().compare(normalizedPath, Qt::CaseInsensitive) == 0) {
                alreadyListed = true;
                break;
            }
        }
        if (alreadyListed) {
            continue;
        }
        efmInputListWidget->addItem(normalizedPath);
        sourceDirectory = QFileInfo(normalizedPath).absolutePath();
        addedAny = true;
    }

    if (addedAny && (!userEditedOutputBase || outputBaseLineEdit->text().trimmed().isEmpty())) {
        outputBaseLineEdit->setText(defaultOutputBaseFromInputs());
        userEditedOutputBase = false;
        refreshDerivedOutputPaths(false);
    }
    updateControlStates();
}

void EfmHandlerDialog::onRemoveEfmInputClicked()
{
    const QList<QListWidgetItem *> selectedItems = efmInputListWidget->selectedItems();
    for (QListWidgetItem *selectedItem : selectedItems) {
        delete efmInputListWidget->takeItem(efmInputListWidget->row(selectedItem));
    }

    if (efmInputListWidget->count() == 0 && !userEditedOutputBase) {
        outputBaseLineEdit->clear();
    }
    updateControlStates();
}

void EfmHandlerDialog::onClearEfmInputsClicked()
{
    efmInputListWidget->clear();
    if (!userEditedOutputBase) {
        outputBaseLineEdit->clear();
    }
    updateControlStates();
}

void EfmHandlerDialog::onBrowseOutputBaseClicked()
{
    QString suggestedPath = normalizePath(outputBaseLineEdit->text());
    if (suggestedPath.isEmpty()) {
        suggestedPath = defaultOutputBaseFromInputs();
    }
    if (suggestedPath.isEmpty()) {
        suggestedPath = chooseStartDirectory(sourceDirectory);
    }

    const QString selectedPath = QFileDialog::getSaveFileName(
        this,
        tr("Select output base path"),
        suggestedPath,
        tr("All Files (*)"));
    if (selectedPath.isEmpty()) {
        return;
    }

    userEditedOutputBase = true;
    outputBaseLineEdit->setText(normalizePath(selectedPath));
    refreshDerivedOutputPaths(false);
}

void EfmHandlerDialog::onBrowseAudioOutputClicked()
{
    QString suggestedPath = normalizePath(audioOutputLineEdit->text());
    if (suggestedPath.isEmpty()) {
        suggestedPath = normalizePath(outputBaseLineEdit->text()) + QStringLiteral(".wav");
    }
    if (suggestedPath.isEmpty()) {
        suggestedPath = chooseStartDirectory(sourceDirectory);
    }

    const QString selectedPath = QFileDialog::getSaveFileName(
        this,
        tr("Select audio output"),
        suggestedPath,
        tr("WAV audio (*.wav);;All Files (*)"));
    if (selectedPath.isEmpty()) {
        return;
    }

    userEditedAudioOutput = true;
    audioOutputLineEdit->setText(normalizePath(selectedPath));
}

void EfmHandlerDialog::onBrowseDataOutputClicked()
{
    QString suggestedPath = normalizePath(dataOutputLineEdit->text());
    if (suggestedPath.isEmpty()) {
        suggestedPath = normalizePath(outputBaseLineEdit->text()) + QStringLiteral(".bin");
    }
    if (suggestedPath.isEmpty()) {
        suggestedPath = chooseStartDirectory(sourceDirectory);
    }

    const QString selectedPath = QFileDialog::getSaveFileName(
        this,
        tr("Select data output"),
        suggestedPath,
        tr("Binary data (*.bin *.dat);;All Files (*)"));
    if (selectedPath.isEmpty()) {
        return;
    }

    userEditedDataOutput = true;
    dataOutputLineEdit->setText(normalizePath(selectedPath));
}

void EfmHandlerDialog::onBrowseAc3InputClicked()
{
    const QString startDirectory = chooseStartDirectory(sourceDirectory);
    const QString selectedPath = QFileDialog::getOpenFileName(
        this,
        tr("Select AC3 symbols input"),
        startDirectory,
        tr("All Files (*)"));
    if (selectedPath.isEmpty()) {
        return;
    }

    const QString normalizedInputPath = normalizePath(selectedPath);
    ac3InputLineEdit->setText(normalizedInputPath);
    sourceDirectory = QFileInfo(normalizedInputPath).absolutePath();
    if (!userEditedAc3Output || ac3OutputLineEdit->text().trimmed().isEmpty()) {
        const QFileInfo inputInfo(normalizedInputPath);
        ac3OutputLineEdit->setText(
            QDir(inputInfo.absolutePath()).filePath(inputInfo.completeBaseName() + QStringLiteral(".ac3")));
    }
}

void EfmHandlerDialog::onBrowseAc3OutputClicked()
{
    QString suggestedPath = normalizePath(ac3OutputLineEdit->text());
    if (suggestedPath.isEmpty()) {
        const QFileInfo inputInfo(normalizePath(ac3InputLineEdit->text()));
        if (inputInfo.exists()) {
            suggestedPath = QDir(inputInfo.absolutePath()).filePath(inputInfo.completeBaseName() + QStringLiteral(".ac3"));
        }
    }
    if (suggestedPath.isEmpty()) {
        suggestedPath = chooseStartDirectory(sourceDirectory);
    }

    const QString selectedPath = QFileDialog::getSaveFileName(
        this,
        tr("Select AC3 output"),
        suggestedPath,
        tr("AC3 bitstream (*.ac3);;All Files (*)"));
    if (selectedPath.isEmpty()) {
        return;
    }

    userEditedAc3Output = true;
    ac3OutputLineEdit->setText(normalizePath(selectedPath));
}

void EfmHandlerDialog::onRunClicked()
{
    if (runInProgress) {
        return;
    }

    cancelRequested = false;
    QStringList generatedAudioTracks;
    QString runError;

    appendLog(tr("Starting EFM-Handler run"));
    setBusy(true);

    const bool success = runSelectedWorkflows(&runError, &generatedAudioTracks);
    setBusy(false);

    if (!success) {
        if (runError.contains(tr("cancelled"), Qt::CaseInsensitive)) {
            appendStatus(tr("Run cancelled."));
            appendLog(tr("Run cancelled by user."));
            return;
        }
        appendStatus(tr("Run failed."));
        QMessageBox::warning(this,
                             tr("EFM-Handler"),
                             runError.isEmpty() ? tr("Run failed.") : runError);
        return;
    }

    appendStatus(tr("Run complete."));
    appendLog(tr("Run complete."));

    if (!generatedAudioTracks.isEmpty()
        && loadDecodedAudioForExportCheckBox
        && loadDecodedAudioForExportCheckBox->isChecked()) {
        QStringList trackNames;
        trackNames.reserve(generatedAudioTracks.size());
        for (int index = 0; index < generatedAudioTracks.size(); ++index) {
            trackNames << tr("EFM Audio");
        }
        emit exportTracksPrepared(generatedAudioTracks, trackNames);
    }

    QMessageBox::information(this,
                             tr("EFM-Handler"),
                             tr("Selected workflows completed successfully."));
}

void EfmHandlerDialog::onCancelClicked()
{
    if (runInProgress) {
        cancelRequested = true;
        appendStatus(tr("Cancelling..."));
        appendLog(tr("Cancellation requested..."));
        return;
    }
    close();
}
