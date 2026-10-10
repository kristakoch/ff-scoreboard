#include "http_json.h"
#include "config.h"
#include "http_body_stream.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

HttpFetch httpFetchJson(const char* url, JsonDocument& filter, JsonDocument& out) {
  HttpFetch r;
  r.status = 0;
  r.parsed = false;
  r.parseError = "";
  r.bodyLen = -1;

  if (WiFi.status() != WL_CONNECTED) {
    r.status = -1;
    Serial.printf("[http] %s: Wi-Fi not connected\n", url);
    return r;
  }

  // Plain TLS without certificate validation. Acceptable here because every
  // endpoint is read-only public data (no credentials, nothing to protect).
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.setReuse(false);   // Connection: close -> one response, then server closes
  http.setConnectTimeout(HTTP_CONNECT_MS);
  http.setTimeout(HTTP_READ_MS);
  http.setUserAgent("esp32-ff-scoreboard/1.0");

  if (!http.begin(client, url)) {
    r.status = -2;
    Serial.printf("[http] %s: begin()/DNS failed\n", url);
    return r;
  }

  r.status = http.GET();
  r.bodyLen = http.getSize();  // -1 when chunked -> HttpBodyStream decodes chunks

  if (r.status == HTTP_CODE_OK) {
    // Filtered streaming parse: ArduinoJson tokenises the whole response but
    // allocates storage only for the keys kept by `filter`. The nesting limit
    // is raised because skipped (filtered-out) subtrees are still traversed.
    // The HttpBodyStream decodes Content-Length or chunked framing so the raw
    // socket never has to be read to connection-close (which the ESP32 TLS
    // stack delivers truncated for large close-delimited bodies).
    HttpBodyStream body(http.getStream(), r.bodyLen);
    DeserializationError err = deserializeJson(
        out, body,
        DeserializationOption::Filter(filter),
        DeserializationOption::NestingLimit(HTTP_JSON_NESTING_LIMIT));
    r.parsed = (err == DeserializationError::Ok);
    r.parseError = err.c_str();
  }
  http.end();

  Serial.printf("[http] %-32s status=%d len=%ld parsed=%s%s%s\n",
                url, r.status, r.bodyLen,
                r.parsed ? "yes" : "no",
                r.parsed ? "" : "  parseError=",
                r.parsed ? "" : r.parseError);
  return r;
}
