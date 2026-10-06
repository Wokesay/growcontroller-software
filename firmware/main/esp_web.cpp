// Bindung der transportneutralen API an esp_http_server, dazu die
// eingebettete Web-App. Live-Daten: Die Web-App fällt ohne SSE auf Abfragen
// alle 3 s zurück; SSE über asynchrone Handler folgt (firmware/README.md).
#include <cstring>
#include <string>

#include <esp_http_server.h>
#include <esp_log.h>

#include "esp_platform.hpp"
#include "web_assets.hpp"

namespace gcfw {

namespace {

constexpr const char* kTag = "gc.web";
constexpr size_t kMaxBody = 64 * 1024;
gc::Api* g_api = nullptr;

const char* statusLine(int s) {
  switch (s) {
    case 200: return "200 OK";
    case 400: return "400 Bad Request";
    case 401: return "401 Unauthorized";
    case 403: return "403 Forbidden";
    case 404: return "404 Not Found";
    case 408: return "408 Request Timeout";
    case 409: return "409 Conflict";
    case 413: return "413 Payload Too Large";
    case 422: return "422 Unprocessable Entity";
    case 423: return "423 Locked";
    case 429: return "429 Too Many Requests";
    case 501: return "501 Not Implemented";
    case 502: return "502 Bad Gateway";
    default: return "500 Internal Server Error";
  }
}

const char* methodName(int m) {
  switch (m) {
    case HTTP_GET: return "GET";
    case HTTP_POST: return "POST";
    case HTTP_PUT: return "PUT";
    case HTTP_PATCH: return "PATCH";
    case HTTP_DELETE: return "DELETE";
    default: return "";
  }
}

std::string urlDecode(const std::string& s) {
  std::string out;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '+') out += ' ';
    else if (s[i] == '%' && i + 2 < s.size()) {
      out += static_cast<char>(std::strtol(s.substr(i + 1, 2).c_str(), nullptr, 16));
      i += 2;
    } else out += s[i];
  }
  return out;
}

void securityHeaders(httpd_req_t* req) {
  httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
  httpd_resp_set_hdr(req, "X-Frame-Options", "DENY");
  httpd_resp_set_hdr(req, "Referrer-Policy", "no-referrer");
  httpd_resp_set_hdr(req, "Content-Security-Policy",
                     "default-src 'self'; img-src 'self' data:; style-src 'self' 'unsafe-inline'; connect-src 'self'; "
                     "frame-ancestors 'none'");
}

std::string header(httpd_req_t* req, const char* name) {
  size_t len = httpd_req_get_hdr_value_len(req, name);
  if (len == 0) return {};
  std::string v(len + 1, '\0');
  if (httpd_req_get_hdr_value_str(req, name, v.data(), v.size()) != ESP_OK) return {};
  v.resize(len);
  return v;
}

std::string tokenOf(httpd_req_t* req) {
  std::string auth = header(req, "Authorization");
  if (auth.rfind("Bearer ", 0) == 0) return auth.substr(7);
  std::string cookie = header(req, "Cookie");
  auto pos = cookie.find("gc_session=");
  if (pos == std::string::npos) return {};
  auto end = cookie.find(';', pos);
  return cookie.substr(pos + 11, end == std::string::npos ? std::string::npos : end - pos - 11);
}

esp_err_t sendError(httpd_req_t* req, int status, const char* body) {
  httpd_resp_set_status(req, statusLine(status));
  httpd_resp_set_type(req, "application/json");
  securityHeaders(req);
  return httpd_resp_sendstr(req, body);
}

esp_err_t apiHandler(httpd_req_t* req) {
  gc::ApiRequest r;
  r.method = methodName(req->method);
  // Herkunft prüfen (DNS-Rebinding, CSRF), bevor der Kern die Anfrage sieht.
  const std::string host = header(req, "Host");
  if (!gc::hostAllowed(host) ||
      !gc::writeAllowed(r.method, header(req, "Sec-Fetch-Site"), header(req, "Origin"), host))
    return sendError(req, 403, "{\"error\":{\"key\":\"api.origin\",\"text\":\"Anfrage von fremder Herkunft abgelehnt\"}}");
  std::string uri = req->uri;
  auto q = uri.find('?');
  r.path = uri.substr(0, q);
  if (q != std::string::npos) {
    std::string rest = uri.substr(q + 1);
    size_t pos = 0;
    while (pos <= rest.size()) {
      size_t amp = rest.find('&', pos);
      std::string kv = rest.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
      auto eq = kv.find('=');
      if (eq != std::string::npos) r.query[urlDecode(kv.substr(0, eq))] = urlDecode(kv.substr(eq + 1));
      if (amp == std::string::npos) break;
      pos = amp + 1;
    }
  }
  if (req->content_len > kMaxBody)
    return sendError(req, 413, "{\"error\":{\"key\":\"api.too_large\",\"text\":\"Anfrage zu groß\"}}");
  r.body.resize(req->content_len);
  size_t got = 0;
  int timeouts = 0;
  while (got < req->content_len) {
    int n = httpd_req_recv(req, r.body.data() + got, req->content_len - got);
    if (n <= 0) {
      // Langsame oder stumme Gegenstelle darf den einzigen Server-Task nicht blockieren.
      if (n == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts < 3) continue;
      if (n == HTTPD_SOCK_ERR_TIMEOUT)
        return sendError(req, 408, "{\"error\":{\"key\":\"api.timeout\",\"text\":\"Anfrage unvollständig\"}}");
      return ESP_FAIL;
    }
    got += static_cast<size_t>(n);
  }
  r.token = tokenOf(req);
  gc::ApiResponse res = g_api->handle(r);
  httpd_resp_set_status(req, statusLine(res.status));
  httpd_resp_set_type(req, res.contentType.c_str());
  securityHeaders(req);
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  for (const auto& [k, v] : res.headers) httpd_resp_set_hdr(req, k.c_str(), v.c_str());
  return httpd_resp_send(req, res.body.data(), static_cast<ssize_t>(res.body.size()));
}

esp_err_t staticHandler(httpd_req_t* req) {
  std::string path = req->uri;
  auto q = path.find('?');
  if (q != std::string::npos) path.resize(q);
  const WebAsset* hit = nullptr;
  const WebAsset* index = nullptr;
  for (size_t i = 0; i < kWebAssetCount; ++i) {
    if (path == kWebAssets[i].path) hit = &kWebAssets[i];
    if (std::strcmp(kWebAssets[i].path, "/index.html") == 0) index = &kWebAssets[i];
  }
  if (!hit) hit = index;  // Single-Page-App: unbekannte Pfade → index.html
  if (!hit) return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Web-App fehlt");
  httpd_resp_set_type(req, hit->type);
  httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
  httpd_resp_set_hdr(req, "Cache-Control", hit == index ? "no-cache" : "public, max-age=31536000, immutable");
  securityHeaders(req);
  return httpd_resp_send(req, reinterpret_cast<const char*>(hit->data), static_cast<ssize_t>(hit->size));
}

}  // namespace

void startWebServer(gc::Api& api) {
  g_api = &api;
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.uri_match_fn = httpd_uri_match_wildcard;
  config.max_uri_handlers = 8;
  config.stack_size = 12288;
  config.lru_purge_enable = true;
  httpd_handle_t server = nullptr;
  if (httpd_start(&server, &config) != ESP_OK) {
    ESP_LOGE(kTag, "Webserver startet nicht");
    return;
  }
  const httpd_method_t methods[] = {HTTP_GET, HTTP_POST, HTTP_PUT, HTTP_PATCH, HTTP_DELETE};
  for (httpd_method_t m : methods) {
    httpd_uri_t u = {};
    u.uri = "/api/*";
    u.method = m;
    u.handler = apiHandler;
    httpd_register_uri_handler(server, &u);
  }
  httpd_uri_t s = {};
  s.uri = "/*";
  s.method = HTTP_GET;
  s.handler = staticHandler;
  httpd_register_uri_handler(server, &s);
  ESP_LOGI(kTag, "Webserver läuft (%u Dateien der Web-App eingebettet)", static_cast<unsigned>(kWebAssetCount));
}

}  // namespace gcfw
