/*
 * Copyright (c) 2020 Roc authors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

//! @file roc_peer/sender.h
//! @brief Sender peer.

#ifndef ROC_PEER_SENDER_H_
#define ROC_PEER_SENDER_H_

#include "roc_address/endpoint_uri.h"
#include "roc_address/interface.h"
#include "roc_address/protocol.h"
#include "roc_core/mutex.h"
#include "roc_core/scoped_ptr.h"
#include "roc_packet/iwriter.h"
#include "roc_peer/basic_peer.h"
#include "roc_peer/context.h"
#include "roc_pipeline/itask_scheduler.h"
#include "roc_pipeline/sender_sink.h"
#include "roc_rtp/format_map.h"

namespace roc {
namespace peer {

//! Sender peer.
class Sender : public BasicPeer, private pipeline::ITaskScheduler {
public:
    //! Initialize.
    Sender(Context& context, const pipeline::SenderConfig& pipeline_config);

    //! Deinitialize.
    ~Sender();

    //! Check if successfully constructed.
    bool valid() const;

    //! Set outgoing interface address.
    bool set_outgoing_address(address::Interface iface, const char* ip);

    //! Enable or disable traffic to broadcast addresses.
    bool set_broadcast_enabled(address::Interface iface, bool enabled);

    //! Enable or disable port squashing.
    bool set_squashing_enabled(address::Interface iface, bool enabled);

    //! Connect peer to remote endpoint.
    bool connect(address::Interface iface, const address::EndpointURI& uri);

    //! Check if all necessary bind and connect calls were made.
    bool is_ready();

    //! Get sender sink.y
    sndio::ISink& sink();

private:
    struct InterfacePort {
        netio::UdpSenderConfig config;
        netio::UdpSenderConfig orig_config;
        netio::NetworkLoop::PortHandle handle;
        packet::IWriter* writer;
        bool squashing_enabled;

        InterfacePort()
            : handle(NULL)
            , writer(NULL)
            , squashing_enabled(true) {
        }
    };

    virtual void schedule_task_processing(pipeline::TaskPipeline&,
                                          core::nanoseconds_t delay);

    virtual void cancel_task_processing(pipeline::TaskPipeline&);

    InterfacePort& select_outgoing_port_(address::Interface, address::AddrFamily family);
    bool setup_outgoing_port_(InterfacePort& port,
                              address::Interface iface,
                              address::AddrFamily family);

    core::Mutex mutex_;

    rtp::FormatMap format_map_;

    pipeline::SenderSink pipeline_;
    pipeline::SenderSink::EndpointSetHandle endpoint_set_;

    pipeline::SenderSink::EndpointHandle source_endpoint_;
    pipeline::SenderSink::EndpointHandle repair_endpoint_;

    InterfacePort ports_[address::Iface_Max];

    ctl::ControlLoop::Tasks::ProcessPipelineTasks process_pipeline_tasks_;
};

} // namespace peer
} // namespace roc

#endif // ROC_PEER_SENDER_H_
