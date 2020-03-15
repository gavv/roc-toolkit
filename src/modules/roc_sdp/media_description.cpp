/*
 * Copyright (c) 2019 Roc authors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "roc_sdp/media_description.h"

namespace roc {
namespace sdp {

MediaDescription::MediaDescription(core::IAllocator& allocator)
    : media_field_(allocator), allocator_(allocator) {
    clear();
}

void MediaDescription::clear() {
    media_field_.clear();
    while(connection_fields_.size() > 0) {
        core::SharedPtr<ConnectionField> c = connection_fields_.back();
        connection_fields_.remove(*c);
    }
}

const char* MediaDescription::media() const {
    if (media_field_.is_empty()) {
        return NULL;
    }
    return media_field_.c_str();
}

bool MediaDescription::set_media(const char* str, size_t str_len) {
    if (!media_field_.set_buf(str, str_len) || media_field_.is_empty()) {
        return false;
    }

    return true;
}

bool MediaDescription::add_connection_field(address::AddrFamily addrtype, const char* str, size_t str_len) {
    core::SharedPtr<ConnectionField> c = new (allocator_) ConnectionField(allocator_);

    if (!c->set_connection_address(addrtype, str, str_len)) {
        return false;
    }

    connection_fields_.push_back(*c);
    return true;
}

void MediaDescription::destroy() {
    allocator_.destroy(*this);
}

} // namespace roc
} // namespace sdp