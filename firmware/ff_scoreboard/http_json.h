#pragma once
// -----------------------------------------------------------------------------
// http_json.h - one HTTPS helper shared by the fantasy fetches.
//
// Key behaviour:
//   * Requests use HTTP/1.1 with Connection: close and an identity-preferred
//     Accept-Encoding (never gzip).
//   * The response is NEVER buffered in RAM. It is streamed straight into
//     ArduinoJson and only the fields allowed by `filter` are materialised.
//   * Framing (Content-Length or chunked) is decoded on the fly by
//     HttpBodyStream (http_body_stream.h). Raw-stream parsing alone breaks on
//     the ESP32 for close-delimited bodies, and chunked bodies can't be parsed
//     from the raw socket either.
// -----------------------------------------------------------------------------
#include <ArduinoJson.h>
#include <HTTPClient.h>  // HTTP_CODE_OK, used by the fetch result mapping

struct HttpFetch {
  int         status;      // HTTP status code, or negative for local/connect failures
  bool        parsed;      // true if the filtered JSON stream parsed completely
  const char* parseError;  // ArduinoJson error string (only meaningful if !parsed)
  long        bodyLen;     // Content-Length advertised by the server, or -1
};

// -1  -> Wi-Fi not connected
// -2  -> HTTPClient::begin() failed
// otherwise the real HTTP status code.
HttpFetch httpFetchJson(const char* url, JsonDocument& filter, JsonDocument& out);
