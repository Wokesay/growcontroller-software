#include "gc/events.hpp"

namespace gc {

void to_json(json& j, const Event& e) {
  j = {{"id", e.id}, {"ts", e.ts}, {"type", e.type}, {"severity", e.severity},
       {"title", e.title}, {"text", e.text}, {"data", e.data}};
}

const Event& EventLog::add(Epoch ts, std::string type, std::string severity, std::string title, std::string text,
                           json data) {
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
  next_ = j.value("next", std::uint64_t{1});
  for (const auto& e : j.value("events", json::array())) {
    Event ev;
    ev.id = e.value("id", std::uint64_t{0});
    ev.ts = e.value("ts", Epoch{0});
    ev.type = jstr(e, "type");
    ev.severity = jstr(e, "severity");
    ev.title = jstr(e, "title");
    ev.text = jstr(e, "text");
    ev.data = e.value("data", json::object());
    events_.push_back(ev);
  }
}

}  // namespace gc
