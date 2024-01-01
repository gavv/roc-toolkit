/*
 * Copyright (c) 2024 Roc Streaming authors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <CppUTest/TestHarness.h>

#include "roc_core/heap_arena.h"
#include "roc_packet/link_meter.h"
#include "roc_packet/packet_factory.h"
#include "roc_packet/queue.h"

namespace roc {
namespace packet {

namespace {

core::HeapArena arena;
PacketFactory packet_factory(arena);

PacketPtr new_packet(seqnum_t sn) {
    PacketPtr packet = packet_factory.new_packet();
    CHECK(packet);

    packet->add_flags(Packet::FlagRTP);
    packet->rtp()->seqnum = sn;

    return packet;
}

class StatusWriter : public IWriter {
public:
    explicit StatusWriter(status::StatusCode code)
        : code_(code) {
    }

    virtual ROC_ATTR_NODISCARD status::StatusCode write(const PacketPtr&) {
        return code_;
    }

private:
    status::StatusCode code_;
};

} // namespace

TEST_GROUP(link_meter) {};

TEST(link_meter, has_metrics) {
    Queue queue;
    LinkMeter meter;
    meter.set_writer(queue);

    CHECK(!meter.has_metrics());

    CHECK_EQUAL(status::StatusOK, meter.write(new_packet(100)));
    CHECK_EQUAL(1, queue.size());

    CHECK(meter.has_metrics());
}

TEST(link_meter, last_seqnum) {
    Queue queue;
    LinkMeter meter;
    meter.set_writer(queue);

    CHECK_EQUAL(0, meter.metrics().ext_last_seqnum);

    CHECK_EQUAL(status::StatusOK, meter.write(new_packet(100)));
    CHECK_EQUAL(100, meter.metrics().ext_last_seqnum);

    // seqnum increased, metric updated
    CHECK_EQUAL(status::StatusOK, meter.write(new_packet(102)));
    CHECK_EQUAL(102, meter.metrics().ext_last_seqnum);

    // seqnum decreased, ignored
    CHECK_EQUAL(status::StatusOK, meter.write(new_packet(101)));
    CHECK_EQUAL(102, meter.metrics().ext_last_seqnum);

    // seqnum increased, metric updated
    CHECK_EQUAL(status::StatusOK, meter.write(new_packet(103)));
    CHECK_EQUAL(103, meter.metrics().ext_last_seqnum);

    CHECK_EQUAL(4, queue.size());
}

TEST(link_meter, last_seqnum_wrap) {
    Queue queue;
    LinkMeter meter;
    meter.set_writer(queue);

    CHECK_EQUAL(0, meter.metrics().ext_last_seqnum);

    // no overflow
    CHECK_EQUAL(status::StatusOK, meter.write(new_packet(65533)));
    CHECK_EQUAL(65533, meter.metrics().ext_last_seqnum);

    // no overflow
    CHECK_EQUAL(status::StatusOK, meter.write(new_packet(65535)));
    CHECK_EQUAL(65535, meter.metrics().ext_last_seqnum);

    // overflow
    CHECK_EQUAL(status::StatusOK, meter.write(new_packet(2)));
    CHECK_EQUAL(65537, meter.metrics().ext_last_seqnum);

    // late packet, ignored
    CHECK_EQUAL(status::StatusOK, meter.write(new_packet(65534)));
    CHECK_EQUAL(65537, meter.metrics().ext_last_seqnum);

    // new packet
    CHECK_EQUAL(status::StatusOK, meter.write(new_packet(5)));
    CHECK_EQUAL(65540, meter.metrics().ext_last_seqnum);

    CHECK_EQUAL(5, queue.size());
}

TEST(link_meter, forward_error) {
    StatusWriter writer(status::StatusNoMem);
    LinkMeter meter;
    meter.set_writer(writer);

    CHECK_EQUAL(status::StatusNoMem, meter.write(new_packet(100)));
}

} // namespace packet
} // namespace roc