#include "fieldtimingwidget.h"

#include "plotwidget.h"

#include <QApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

FieldTimingWidget::FieldTimingWidget(QWidget *parent)
    : QWidget(parent)
    , scrollBar_(new QScrollBar(Qt::Horizontal, this))
    , scrollOffset_(0)
    , zoomFactor_(1.0)
    , isDragging_(false)
    , dragStartScrollValue_(0)
{
    connect(scrollBar_, &QScrollBar::valueChanged, this, [this](int value) {
        scrollOffset_ = value;
        update();
    });

    setMinimumSize(600, 400);
    setMouseTracking(true);
}

void FieldTimingWidget::setFieldData(const std::vector<uint16_t> &samples,
                                     const std::vector<uint16_t> &samples2,
                                     const std::vector<uint16_t> &ySamples,
                                     const std::vector<uint16_t> &cSamples,
                                     const std::vector<uint16_t> &ySamples2,
                                     const std::vector<uint16_t> &cSamples2,
                                     const std::optional<TbcMetaData::VideoParameters> &videoParameters)
{
    field1Samples_ = samples;
    field2Samples_ = samples2;
    y1Samples_ = ySamples;
    c1Samples_ = cSamples;
    y2Samples_ = ySamples2;
    c2Samples_ = cSamples2;
    videoParameters_ = videoParameters;

    updateScrollBar();
    update();
}

void FieldTimingWidget::setChannelMode(ChannelMode mode)
{
    if (channelMode_ == mode) {
        return;
    }
    channelMode_ = mode;
    updateScrollBar();
    update();
}

void FieldTimingWidget::scrollToLine(int lineNumber)
{
    if (!videoParameters_.has_value() || videoParameters_->fieldWidth <= 0) {
        return;
    }

    const int lineStartSample = qMax(0, lineNumber - 1) * videoParameters_->fieldWidth;
    const int visibleWidth = width() - (2 * MARGIN) - 50;
    const double pixelsPerSample = getBasePixelsPerSample() * zoomFactor_;
    const int samplesPerView = qMax(1, static_cast<int>(visibleWidth / pixelsPerSample));
    const int targetOffset = qMax(0, lineStartSample - (samplesPerView / 2));

    if (scrollBar_->isEnabled()) {
        scrollBar_->setValue(targetOffset);
    }
}

void FieldTimingWidget::setZoomFactor(double zoomFactor)
{
    const double clampedZoom = qMax(0.01, zoomFactor);
    if (qFuzzyCompare(clampedZoom, zoomFactor_)) {
        return;
    }

    const int visibleWidth = width() - (2 * MARGIN) - 50;
    const double previousPixelsPerSample = getBasePixelsPerSample() * zoomFactor_;
    const int previousSamplesPerView = qMax(1, static_cast<int>(visibleWidth / previousPixelsPerSample));
    const int previousCenterSample = scrollOffset_ + (previousSamplesPerView / 2);

    zoomFactor_ = clampedZoom;
    updateScrollBar();

    const double newPixelsPerSample = getBasePixelsPerSample() * zoomFactor_;
    const int newSamplesPerView = qMax(1, static_cast<int>(visibleWidth / newPixelsPerSample));
    int newOffset = qMax(0, previousCenterSample - (newSamplesPerView / 2));
    newOffset = qMin(newOffset, scrollBar_->maximum());
    scrollBar_->setValue(newOffset);

    update();
}

double FieldTimingWidget::getBasePixelsPerSample() const
{
    const int visibleWidth = width() - (2 * MARGIN) - 50;
    if (visibleWidth <= 0) {
        return DEFAULT_PIXELS_PER_SAMPLE;
    }

    const size_t totalSamples = std::max({
        field1Samples_.size(),
        field2Samples_.size(),
        y1Samples_.size(),
        c1Samples_.size(),
        y2Samples_.size(),
        c2Samples_.size()
    });
    if (totalSamples == 0) {
        return DEFAULT_PIXELS_PER_SAMPLE;
    }

    return static_cast<double>(visibleWidth) / static_cast<double>(totalSamples);
}

void FieldTimingWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const QPalette palette = this->palette();
    painter.fillRect(rect(), palette.color(QPalette::Base));

    const int scrollBarHeight = scrollBar_->sizeHint().height();
    const int leftMargin = 50;
    QRect graphArea(MARGIN + leftMargin,
                    MARGIN,
                    width() - (2 * MARGIN) - leftMargin,
                    height() - (2 * MARGIN) - scrollBarHeight - 10);

    if (field1Samples_.empty() && field2Samples_.empty()
        && y1Samples_.empty() && c1Samples_.empty()
        && y2Samples_.empty() && c2Samples_.empty()) {
        painter.setPen(palette.color(QPalette::Disabled, QPalette::Text));
        painter.drawText(rect(), Qt::AlignCenter, tr("No field data available"));
        return;
    }

    drawGraph(painter, graphArea);
}

void FieldTimingWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);

    const int scrollBarHeight = scrollBar_->sizeHint().height();
    scrollBar_->setGeometry(MARGIN,
                            height() - scrollBarHeight - 5,
                            width() - (2 * MARGIN),
                            scrollBarHeight);
    updateScrollBar();
}

void FieldTimingWidget::wheelEvent(QWheelEvent *event)
{
    if (!scrollBar_->isEnabled()) {
        QWidget::wheelEvent(event);
        return;
    }

    const int delta = -event->angleDelta().y() / 8;
    scrollBar_->setValue(scrollBar_->value() + delta);
    event->accept();
}

void FieldTimingWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && scrollBar_->isEnabled()) {
        isDragging_ = true;
        dragStartPos_ = event->pos();
        dragStartScrollValue_ = scrollBar_->value();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }

    QWidget::mousePressEvent(event);
}

void FieldTimingWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (!isDragging_) {
        QWidget::mouseMoveEvent(event);
        return;
    }

    const int dx = event->pos().x() - dragStartPos_.x();
    const double pixelsPerSample = getBasePixelsPerSample() * zoomFactor_;
    const int sampleDelta = static_cast<int>(-dx / pixelsPerSample);
    scrollBar_->setValue(dragStartScrollValue_ + sampleDelta);
    event->accept();
}

void FieldTimingWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && isDragging_) {
        isDragging_ = false;
        setCursor(Qt::ArrowCursor);
        event->accept();
        return;
    }

    QWidget::mouseReleaseEvent(event);
}

void FieldTimingWidget::updateScrollBar()
{
    const size_t totalSamples = std::max({
        field1Samples_.size(),
        field2Samples_.size(),
        y1Samples_.size(),
        c1Samples_.size(),
        y2Samples_.size(),
        c2Samples_.size()
    });
    if (totalSamples == 0) {
        scrollBar_->setRange(0, 0);
        scrollBar_->setEnabled(false);
        return;
    }

    const int visibleWidth = width() - (2 * MARGIN) - 50;
    const double pixelsPerSample = getBasePixelsPerSample() * zoomFactor_;
    const int samplesPerView = qMax(1, static_cast<int>(visibleWidth / pixelsPerSample));
    if (totalSamples <= static_cast<size_t>(samplesPerView)) {
        scrollBar_->setRange(0, 0);
        scrollBar_->setEnabled(false);
        return;
    }

    const int maxOffset = static_cast<int>(totalSamples) - samplesPerView;
    scrollBar_->setRange(0, maxOffset);
    scrollBar_->setPageStep(samplesPerView);
    scrollBar_->setSingleStep(qMax(1, samplesPerView / 10));
    scrollBar_->setEnabled(true);
}

void FieldTimingWidget::drawGraph(QPainter &painter, const QRect &graphArea)
{
    const bool isDarkTheme = PlotWidget::isDarkTheme();
    const QColor axisColor = isDarkTheme ? Qt::white : Qt::black;
    const QColor gridColor = QColor(128, 128, 128, 120);
    const QColor boundaryColor = isDarkTheme ? QColor(255, 255, 100) : QColor(200, 180, 0);

    painter.setPen(axisColor);
    painter.drawRect(graphArea);

    double minMv = 0.0;
    double maxMv = 0.0;
    getMillivoltRange(minMv, maxMv);
    if (qFuzzyCompare(minMv, maxMv)) {
        maxMv = minMv + 1.0;
    }

    QFont labelFont = painter.font();
    labelFont.setPointSize(8);
    painter.setFont(labelFont);

    const double gridStepMv = 50.0;
    const double labelStepMv = 100.0;
    const double firstGrid = std::floor(minMv / gridStepMv) * gridStepMv;

    painter.setPen(gridColor);
    for (double mv = firstGrid; mv <= maxMv; mv += gridStepMv) {
        const double normalized = (mv - minMv) / (maxMv - minMv);
        const int y = graphArea.bottom() - static_cast<int>(normalized * graphArea.height());
        if (y >= graphArea.top() && y <= graphArea.bottom()) {
            painter.drawLine(graphArea.left(), y, graphArea.right(), y);
        }
    }

    painter.setPen(axisColor);
    const double firstLabel = std::floor(minMv / labelStepMv) * labelStepMv;
    for (double mv = firstLabel; mv <= maxMv; mv += labelStepMv) {
        const double normalized = (mv - minMv) / (maxMv - minMv);
        const int y = graphArea.bottom() - static_cast<int>(normalized * graphArea.height());
        if (y < graphArea.top() || y > graphArea.bottom()) {
            continue;
        }
        const QString label = QStringLiteral("%1 mV").arg(static_cast<int>(mv));
        const QRect labelRect(MARGIN, y - 6, graphArea.left() - MARGIN - 5, 12);
        painter.drawText(labelRect, Qt::AlignRight | Qt::AlignVCenter, label);
    }

    if (videoParameters_.has_value() && videoParameters_->fieldWidth > 0) {
        const size_t totalSamples = std::max({
            field1Samples_.size(),
            field2Samples_.size(),
            y1Samples_.size(),
            y2Samples_.size()
        });
        const int visibleWidth = graphArea.width();
        const double pixelsPerSample = getBasePixelsPerSample() * zoomFactor_;
        const int samplesPerView = qMax(1, static_cast<int>(visibleWidth / pixelsPerSample));
        const int startSample = scrollOffset_;
        const int endSample = qMin(startSample + samplesPerView, static_cast<int>(totalSamples));
        const int linesVisible = qMax(1, samplesPerView / videoParameters_->fieldWidth);

        int markerInterval = 1;
        if (linesVisible > 150) {
            markerInterval = 50;
        } else if (linesVisible > 60) {
            markerInterval = 10;
        }

        painter.setPen(QPen(boundaryColor, 1, Qt::DotLine));
        for (int line = 0; (line * videoParameters_->fieldWidth) < endSample; ++line) {
            if (line % markerInterval != 0) {
                continue;
            }
            const int samplePosition = line * videoParameters_->fieldWidth;
            if (samplePosition < startSample || samplePosition > endSample) {
                continue;
            }

            const int x = graphArea.left() + static_cast<int>((samplePosition - startSample) * pixelsPerSample);
            painter.drawLine(x, graphArea.top(), x, graphArea.bottom());

            if (markerInterval == 1 || line % (markerInterval * 2) == 0) {
                painter.setPen(axisColor);
                const QRect labelRect(x - 16, graphArea.bottom() + 5, 32, 12);
                painter.drawText(labelRect, Qt::AlignCenter, QString::number(line + 1));
                painter.setPen(QPen(boundaryColor, 1, Qt::DotLine));
            }
        }
    }

    painter.setPen(axisColor);
    painter.drawText(graphArea.center().x() - 50, height() - 5, tr("Sample position"));

    const bool hasYc = !y1Samples_.empty() || !c1Samples_.empty() || !y2Samples_.empty() || !c2Samples_.empty();
    const bool hasSecondField = !field2Samples_.empty() || !y2Samples_.empty() || !c2Samples_.empty();

    if (hasYc) {
        const QColor y1Color = isDarkTheme ? QColor(255, 255, 100) : QColor(200, 180, 0);
        const QColor c1Color = isDarkTheme ? QColor(100, 150, 255) : QColor(0, 80, 200);
        const QColor y2Color = isDarkTheme ? QColor(255, 255, 180) : QColor(230, 210, 40);
        const QColor c2Color = isDarkTheme ? QColor(160, 190, 255) : QColor(80, 120, 220);

        const bool showY = (channelMode_ == ChannelMode::YPlusC || channelMode_ == ChannelMode::YOnly);
        const bool showC = (channelMode_ == ChannelMode::YPlusC || channelMode_ == ChannelMode::COnly);

        if (showY && !y1Samples_.empty()) {
            drawSamples(painter, graphArea, y1Samples_, y1Color);
        }
        if (showC && !c1Samples_.empty()) {
            drawSamples(painter, graphArea, c1Samples_, c1Color);
        }
        if (hasSecondField) {
            if (showY && !y2Samples_.empty()) {
                drawSamples(painter, graphArea, y2Samples_, y2Color);
            }
            if (showC && !c2Samples_.empty()) {
                drawSamples(painter, graphArea, c2Samples_, c2Color);
            }
        }
    } else {
        const QColor field1Color = isDarkTheme ? QColor(100, 200, 255) : QColor(0, 100, 200);
        const QColor field2Color = isDarkTheme ? QColor(255, 255, 100) : QColor(200, 180, 0);
        if (!field1Samples_.empty()) {
            drawSamples(painter, graphArea, field1Samples_, field1Color);
        }
        if (!field2Samples_.empty()) {
            drawSamples(painter, graphArea, field2Samples_, field2Color);
        }
    }
}

void FieldTimingWidget::drawSamples(QPainter &painter, const QRect &graphArea,
                                    const std::vector<uint16_t> &samples, const QColor &color, int yOffset)
{
    if (samples.empty()) {
        return;
    }

    painter.setPen(QPen(color, 1));

    const int visibleWidth = graphArea.width();
    const double pixelsPerSample = getBasePixelsPerSample() * zoomFactor_;
    const int samplesPerView = qMax(1, static_cast<int>(visibleWidth / pixelsPerSample));
    const int startSample = scrollOffset_;
    const int endSample = qMin(startSample + samplesPerView, static_cast<int>(samples.size()));
    if (startSample >= static_cast<int>(samples.size())) {
        return;
    }

    double minMv = 0.0;
    double maxMv = 0.0;
    getMillivoltRange(minMv, maxMv);
    if (qFuzzyCompare(minMv, maxMv)) {
        maxMv = minMv + 1.0;
    }

    int linesVisible = 0;
    if (videoParameters_.has_value() && videoParameters_->fieldWidth > 0) {
        linesVisible = qMax(1, samplesPerView / videoParameters_->fieldWidth);
    }

    if (pixelsPerSample < 1.0 && linesVisible > 12) {
        const double samplesPerPixel = 1.0 / pixelsPerSample;
        for (int px = 0; px < visibleWidth; ++px) {
            const int x = graphArea.left() + px;
            const int bucketStart = startSample + static_cast<int>(px * samplesPerPixel);
            const int bucketEnd = qMin(startSample + static_cast<int>((px + 1) * samplesPerPixel),
                                       static_cast<int>(samples.size()));
            if (bucketStart >= bucketEnd || bucketStart >= static_cast<int>(samples.size())) {
                continue;
            }

            uint16_t minSample = samples[bucketStart];
            uint16_t maxSample = samples[bucketStart];
            for (int i = bucketStart; i < bucketEnd; ++i) {
                minSample = std::min(minSample, samples[i]);
                maxSample = std::max(maxSample, samples[i]);
            }

            const double minMvValue = convertSampleToMillivolts(minSample);
            const double maxMvValue = convertSampleToMillivolts(maxSample);
            const double minNorm = (minMvValue - minMv) / (maxMv - minMv);
            const double maxNorm = (maxMvValue - minMv) / (maxMv - minMv);

            const int yTop = graphArea.bottom() - static_cast<int>(maxNorm * graphArea.height()) + yOffset;
            const int yBottom = graphArea.bottom() - static_cast<int>(minNorm * graphArea.height()) + yOffset;
            painter.drawLine(x, yTop, x, yBottom);
        }
        return;
    }

    QPainterPath path;
    bool firstPoint = true;
    for (int i = startSample; i < endSample; ++i) {
        const int x = graphArea.left() + static_cast<int>((i - startSample) * pixelsPerSample);
        const double mv = convertSampleToMillivolts(samples[i]);
        const double normalized = (mv - minMv) / (maxMv - minMv);
        const int y = graphArea.bottom() - static_cast<int>(normalized * graphArea.height()) + yOffset;
        if (firstPoint) {
            path.moveTo(x, y);
            firstPoint = false;
        } else {
            path.lineTo(x, y);
        }
    }

    painter.drawPath(path);
}

double FieldTimingWidget::convertSampleToMillivolts(uint16_t sample) const
{
    if (!videoParameters_.has_value()) {
        return sample / 100.0;
    }

    const auto &videoParameters = videoParameters_.value();
    const bool ntscLike = (videoParameters.system == NTSC || videoParameters.system == PAL_M);
    const double ireToMillivolts = ntscLike ? 7.143 : 7.0;
    const qint32 span = videoParameters.white16bIre - videoParameters.black16bIre;
    if (span <= 0) {
        return sample / 100.0;
    }

    const double ire = (static_cast<double>(sample) - static_cast<double>(videoParameters.black16bIre))
        * 100.0 / static_cast<double>(span);
    return ire * ireToMillivolts;
}

double FieldTimingWidget::getMillivoltRange(double &minMillivolts, double &maxMillivolts) const
{
    if (!videoParameters_.has_value()) {
        minMillivolts = 0.0;
        maxMillivolts = 655.35;
        return maxMillivolts - minMillivolts;
    }

    const auto &videoParameters = videoParameters_.value();
    const qint32 span = videoParameters.white16bIre - videoParameters.black16bIre;
    if (span <= 0) {
        minMillivolts = -200.0;
        maxMillivolts = 1000.0;
        return maxMillivolts - minMillivolts;
    }

    minMillivolts = convertSampleToMillivolts(0);
    maxMillivolts = convertSampleToMillivolts(65535);
    if (minMillivolts > maxMillivolts) {
        std::swap(minMillivolts, maxMillivolts);
    }
    return maxMillivolts - minMillivolts;
}
