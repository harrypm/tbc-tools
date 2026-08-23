/*
 * File:        waveformmonitordialog.cpp
 * Module:      ld-analyse
 * Purpose:     Waveform monitor dialog implementation
 *
 * Ported from decode-orc (orc/gui/waveformmonitordialog.cpp), adapted to be
 * the 16-bit TBC → 10-bit CVBS_U10_4FSC boundary for ld-analyse.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "waveformmonitordialog.h"

#include "tbc_cvbs_conversion.h"
#include "waveformmonitorwidget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSettings>
#include <QSlider>
#include <QVBoxLayout>
#include <algorithm>
#include <cstdint>

// Intensity slider: range 1–100 maps to gain 0.1–10.0 (divide by 10).
static constexpr int kSliderMin = 1;
static constexpr int kSliderMax = 100;
static constexpr int kSliderDefault = 30;  // 3.0×
static constexpr double kSliderScale = 10.0;

// Combo box item indices must match the channel selection order.
static constexpr int kChannelIndexYPlusC = 0;
static constexpr int kChannelIndexYOnly = 1;

// Range combo box item indices.
static constexpr int kRangeIndexActiveVideo = 0;
static constexpr int kRangeIndexWholeFrame = 1;

WaveformMonitorDialog::WaveformMonitorDialog(QWidget *parent)
    : QDialog(parent),
      monitor_widget_(nullptr),
      channel_combo_(nullptr),
      range_combo_(nullptr),
      phosphor_check_(nullptr),
      gain_slider_(nullptr),
      gain_value_label_(nullptr) {
  setWindowFlags(Qt::Window);
  setModal(false);
  setAttribute(Qt::WA_DeleteOnClose, false);
  setWindowTitle("Waveform Monitor");

  setupUI();

  QSettings settings;
  const QByteArray geom =
      settings.value("WaveformMonitorDialog/geometry").toByteArray();
  if (!geom.isEmpty()) {
    restoreGeometry(geom);
  } else {
    resize(900, 500);
  }

  const bool phosphor =
      settings.value("WaveformMonitorDialog/phosphorMode", false).toBool();
  phosphor_check_->setChecked(phosphor);
  monitor_widget_->setPhosphorMode(phosphor);
}

WaveformMonitorDialog::~WaveformMonitorDialog() {
  QSettings settings;
  settings.setValue("WaveformMonitorDialog/geometry", saveGeometry());
  if (phosphor_check_) {
    settings.setValue("WaveformMonitorDialog/phosphorMode",
                      phosphor_check_->isChecked());
  }
}

void WaveformMonitorDialog::setupUI() {
  auto *main_layout = new QVBoxLayout(this);
  main_layout->setContentsMargins(4, 4, 4, 4);
  main_layout->setSpacing(4);

  monitor_widget_ = new WaveformMonitorWidget(this);
  main_layout->addWidget(monitor_widget_, 1);

  auto *controls = new QHBoxLayout();

  controls->addWidget(new QLabel("Channel:"));
  channel_combo_ = new QComboBox();
  channel_combo_->addItem("Y+C (Composite)", kChannelIndexYPlusC);
  channel_combo_->addItem("Y (Luma only)", kChannelIndexYOnly);
  channel_combo_->setCurrentIndex(kChannelIndexYOnly);
  channel_combo_->setToolTip(
      "Y+C: full composite signal (composite source) or summed Y+C (Y/C "
      "source).\n"
      "Y: luma only — separate Y channel when available, otherwise the "
      "composite signal low-pass filtered to remove the colour subcarrier.");
  controls->addWidget(channel_combo_);
  controls->addSpacing(12);

  controls->addWidget(new QLabel("Range:"));
  range_combo_ = new QComboBox();
  range_combo_->addItem("Active video", kRangeIndexActiveVideo);
  range_combo_->addItem("Whole frame", kRangeIndexWholeFrame);
  range_combo_->setCurrentIndex(kRangeIndexActiveVideo);
  range_combo_->setToolTip(
      "Active video: accumulate only the visible portion of each line "
      "(blanking excluded).\n"
      "Whole frame: accumulate all samples including sync and blanking.");
  controls->addWidget(range_combo_);
  controls->addSpacing(12);

  phosphor_check_ = new QCheckBox("Phosphor");
  phosphor_check_->setToolTip(
      "Display green trace on black background, emulating a classic "
      "analogue oscilloscope phosphor screen.");
  controls->addWidget(phosphor_check_);
  controls->addSpacing(12);

  controls->addWidget(new QLabel("Intensity:"));
  gain_slider_ = new QSlider(Qt::Horizontal);
  gain_slider_->setRange(kSliderMin, kSliderMax);
  gain_slider_->setValue(kSliderDefault);
  gain_slider_->setTickPosition(QSlider::NoTicks);
  gain_slider_->setToolTip(
      "Intensity gain — higher values brighten sparse signals and reach "
      "saturation sooner; lower values reduce saturation in uniform scenes.");
  controls->addWidget(gain_slider_, 1);
  gain_value_label_ = new QLabel();
  gain_value_label_->setMinimumWidth(40);
  gain_value_label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  controls->addWidget(gain_value_label_);

  main_layout->addLayout(controls);

  const double initial_gain = kSliderDefault / kSliderScale;
  monitor_widget_->setGain(initial_gain);
  gain_value_label_->setText(QString::number(initial_gain, 'f', 1) +
                             QString::fromUtf8("×"));

  connect(gain_slider_, &QSlider::valueChanged, this, [this](int v) {
    const double gain = v / kSliderScale;
    monitor_widget_->setGain(gain);
    gain_value_label_->setText(QString::number(gain, 'f', 1) +
                               QString::fromUtf8("×"));
  });

  connect(channel_combo_,
          QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          [this](int) { updateWidgetForCurrentChannel(); });

  connect(range_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, [this](int) { updateWidgetForCurrentChannel(); });

  connect(phosphor_check_, &QCheckBox::toggled, this,
          [this](bool checked) { monitor_widget_->setPhosphorMode(checked); });
}

// ---------------------------------------------------------------------------
// Data ingestion
// ---------------------------------------------------------------------------

std::vector<int16_t> WaveformMonitorDialog::convertTbcToCvbs(
    const std::vector<uint16_t> &tbc, int32_t blanking16, int32_t white16,
    ::VideoSystem system) {
  std::vector<int16_t> out;
  out.reserve(tbc.size());
  for (const uint16_t s : tbc) {
    out.push_back(tbc::cvbs::tbc_to_cvbs(s, blanking16, white16, system));
  }
  return out;
}

void WaveformMonitorDialog::setData(
    std::vector<uint16_t> composite_samples, std::vector<uint16_t> y_samples,
    std::vector<uint16_t> c_samples, int first_field_height,
    int second_field_height,
    const TbcMetaData::VideoParameters &video_params) {
  video_params_ = video_params;

  // Resolve the 16-bit blanking once (handles NTSC 7.5 IRE setup derivation)
  // and convert every channel to the 10-bit CVBS domain up front, so channel
  // switching later needs no re-conversion.
  const int32_t blanking16 = tbc::cvbs::resolve_blanking_16b(
      video_params.system, video_params.black16bIre, video_params.white16bIre,
      video_params.blanking16bIre);
  const int32_t white16 = (video_params.white16bIre > blanking16)
                              ? video_params.white16bIre
                              : blanking16 + 1;

  composite_cvbs_ =
      composite_samples.empty()
          ? std::vector<int16_t>{}
          : convertTbcToCvbs(composite_samples, blanking16, white16,
                             video_params.system);
  y_cvbs_ = y_samples.empty()
                ? std::vector<int16_t>{}
                : convertTbcToCvbs(y_samples, blanking16, white16,
                                   video_params.system);
  c_cvbs_ = c_samples.empty()
                ? std::vector<int16_t>{}
                : convertTbcToCvbs(c_samples, blanking16, white16,
                                   video_params.system);
  first_field_height_ = first_field_height;
  second_field_height_ = second_field_height;

  updateWidgetForCurrentChannel();

  if (!isVisible() && parentWidget() && parentWidget()->isVisible()) {
    show();
    raise();
    activateWindow();
  }
}

// ---------------------------------------------------------------------------
// Channel switching
// ---------------------------------------------------------------------------

void WaveformMonitorDialog::updateWidgetForCurrentChannel() {
  const int ch_idx =
      channel_combo_ ? channel_combo_->currentIndex() : kChannelIndexYOnly;
  const bool y_only = (ch_idx == kChannelIndexYOnly);

  const bool clip_to_active =
      !range_combo_ || range_combo_->currentIndex() == kRangeIndexActiveVideo;

  // Build a working copy of VideoParameters for this render pass.  When
  // "whole frame" is selected, clear the active-video sample bounds so the
  // widget accumulates all samples across each full line including sync and
  // blanking.  Signal levels and system are preserved for Y-axis mV mapping
  // and level markers.
  TbcMetaData::VideoParameters display_params = video_params_;
  if (!clip_to_active) {
    display_params.activeVideoStart = -1;
    display_params.activeVideoEnd = -1;
  }

  // Per-field first active picture line (0-based).  ld-decode stores
  // firstActiveFrameLine as a frame line index; interlacing halves it to the
  // per-field line index.  Falls back to 0 (no VBI strip) when unset.
  int first_active_line = 0;
  if (clip_to_active && video_params_.firstActiveFrameLine > 0) {
    first_active_line = video_params_.firstActiveFrameLine / 2;
  }

  // Select and prepare the sample data according to the channel mode.
  std::vector<int16_t> raw_samples;

  if (!y_only) {
    if (!composite_cvbs_.empty()) {
      raw_samples = composite_cvbs_;
    } else if (!y_cvbs_.empty()) {
      if (!c_cvbs_.empty() && c_cvbs_.size() == y_cvbs_.size()) {
        // Y/C source — reconstruct composite by summing luma and chroma in
        // the 10-bit domain.
        raw_samples.resize(y_cvbs_.size());
        for (size_t i = 0; i < y_cvbs_.size(); ++i) {
          raw_samples[i] = static_cast<int16_t>(
              std::clamp(static_cast<int32_t>(y_cvbs_[i]) + c_cvbs_[i],
                         static_cast<int32_t>(INT16_MIN),
                         static_cast<int32_t>(INT16_MAX)));
        }
      } else {
        raw_samples = y_cvbs_;
      }
    }
  } else {
    if (!y_cvbs_.empty()) {
      raw_samples = y_cvbs_;
    } else if (!composite_cvbs_.empty()) {
      raw_samples = extractYFromComposite(composite_cvbs_);
    }
  }

  if (raw_samples.empty()) return;

  int f1 = first_field_height_;
  int f2 = second_field_height_;
  const std::vector<int16_t> &display_samples =
      (first_active_line > 0)
          ? sliceToActiveLines(raw_samples, f1, f2, first_active_line)
          : raw_samples;

  monitor_widget_->setYOnlyMode(y_only);
  monitor_widget_->setData(display_samples, f1, f2, display_params);
}

// ---------------------------------------------------------------------------
// Vertical line slicing — strip VBI lines from each field
// ---------------------------------------------------------------------------

std::vector<int16_t> WaveformMonitorDialog::sliceToActiveLines(
    const std::vector<int16_t> &samples, int &field1_height,
    int &field2_height, int first_active_line) {
  if (first_active_line <= 0) return samples;

  const int total_lines = field1_height + field2_height;
  if (total_lines <= 0 || samples.empty()) return samples;

  const int spl = static_cast<int>(samples.size()) / total_lines;
  if (spl <= 0) return samples;

  const int skip1 = std::min(first_active_line, field1_height);
  const int new_f1 = field1_height - skip1;
  const int skip2 =
      (field2_height > 0) ? std::min(first_active_line, field2_height) : 0;
  const int new_f2 = (field2_height > 0) ? field2_height - skip2 : 0;

  if (new_f1 + new_f2 <= 0) return {};

  std::vector<int16_t> result;
  result.reserve(static_cast<size_t>(new_f1 + new_f2) *
                 static_cast<size_t>(spl));

  const auto *base = samples.data();
  result.insert(result.end(), base + static_cast<ptrdiff_t>(skip1) * spl,
                base + static_cast<ptrdiff_t>(field1_height) * spl);

  if (field2_height > 0) {
    result.insert(
        result.end(),
        base + static_cast<ptrdiff_t>(field1_height + skip2) * spl,
        base + static_cast<ptrdiff_t>(field1_height + field2_height) * spl);
  }

  field1_height = new_f1;
  field2_height = new_f2;
  return result;
}

// ---------------------------------------------------------------------------
// Chroma extraction — 4-tap FIR (notch at fs/4)
// ---------------------------------------------------------------------------

// For 4FSC-sampled data the colour subcarrier sits exactly at fs/4.  A 4-point
// moving average [0.25, 0.25, 0.25, 0.25] has a null at fs/4 and at all
// harmonics thereof, giving clean luma separation without a separate decode
// pass.  Operates on the 10-bit CVBS-domain composite samples.
std::vector<int16_t> WaveformMonitorDialog::extractYFromComposite(
    const std::vector<int16_t> &composite) {
  const int n = static_cast<int>(composite.size());
  std::vector<int16_t> result(static_cast<size_t>(n), 0);
  for (int i = 2; i < n - 1; ++i) {
    const int32_t sum = static_cast<int32_t>(composite[i - 2]) +
                        composite[i - 1] + composite[i] + composite[i + 1];
    result[static_cast<size_t>(i)] = static_cast<int16_t>(sum / 4);
  }
  return result;
}

void WaveformMonitorDialog::setAmplitudeUnit(
    tbc::amp::AmplitudeDisplayUnit unit) {
  if (monitor_widget_) {
    monitor_widget_->setAmplitudeUnit(unit);
  }
}
