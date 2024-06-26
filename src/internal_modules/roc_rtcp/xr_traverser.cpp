/*
 * Copyright (c) 2022 Roc Streaming authors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "roc_rtcp/xr_traverser.h"
#include "roc_rtcp/headers.h"

namespace roc {
namespace rtcp {

XrTraverser::XrTraverser(const core::Slice<uint8_t>& buf)
    : buf_(buf)
    , parsed_(false)
    , blocks_count_(0) {
    roc_panic_if_msg(!buf, "xr traverser: null slice");
}

bool XrTraverser::parse() {
    roc_panic_if_msg(parsed_, "xr traverser: packet already parsed");

    if (buf_.size() < sizeof(header::XrPacket)) {
        return false;
    }

    const header::XrPacket* xr = (const header::XrPacket*)buf_.data();
    if (xr->header().type() != header::RTCP_XR) {
        return false;
    }

    const size_t packet_len = xr->header().len_bytes();
    if (packet_len > buf_.size()) {
        return false;
    }

    // Remove padding.
    if (xr->header().has_padding()) {
        const uint8_t padding_len = buf_[packet_len - 1];
        if (padding_len < 1 || padding_len > packet_len - sizeof(header::XrPacket)) {
            return false;
        }
        buf_ = buf_.subslice(0, packet_len - padding_len);
    }

    // XR packets don't use counter field of the packet header,
    // so we compute block count manually.
    Iterator iter(*this);
    while (iter.next() != Iterator::END) {
        blocks_count_++;
    }

    parsed_ = true;
    return true;
}

XrTraverser::Iterator XrTraverser::iter() const {
    roc_panic_if_msg(!parsed_, "xr traverser: packet not parsed");

    Iterator iter(*this);
    return iter;
}

size_t XrTraverser::blocks_count() const {
    roc_panic_if_msg(!parsed_, "xr traverser: packet not parsed");

    return blocks_count_;
}

const header::XrPacket& XrTraverser::packet() const {
    roc_panic_if_msg(!parsed_, "xr traverser: packet not parsed");

    return *(const header::XrPacket*)buf_.data();
}

XrTraverser::Iterator::Iterator(const XrTraverser& traverser)
    : state_(BEGIN)
    , buf_(traverser.buf_)
    , cur_pos_(0)
    , cur_blk_header_(NULL)
    , cur_blk_len_(0)
    , error_(false) {
}

XrTraverser::Iterator::State XrTraverser::Iterator::next() {
    next_block_();
    return state_;
}

bool XrTraverser::Iterator::error() const {
    return error_;
}

void XrTraverser::Iterator::next_block_() {
    if (state_ == END) {
        return;
    }

    if (state_ == BEGIN) {
        // Skip packet header.
        cur_pos_ += sizeof(header::XrPacket);
        if (cur_pos_ > buf_.size()) {
            // Packet header larger than buffer.
            error_ = true;
            state_ = END;
            return;
        }
    }

    if (state_ != BEGIN) {
        // Go to next block.
        cur_pos_ += cur_blk_len_;
    }

    // Skip blocks until found known type.
    for (;;) {
        if (cur_pos_ == buf_.size()) {
            // Last block.
            state_ = END;
            return;
        }

        if (cur_pos_ + sizeof(header::XrBlockHeader) > buf_.size()) {
            // Block header larger than remaining buffer.
            error_ = true;
            state_ = END;
            return;
        }

        cur_blk_header_ = (const header::XrBlockHeader*)&buf_[cur_pos_];
        cur_blk_len_ = cur_blk_header_->len_bytes();

        if (cur_pos_ + cur_blk_len_ > buf_.size()) {
            // Block size larger than remaining buffer.
            error_ = true;
            state_ = END;
            return;
        }

        // Check for known block types.
        switch (cur_blk_header_->block_type()) {
        case header::XR_RRTR:
            if (!check_rrtr_()) {
                // Skipping invalid block.
                error_ = true;
                break;
            }
            state_ = RRTR_BLOCK;
            return;
        case header::XR_DLRR:
            if (!check_dlrr_()) {
                // Skipping invalid block.
                error_ = true;
                break;
            }
            state_ = DLRR_BLOCK;
            return;
        case header::XR_MEASUREMENT_INFO:
            if (!check_measurement_info_()) {
                // Skipping invalid block.
                error_ = true;
                break;
            }
            state_ = MEASUREMENT_INFO_BLOCK;
            return;
        case header::XR_DELAY_METRICS:
            if (!check_delay_metrics_()) {
                // Skipping invalid block.
                error_ = true;
                break;
            }
            state_ = DELAY_METRICS_BLOCK;
            return;
        case header::XR_QUEUE_METRICS:
            if (!check_queue_metrics_()) {
                // Skipping invalid block.
                error_ = true;
                break;
            }
            state_ = QUEUE_METRICS_BLOCK;
            return;
        default:
            // Unknown block.
            break;
        }

        // Skip to next block.
        cur_pos_ += cur_blk_len_;
    }
}

bool XrTraverser::Iterator::check_rrtr_() {
    if (cur_blk_len_ < sizeof(header::XrRrtrBlock)) {
        return false;
    }

    return true;
}

bool XrTraverser::Iterator::check_dlrr_() {
    const header::XrDlrrBlock* dlrr = (const header::XrDlrrBlock*)cur_blk_header_;

    if (cur_blk_len_ < sizeof(header::XrDlrrBlock)
            + dlrr->num_subblocks() * sizeof(header::XrDlrrSubblock)) {
        return false;
    }

    return true;
}

bool XrTraverser::Iterator::check_measurement_info_() {
    if (cur_blk_len_ < sizeof(header::XrMeasurementInfoBlock)) {
        return false;
    }

    return true;
}

bool XrTraverser::Iterator::check_delay_metrics_() {
    if (cur_blk_len_ < sizeof(header::XrDelayMetricsBlock)) {
        return false;
    }

    return true;
}

bool XrTraverser::Iterator::check_queue_metrics_() {
    if (cur_blk_len_ != sizeof(header::XrQueueMetricsBlock)) {
        return false;
    }

    return true;
}

const header::XrRrtrBlock& XrTraverser::Iterator::get_rrtr() const {
    roc_panic_if_msg(state_ != RRTR_BLOCK,
                     "xr traverser: get_rrtr() called in wrong state %d", (int)state_);

    return *(const header::XrRrtrBlock*)cur_blk_header_;
}

const header::XrDlrrBlock& XrTraverser::Iterator::get_dlrr() const {
    roc_panic_if_msg(state_ != DLRR_BLOCK,
                     "xr traverser: get_dlrr() called in wrong state %d", (int)state_);

    return *(const header::XrDlrrBlock*)cur_blk_header_;
}

const header::XrMeasurementInfoBlock&
XrTraverser::Iterator::get_measurement_info() const {
    roc_panic_if_msg(state_ != MEASUREMENT_INFO_BLOCK,
                     "xr traverser: get_measurement_info() called in wrong state %d",
                     (int)state_);

    return *(const header::XrMeasurementInfoBlock*)cur_blk_header_;
}

const header::XrDelayMetricsBlock& XrTraverser::Iterator::get_delay_metrics() const {
    roc_panic_if_msg(state_ != DELAY_METRICS_BLOCK,
                     "xr traverser: get_delay_metrics() called in wrong state %d",
                     (int)state_);

    return *(const header::XrDelayMetricsBlock*)cur_blk_header_;
}

const header::XrQueueMetricsBlock& XrTraverser::Iterator::get_queue_metrics() const {
    roc_panic_if_msg(state_ != QUEUE_METRICS_BLOCK,
                     "xr traverser: get_queue_metrics() called in wrong state %d",
                     (int)state_);

    return *(const header::XrQueueMetricsBlock*)cur_blk_header_;
}

} // namespace rtcp
} // namespace roc
