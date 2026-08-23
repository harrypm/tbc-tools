#ifndef FIELDTIMINGWIDGET_H
#define FIELDTIMINGWIDGET_H

#include <QScrollBar>
#include <QWidget>
#include <cstdint>
#include <optional>
#include <vector>

#include "tbcmetadata.h"

class QColor;
class QPainter;

class FieldTimingWidget : public QWidget
{
    Q_OBJECT

public:
    enum class ChannelMode {
        YPlusC = 0,
        YOnly = 1,
        COnly = 2
    };

    explicit FieldTimingWidget(QWidget *parent = nullptr);

    void setFieldData(const std::vector<uint16_t> &samples,
                      const std::vector<uint16_t> &samples2 = {},
                      const std::vector<uint16_t> &ySamples = {},
                      const std::vector<uint16_t> &cSamples = {},
                      const std::vector<uint16_t> &ySamples2 = {},
                      const std::vector<uint16_t> &cSamples2 = {},
                      const std::optional<TbcMetaData::VideoParameters> &videoParameters = std::nullopt);
    void setChannelMode(ChannelMode mode);

    void scrollToLine(int lineNumber);
    void setZoomFactor(double zoomFactor);
    double getBasePixelsPerSample() const;

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    void updateScrollBar();
    void drawGraph(QPainter &painter, const QRect &graphArea);
    void drawSamples(QPainter &painter, const QRect &graphArea,
                     const std::vector<uint16_t> &samples, const QColor &color, int yOffset = 0);
    double convertSampleToMillivolts(uint16_t sample) const;
    double getMillivoltRange(double &minMillivolts, double &maxMillivolts) const;

    std::vector<uint16_t> field1Samples_;
    std::vector<uint16_t> field2Samples_;
    std::vector<uint16_t> y1Samples_;
    std::vector<uint16_t> c1Samples_;
    std::vector<uint16_t> y2Samples_;
    std::vector<uint16_t> c2Samples_;

    QScrollBar *scrollBar_;
    int scrollOffset_;

    std::optional<TbcMetaData::VideoParameters> videoParameters_;
    ChannelMode channelMode_ = ChannelMode::YPlusC;

    static constexpr int MARGIN = 40;
    static constexpr double DEFAULT_PIXELS_PER_SAMPLE = 0.5;
    double zoomFactor_;

    bool isDragging_;
    QPoint dragStartPos_;
    int dragStartScrollValue_;
};

#endif // FIELDTIMINGWIDGET_H
