#pragma once
// -----------------------------------------------------------------------------
// http_body_stream.h - decodes an HTTP response body framing on the fly.
//
// Why this exists: on the ESP32, raw-stream parsing only works reliably when the
// server supplies a Content-Length. ESPN's reply framing varies:
//   * HTTP/1.1 -> Transfer-Encoding: chunked (no Content-Length)
//   * large HTTP/1.0 replies -> close-delimited (no Content-Length), which the
//     ESP32 TLS stack delivers truncated (ArduinoJson reports IncompleteInput)
//   * small replies -> Content-Length (works)
// This Stream adaptor decodes both Content-Length and chunked framing so the
// response can be streamed straight into ArduinoJson in every case. It is the
// ONLY consumer needed by ArduinoJson's Stream reader, which pulls bytes via
// readBytes() (implemented by the base Stream class in terms of read()).
// -----------------------------------------------------------------------------
#include <Arduino.h>

class HttpBodyStream : public Stream {
 public:
  // knownLength is the response Content-Length, or -1 when absent (=> chunked).
  HttpBodyStream(Stream& raw, long knownLength)
      : _raw(raw),
        _chunked(knownLength < 0),
        _remaining(knownLength),
        _chunkRemaining(0),
        _state(_chunked ? STATE_CHUNK_SIZE : STATE_DONE),
        _hdrLen(0) {
    setTimeout(15000);  // per-byte budget for Stream::timedRead()
  }

  int available() override { return _raw.available(); }
  int peek() override { return -1; }  // unused by ArduinoJson's stream reader
  size_t write(uint8_t) override { return 0; }
  size_t write(const uint8_t*, size_t) override { return 0; }

  int read() override {
    if (_chunked) return readChunked();

    // Fixed-length (Content-Length) framing.
    if (_remaining <= 0) return -1;
    int b = _raw.read();
    if (b >= 0) _remaining--;
    return b;
  }

 private:
  static constexpr uint8_t STATE_CHUNK_SIZE = 0;
  static constexpr uint8_t STATE_CHUNK_DATA = 1;
  static constexpr uint8_t STATE_CHUNK_EOL  = 2;  // CRLF after chunk data
  static constexpr uint8_t STATE_TRAILERS   = 3;  // after the terminal 0 chunk
  static constexpr uint8_t STATE_DONE       = 4;

  int readChunked() {
    for (;;) {
      if (_state == STATE_CHUNK_SIZE) {
        int b = _raw.read();
        if (b < 0) return -1;                  // connection failed/timed out
        if (b == '\n') {
          long size = parseHex(_hdr, _hdrLen); // also clears extensions ";..."
          _hdrLen = 0;
          if (size == 0) {
            _state = STATE_TRAILERS;           // terminal chunk, read trailers
            continue;
          }
          _chunkRemaining = size;
          _state = STATE_CHUNK_DATA;
          continue;
        }
        if (_hdrLen < sizeof(_hdr) - 1 && b != '\r') _hdr[_hdrLen++] = (char)b;
        continue;
      }

      if (_state == STATE_CHUNK_DATA) {
        if (_chunkRemaining <= 0) {
          _state = STATE_CHUNK_EOL;            // chunk body consumed
          continue;
        }
        int b = _raw.read();
        if (b < 0) return -1;
        _chunkRemaining--;
        if (_chunkRemaining == 0) _state = STATE_CHUNK_EOL;
        return b;
      }

      if (_state == STATE_CHUNK_EOL) {         // consume trailing CRLF
        int b = _raw.read();
        if (b < 0) return -1;
        if (b == '\n') _state = STATE_CHUNK_SIZE;
        continue;
      }

      if (_state == STATE_TRAILERS) {          // blank line ends trailers
        int b = _raw.read();
        if (b < 0) return -1;
        if (b == '\n') {
          _state = STATE_DONE;
          return -1;                           // clean end of body
        }
        if (b == '\r') {                       // could be the empty line "\r\n"
          b = _raw.read();
          if (b == '\n') {
            _state = STATE_DONE;
            return -1;
          }
        }
        while (b != '\n' && b >= 0) b = _raw.read();  // skip a real trailer line
        continue;
      }

      return -1;                               // STATE_DONE
    }
  }

  static long parseHex(const char* s, size_t len) {
    long v = 0;
    for (size_t i = 0; i < len; i++) {
      char c = s[i];
      int d;
      if (c >= '0' && c <= '9') d = c - '0';
      else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
      else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
      else break;                              // chunk extension after ';'
      v = v * 16 + d;
    }
    return v;
  }

  Stream& _raw;
  bool     _chunked;
  long     _remaining;      // bytes left (fixed-length mode)
  long     _chunkRemaining; // bytes left in the current chunk
  uint8_t  _state;
  char     _hdr[32];        // chunk-size line buffer
  size_t   _hdrLen;
};