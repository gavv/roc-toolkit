/*
 * Copyright (c) 2023 Roc Streaming authors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

/**
 * \file roc/metrics.h
 * \brief Metrics.
 */

#ifndef ROC_METRICS_H_
#define ROC_METRICS_H_

#include "roc/platform.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Metrics for a single connection between sender and receiver.
 *
 * On receiver, represents one connected sender. Similarly, on sender  represents one
 * connected receiver. It doesn't matter who initiated connection, sender or receiver.
 *
 * Metrics calculations require usage of a control protocol like \ref ROC_PROTO_RTCP.
 * Without that, many metrics will remain zero.
 *
 * Any metric may also remain zero initially, until enough statistics is accumulated.
 */
typedef struct roc_connection_metrics {
    /** Estimated end-to-end latency, in nanoseconds.
     *
     * Determines how much time passes after a frame is written to sender and before
     * it is read from receiver. Consists of sender latency, network latency,
     * and receiver latency.
     *
     * Computations are based on RTCP and system clock. If system clocks of sender
     * and receiver are not synchronized, latency will be calculated incorrectly.
     *
     * Metric is available on both sender and receiver.
     */
    unsigned long long e2e_latency;

    /** Estimated round-trip time, in nanoseconds.
     *
     * Determines how much times it takes for a packet to go from one side (sender or
     * receiver) to another, and back. Covers delay introduced by network and by sender
     * and receiver network queues.
     *
     * Computations are based on NTP-like timestamp exchange based on RTCP protocol.
     *
     * Metric is available on both sender and receiver.
     */
    unsigned long long rtt;

    /** Estimated interarrival jitter, in nanoseconds.
     *
     * Determines expected variance of inter-packet arrival period. Covers jitter
     * introduced by network and by sender and receiver network queues.
     *
     * Metric is available on both sender and receiver.
     */
    unsigned long long jitter;

    /** Total amount of packets that receiver expects to be delivered.
     *
     * Determines an estimation of how many packets were generated by sender, regardless
     * of losses and duplicates. The count begins when the connection is established.
     *
     * Computations are based on RTP seqnums.
     *
     * Metric is available on both sender and receiver.
     */
    unsigned long long expected_packets;

    /** Cumulative count of lost packets.
     *
     * Determines an estimation of how many packets were lost. The count begins when the
     * connection is established.
     *
     * Computations are based on RTP seqnums and are imprecise if there are frequent
     * duplicates caused by network.
     *
     * Metric is available on both sender and receiver.
     */
    unsigned long long lost_packets;

    /** Cumulative count of late packets.
     *
     * Determines how many packets were dropped on receiver because they were delivered
     * too late to be decoded.
     *
     * Metric is available only on receiver.
     */
    unsigned long long late_packets;

    /** Cumulative count of packets successfully recovered by FEC.
     *
     * Determines how many packets were lost or late, but were successfully recovered
     * and decoded in-time by FEC on receiver.
     *
     * Metric is available only on receiver.
     */
    unsigned long long recovered_packets;
} roc_connection_metrics;

/** Receiver metrics.
 *
 * Holds receiver-side metrics that are not specific to connection.
 * If multiple slots are used, each slot has its own metrics.
 */
typedef struct roc_receiver_metrics {
    /** Number of active connections.
     *
     * Defines how much senders are currently connected to receiver.
     * When there are no connections, receiver produces silence.
     */
    unsigned int connection_count;
} roc_receiver_metrics;

/** Sender metrics.
 *
 * Holds sender-side metrics that are not specific to connection.
 * If multiple slots are used, each slot has its own metrics.
 */
typedef struct roc_sender_metrics {
    /** Number of active connections.
     *
     * Defines how much receivers are currently discovered.
     *
     * If a control or signaling protocol like \ref ROC_PROTO_RTSP or
     * \ref ROC_PROTO_RTCP is not used, sender doesn't know about receivers and
     * doesn't have connection metrics.
     *
     * If such a protocol is used, in case of unicast, sender will have a single
     * connection, and in case of multicast, sender may have multiple
     * connections, one per each discovered receiver.
     */
    unsigned int connection_count;
} roc_sender_metrics;

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROC_METRICS_H_ */
