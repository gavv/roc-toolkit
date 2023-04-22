/*
 * Copyright (c) 2023 Roc Streaming authors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

//! @file roc_sndio/target_alsa/roc_sndio/alsa_device.h
//! @brief ALSA device.

#ifndef ROC_SNDIO_ALSA_DEVICE_H_
#define ROC_SNDIO_ALSA_DEVICE_H_

#include <alsa/asoundlib.h>

#include "roc_audio/frame.h"
#include "roc_core/noncopyable.h"
#include "roc_core/rate_limiter.h"
#include "roc_core/stddefs.h"
#include "roc_core/time.h"
#include "roc_packet/units.h"
#include "roc_sndio/config.h"
#include "roc_sndio/device_state.h"
#include "roc_sndio/device_type.h"

namespace roc {
namespace sndio {

//! ALSA device.
//! Base class for ALSA source and sink.
class AlsaDevice : public core::NonCopyable<> {
public:
    //! Open output device.
    bool open(const char* device);

protected:
    //! Initialize.
    AlsaDevice(const Config& config, DeviceType device_type);
    ~AlsaDevice();

    //! Get device state.
    DeviceState state() const;

    //! Pause reading.
    void pause();

    //! Resume paused reading.
    bool resume();

    //! Restart reading from the beginning.
    bool restart();

    //! Get sample specification of the sink.
    audio::SampleSpec sample_spec() const;

    //! Get latency of the sink.
    core::nanoseconds_t latency() const;

    //! Check if the sink has own clock.
    bool has_clock() const;

    //! Process audio frame.
    bool request(audio::Frame& frame);

private:
    const DeviceType device_type_;
    const char* device_;

    Config config_;
    size_t frame_size_;

    snd_pcm_t* pcm_;

    core::RateLimiter rate_limiter_;
};

} // namespace sndio
} // namespace roc

#endif // ROC_SNDIO_ALSA_DEVICE_H_
