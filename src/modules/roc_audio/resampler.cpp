/*
 * Copyright (c) 2015 Roc authors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "roc_audio/resampler.h"
#include "roc_core/log.h"
#include "roc_core/macros.h"
#include "roc_core/panic.h"
#include "roc_core/stddefs.h"

namespace roc {
namespace audio {

namespace {

//! Fixed point type Q8.24 for realizing computations of curr_frame_ in fixed point
//! arithmetic. Sometimes this computations requires ceil(...) and floor(...) and
//! it is very CPU-time hungry in floating point variant on x86.
typedef uint32_t fixedpoint_t;

// Signed version of fixedpoint_t.
typedef int32_t signed_fixedpoint_t;

const uint32_t INTEGER_PART_MASK = 0xFFF00000;
const uint32_t FRACT_PART_MASK = 0x000FFFFF;
const uint32_t FRACT_BIT_COUNT = 20;

// One in terms of Q8.24.
const fixedpoint_t qt_one = 1 << FRACT_BIT_COUNT;

// Convert float to fixed-point.
inline fixedpoint_t float_to_fixedpoint(const float t) {
    return (fixedpoint_t)(t * (float)qt_one);
}

inline size_t fixedpoint_to_size(const fixedpoint_t t) {
    return t >> FRACT_BIT_COUNT;
}

// Rounds x (Q8.24) upward.
inline fixedpoint_t qceil(const fixedpoint_t x) {
    if ((x & FRACT_PART_MASK) == 0) {
        return x & INTEGER_PART_MASK;
    } else {
        return (x & INTEGER_PART_MASK) + qt_one;
    }
}

// Rounds x (Q8.24) downward.
inline fixedpoint_t qfloor(const fixedpoint_t x) {
    // Just remove fractional part.
    return x & INTEGER_PART_MASK;
}

// Returns fractional part of x in f32.
inline float fractional(const fixedpoint_t x) {
    return (float)(x & FRACT_PART_MASK) * ((float)1. / (float)qt_one);
}

// Returns log2(n) assuming that n is a power of two.
inline size_t calc_bits(size_t n) {
    size_t c = 0;
    while ((n & 1) == 0 && c != sizeof(n) * 8) {
        n >>= 1;
        c++;
    }
    return c;
}

} // namespace

Resampler::Resampler(core::IAllocator& allocator,
                     const ResamplerConfig& config,
                     packet::channel_mask_t channels)
    : channel_mask_(channels)
    , channels_num_(packet::num_channels(channel_mask_))
    , prev_frame_(NULL)
    , curr_frame_(NULL)
    , next_frame_(NULL)
    , out_frame_i_(0)
    , scaling_(1.0)
    , window_size_(config.frame_size)
    , channel_len_(window_size_ / channels_num_)
    , window_len_(config.window_size)
    , qt_half_sinc_window_len_(float_to_fixedpoint(window_len_))
    , window_interp_(config.window_interp)
    , window_interp_bits_(calc_bits(config.window_interp))
    , sinc_table_(allocator)
    , sinc_table_ptr_(NULL)
    , qt_half_window_len_(float_to_fixedpoint((float)window_len_ / scaling_))
    , qt_epsilon_(float_to_fixedpoint(5e-8f))
    , default_sample_(float_to_fixedpoint(0))
    , qt_window_size_(fixedpoint_t(channel_len_ << FRACT_BIT_COUNT))
    , qt_sample_(default_sample_)
    , qt_dt_(0)
    , cutoff_freq_(0.9f)
    , valid_(false) {
    if (!check_config_()) {
        return;
    }
    if (!fill_sinc_()) {
        return;
    }
    valid_ = true;
}

bool Resampler::valid() const {
    return valid_;
}

bool Resampler::set_scaling(float new_scaling) {
    // Window's size changes according to scaling. If new window size
    // doesn't fit to the frames size -- deny changes.
    if (window_len_ * new_scaling >= channel_len_) {
        roc_log(LogError,
                "resampler: scaling does not fit frame size:"
                " window=%lu frame=%lu scaling=%.5f",
                (unsigned long)window_len_, (unsigned long)window_size_,
                (double)new_scaling);
        return false;
    }

    // In case of upscaling one should properly shift the edge frequency
    // of the digital filter. In both cases it's sensible to decrease the
    // edge frequency to leave some.
    if (new_scaling > 1.0f) {
        const fixedpoint_t new_qt_half_window_len =
            float_to_fixedpoint((float)window_len_ / cutoff_freq_ * new_scaling);

        // Check that resample_() will not go out of bounds.
        // Otherwise -- deny changes.
        const bool out_of_bounds =
            fixedpoint_to_size(qceil(qt_window_size_ - new_qt_half_window_len))
                > channel_len_
            || fixedpoint_to_size(qfloor(new_qt_half_window_len)) + 1 > channel_len_;

        if (out_of_bounds) {
            roc_log(LogError,
                    "resampler: scaling does not fit window size:"
                    " window=%lu frame=%lu scaling=%.5f",
                    (unsigned long)window_len_, (unsigned long)window_size_,
                    (double)new_scaling);
            return false;
        }

        qt_sinc_step_ = float_to_fixedpoint(cutoff_freq_ / new_scaling);
        qt_half_window_len_ = new_qt_half_window_len;
    } else {
        qt_sinc_step_ = float_to_fixedpoint(cutoff_freq_);
        qt_half_window_len_ = float_to_fixedpoint((float)window_len_ / cutoff_freq_);
    }

    scaling_ = new_scaling;

    return true;
}

bool Resampler::resample_buff(Frame& out) {
    roc_panic_if(!prev_frame_);
    roc_panic_if(!curr_frame_);
    roc_panic_if(!next_frame_);

    for (; out_frame_i_ < out.size(); out_frame_i_ += channels_num_) {
        if (qt_sample_ >= qt_window_size_) {
            return false;
        }

        if ((qt_sample_ & FRACT_PART_MASK) < qt_epsilon_) {
            qt_sample_ &= INTEGER_PART_MASK;
        } else if ((qt_one - (qt_sample_ & FRACT_PART_MASK)) < qt_epsilon_) {
            qt_sample_ &= INTEGER_PART_MASK;
            qt_sample_ += qt_one;
        }

        for (size_t channel = 0; channel < channels_num_; ++channel) {
            out.data()[out_frame_i_ + channel] = resample_(channel);
        }
        qt_sample_ += qt_dt_;
    }
    out_frame_i_ = 0;
    return true;
}

bool Resampler::check_config_() const {
    if (channels_num_ < 1) {
        roc_log(LogError, "resampler: invalid num_channels: num_channels=%lu",
                (unsigned long)channels_num_);
        return false;
    }

    if (channel_len_ > ((fixedpoint_t)-1 >> FRACT_BIT_COUNT)) {
        roc_log(LogError,
                "resampler: frame_size is too much: frame_size=%lu num_channels=%lu",
                (unsigned long)window_size_, (unsigned long)channels_num_);
        return false;
    }

    if (window_size_ != channel_len_ * channels_num_) {
        roc_log(LogError, "resampler: frame_size is not multiple of num_channels:"
                          " frame_size=%lu num_channels=%lu",
                (unsigned long)window_size_, (unsigned long)channels_num_);
        return false;
    }

    if (size_t(1 << window_interp_bits_) != window_interp_) {
        roc_log(LogError,
                "resampler: window_interp is not power of two: window_interp=%lu",
                (unsigned long)window_interp_);
        return false;
    }

    return true;
}

void Resampler::renew_buffers(core::Slice<sample_t>& prev,
                              core::Slice<sample_t>& cur,
                              core::Slice<sample_t>& next) {
    roc_panic_if(window_len_ * scaling_ >= channel_len_);
    roc_panic_if(prev.size() != window_size_);
    roc_panic_if(cur.size() != window_size_);
    roc_panic_if(next.size() != window_size_);

    if (qt_sample_ >= qt_window_size_) {
        qt_sample_ -= qt_window_size_;
    }

    // scaling_ may change every frame so it have to be smooth.
    qt_dt_ = float_to_fixedpoint(scaling_);

    prev_frame_ = prev.data();
    curr_frame_ = cur.data();
    next_frame_ = next.data();
}

bool Resampler::fill_sinc_() {
    if (!sinc_table_.resize(window_len_ * window_interp_ + 2)) {
        roc_log(LogError, "resampler: can't allocate sinc table");
        return false;
    }

    const double sinc_step = 1.0 / (double)window_interp_;
    double sinc_t = sinc_step;

    sinc_table_[0] = 1.0f;
    for (size_t i = 1; i < sinc_table_.size(); ++i) {
        // const float window = 1;
        const double window = 0.54
            - 0.46 * cos(2 * M_PI
                         * ((double)(i - 1) / 2.0 / (double)sinc_table_.size() + 0.5));
        sinc_table_[i] = (float)(sin(M_PI * sinc_t) / M_PI / sinc_t * window);
        sinc_t += sinc_step;
    }
    sinc_table_[sinc_table_.size() - 2] = 0;
    sinc_table_[sinc_table_.size() - 1] = 0;

    sinc_table_ptr_ = &sinc_table_[0];

    return true;
}

// Computes sinc value in x position using linear interpolation between
// table values from sinc_table.h
//
// During going through input signal window only integer part of argument changes,
// that's why there are two arguments in this function: integer part and fractional
// part of time coordinate.
sample_t Resampler::sinc_(const fixedpoint_t x, const float fract_x) {
    const size_t index = (x >> (FRACT_BIT_COUNT - window_interp_bits_));

    const sample_t hl = sinc_table_ptr_[index];     // table index smaller than x
    const sample_t hh = sinc_table_ptr_[index + 1]; // table index next to x

    const sample_t result = hl + fract_x * (hh - hl);

    return scaling_ > 1.0f ? result / scaling_ : result;
}

sample_t Resampler::resample_(const size_t channel_offset) {
    const fixedpoint_t qt_sample_minus_half = (qt_sample_ - qt_half_window_len_);
    const fixedpoint_t qt_sample_plus_half = (qt_sample_ + qt_half_window_len_);

    // Previous frame bounds.
    size_t ind_prev_begin;
    const size_t ind_prev_end = channelize_index(channel_len_, channel_offset);

    // Current frame bounds.
    size_t ind_cur_begin;
    size_t ind_cur_end;

    // Next frame bounds.
    const size_t ind_next_begin = channelize_index(0, channel_offset);
    size_t ind_next_end;

    if ((signed_fixedpoint_t)qt_sample_minus_half >= 0) {
        ind_prev_begin = channel_len_;
        ind_cur_begin = fixedpoint_to_size(qceil(qt_sample_minus_half));
    } else {
        ind_prev_begin = fixedpoint_to_size(qceil(qt_sample_minus_half + qt_window_size_));
        ind_cur_begin = 0;
    }

    ind_prev_begin = channelize_index(ind_prev_begin, channel_offset);
    ind_cur_begin = channelize_index(ind_cur_begin, channel_offset);

    if (qt_sample_plus_half > qt_window_size_) {
        ind_cur_end = channel_len_ - 1;
        ind_next_end =
            fixedpoint_to_size(qfloor(qt_sample_plus_half - qt_window_size_)) + 1;
    } else {
        ind_cur_end = fixedpoint_to_size(qfloor(qt_sample_plus_half));
        ind_next_end = 0;
    }

    ind_cur_end = channelize_index(ind_cur_end, channel_offset);
    ind_next_end = channelize_index(ind_next_end, channel_offset);

    const long_fixedpoint_t qt_pos = qt_window_size_ + qt_sample_
        - qceil(qt_window_size_ + qt_sample_ - qt_half_window_len_);

    // Sinc table position for the previous frame end.
    const fixedpoint_t qt_sinc_prev_end =
        (fixedpoint_t)((qt_pos * (long_fixedpoint_t)qt_sinc_step_) >> FRACT_BIT_COUNT);

    // Sinc table position for the current frame center.
    const fixedpoint_t qt_sinc_cur_center = qt_sinc_prev_end % qt_sinc_step_;

    // Sinc table position for the current frame end.
    const fixedpoint_t qt_sinc_cur_end = qt_sinc_prev_end
        - fixedpoint_t(ind_prev_end - ind_prev_begin) / channels_num_ * qt_sinc_step_;

    // Current frame center.
    const size_t ind_cur_center = ind_cur_begin
        + (qt_sinc_cur_end - qt_sinc_cur_center) * channels_num_ / qt_sinc_step_;

    // Sinc table position.
    fixedpoint_t qt_sinc_pos;

    // Fractional part of time position at the begining.
    float f_sinc_pos_fract;

    // Frame index.
    size_t i;

    // Output sample.
    sample_t accumulator = 0;

    qt_sinc_pos = qt_sinc_cur_center;
    f_sinc_pos_fract = fractional(qt_sinc_prev_end << window_interp_bits_);

    // Run through the left half of the current frame.
    for (i = ind_cur_center; i != ind_cur_begin; i -= channels_num_) {
        accumulator += curr_frame_[i] * sinc_(qt_sinc_pos, f_sinc_pos_fract);
        qt_sinc_pos += qt_sinc_step_;
    }
    accumulator += curr_frame_[i] * sinc_(qt_sinc_pos, f_sinc_pos_fract);

    // Run through the previous frame.
    for (i = ind_prev_end; i != ind_prev_begin; i -= channels_num_) {
        accumulator += prev_frame_[i] * sinc_(qt_sinc_pos, f_sinc_pos_fract);
        qt_sinc_pos += qt_sinc_step_;
    }
    accumulator += prev_frame_[i] * sinc_(qt_sinc_pos, f_sinc_pos_fract);

    qt_sinc_pos = qt_sinc_step_ - qt_sinc_cur_center;
    f_sinc_pos_fract = fractional(qt_sinc_pos << window_interp_bits_);

    // Run through the right half of the current frame.
    for (i = ind_cur_center + channels_num_; i <= ind_cur_end; i += channels_num_) {
        accumulator += curr_frame_[i] * sinc_(qt_sinc_pos, f_sinc_pos_fract);
        qt_sinc_pos += qt_sinc_step_;
    }

    // Run through the next frame.
    for (i = ind_next_begin; i < ind_next_end; i += channels_num_) {
        accumulator += next_frame_[i] * sinc_(qt_sinc_pos, f_sinc_pos_fract);
        qt_sinc_pos += qt_sinc_step_;
    }

    return accumulator;
}

} // namespace audio
} // namespace roc
