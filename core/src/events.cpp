// SPDX-License-Identifier: AGPL-3.0-or-later
#include "gc/events.hpp"

#include <algorithm>

namespace gc {

namespace {

// A saved message; events written before SD-032 hold plain German text,
// which stays as it is (no key).
Msg msgFromJson(const json& e, const char* field) {
  if (!e.contains(field)) return {};
  const json& v = e[field];
  if (v.is_string()) return {"", v.get<std::string>(), json::object()};
  if (!v.is_object()) return {};
  Msg m{jstr(v, "key"), jstr(v, "text"), json::object()};
  if (v.contains("args") && v["args"].is_object()) m.args = v["args"];
  return m;
}

}  // namespace

void to_json(json& j, const Event& e) {
  j = {{"id", e.id}, {"ts", e.ts}, {"type", e.type}, {"severity", e.severity},
       {"title", e.title}, {"text", e.text}, {"data", e.data}};
}

Epoch EventLog::newestTs() const {
  Epoch t = 0;
  for (const auto& e : events_) t = std::max(t, e.ts);
  return t;
}

const Event& EventLog::add(Epoch ts, std::string type, std::string severity, Msg title, Msg text, json data) {
  Event e;
  e.id = next_++;
  e.ts = ts;
  e.type = std::move(type);
  e.severity = std::move(severity);
  e.title = std::move(title);
  e.text = std::move(text);
  e.data = std::move(data);
  events_.push_back(std::move(e));
  while (events_.size() > cap_) events_.pop_front();
  return events_.back();
}

std::vector<Event> EventLog::query(Epoch from, Epoch to, const std::string& type, size_t limit) const {
  std::vector<Event> out;
  for (auto it = events_.rbegin(); it != events_.rend() && out.size() < limit; ++it) {
    if (it->ts < from || it->ts > to) continue;
    if (!type.empty() && it->type != type) continue;
    out.push_back(*it);
  }
  return out;  // neueste zuerst
}

json EventLog::toJson() const {
  json arr = json::array();
  for (const auto& e : events_) arr.push_back(e);
  return {{"next", next_}, {"events", arr}};
}

void EventLog::load(const json& j) {
  events_.clear();
  // Fremde Datei: nur geprüft lesen, kaputte Einträge überspringen (kein Absturz beim Start).
  double next = jnum(j, "next", 1);
  next_ = isNum(next) && next >= 1 ? static_cast<std::uint64_t>(next) : 1;
  const json events = j.is_object() && j.contains("events") && j["events"].is_array() ? j["events"] : json::array();
  for (const auto& e : events) {
    if (!e.is_object()) continue;
    Event ev;
    double id = jnum(e, "id", 0), ts = jnum(e, "ts", 0);
    ev.id = isNum(id) && id >= 0 && id < 9e15 ? static_cast<std::uint64_t>(id) : 0;
    // A time outside any plausible range (a broken file) is not carried
    // over: it would anchor the clock after a restart (PD-069).
    ev.ts = isNum(ts) && ts >= 0 && ts <= static_cast<double>(kNotAfter) ? static_cast<Epoch>(ts) : 0;
    ev.type = jstr(e, "type");
    ev.severity = jstr(e, "severity");
    ev.title = msgFromJson(e, "title");
    ev.text = msgFromJson(e, "text");
    ev.data = e.contains("data") && e["data"].is_object() ? e["data"] : json::object();
    events_.push_back(ev);
  }
}

}  // namespace gc
