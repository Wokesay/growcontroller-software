// SPDX-License-Identifier: AGPL-3.0-or-later
// Texts the hub sends (SD-032): key, arguments and English text.
#include <doctest/doctest.h>

#include "gc/messages.hpp"

using namespace gc;

TEST_CASE("Messages: numbers with and without decimals, a missing value is never 0") {
  CHECK(render("{a:2} {b} {c:0}", {{"a", 5.8}, {"b", 1.5}, {"c", 12.4}}) == "5.80 1.5 12");
  CHECK(render("{n} rounds", {{"n", 3}}) == "3 rounds");
  CHECK(render("{a:1} and {b}", {{"a", nullptr}}) == "– and –");  // R5
  CHECK(render("{a:2}", {{"a", kNaN}}) == "–");
  CHECK(render("{name}", {{"name", "Part A"}}) == "Part A");
}

TEST_CASE("Messages: braces that are no placeholder stay as they are") {
  CHECK(render("{ x} {x {} {x:a}", {{"x", 1}}) == "{ x} {x {} {x:a}");
  CHECK(render("no args", json::object()) == "no args");
}

TEST_CASE("Messages: a message inside an argument gives its text") {
  Msg inner = say("ph.no_down");
  Msg m = say("ec.invalid", {{"reason", inner}});
  CHECK(m.key == "ec.invalid");
  CHECK(m.text == "Blocked: EC – Blocked: no calibrated pH− assigned");
  CHECK(m.args["reason"]["key"] == "ph.no_down");
}

TEST_CASE("Messages: key and arguments travel with the English text") {
  Msg m = say("ec.ok", {{"ec", 1.4}});
  CHECK(m.text == "Resting: EC on target (1.40 mS/cm)");
  json j = m;
  CHECK(j["key"] == "ec.ok");
  CHECK(j["args"]["ec"] == 1.4);
  CHECK(knownMessage("ec.ok"));
}

TEST_CASE("Messages: an unknown key shows the key, never an empty text") {
  CHECK_FALSE(knownMessage("no.such.key"));
  CHECK(say("no.such.key").text == "no.such.key");
}
