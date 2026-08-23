#include "fieldtimingdialog.h"

#include "fieldtimingwidget.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

FieldTimingDialog::FieldTimingDialog(QWidget *parent)
    : QDialog(parent)
{
    setupUi();
    setWindowTitle(tr("Field Timing Scope"));
    setWindowFlags(Qt::Window);
    setModal(false);
    setAttribute(Qt::WA_DeleteOnClose, false);
    resize(900, 500);

    QSettings settings;
    restoreGeometry(settings.value(QStringLiteral("FieldTimingDialog/geometry")).toByteArray());
}

FieldTimingDialog::~FieldTimingDialog()
{
    QSettings settings;
    settings.setValue(QStringLiteral("FieldTimingDialog/geometry"), saveGeometry());
}

void FieldTimingDialog::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);

    timingWidget_ = new FieldTimingWidget(this);
    mainLayout->addWidget(timingWidget_, 1);

    auto *controlsLayout = new QHBoxLayout();

    auto *lineLabel = new QLabel(tr("Line:"), this);
    controlsLayout->addWidget(lineLabel);

    lineSpinBox_ = new QSpinBox(this);
    lineSpinBox_->setMinimum(1);
    lineSpinBox_->setMaximum(625);
    lineSpinBox_->setValue(1);
    lineSpinBox_->setMinimumWidth(80);
    connect(lineSpinBox_, &QSpinBox::editingFinished, this, [this]() {
        timingWidget_->scrollToLine(lineSpinBox_->value());
    });
    controlsLayout->addWidget(lineSpinBox_);

    auto *jumpLineButton = new QPushButton(tr("Jump to line"), this);
    jumpLineButton->setAutoDefault(false);
    connect(jumpLineButton, &QPushButton::clicked, this, [this]() {
        timingWidget_->scrollToLine(lineSpinBox_->value());
    });
    controlsLayout->addWidget(jumpLineButton);

    controlsLayout->addSpacing(20);

    channelLabel_ = new QLabel(tr("Channel:"), this);
    channelLabel_->setVisible(false);
    controlsLayout->addWidget(channelLabel_);

    channelCombo_ = new QComboBox(this);
    channelCombo_->addItem(tr("Y + C"), static_cast<int>(FieldTimingWidget::ChannelMode::YPlusC));
    channelCombo_->addItem(tr("Luma (Y)"), static_cast<int>(FieldTimingWidget::ChannelMode::YOnly));
    channelCombo_->addItem(tr("Chroma (C)"), static_cast<int>(FieldTimingWidget::ChannelMode::COnly));
    channelCombo_->setCurrentIndex(0);
    channelCombo_->setVisible(false);
    connect(channelCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        const auto mode = static_cast<FieldTimingWidget::ChannelMode>(channelCombo_->itemData(index).toInt());
        timingWidget_->setChannelMode(mode);
    });
    controlsLayout->addWidget(channelCombo_);

    controlsLayout->addStretch();

    auto *zoomLabel = new QLabel(tr("Lines:"), this);
    controlsLayout->addWidget(zoomLabel);

    auto *zoomInButton = new QPushButton(QStringLiteral("-"), this);
    zoomInButton->setMaximumWidth(30);
    zoomInButton->setAutoRepeat(true);
    zoomInButton->setAutoRepeatDelay(250);
    zoomInButton->setAutoRepeatInterval(50);
    connect(zoomInButton, &QPushButton::clicked, this, [this]() {
        zoomSlider_->setValue(qMax(zoomSlider_->minimum(), zoomSlider_->value() - 1));
    });
    controlsLayout->addWidget(zoomInButton);

    zoomSlider_ = new QSlider(Qt::Horizontal, this);
    zoomSlider_->setMinimum(2);
    zoomSlider_->setMaximum(625);
    zoomSlider_->setValue(linesShown_);
    zoomSlider_->setTickPosition(QSlider::TicksBelow);
    zoomSlider_->setTickInterval(50);
    zoomSlider_->setMaximumWidth(170);
    connect(zoomSlider_, &QSlider::valueChanged, this, [this](int value) {
        linesShown_ = value;
        zoomValueLabel_->setText(QString::number(linesShown_));
        applyZoomForLines(linesShown_);
    });
    controlsLayout->addWidget(zoomSlider_);

    auto *zoomOutButton = new QPushButton(QStringLiteral("+"), this);
    zoomOutButton->setMaximumWidth(30);
    zoomOutButton->setAutoRepeat(true);
    zoomOutButton->setAutoRepeatDelay(250);
    zoomOutButton->setAutoRepeatInterval(50);
    connect(zoomOutButton, &QPushButton::clicked, this, [this]() {
        zoomSlider_->setValue(qMin(zoomSlider_->maximum(), zoomSlider_->value() + 1));
    });
    controlsLayout->addWidget(zoomOutButton);

    zoomValueLabel_ = new QLabel(QString::number(linesShown_), this);
    zoomValueLabel_->setMinimumWidth(40);
    controlsLayout->addWidget(zoomValueLabel_);

    controlsLayout->addSpacing(12);

    auto *closeButton = new QPushButton(tr("Close"), this);
    closeButton->setAutoDefault(false);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::close);
    controlsLayout->addWidget(closeButton);

    mainLayout->addLayout(controlsLayout);
}

void FieldTimingDialog::setFieldData(const QString &sourceLabel,
                                     qint32 firstFieldNumber,
                                     const std::vector<uint16_t> &samples,
                                     std::optional<qint32> secondFieldNumber,
                                     const std::vector<uint16_t> &samples2,
                                     const std::vector<uint16_t> &ySamples,
                                     const std::vector<uint16_t> &cSamples,
                                     const std::vector<uint16_t> &ySamples2,
                                     const std::vector<uint16_t> &cSamples2,
                                     const std::optional<TbcMetaData::VideoParameters> &videoParameters,
                                     qint32 firstFieldHeight,
                                     qint32 secondFieldHeight)
{
    firstFieldHeight_ = firstFieldHeight;
    secondFieldHeight_ = secondFieldHeight;

    QString title = tr("Field Timing Scope - %1 - Field %2").arg(sourceLabel, QString::number(firstFieldNumber));
    if (secondFieldNumber.has_value()) {
        title += tr(" + %1").arg(QString::number(secondFieldNumber.value()));
    }
    setWindowTitle(title);

    timingWidget_->setFieldData(samples, samples2, ySamples, cSamples, ySamples2, cSamples2, videoParameters);

    const bool hasYcData = !ySamples.empty() || !cSamples.empty() || !ySamples2.empty() || !cSamples2.empty();
    channelLabel_->setVisible(hasYcData);
    channelCombo_->setVisible(hasYcData);
    if (!hasYcData) {
        timingWidget_->setChannelMode(FieldTimingWidget::ChannelMode::YPlusC);
    } else {
        const int currentIndex = qBound(0, channelCombo_->currentIndex(), channelCombo_->count() - 1);
        channelCombo_->setCurrentIndex(currentIndex);
    }

    qint32 totalLines = firstFieldHeight_;
    if (secondFieldNumber.has_value() && secondFieldHeight_ > 0) {
        totalLines += secondFieldHeight_;
    }
    if (totalLines <= 0 && videoParameters.has_value()) {
        totalLines = videoParameters->fieldHeight;
        if (secondFieldNumber.has_value()) {
            totalLines += videoParameters->fieldHeight;
        }
    }
    if (totalLines <= 0) {
        totalLines = 625;
    }

    totalLines = qMax(2, totalLines);
    lineSpinBox_->setMaximum(totalLines);
    lineSpinBox_->setValue(qBound(1, lineSpinBox_->value(), totalLines));

    linesShown_ = qBound(2, linesShown_, totalLines);
    zoomSlider_->blockSignals(true);
    zoomSlider_->setMaximum(totalLines);
    zoomSlider_->setValue(linesShown_);
    zoomSlider_->blockSignals(false);
    zoomValueLabel_->setText(QString::number(linesShown_));
    applyZoomForLines(linesShown_);
}

void FieldTimingDialog::applyZoomForLines(qint32 linesToShow)
{
    qint32 totalLines = firstFieldHeight_;
    if (secondFieldHeight_ > 0) {
        totalLines += secondFieldHeight_;
    }
    if (totalLines <= 0) {
        totalLines = zoomSlider_->maximum();
    }
    totalLines = qMax(2, totalLines);

    const qint32 clampedLines = qBound(2, linesToShow, totalLines);
    const double zoomFactor = static_cast<double>(totalLines) / static_cast<double>(clampedLines);
    timingWidget_->setZoomFactor(zoomFactor);
}
