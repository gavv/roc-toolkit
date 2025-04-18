/*
 * Copyright (c) 2022 Roc Streaming authors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "roc_core/allocation_policy.h"

namespace roc {
namespace core {

ArenaAllocation::~ArenaAllocation() {
}

PoolAllocation::~PoolAllocation() {
}

NoopAllocation::~NoopAllocation() {
}

} // namespace core
} // namespace roc
