#ifndef FIELDTIMINGDIALOG_H
#define FIELDTIMINGDIALOG_H

#include <QDialog>
#include <cstdint>
#include <optional>
#include <vector>

#include "tbcmetadata.h"

class FieldTimingWidget;
class QLabel;
class QComboBox;
class QSlider;
class QSpinBox;

class FieldTimingDialog : public QDialog
{
    Q_OBJECT

public:
    explicit FieldTimingDialog(QWidget *parent = nullptr);
    ~FieldTimingDialog() override;

    void setFieldData(const QString &sourceLabel,
                      qint32 firstFieldNumber,
                      const std::vector<uint16_t> &samples,
                      std::optional<qint32> secondFieldNumber = std::nullopt,
                      const std::vector<uint16_t> &samples2 = {},
                      const std::vector<uint16_t> &ySamples = {},
                      const std::vector<uint16_t> &cSamples = {},
                      const std::vector<uint16_t> &ySamples2 = {},
                      const std::vector<uint16_t> &cSamples2 = {},
                      const std::optional<TbcMetaData::VideoParameters> &videoParameters = std::nullopt,
                      qint32 firstFieldHeight = 0,
                      qint32 secondFieldHeight = 0);

private:
    void setupUi();
    void applyZoomForLines(qint32 linesToShow);

    FieldTimingWidget *timingWidget_ = nullptr;
    QLabel *channelLabel_ = nullptr;
    QComboBox *channelCombo_ = nullptr;
    QSlider *zoomSlider_ = nullptr;
    QLabel *zoomValueLabel_ = nullptr;
    QSpinBox *lineSpinBox_ = nullptr;

    qint32 firstFieldHeight_ = 0;
    qint32 secondFieldHeight_ = 0;
    qint32 linesShown_ = 625;
};

#endif // FIELDTIMINGDIALOG_H
