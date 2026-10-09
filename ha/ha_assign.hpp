// SPDX-License-Identifier: AGPL-3.0-or-later
// Picking a Home Assistant sensor for a measuring role in one step, so the
// web app never leaves half of it done: select the entity, accept the device
// under its Home Assistant name and bind the role. Taking the sensor away
// again unbinds the role, removes the device and deselects the entity.
#pragma once

#include <functional>
#include <string>

#include "gc/hub.hpp"
#include "ha_bus.hpp"

namespace ha {

// entity "" takes the role's Home Assistant sensor away. letHubSee is called
// while waiting for the hub to see a newly selected device (the server
// sleeps for a moment; a test ticks the hub). Never called with the hub's
// lock held.
gc::Result assign(gc::Hub& hub, HaBus& bus, const std::string& role, const std::string& entity,
                  const std::function<void()>& letHubSee);

}  // namespace ha
