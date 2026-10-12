// SPDX-License-Identifier: AGPL-3.0-or-later
// Picking a Home Assistant sensor for a measuring role in one step, so the
// web app never leaves half of it done: select the entity, accept the device
// under its Home Assistant name and bind the role; only then is the role's
// former sensor dropped. Taking the sensor away again unbinds the role,
// removes the device and stops reading it. The picks live in the hub's
// configuration (devices ha.<entity> of class ha_<measure>, ha_ph_raw for a
// pH probe's voltage, SD-041); at start they are handed back to the bus.
#pragma once

#include <functional>
#include <string>

#include "gc/api.hpp"
#include "gc/hub.hpp"
#include "ha_bus.hpp"

namespace ha {

// entity "" takes the role's Home Assistant sensor away. letHubSee is called
// while waiting for the hub to see a newly selected device (the server
// sleeps for a moment; a test ticks the hub). Never called with the hub's
// lock held.
gc::Result assign(gc::Hub& hub, HaBus& bus, const std::string& role, const std::string& entity,
                  const std::function<void()>& letHubSee);

// The picks in the hub's configuration, handed to the bus at start.
void adoptFromConfig(gc::Hub& hub, HaBus& bus);
// The signs of life in the hub's configuration handed to the bus (SD-042):
// every tick, so an import or a change in the web app takes effect at once.
// Called with the hub's lock held.
void syncLife(gc::Hub& hub, HaBus& bus);
// A role's sign of life (SD-042): source "" removes it; a sign-of-life text
// needs a channel it lists now, a timestamp sensor none.
gc::Result setLife(gc::Hub& hub, HaBus& bus, const std::string& role, const std::string& source, const std::string& channel);

// The server's routes without the HTTP layer: signed in, then the work.
// GET /api/v1/ha/candidates (?others=1 adds the other sensors, SD-036),
// POST /api/v1/ha/assign {"role", "entity"} and POST /api/v1/ha/life
// {"role", "source", "channel"} (SD-042).
gc::Result candidatesRoute(gc::Api& api, const HaBus& bus, const gc::ApiRequest& req);
gc::Result assignRoute(gc::Api& api, gc::Hub& hub, HaBus& bus, const gc::ApiRequest& req,
                       const std::function<void()>& letHubSee);
gc::Result lifeRoute(gc::Api& api, gc::Hub& hub, HaBus& bus, const gc::ApiRequest& req);

}  // namespace ha
