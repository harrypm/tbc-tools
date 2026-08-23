/*
 * File:        waveformmonitordialog.h
 * Module:      ld-analyse
 * Purpose:     Waveform monitor dialog
 *
 * Ported from decode-orc (orc/gui/waveformmonitordialog.{h,cpp}) and adapted
 * to ld-analyse: the dialog is the 16-bit TBC → 10-bit CVBS_U10_4FSC boundary.
 * It receives legacy 16-bit field samples + TbcMetaData::VideoParameters
 * from TbcSource, converts samples via tbc::cvbs::tbc_to_cvbs, performs channel
 * selection (Y+C / Y-only) and active-video line slicing, then hands 10-bit
 * samples to WaveformMonitorWidget.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef WAVEFORMMONITORDIALOG_H
#define WAVEFORMMONITORDIALOG_H

#include "amplitude_conversion.h"
#include "tbcmetadata.h"

#include <QDialog>
#include <cstdint>
#include <vector>

class WaveformMonitorWidget;
class QCheckBox;
class QComboBox;
class QLabel;
class QSlider;

class WaveformMonitorDialog : public QDialog {
  Q_OBJECT

 public:
  explicit WaveformMonitorDialog(QWidget *parent = nullptr);
  ~WaveformMonitorDialog();

  // Feed new frame data to the waveform monitor.
  // composite_samples / y_samples / c_samples are the per-field 16-bit TBC
  // samples (uint16_t) from TbcSource::getFieldTimingData(); y_samples /
  // c_samples are empty for composite sources.  first_field_height and
  // second_field_height are the line counts of the two fields (0 = single).
  // The dialog converts samples to the 10-bit CVBS domain internally.
  void setData(std::vector<uint16_t> composite_samples,
               std::vector<uint16_t> y_samples,
               std::vector<uint16_t> c_samples, int first_field_height,
               int second_field_height,
               const TbcMetaData::VideoParameters &video_params);

  WaveformMonitorWidget *monitorWidget() const { return monitor_widget_; }

  void setAmplitudeUnit(tbc::amp::AmplitudeDisplayUnit unit);

 private:
  void setupUI();
  void updateWidgetForCurrentChannel();

  // 4-tap moving-average FIR — notch at fs/4 removes the 4FSC colour
  // subcarrier, giving clean luma separation from a composite signal.
  static std::vector<int16_t> extractYFromComposite(
      const std::vector<int16_t> &composite);

  // Strip VBI lines from the front of each field.  field1_height and
  // field2_height are updated to reflect the reduced line counts.
  static std::vector<int16_t> sliceToActiveLines(
      const std::vector<int16_t> &samples, int &field1_height,
      int &field2_height, int first_active_line);

  // Convert a flat 16-bit TBC field sample buffer to the 10-bit CVBS domain.
  static std::vector<int16_t> convertTbcToCvbs(
      const std::vector<uint16_t> &tbc, int32_t blanking16, int32_t white16,
      ::VideoSystem system);

  WaveformMonitorWidget *monitor_widget_;
  QComboBox *channel_combo_;
  QComboBox *range_combo_;
  QCheckBox *phosphor_check_;
  QSlider *gain_slider_;
  QLabel *gain_value_label_;

  // Stored frame data (10-bit CVBS domain) — held so the channel selector can
  // switch without a new render request.
  std::vector<int16_t> composite_cvbs_;
  std::vector<int16_t> y_cvbs_;
  std::vector<int16_t> c_cvbs_;
  int first_field_height_ = 0;
  int second_field_height_ = 0;
  TbcMetaData::VideoParameters video_params_{};
};

#endif  // WAVEFORMMONITORDIALOG_H
