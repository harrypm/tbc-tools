/*
 * File:        waveformmonitorwidget.h
 * Module:      ld-analyse
 * Purpose:     Waveform monitor raster widget (multi-line sample-luminance histogram)
 *
 * Ported from decode-orc (orc/gui/waveformmonitorwidget.{h,cpp}) and adapted
 * to ld-analyse: samples arrive already in the 10-bit CVBS_U10_4FSC domain
 * (the dialog converts legacy 16-bit TBC samples via tbc::cvbs::tbc_to_cvbs),
 * and level/geometry data comes from LdDecodeMetaData::VideoParameters.
 * Amplitude maths use tbc::amp (samples10_to_mv / samples10_to_ire), so the
 * Y-axis and markers match decode-orc's spec-referenced conversions exactly.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef WAVEFORMMONITORWIDGET_H
#define WAVEFORMMONITORWIDGET_H

#include "amplitude_conversion.h"
#include "cvbs_signal_constants.h"
#include "lddecodemetadata.h"

#include <QImage>
#include <QWidget>
#include <cstdint>
#include <optional>
#include <vector>

class WaveformMonitorWidget : public QWidget {
  Q_OBJECT

 public:
  explicit WaveformMonitorWidget(QWidget *parent = nullptr);

  // Load frame data and rebuild the accumulation buffer.
  // composite_samples: flat concatenation of all field samples, already in the
  // 10-bit CVBS_U10_4FSC domain (int16_t).  first_field_height / second_field
  // _height are the line counts of the two fields (second 0 = single field).
  // video_params: ld-decode VideoParameters — used for active-video X bounds,
  // sample rate (µs-per-sample), active frame lines (VBI exclusion), and the
  // 16-bit black level (converted to 10-bit for the Black marker).
  void setData(const std::vector<int16_t> &composite_samples,
               int first_field_height, int second_field_height,
               const LdDecodeMetaData::VideoParameters &video_params);

  void setGain(double gain);
  double gain() const { return gain_; }

  void setPhosphorMode(bool enabled);
  bool phosphorMode() const { return phosphor_mode_; }

  // Constrain the Y-axis to the legal luma range when displaying a Y-only
  // channel.  Must be called before setData() to take effect on the frame.
  void setYOnlyMode(bool y_only);
  bool yOnlyMode() const { return y_only_mode_; }

  void setAmplitudeUnit(tbc::amp::AmplitudeDisplayUnit unit);

 protected:
  void paintEvent(QPaintEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;

 private:
  void accumulate(const std::vector<int16_t> &samples, int total_lines,
                  int active_start, int active_end, int32_t blanking_level,
                  int32_t white_level, ::VideoSystem sys);
  void rebuildImage(const QRect &plot_area);
  void drawGrid(QPainter &painter, const QRect &plot_area) const;
  void drawYAxis(QPainter &painter, const QRect &plot_area) const;
  void drawXAxis(QPainter &painter, const QRect &plot_area) const;
  void drawLevelMarkers(QPainter &painter, const QRect &plot_area) const;
  int mvToPixelY(double mv, const QRect &plot_area) const;
  QRect plotArea() const;

  QColor displayBackground() const;
  QColor displayTrace() const;
  QColor displayAxis() const;
  QColor displayGrid() const;

  // Accumulation buffer: count_buffer_[x_sample][y_bin]
  std::vector<std::vector<uint32_t>> count_buffer_;
  int x_samples_ = 0;
  int active_video_start_ = 0;
  double us_per_sample_ = 1000000.0 / 14318181.8;  // default: NTSC 4FSC
  int y_bins_ = 0;
  double y_min_mv_ = -350.0;
  double y_max_mv_ = 950.0;
  static constexpr double kBinWidthMv = 1.0;
  int line_count_ = 0;

  double gain_ = 1.0;
  bool phosphor_mode_ = false;
  bool y_only_mode_ = false;
  tbc::amp::AmplitudeDisplayUnit amplitude_unit_ =
      tbc::amp::AmplitudeDisplayUnit::IRE;
  bool image_dirty_ = true;
  QImage cached_image_;

  // Resolved 10-bit CVBS levels (normative) + 16-bit black for the Black
  // marker, plus the resolved 16-bit blanking used by tbc_to_cvbs.
  ::VideoSystem system_ = NTSC;
  int32_t cvbs_sync_tip_10_ = 16;
  int32_t cvbs_blanking_10_ = 240;
  int32_t cvbs_white_10_ = 800;
  int32_t cvbs_black_10_ = 282;
  int32_t cvbs_peak_10_ = 1019;
  int32_t blanking_16_ = 15360;
  int32_t white_16_ = 51200;
  int32_t black_16_ = 18048;
  bool have_levels_ = false;

  static constexpr float kBrightnessBias = 64.0f;

  static constexpr int kLeftMargin = 55;
  static constexpr int kRightMargin = 10;
  static constexpr int kTopMargin = 15;
  static constexpr int kBottomMargin = 50;
};

#endif  // WAVEFORMMONITORWIDGET_H
