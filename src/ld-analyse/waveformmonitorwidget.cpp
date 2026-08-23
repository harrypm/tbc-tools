/*
 * File:        waveformmonitorwidget.cpp
 * Module:      ld-analyse
 * Purpose:     Waveform monitor raster widget implementation
 *
 * Ported from decode-orc (orc/gui/waveformmonitorwidget.cpp), adapted to
 * ld-analyse's 10-bit-CVBS-domain input + TbcMetaData::VideoParameters.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "waveformmonitorwidget.h"

#include "plotwidget.h"            // PlotWidget::isDarkTheme()
#include "tbc_cvbs_conversion.h"   // tbc::cvbs::tbc_to_cvbs, resolve_blanking_16b
#include "theme_color_tokens.h"

#include <QFontMetrics>
#include <QPainter>
#include <QPen>
#include <QRect>
#include <QResizeEvent>
#include <QSizePolicy>
#include <algorithm>
#include <cmath>

WaveformMonitorWidget::WaveformMonitorWidget(QWidget *parent)
    : QWidget(parent) {
  setMinimumSize(400, 300);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

QRect WaveformMonitorWidget::plotArea() const {
  return QRect(kLeftMargin, kTopMargin, width() - kLeftMargin - kRightMargin,
               height() - kTopMargin - kBottomMargin);
}

// ---------------------------------------------------------------------------
// Data ingestion
// ---------------------------------------------------------------------------

void WaveformMonitorWidget::setData(
    const std::vector<int16_t> &composite_samples, int first_field_height,
    int second_field_height,
    const TbcMetaData::VideoParameters &video_params) {
  system_ = video_params.system;

  // Resolve the 16-bit blanking (handles NTSC 7.5 IRE setup derivation) and
  // derive the 10-bit CVBS reference levels.  Samples already arrive in the
  // 10-bit domain, so accumulation uses the normative 10-bit blanking/white.
  blanking_16_ = tbc::cvbs::resolve_blanking_16b(
      video_params.system, video_params.black16bIre, video_params.white16bIre,
      video_params.blanking16bIre);
  white_16_ = (video_params.white16bIre > blanking_16_)
                  ? video_params.white16bIre
                  : static_cast<int32_t>(tbc::cvbs::kTbcNtscWhite);
  black_16_ = (video_params.black16bIre >= 0)
                  ? video_params.black16bIre
                  : (video_params.system == PAL
                         ? static_cast<int32_t>(tbc::cvbs::kTbcPalBlanking)
                         : static_cast<int32_t>(tbc::cvbs::kTbcNtscBlack));

  cvbs_blanking_10_ = tbc::cvbs::cvbs_blanking(system_);
  cvbs_white_10_ = tbc::cvbs::cvbs_white(system_);
  cvbs_sync_tip_10_ = tbc::cvbs::cvbs_sync_tip(system_);
  cvbs_peak_10_ = tbc::cvbs::cvbs_peak(system_);
  cvbs_black_10_ = tbc::cvbs::tbc_to_cvbs(
      static_cast<uint16_t>(std::clamp(black_16_, 0, 65535)), blanking_16_,
      white_16_, system_);
  have_levels_ = (white_16_ > blanking_16_);

  // Derive Y-axis range (always in mV — display unit only affects rendering).
  y_min_mv_ = -350.0;
  y_max_mv_ = 950.0;
  if (y_only_mode_) {
    const double amv = tbc::amp::active_video_mv(system_);
    y_min_mv_ = -100.0;
    y_max_mv_ = amv + 100.0;
  } else if (have_levels_) {
    // Match decode-orc's waveform framing: derive the display envelope from
    // normative sync-tip and peak limits with a small headroom margin.
    const double sync_tip_mv =
        tbc::amp::samples10_to_mv(cvbs_sync_tip_10_, cvbs_blanking_10_,
                                  cvbs_white_10_, system_);
    const double peak_mv =
        tbc::amp::samples10_to_mv(cvbs_peak_10_, cvbs_blanking_10_,
                                  cvbs_white_10_, system_);
    const double span = peak_mv - sync_tip_mv;
    if (span > 0.0) {
      y_min_mv_ = sync_tip_mv - span * 0.05;
      y_max_mv_ = peak_mv + span * 0.05;
    }
  }
  y_bins_ = static_cast<int>((y_max_mv_ - y_min_mv_) / kBinWidthMv) + 1;

  const int total_lines =
      first_field_height + (second_field_height > 0 ? second_field_height : 0);
  if (total_lines <= 0 || composite_samples.empty()) {
    count_buffer_.clear();
    x_samples_ = 0;
    active_video_start_ = 0;
    line_count_ = 0;
    image_dirty_ = true;
    update();
    return;
  }

  const int samples_per_line =
      static_cast<int>(composite_samples.size()) / total_lines;
  if (samples_per_line <= 0) {
    count_buffer_.clear();
    x_samples_ = 0;
    active_video_start_ = 0;
    line_count_ = 0;
    image_dirty_ = true;
    update();
    return;
  }

  int active_start = 0;
  int active_end = samples_per_line - 1;  // inclusive
  if (video_params.activeVideoStart >= 0 &&
      video_params.activeVideoEnd > video_params.activeVideoStart) {
    active_start = std::min(video_params.activeVideoStart, samples_per_line - 1);
    active_end = std::min(video_params.activeVideoEnd - 1, samples_per_line - 1);
  }

  active_video_start_ = active_start;

  // µs per sample for the X-axis time labels.
  if (video_params.sampleRate > 0.0) {
    us_per_sample_ = 1000000.0 / video_params.sampleRate;
  } else {
    us_per_sample_ = 1000000.0 / tbc::cvbs::sample_rate_from_system(system_);
  }

  accumulate(composite_samples, total_lines, active_start, active_end,
             cvbs_blanking_10_, cvbs_white_10_, system_);
  line_count_ = total_lines;
  image_dirty_ = true;
  update();
}

void WaveformMonitorWidget::accumulate(const std::vector<int16_t> &samples,
                                       int total_lines, int active_start,
                                       int active_end, int32_t blanking_level,
                                       int32_t white_level,
                                       ::VideoSystem sys) {
  const int active_width = active_end - active_start + 1;
  if (active_width <= 0 || total_lines <= 0) return;

  const int samples_per_line = static_cast<int>(samples.size()) / total_lines;
  if (samples_per_line <= 0) return;

  x_samples_ = active_width;
  count_buffer_.assign(static_cast<size_t>(x_samples_),
                       std::vector<uint32_t>(static_cast<size_t>(y_bins_), 0));

  const bool have_levels = (white_level > blanking_level);

  for (int line = 0; line < total_lines; ++line) {
    const int line_start = line * samples_per_line;
    if (line_start + active_end >= static_cast<int>(samples.size())) break;

    for (int x = 0; x < active_width; ++x) {
      const int16_t raw =
          samples[static_cast<size_t>(line_start) +
                  static_cast<size_t>(active_start) + static_cast<size_t>(x)];

      const double mv = have_levels
                            ? tbc::amp::samples10_to_mv(raw, blanking_level,
                                                        white_level, sys)
                            : static_cast<double>(raw);

      const int y_bin = static_cast<int>((mv - y_min_mv_) / kBinWidthMv);
      if (y_bin >= 0 && y_bin < y_bins_) {
        count_buffer_[static_cast<size_t>(x)][static_cast<size_t>(y_bin)]++;
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Gain and display mode
// ---------------------------------------------------------------------------

void WaveformMonitorWidget::setGain(double gain) {
  gain_ = std::clamp(gain, 0.1, 10.0);
  image_dirty_ = true;
  update();
}

void WaveformMonitorWidget::setPhosphorMode(bool enabled) {
  if (phosphor_mode_ == enabled) return;
  phosphor_mode_ = enabled;
  image_dirty_ = true;
  update();
}

void WaveformMonitorWidget::setYOnlyMode(bool y_only) {
  if (y_only_mode_ == y_only) return;
  y_only_mode_ = y_only;
  image_dirty_ = true;
  update();
}

void WaveformMonitorWidget::setAmplitudeUnit(
    tbc::amp::AmplitudeDisplayUnit unit) {
  if (amplitude_unit_ == unit) return;
  amplitude_unit_ = unit;
  image_dirty_ = true;
  update();
}

// ---------------------------------------------------------------------------
// Color helpers
// ---------------------------------------------------------------------------

QColor WaveformMonitorWidget::displayBackground() const {
  if (phosphor_mode_) return QColor(0, 0, 0);
  return palette().color(QPalette::Base);
}

QColor WaveformMonitorWidget::displayTrace() const {
  if (phosphor_mode_) return QColor(0, 230, 0);
  return theme_tokens::plotColor(theme_tokens::PlotColorToken::CompositePrimary,
                                 PlotWidget::isDarkTheme());
}

QColor WaveformMonitorWidget::displayAxis() const {
  if (phosphor_mode_) return Qt::white;
  return palette().color(QPalette::WindowText);
}

QColor WaveformMonitorWidget::displayGrid() const {
  if (phosphor_mode_) return QColor(55, 55, 55);
  return theme_tokens::gridLine(palette());
}

// ---------------------------------------------------------------------------
// Qt events
// ---------------------------------------------------------------------------

void WaveformMonitorWidget::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
  image_dirty_ = true;
}

void WaveformMonitorWidget::paintEvent(QPaintEvent *) {
  QPainter painter(this);

  painter.fillRect(rect(), displayBackground());

  const QRect pa = plotArea();
  if (pa.isEmpty()) return;

  if (count_buffer_.empty() || x_samples_ == 0 || y_bins_ == 0) {
    painter.setPen(displayAxis());
    painter.drawText(pa, Qt::AlignCenter, "No data");
    drawYAxis(painter, pa);
    drawXAxis(painter, pa);
    return;
  }

  if (image_dirty_ || cached_image_.size() != pa.size()) {
    rebuildImage(pa);
    image_dirty_ = false;
  }

  drawGrid(painter, pa);
  painter.drawImage(pa.topLeft(), cached_image_);

  drawLevelMarkers(painter, pa);
  drawYAxis(painter, pa);
  drawXAxis(painter, pa);
}

// ---------------------------------------------------------------------------
// Image rebuild — area-max accumulation
// ---------------------------------------------------------------------------

void WaveformMonitorWidget::rebuildImage(const QRect &pa) {
  if (pa.isEmpty() || x_samples_ == 0 || y_bins_ == 0) return;

  cached_image_ = QImage(pa.size(), QImage::Format_ARGB32_Premultiplied);
  cached_image_.fill(Qt::transparent);

  const int img_w = pa.width();
  const int img_h = pa.height();
  const size_t w = static_cast<size_t>(img_w);
  const size_t buf_sz = w * static_cast<size_t>(img_h);

  // Area-max mapping with a brightness knee (ITU-R BT.601 broadcast norm),
  // matching decode-orc's WaveformMonitorWidget.  Full saturation is reached
  // after ~(255 - kBrightnessBias) / (5*gain) occurrences.
  const float k = 5.0f * static_cast<float>(gain_);

  std::vector<float> bright(buf_sz, 0.0f);

  for (int px = 0; px < img_w; ++px) {
    const int xi_lo =
        static_cast<int>(static_cast<double>(px) / img_w * x_samples_);
    const int xi_hi = std::min(
        static_cast<int>(static_cast<double>(px + 1) / img_w * x_samples_),
        x_samples_ - 1);

    for (int py = 0; py < img_h; ++py) {
      const int yi_lo = static_cast<int>(static_cast<double>(img_h - py - 1) /
                                         img_h * y_bins_);
      const int yi_hi = std::min(
          static_cast<int>(static_cast<double>(img_h - py) / img_h * y_bins_),
          y_bins_ - 1);

      uint32_t max_count = 0;
      for (int xi = xi_lo; xi <= xi_hi; ++xi) {
        const auto &col = count_buffer_[static_cast<size_t>(xi)];
        for (int yi = yi_lo; yi <= yi_hi; ++yi) {
          max_count = std::max(max_count, col[static_cast<size_t>(yi)]);
        }
      }
      if (max_count == 0) continue;

      const float b = std::min(
          1.0f, (static_cast<float>(max_count) * k + kBrightnessBias) / 255.0f);
      bright[static_cast<size_t>(py) * w + static_cast<size_t>(px)] = b;
    }
  }

  const QColor back_color = displayBackground();
  const QColor plot_color = displayTrace();
  const float br = static_cast<float>(back_color.redF());
  const float bg = static_cast<float>(back_color.greenF());
  const float bb = static_cast<float>(back_color.blueF());
  const float pr = static_cast<float>(plot_color.redF());
  const float pg = static_cast<float>(plot_color.greenF());
  const float pb = static_cast<float>(plot_color.blueF());

  for (int py = 0; py < img_h; ++py) {
    const size_t row = static_cast<size_t>(py) * w;
    for (int px = 0; px < img_w; ++px) {
      const float b = bright[row + static_cast<size_t>(px)];
      if (b <= 0.0f) continue;
      const int cr = static_cast<int>((br + (pr - br) * b) * 255.0f);
      const int cg = static_cast<int>((bg + (pg - bg) * b) * 255.0f);
      const int cb = static_cast<int>((bb + (pb - bb) * b) * 255.0f);
      cached_image_.setPixel(px, py, qRgb(cr, cg, cb));
    }
  }
}

// ---------------------------------------------------------------------------
// Axis and markers
// ---------------------------------------------------------------------------

int WaveformMonitorWidget::mvToPixelY(double mv, const QRect &pa) const {
  const double range = y_max_mv_ - y_min_mv_;
  if (range <= 0.0) return pa.center().y();
  const double t = (mv - y_min_mv_) / range;
  return pa.bottom() - static_cast<int>(t * pa.height());
}

void WaveformMonitorWidget::drawYAxis(QPainter &painter,
                                      const QRect &pa) const {
  const QColor axis_color = displayAxis();
  painter.setPen(axis_color);

  painter.drawLine(pa.left(), pa.top(), pa.left(), pa.bottom());

  const QFontMetrics fm(painter.font());

  const int32_t blanking = cvbs_blanking_10_;
  const int32_t white = cvbs_white_10_;
  const ::VideoSystem sys = system_;

  const double tick_step = tbc::amp::amplitude_major_tick(amplitude_unit_);
  double min_dv, max_dv;
  if (amplitude_unit_ == tbc::amp::AmplitudeDisplayUnit::Millivolts) {
    min_dv = y_min_mv_;
    max_dv = y_max_mv_;
  } else {
    min_dv = tbc::amp::samples10_to_display(
        tbc::amp::mv_to_samples10(y_min_mv_, blanking, white, sys), blanking,
        white, sys, amplitude_unit_);
    max_dv = tbc::amp::samples10_to_display(
        tbc::amp::mv_to_samples10(y_max_mv_, blanking, white, sys), blanking,
        white, sys, amplitude_unit_);
  }

  const double first_tick = tbc::amp::snap_ceil(min_dv, tick_step);
  for (double dv = first_tick; dv <= max_dv + tick_step * 0.01;
       dv += tick_step) {
    double mv;
    if (amplitude_unit_ == tbc::amp::AmplitudeDisplayUnit::Millivolts) {
      mv = dv;
    } else {
      mv = tbc::amp::samples10_to_mv(
          tbc::amp::display_to_samples10(dv, blanking, white, sys,
                                         amplitude_unit_),
          blanking, white, sys);
    }
    const int py = mvToPixelY(mv, pa);
    if (py < pa.top() || py > pa.bottom()) continue;

    painter.drawLine(pa.left() - 4, py, pa.left(), py);

    const QString label = QString::number(static_cast<int>(std::round(dv)));
    const QRect lr(0, py - fm.height() / 2, kLeftMargin - 6, fm.height());
    painter.drawText(lr, Qt::AlignRight | Qt::AlignVCenter, label);
  }

  painter.save();
  painter.setPen(axis_color);
  painter.translate(12, pa.center().y());
  painter.rotate(-90);
  painter.drawText(
      QRect(-50, -10, 100, 20), Qt::AlignCenter,
      QString::fromStdString(tbc::amp::amplitude_axis_title(amplitude_unit_)));
  painter.restore();
}

void WaveformMonitorWidget::drawGrid(QPainter &painter,
                                     const QRect &pa) const {
  painter.setPen(QPen(displayGrid(), 1, Qt::SolidLine));

  const int32_t blanking = cvbs_blanking_10_;
  const int32_t white = cvbs_white_10_;
  const ::VideoSystem sys = system_;

  const double y_tick_step = tbc::amp::amplitude_major_tick(amplitude_unit_);
  double min_dv, max_dv;
  if (amplitude_unit_ == tbc::amp::AmplitudeDisplayUnit::Millivolts) {
    min_dv = y_min_mv_;
    max_dv = y_max_mv_;
  } else {
    min_dv = tbc::amp::samples10_to_display(
        tbc::amp::mv_to_samples10(y_min_mv_, blanking, white, sys), blanking,
        white, sys, amplitude_unit_);
    max_dv = tbc::amp::samples10_to_display(
        tbc::amp::mv_to_samples10(y_max_mv_, blanking, white, sys), blanking,
        white, sys, amplitude_unit_);
  }
  const double first_y = tbc::amp::snap_ceil(min_dv, y_tick_step);
  for (double dv = first_y; dv <= max_dv + y_tick_step * 0.01;
       dv += y_tick_step) {
    double mv;
    if (amplitude_unit_ == tbc::amp::AmplitudeDisplayUnit::Millivolts) {
      mv = dv;
    } else {
      mv = tbc::amp::samples10_to_mv(
          tbc::amp::display_to_samples10(dv, blanking, white, sys,
                                         amplitude_unit_),
          blanking, white, sys);
    }
    const int py = mvToPixelY(mv, pa);
    if (py < pa.top() || py > pa.bottom()) continue;
    painter.drawLine(pa.left(), py, pa.right(), py);
  }

  if (x_samples_ <= 0) return;

  const double start_us = active_video_start_ * us_per_sample_;
  const double end_us = (active_video_start_ + x_samples_) * us_per_sample_;
  const double total_us = end_us - start_us;
  const double raw_step = total_us / 7.0;
  const double mag = std::pow(10.0, std::floor(std::log10(raw_step)));
  double tick_us = mag;
  if (raw_step / mag >= 5.0) {
    tick_us = mag * 5.0;
  } else if (raw_step / mag >= 2.0) {
    tick_us = mag * 2.0;
  }
  const double first_tick = std::ceil(start_us / tick_us) * tick_us;
  for (double t = first_tick; t <= end_us + tick_us * 0.01; t += tick_us) {
    const int x_buf =
        static_cast<int>(std::round(t / us_per_sample_)) - active_video_start_;
    if (x_buf < 0 || x_buf >= x_samples_) continue;
    const int px = pa.left() + x_buf * pa.width() / x_samples_;
    if (px < pa.left() || px > pa.right()) continue;
    painter.drawLine(px, pa.top(), px, pa.bottom());
  }
}

void WaveformMonitorWidget::drawXAxis(QPainter &painter,
                                      const QRect &pa) const {
  const QColor axis_color = displayAxis();
  painter.setPen(axis_color);

  painter.drawLine(pa.left(), pa.bottom(), pa.right(), pa.bottom());

  if (x_samples_ <= 0) return;

  const QFontMetrics fm(painter.font());

  const double start_us = active_video_start_ * us_per_sample_;
  const double end_us = (active_video_start_ + x_samples_) * us_per_sample_;
  const double total_us = end_us - start_us;
  const double raw_step = total_us / 7.0;
  const double mag = std::pow(10.0, std::floor(std::log10(raw_step)));
  double tick_us = mag;
  if (raw_step / mag >= 5.0) {
    tick_us = mag * 5.0;
  } else if (raw_step / mag >= 2.0) {
    tick_us = mag * 2.0;
  }
  const double first_tick = std::ceil(start_us / tick_us) * tick_us;
  for (double t = first_tick; t <= end_us + tick_us * 0.01; t += tick_us) {
    const int x_buf =
        static_cast<int>(std::round(t / us_per_sample_)) - active_video_start_;
    if (x_buf < 0 || x_buf >= x_samples_) continue;
    const int px = pa.left() + x_buf * pa.width() / x_samples_;
    if (px < pa.left() || px > pa.right()) continue;

    painter.drawLine(px, pa.bottom(), px, pa.bottom() + 4);

    const QString label = QString::number(t, 'f', 1);
    const int label_w = fm.horizontalAdvance(label);
    const QRect lr(px - label_w / 2, pa.bottom() + 5, label_w + 2, fm.height());
    painter.drawText(lr, Qt::AlignHCenter | Qt::AlignTop, label);
  }

  const QString title = QString::fromUtf8("Time (µs)");
  const int title_y = pa.bottom() + 5 + fm.height() + 2;
  painter.drawText(QRect(pa.left(), title_y, pa.width(), fm.height()),
                   Qt::AlignHCenter | Qt::AlignTop, title);
}

void WaveformMonitorWidget::drawLevelMarkers(QPainter &painter,
                                             const QRect &pa) const {
  if (!have_levels_) return;

  const ::VideoSystem sys = system_;
  const int32_t blanking = cvbs_blanking_10_;
  const int32_t white = cvbs_white_10_;

  struct LevelSpec {
    int32_t raw10;
    const char *name;
    Qt::PenStyle style;
    qreal alpha;
  };

  const LevelSpec levels[] = {
      {cvbs_sync_tip_10_, "Sync tip", Qt::DashLine, 0.60},
      {blanking, "Blanking", Qt::DashLine, 0.35},
      {(cvbs_black_10_ == blanking ? -1 : cvbs_black_10_), "Black",
       Qt::DashDotLine, 0.50},
      {white, "White", Qt::DashLine, 0.70},
      {cvbs_peak_10_, "Peak", Qt::DashLine, 0.55},
  };

  for (const auto &lv : levels) {
    if (lv.raw10 < 0) continue;

    const double mv =
        tbc::amp::samples10_to_mv(lv.raw10, blanking, white, sys);
    const int py = mvToPixelY(mv, pa);
    if (py < pa.top() || py > pa.bottom()) continue;

    QColor lc;
    if (phosphor_mode_) {
      lc = QColor(255, 255, 255, static_cast<int>(255.0 * lv.alpha));
    } else {
      lc = theme_tokens::neutralLine(palette(), lv.alpha);
    }
    painter.setPen(QPen(lc, 1, lv.style));
    painter.drawLine(pa.left(), py, pa.right(), py);

    const std::string amp = tbc::amp::format_amplitude(lv.raw10, blanking,
                                                       white, sys, amplitude_unit_);
    painter.setPen(lc);
    painter.drawText(
        pa.left() + 5, py - 3,
        QString::fromStdString(std::string(lv.name) + " (" + amp + ")"));
  }
}
