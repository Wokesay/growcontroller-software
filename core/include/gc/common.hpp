// Gemeinsame Typen des Kerns. Der Kern ist plattformneutral: keine
// ESP-IDF-, POSIX- oder Netzwerk-Header in core/ (Architekturtest prüft das).
#pragma once

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace gc {

using json = nlohmann::json;

// Monotone Zeit in Millisekunden (Gerät: seit Start; Simulator: virtuelle Zeit).
using Ms = std::int64_t;
// Wanduhr in Sekunden seit 1970 (für Verlauf und Ereignisse).
using Epoch = std::int64_t;

constexpr Ms kSecond = 1000;
constexpr Ms kMinute = 60 * kSecond;
constexpr Ms kHour = 60 * kMinute;

// Ein fehlender Wert ist nie 0 (Quelle: RAT-006). Fehlende Zahlen sind
// std::nullopt oder NaN und werden in JSON als null ausgegeben.
inline constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
inline bool isNum(double v) { return std::isfinite(v); }

// Uhr wird injiziert (Regel R8): deterministische Tests, Zeitraffer im Simulator.
class IClock {
 public:
  virtual ~IClock() = default;
  virtual Ms nowMs() const = 0;
  virtual Epoch epoch() const = 0;
};

// Klartext mit Schlüssel für spätere Übersetzung (Texte über Schlüssel mit
// Argumenten, Vorschlag architekt). Der Kern liefert vorerst Deutsch.
struct Msg {
  std::string key;
  std::string text;
  json args = json::object();
};
void to_json(json& j, const Msg& m);

// JSON-Hilfen: Zahl oder null, nie 0 als Ersatz.
inline json numOrNull(double v) { return isNum(v) ? json(v) : json(nullptr); }
inline json numOrNull(const std::optional<double>& v) {
  return v && isNum(*v) ? json(*v) : json(nullptr);
}
double jnum(const json& j, const char* key, double fallback = kNaN);
std::string jstr(const json& j, const char* key, const std::string& fallback = "");
bool jbool(const json& j, const char* key, bool fallback);

// Deutsches Zahlformat für Klartexte ("5,8").
std::string fmt(double v, int decimals);

}  // namespace gc
