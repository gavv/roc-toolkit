/*
 * Copyright (c) 2015 Roc Streaming authors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "roc_fec/block_writer.h"
#include "roc_core/fast_random.h"
#include "roc_core/log.h"
#include "roc_core/panic.h"
#include "roc_packet/fec_scheme_to_str.h"
#include "roc_status/code_to_str.h"

namespace roc {
namespace fec {

BlockWriter::BlockWriter(const BlockWriterConfig& config,
                         packet::FecScheme fec_scheme,
                         IBlockEncoder& block_encoder,
                         packet::IWriter& writer,
                         packet::IComposer& source_composer,
                         packet::IComposer& repair_composer,
                         packet::PacketFactory& packet_factory,
                         core::IArena& arena)
    : cur_sblen_(0)
    , next_sblen_(0)
    , cur_rblen_(0)
    , next_rblen_(0)
    , cur_payload_size_(0)
    , block_encoder_(block_encoder)
    , writer_(writer)
    , source_composer_(source_composer)
    , repair_composer_(repair_composer)
    , packet_factory_(packet_factory)
    , repair_block_(arena)
    , first_packet_(true)
    , alive_(true)
    , cur_packet_(0)
    , fec_scheme_(fec_scheme)
    , prev_block_timestamp_valid_(false)
    , prev_block_timestamp_(0)
    , block_max_duration_(0)
    , init_status_(status::NoStatus) {
    if ((init_status_ = block_encoder_.init_status()) != status::StatusOK) {
        return;
    }

    cur_sbn_ = (packet::blknum_t)core::fast_random_range(0, packet::blknum_t(-1));
    cur_block_repair_sn_ =
        (packet::seqnum_t)core::fast_random_range(0, packet::seqnum_t(-1));

    if (!resize(config.n_source_packets, config.n_repair_packets)) {
        init_status_ = status::StatusNoMem;
        return;
    }

    init_status_ = status::StatusOK;
}

status::StatusCode BlockWriter::init_status() const {
    return init_status_;
}

bool BlockWriter::is_alive() const {
    roc_panic_if(init_status_ != status::StatusOK);

    return alive_;
}

packet::stream_timestamp_t BlockWriter::max_block_duration() const {
    roc_panic_if(init_status_ != status::StatusOK);

    return (packet::stream_timestamp_t)block_max_duration_;
}

bool BlockWriter::resize(size_t sblen, size_t rblen) {
    roc_panic_if(init_status_ != status::StatusOK);

    if (next_sblen_ == sblen && next_rblen_ == rblen) {
        return true;
    }

    if (sblen == 0) {
        roc_log(LogError, "fec block writer: resize: sblen can't be zero");
        return false;
    }

    const size_t new_blen = sblen + rblen;

    if (new_blen > block_encoder_.max_block_length()) {
        roc_log(LogDebug,
                "fec block writer: can't update block length, maximum value exceeded:"
                " cur_sbl=%lu cur_rbl=%lu new_sbl=%lu new_rbl=%lu max_blen=%lu",
                (unsigned long)cur_sblen_, (unsigned long)cur_rblen_,
                (unsigned long)sblen, (unsigned long)rblen,
                (unsigned long)block_encoder_.max_block_length());
        return false;
    }

    roc_log(LogDebug,
            "fec block writer: update block size:"
            " cur_sbl=%lu cur_rbl=%lu new_sbl=%lu new_rbl=%lu",
            (unsigned long)cur_sblen_, (unsigned long)cur_rblen_, (unsigned long)sblen,
            (unsigned long)rblen);

    next_sblen_ = sblen;
    next_rblen_ = rblen;

    // max_block_duration() reports maximum duration since last resize,
    // so when resize happens, we reset maximum.
    prev_block_timestamp_valid_ = false;

    return true;
}

status::StatusCode BlockWriter::write(const packet::PacketPtr& pp) {
    roc_panic_if(init_status_ != status::StatusOK);
    roc_panic_if(!pp);

    if (!alive_) {
        // TODO(gh-183): return StatusDead
        return status::StatusOK;
    }

    validate_fec_packet_(pp);

    if (first_packet_) {
        first_packet_ = false;
    }

    if (cur_packet_ == 0) {
        if (!begin_block_(pp)) {
            // TODO(gh-183): return status
            return status::StatusOK;
        }
    }

    if (!validate_source_packet_(pp)) {
        // TODO(gh-183): return status
        return status::StatusOK;
    }

    const status::StatusCode code = write_source_packet_(pp);
    // TODO(gh-183): forward status
    roc_panic_if(code != status::StatusOK);

    cur_packet_++;

    if (cur_packet_ == cur_sblen_) {
        end_block_();
        next_block_();
    }

    return status::StatusOK;
}

bool BlockWriter::begin_block_(const packet::PacketPtr& pp) {
    update_block_duration_(pp);

    if (!apply_sizes_(next_sblen_, next_rblen_, pp->fec()->payload.size())) {
        return false;
    }

    roc_log(LogTrace,
            "fec block writer: begin block: sbn=%lu sblen=%lu rblen=%lu payload_size=%lu",
            (unsigned long)cur_sbn_, (unsigned long)cur_sblen_, (unsigned long)cur_rblen_,
            (unsigned long)cur_payload_size_);

    if (!block_encoder_.begin_block(cur_sblen_, cur_rblen_, cur_payload_size_)) {
        roc_log(LogError,
                "fec block writer: can't begin encoder block, shutting down:"
                " sblen=%lu rblen=%lu",
                (unsigned long)cur_sblen_, (unsigned long)cur_rblen_);
        return (alive_ = false);
    }

    return true;
}

void BlockWriter::end_block_() {
    make_repair_packets_();
    encode_repair_packets_();
    compose_repair_packets_();
    write_repair_packets_();

    block_encoder_.end_block();
}

void BlockWriter::next_block_() {
    cur_block_repair_sn_ += (packet::seqnum_t)cur_rblen_;
    cur_sbn_++;
    cur_packet_ = 0;
}

bool BlockWriter::apply_sizes_(size_t sblen, size_t rblen, size_t payload_size) {
    if (payload_size == 0) {
        roc_log(LogError, "fec block writer: payload size can't be zero");
        return (alive_ = false);
    }

    if (repair_block_.size() != rblen) {
        if (!repair_block_.resize(rblen)) {
            roc_log(LogError,
                    "fec block writer: can't allocate repair block memory, shutting down:"
                    " cur_rbl=%lu new_rbl=%lu",
                    (unsigned long)repair_block_.size(), (unsigned long)rblen);
            return (alive_ = false);
        }
    }

    cur_sblen_ = sblen;
    cur_rblen_ = rblen;
    cur_payload_size_ = payload_size;

    return true;
}

status::StatusCode BlockWriter::write_source_packet_(const packet::PacketPtr& pp) {
    block_encoder_.set_buffer(cur_packet_, pp->fec()->payload);

    fill_packet_fec_fields_(pp, (packet::seqnum_t)cur_packet_);

    if (!source_composer_.compose(*pp)) {
        // TODO(gh-183): return status from composer
        roc_panic("fec block writer: can't compose source packet");
    }
    pp->add_flags(packet::Packet::FlagComposed);

    return writer_.write(pp);
}

void BlockWriter::make_repair_packets_() {
    for (size_t i = 0; i < cur_rblen_; i++) {
        packet::PacketPtr rp = make_repair_packet_((packet::seqnum_t)i);
        if (!rp) {
            continue;
        }
        repair_block_[i] = rp;
    }
}

packet::PacketPtr BlockWriter::make_repair_packet_(packet::seqnum_t pack_n) {
    packet::PacketPtr packet = packet_factory_.new_packet();
    if (!packet) {
        roc_log(LogError, "fec block writer: can't allocate packet");
        return NULL;
    }

    core::Slice<uint8_t> buffer = packet_factory_.new_packet_buffer();
    if (!buffer) {
        roc_log(LogError, "fec block writer: can't allocate buffer");
        // TODO(gh-183): return StatusNoMem
        return NULL;
    }

    if (!repair_composer_.align(buffer, 0, block_encoder_.buffer_alignment())) {
        roc_log(LogError, "fec block writer: can't align packet buffer");
        // TODO(gh-183): return status from composer
        return NULL;
    }

    if (!repair_composer_.prepare(*packet, buffer, cur_payload_size_)) {
        roc_log(LogError, "fec block writer: can't prepare packet");
        // TODO(gh-183): return status from composer
        return NULL;
    }
    packet->add_flags(packet::Packet::FlagPrepared);

    packet->set_buffer(buffer);

    validate_fec_packet_(packet);
    fill_packet_fec_fields_(packet, (packet::seqnum_t)cur_sblen_ + pack_n);

    return packet;
}

void BlockWriter::encode_repair_packets_() {
    for (size_t i = 0; i < cur_rblen_; i++) {
        packet::PacketPtr rp = repair_block_[i];
        if (rp) {
            block_encoder_.set_buffer(cur_sblen_ + i, rp->fec()->payload);
        }
    }
    block_encoder_.fill_buffers();
}

void BlockWriter::compose_repair_packets_() {
    for (size_t i = 0; i < cur_rblen_; i++) {
        packet::PacketPtr rp = repair_block_[i];
        if (!rp) {
            continue;
        }

        if (!repair_composer_.compose(*rp)) {
            // TODO(gh-183): return status from composer
            roc_panic("fec block writer: can't compose repair packet");
        }
        rp->add_flags(packet::Packet::FlagComposed);
    }
}

status::StatusCode BlockWriter::write_repair_packets_() {
    for (size_t i = 0; i < cur_rblen_; i++) {
        packet::PacketPtr rp = repair_block_[i];
        if (!rp) {
            continue;
        }

        const status::StatusCode code = writer_.write(repair_block_[i]);
        // TODO(gh-183): forward status
        roc_panic_if(code != status::StatusOK);

        repair_block_[i] = NULL;
    }

    return status::StatusOK;
}

void BlockWriter::fill_packet_fec_fields_(const packet::PacketPtr& packet,
                                          packet::seqnum_t pack_n) {
    packet::FEC& fec = *packet->fec();

    fec.encoding_symbol_id = pack_n;
    fec.source_block_number = cur_sbn_;
    fec.source_block_length = cur_sblen_;
    fec.block_length = cur_sblen_ + cur_rblen_;
}

void BlockWriter::validate_fec_packet_(const packet::PacketPtr& pp) {
    if (!pp->has_flags(packet::Packet::FlagPrepared)) {
        roc_panic("fec block writer: unexpected packet: should be prepared");
    }

    if (pp->has_flags(packet::Packet::FlagComposed)) {
        roc_panic("fec block writer: unexpected packet: should not be composed");
    }

    const packet::FEC* fec = pp->fec();
    if (!fec) {
        roc_panic("fec block writer: unexpected non-fec packet");
    }

    if (fec->fec_scheme != fec_scheme_) {
        roc_panic("fec block writer: unexpected packet fec scheme:"
                  " packet_scheme=%s session_scheme=%s",
                  packet::fec_scheme_to_str(fec->fec_scheme),
                  packet::fec_scheme_to_str(fec_scheme_));
    }
}

bool BlockWriter::validate_source_packet_(const packet::PacketPtr& pp) {
    const size_t payload_size = pp->fec()->payload.size();

    if (payload_size != cur_payload_size_) {
        roc_log(LogError,
                "fec block writer: can't change payload size in the middle of a block:"
                " sbn=%lu esi=%lu old_size=%lu new_size=%lu",
                (unsigned long)cur_sbn_, (unsigned long)cur_packet_,
                (unsigned long)cur_payload_size_, (unsigned long)payload_size);
        // TODO(gh-183): return status
        return (alive_ = false);
    }

    return true;
}

void BlockWriter::update_block_duration_(const packet::PacketPtr& curr_block_pkt) {
    packet::stream_timestamp_diff_t block_dur = 0;
    if (prev_block_timestamp_valid_) {
        block_dur = packet::stream_timestamp_diff(curr_block_pkt->stream_timestamp(),
                                                  prev_block_timestamp_);
    }
    if (block_dur < 0) {
        roc_log(LogTrace, "fec reader: negative block duration: prev_ts=%lu curr_ts=%lu",
                (unsigned long)prev_block_timestamp_,
                (unsigned long)curr_block_pkt->stream_timestamp());
        prev_block_timestamp_valid_ = false;
    } else {
        block_max_duration_ = std::max(block_max_duration_, block_dur);
        prev_block_timestamp_ = curr_block_pkt->stream_timestamp();
        prev_block_timestamp_valid_ = true;
    }
}

} // namespace fec
} // namespace roc
