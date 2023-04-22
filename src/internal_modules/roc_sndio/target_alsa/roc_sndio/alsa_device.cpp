/*
 * Copyright (c) 2023 Roc Streaming authors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "roc_sndio/alsa_device.h"
#include "roc_core/log.h"
#include "roc_core/macro_helpers.h"
#include "roc_core/panic.h"
#include "roc_core/time.h"
#include "roc_sndio/device_type.h"

namespace roc {
namespace sndio {

namespace {

const core::nanoseconds_t ReportInterval = 10 * core::Second;

struct AlsaQuery {
    snd_pcm_access_t requested_access[SND_PCM_ACCESS_LAST];
    snd_pcm_access_t selected_access;

    snd_pcm_format_t requested_format[SND_PCM_FORMAT_LAST];
    snd_pcm_format_t selected_format;

    unsigned int requested_rate[8];
    unsigned int selected_rate;

    unsigned int requested_channels[8];
    unsigned int selected_channels;

    unsigned int requested_periods[8];
    unsigned int selected_periods;

    unsigned int requested_latency_us;
    unsigned int selected_latency_us;

    snd_pcm_uframes_t selected_buffer_size;
    snd_pcm_uframes_t selected_period_size;

    AlsaQuery() {
        memset(this, 0, sizeof(*this));
    }
};

bool alsa_negotiate_access(DeviceType type,
                           AlsaQuery& query,
                           snd_pcm_hw_params_t* hw_params,
                           snd_pcm_t* pcm) {
    bool has_selected_access = false;
    int err = 0;

    for (unsigned i = 0; i < ROC_ARRAY_SIZE(query.requested_access); i++) {
        if (query.requested_access[i] == SND_PCM_ACCESS_LAST) {
            break;
        }
        if (snd_pcm_hw_params_test_access(pcm, hw_params, query.requested_access[i])
            == 0) {
            if ((err = snd_pcm_hw_params_set_access(pcm, hw_params,
                                                    query.requested_access[i]))
                < 0) {
                roc_log(LogError, "alsa %s: snd_pcm_hw_params_set_access(): %s",
                        device_type_to_str(type), snd_strerror(err));
                return false;
            }
            roc_log(
                LogTrace,
                "alsa %s: snd_pcm_hw_params_set_access() succeeded: index=%u access=%ld",
                device_type_to_str(type), i, (long)query.requested_access[i]);
            has_selected_access = true;
            break;
        }
    }

    if (!has_selected_access) {
        roc_log(LogTrace,
                "alsa %s: snd_pcm_hw_params_set_access() failed for all requested values",
                device_type_to_str(type));
    }

    if ((err = snd_pcm_hw_params_get_access(hw_params, &query.selected_access)) < 0) {
        roc_log(LogError, "alsa %s: snd_pcm_hw_params_get_access(): %s",
                device_type_to_str(type), snd_strerror(err));
        return false;
    }

    return true;
}

bool alsa_negotiate_format(DeviceType type,
                           AlsaQuery& query,
                           snd_pcm_hw_params_t* hw_params,
                           snd_pcm_t* pcm) {
    bool has_selected_format = false;
    int err = 0;

    for (unsigned i = 0; i < ROC_ARRAY_SIZE(query.requested_format); i++) {
        if (query.requested_format[i] == SND_PCM_FORMAT_LAST) {
            break;
        }
        if (snd_pcm_hw_params_test_format(pcm, hw_params, query.requested_format[i])
            == 0) {
            if ((err = snd_pcm_hw_params_set_format(pcm, hw_params,
                                                    query.requested_format[i]))
                < 0) {
                roc_log(LogError, "alsa %s: snd_pcm_hw_params_set_format(): %s",
                        device_type_to_str(type), snd_strerror(err));
                return false;
            }
            roc_log(
                LogTrace,
                "alsa %s: snd_pcm_hw_params_set_format() succeeded: index=%u format=%ld",
                device_type_to_str(type), i, (long)query.requested_format[i]);
            has_selected_format = true;
            break;
        }
    }

    if (!has_selected_format) {
        roc_log(LogTrace,
                "alsa %s: snd_pcm_hw_params_set_format() failed for all requested values",
                device_type_to_str(type));
    }

    if ((err = snd_pcm_hw_params_get_format(hw_params, &query.selected_format)) < 0) {
        roc_log(LogError, "alsa %s: snd_pcm_hw_params_get_format(): %s",
                device_type_to_str(type), snd_strerror(err));
        return false;
    }

    return true;
}

bool alsa_negotiate_rate(DeviceType type,
                         AlsaQuery& query,
                         snd_pcm_hw_params_t* hw_params,
                         snd_pcm_t* pcm) {
    int err = 0;

    if ((err = snd_pcm_hw_params_set_rate_resample(pcm, hw_params, 0)) < 0) {
        roc_log(LogError, "alsa %s: snd_pcm_hw_params_set_rate_resample(): %s",
                device_type_to_str(type), snd_strerror(err));
        return false;
    }
    roc_log(LogTrace, "alsa %s: snd_pcm_hw_params_set_rate_resample() succeeded",
            device_type_to_str(type));

    bool has_selected_rate = false;

    for (unsigned i = 0; i < ROC_ARRAY_SIZE(query.requested_rate); i++) {
        if (query.requested_rate[i] == 0) {
            break;
        }
        if (snd_pcm_hw_params_test_rate(pcm, hw_params, query.requested_rate[i], 0)
            == 0) {
            if ((err = snd_pcm_hw_params_set_rate(pcm, hw_params, query.requested_rate[i],
                                                  0))
                < 0) {
                roc_log(LogError, "alsa %s: snd_pcm_hw_params_set_rate(): %s",
                        device_type_to_str(type), snd_strerror(err));
                return false;
            }
            roc_log(LogTrace,
                    "alsa %s: snd_pcm_hw_params_set_rate() succeeded: index=%u rate=%lu",
                    device_type_to_str(type), i, (unsigned long)query.requested_rate[i]);
            has_selected_rate = true;
            break;
        }
    }

    if (!has_selected_rate) {
        for (unsigned i = 0; i < ROC_ARRAY_SIZE(query.requested_rate); i++) {
            if (query.requested_rate[i] == 0) {
                break;
            }
            unsigned int rate = query.requested_rate[i];
            if ((err = snd_pcm_hw_params_set_rate_near(pcm, hw_params, &rate, NULL))
                == 0) {
                roc_log(LogTrace,
                        "alsa %s: snd_pcm_hw_params_set_rate_near() succeeded:"
                        " index=%u requested_rate=%lu selected_rate=%lu",
                        device_type_to_str(type), i,
                        (unsigned long)query.requested_rate[i], (unsigned long)rate);
                has_selected_rate = true;
                break;
            }
        }
    }

    if (!has_selected_rate) {
        roc_log(
            LogTrace,
            "alsa %s: snd_pcm_hw_params_set_rate() and snd_pcm_hw_params_set_rate_near()"
            " failed for all requested values",
            device_type_to_str(type));
    }

    if ((err = snd_pcm_hw_params_get_rate(hw_params, &query.selected_rate, NULL)) < 0) {
        roc_log(LogError, "alsa %s: snd_pcm_hw_params_get_rate(): %s",
                device_type_to_str(type), snd_strerror(err));
        return false;
    }

    return true;
}

bool alsa_negotiate_channels(DeviceType type,
                             AlsaQuery& query,
                             snd_pcm_hw_params_t* hw_params,
                             snd_pcm_t* pcm) {
    bool has_selected_channels = false;
    int err = 0;

    for (unsigned i = 0; i < ROC_ARRAY_SIZE(query.requested_channels); i++) {
        if (query.requested_channels[i] == 0) {
            break;
        }
        if (snd_pcm_hw_params_test_channels(pcm, hw_params, query.requested_channels[i])
            == 0) {
            if ((err = snd_pcm_hw_params_set_channels(pcm, hw_params,
                                                      query.requested_channels[i]))
                < 0) {
                roc_log(LogError, "alsa %s: snd_pcm_hw_params_set_channels(): %s",
                        device_type_to_str(type), snd_strerror(err));
                return false;
            }
            roc_log(LogTrace,
                    "alsa %s: snd_pcm_hw_params_set_channels() succeeded:"
                    " index=%u channels=%lu",
                    device_type_to_str(type), i,
                    (unsigned long)query.requested_channels[i]);
            has_selected_channels = true;
            break;
        }
    }

    if (!has_selected_channels) {
        for (unsigned i = 0; i < ROC_ARRAY_SIZE(query.requested_channels); i++) {
            if (query.requested_channels[i] == 0) {
                break;
            }
            unsigned int channels = query.requested_channels[i];
            if ((err = snd_pcm_hw_params_set_channels_near(pcm, hw_params, &channels))
                == 0) {
                roc_log(LogTrace,
                        "alsa %s: snd_pcm_hw_params_set_channels_near() succeeded:"
                        " index=%u requested_channels=%lu selected_channels=%lu",
                        device_type_to_str(type), i,
                        (unsigned long)query.requested_channels[i],
                        (unsigned long)channels);
                has_selected_channels = true;
                break;
            }
        }
    }

    if (!has_selected_channels) {
        roc_log(LogTrace,
                "alsa %s: snd_pcm_hw_params_set_channels() and "
                " snd_pcm_hw_params_set_channels_near()"
                " failed for all requested values",
                device_type_to_str(type));
    }

    if ((err = snd_pcm_hw_params_get_channels(hw_params, &query.selected_channels)) < 0) {
        roc_log(LogError, "alsa %s: snd_pcm_hw_params_get_channels(): %s",
                device_type_to_str(type), snd_strerror(err));
        return false;
    }

    return true;
}

bool alsa_negotiate_periods(DeviceType type,
                            AlsaQuery& query,
                            snd_pcm_hw_params_t* hw_params,
                            snd_pcm_t* pcm) {
    bool has_selected_periods = false;
    int err = 0;

    for (unsigned i = 0; i < ROC_ARRAY_SIZE(query.requested_periods); i++) {
        if (query.requested_periods[i] == 0) {
            break;
        }
        if (snd_pcm_hw_params_test_periods(pcm, hw_params, query.requested_periods[i], 0)
            == 0) {
            if ((err = snd_pcm_hw_params_set_periods(pcm, hw_params,
                                                     query.requested_periods[i], 0))
                < 0) {
                roc_log(LogError, "alsa %s: snd_pcm_hw_params_set_periods(): %s",
                        device_type_to_str(type), snd_strerror(err));
                return false;
            }
            roc_log(LogTrace,
                    "alsa %s: snd_pcm_hw_params_set_periods() succeeded:"
                    " index=%u periods=%lu",
                    device_type_to_str(type), i,
                    (unsigned long)query.requested_periods[i]);
            has_selected_periods = true;
            break;
        }
    }

    if (!has_selected_periods) {
        for (unsigned i = 0; i < ROC_ARRAY_SIZE(query.requested_periods); i++) {
            if (query.requested_periods[i] == 0) {
                break;
            }
            unsigned int periods = query.requested_periods[i];
            if ((err = snd_pcm_hw_params_set_periods_near(pcm, hw_params, &periods, NULL))
                == 0) {
                roc_log(LogTrace,
                        "alsa %s: snd_pcm_hw_params_set_periods_near() succeeded:"
                        " index=%u requested_periods=%lu selected_periods=%lu",
                        device_type_to_str(type), i,
                        (unsigned long)query.requested_periods[i],
                        (unsigned long)periods);
                has_selected_periods = true;
                break;
            }
        }
    }

    if (!has_selected_periods) {
        roc_log(LogTrace,
                "alsa %s: snd_pcm_hw_params_set_periods() and "
                " snd_pcm_hw_params_set_periods_near()"
                " failed for all requested values",
                device_type_to_str(type));
    }

    if ((err = snd_pcm_hw_params_get_periods(hw_params, &query.selected_periods, NULL))
        < 0) {
        roc_log(LogError, "alsa %s: snd_pcm_hw_params_get_periods(): %s",
                device_type_to_str(type), snd_strerror(err));
        return false;
    }

    return true;
}

bool alsa_negotiate_latency(DeviceType type,
                            AlsaQuery& query,
                            snd_pcm_hw_params_t* hw_params,
                            snd_pcm_t* pcm) {
    int err = 0;

    // Calculate buffer size that is multiple of both period count and channel count.
    // Measured in # of frames (i.e. doesn't take channel count into account).
    // Defines size of the whole ring buffer.
    roc_panic_if(query.requested_latency_us == 0);
    roc_panic_if(query.selected_rate == 0);
    roc_panic_if(query.selected_channels == 0);
    roc_panic_if(query.selected_periods == 0);

    snd_pcm_uframes_t requested_buffer_size =
        (snd_pcm_uframes_t)((double)query.requested_latency_us * query.selected_rate
                            / 1000000);

    const unsigned int buffer_multiple =
        (query.selected_periods * query.selected_channels);

    if (requested_buffer_size % buffer_multiple != 0) {
        requested_buffer_size +=
            (buffer_multiple - (requested_buffer_size % buffer_multiple));
    }

    roc_panic_if(requested_buffer_size % query.selected_periods != 0);
    roc_panic_if(requested_buffer_size % query.selected_channels != 0);

    // Calculate period size based on buffer size.
    // Measured in # of frames (i.e. doesn't take channel count into account).
    // Defines size of one I/O chunk inside ring buffer.
    snd_pcm_uframes_t requested_period_size =
        (requested_buffer_size / query.selected_periods);

    /* negotiate period size with ALSA */
    snd_pcm_uframes_t period_size = requested_period_size;
    if ((err = snd_pcm_hw_params_set_period_size_near(pcm, hw_params, &period_size, NULL))
        < 0) {
        roc_log(LogError, "alsa %s: snd_pcm_hw_params_set_period_size_near(): %s",
                device_type_to_str(type), snd_strerror(err));
        return false;
    }
    query.selected_period_size = period_size;
    roc_log(LogTrace,
            "alsa %s: snd_pcm_hw_params_set_period_size_near() succeeded:"
            " requested_size=%lu selected_size=%lu",
            device_type_to_str(type), (unsigned long)requested_period_size,
            (unsigned long)period_size);

    // Update buffer size from negotiated period size.
    requested_buffer_size = query.selected_period_size * query.selected_periods;

    // Negotiate buffer size with ALSA.
    snd_pcm_uframes_t buffer_size = requested_buffer_size;
    if ((err = snd_pcm_hw_params_set_buffer_size_near(pcm, hw_params, &buffer_size))
        < 0) {
        roc_log(LogError, "alsa %s: snd_pcm_hw_params_set_buffer_size_near(): %s",
                device_type_to_str(type), snd_strerror(err));
        return false;
    }
    query.selected_buffer_size = buffer_size;
    roc_log(LogTrace,
            "alsa %s: snd_pcm_hw_params_set_buffer_size_near() succeeded:"
            " requested_size=%lu selected_size=%lu",
            device_type_to_str(type), (unsigned long)requested_buffer_size,
            (unsigned long)buffer_size);

    // Retrieve buffer time from ALSA.
    if ((err = snd_pcm_hw_params_get_buffer_time(hw_params, &query.selected_latency_us,
                                                 NULL))
        < 0) {
        roc_log(LogError, "alsa %s: snd_pcm_hw_params_get_buffer_time(): %s",
                device_type_to_str(type), snd_strerror(err));
        return false;
    }

    return true;
}

bool alsa_set_hw_params(DeviceType type, AlsaQuery& query, snd_pcm_t* pcm) {
    roc_panic_if(!pcm);

    snd_pcm_hw_params_t* hw_params = NULL;
    snd_pcm_hw_params_alloca(&hw_params);

    int err = 0;

    if ((err = snd_pcm_hw_params_any(pcm, hw_params)) < 0) {
        roc_log(LogError, "alsa %s: snd_pcm_hw_params_any(): %s",
                device_type_to_str(type), snd_strerror(err));
        return false;
    }

    if (!alsa_negotiate_access(type, query, hw_params, pcm)) {
        return false;
    }

    if (!alsa_negotiate_format(type, query, hw_params, pcm)) {
        return false;
    }

    if (!alsa_negotiate_rate(type, query, hw_params, pcm)) {
        return false;
    }

    if (!alsa_negotiate_channels(type, query, hw_params, pcm)) {
        return false;
    }

    if (!alsa_negotiate_periods(type, query, hw_params, pcm)) {
        return false;
    }

    if (!alsa_negotiate_latency(type, query, hw_params, pcm)) {
        return false;
    }

    if ((err = snd_pcm_hw_params(pcm, hw_params)) < 0) {
        roc_log(LogError, "alsa %s: snd_pcm_hw_params(): %s", device_type_to_str(type),
                snd_strerror(err));
        return false;
    }

    return true;
}

bool alsa_set_sw_params(DeviceType type, AlsaQuery& query, snd_pcm_t* pcm) {
    roc_panic_if(!pcm);

    roc_panic_if(query.selected_buffer_size == 0);
    roc_panic_if(query.selected_period_size == 0);

    snd_pcm_sw_params_t* sw_params = NULL;
    snd_pcm_sw_params_alloca(&sw_params);

    int err = 0;

    if ((err = snd_pcm_sw_params_current(pcm, sw_params)) < 0) {
        roc_log(LogError, "alsa %s: snd_pcm_sw_params_current(): %s",
                device_type_to_str(type), snd_strerror(err));
        return false;
    }

    switch (type) {
    case DeviceType_Sink:
        // Set initial threshold after which ALSA starts playback.
        if ((err = snd_pcm_sw_params_set_start_threshold(pcm, sw_params,
                                                         query.selected_buffer_size))
            < 0) {
            roc_log(LogError, "alsa %s: snd_pcm_sw_params_set_start_threshold(): %s",
                    device_type_to_str(type), snd_strerror(err));
            return false;
        }

        // Set minimum threshold below which ALSA won't allow to perform write.
        if ((err = snd_pcm_sw_params_set_avail_min(pcm, sw_params,
                                                   query.selected_period_size))
            < 0) {
            roc_log(LogError, "alsa %s: snd_pcm_sw_params_set_avail_min(): %s",
                    device_type_to_str(type), snd_strerror(err));
            return false;
        }

        break;

    case DeviceType_Source:
        // Set minimum threshold below which ALSA won't allow to perform read.
        if ((err = snd_pcm_sw_params_set_avail_min(pcm, sw_params,
                                                   query.selected_period_size))
            < 0) {
            roc_log(LogError, "alsa %s: snd_pcm_sw_params_set_avail_min(): %s",
                    device_type_to_str(type), snd_strerror(err));
            return false;
        }

        break;
    }

    if ((err = snd_pcm_sw_params(pcm, sw_params)) < 0) {
        roc_log(LogError, "alsa %s: snd_pcm_sw_params(): %s", device_type_to_str(type),
                snd_strerror(err));
        return false;
    }

    return true;
}

bool alsa_open(DeviceType device_type, const char* device, snd_pcm_t** pcm) {
    const snd_pcm_stream_t mode =
        device_type == DeviceType_Sink ? SND_PCM_STREAM_PLAYBACK : SND_PCM_STREAM_CAPTURE;

    int err = 0;
    if ((err = snd_pcm_open(pcm, device, mode, 0)) < 0) {
        roc_log(LogError, "alsa %s: can't open device \"%s\": snd_pcm_open(): %s",
                device_type_to_str(device_type), device, snd_strerror(err));
        return false;
    }

    return true;
}

} // namespace

AlsaDevice::AlsaDevice(const Config& config, DeviceType device_type)
    : device_type_(device_type)
    , device_(NULL)
    , config_(config)
    , frame_size_(0)
    , pcm_(NULL)
    , rate_limiter_(ReportInterval) {
}

AlsaDevice::~AlsaDevice() {
    roc_log(LogDebug, "alsa %s: closing device", device_type_to_str(device_type_));

    // TODO: close
}

bool AlsaDevice::open(const char* device) {
    if (pcm_) {
        roc_panic("alsa %s: can't call open() twice", device_type_to_str(device_type_));
    }

    roc_log(LogDebug, "alsa %s: opening device: device=%s",
            device_type_to_str(device_type_), device);

    if (!alsa_open(device_type_, device, &pcm_)) {
        return false;
    }

    AlsaQuery query;

    query.requested_access[0] = SND_PCM_ACCESS_RW_INTERLEAVED;
    query.requested_access[1] = SND_PCM_ACCESS_LAST;

    query.requested_format[0] = SND_PCM_FORMAT_FLOAT;
    query.requested_format[1] = SND_PCM_FORMAT_LAST;

    query.requested_rate[0] = config_.sample_spec.sample_rate();
    query.requested_rate[1] = 0;

    query.requested_channels[0] = config_.sample_spec.num_channels();
    query.requested_channels[1] = 0;

    query.requested_periods[0] = 2;
    query.requested_periods[1] = 0;

    query.requested_latency_us = size_t(config_.latency / core::Microsecond);

    if (!alsa_set_hw_params(device_type_, query, pcm_)) {
        return false;
    }

    if (!alsa_set_sw_params(device_type_, query, pcm_)) {
        return false;
    }

    config_.sample_spec.set_sample_rate(query.selected_rate);
    config_.sample_spec.set_channel_mask((1 << (query.selected_channels + 1)) - 1);

    config_.latency = core::nanoseconds_t(query.selected_latency_us * core::Microsecond);

    frame_size_ = size_t(query.selected_period_size * config_.sample_spec.num_channels());

    return true;
}

DeviceState AlsaDevice::state() const {
    // TODO
}

void AlsaDevice::pause() {
    // TODO
}

bool AlsaDevice::resume() {
    // TODO
}

bool AlsaDevice::restart() {
    // TODO
}

audio::SampleSpec AlsaDevice::sample_spec() const {
    // TODO
}

core::nanoseconds_t AlsaDevice::latency() const {
    // TODO
}

bool AlsaDevice::has_clock() const {
    return true;
}

bool AlsaDevice::request(audio::Frame& frame) {
    // TODO
}

} // namespace sndio
} // namespace roc
