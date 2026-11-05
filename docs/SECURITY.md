# Security

## The Gemini key never reaches the device

```
Android app ──HTTPS + app token──► wechess_proxy ──HTTPS + GEMINI_API_KEY──► Google
```

- `GEMINI_API_KEY` lives only in the proxy process environment. It is not in the
  repository, the APK, the WebView, the app's data directory, or any log line.
- The app authenticates to the proxy with `WECHESS_PROXY_TOKEN`, an independent
  shared token. Rotating it does not touch the Gemini key.
- The proxy enforces a model allowlist (`GEMINI_MODELS`), a JSON body size cap and a
  per-client request rate limit (`REQUESTS_PER_MINUTE`).
- Proxy error responses never include the upstream key; the client maps `401`,
  `429` and `503` to their own messages.

If a key is ever exposed, revoke it in Google AI Studio; the app needs no change.

## Local server hardening

- Binds `127.0.0.1` only; there is no configuration to expose it on a LAN.
- One request per connection, `Connection: close`, request size capped (8 MB),
  malformed requests answered with `400` and closed.
- `Content-Security-Policy: default-src 'none'; script-src 'none'; style-src 'self'
  'unsafe-inline'` on every HTML response — even if markup injection happened, no
  script could run.
- All dynamic text is HTML-escaped (`htmlEscape`) at render time; header values from
  imported PGNs are treated as hostile input (there is a test for it).
- Game ids are validated before use in filesystem paths, so `../` sequences cannot
  escape `games/`.
- Responses are `no-store`; no cookies, no sessions, no authentication on the local
  interface (it serves only the same user's own games).

### Residual risk: other apps on the device

Android has a single loopback interface, so another app on the same device can
connect to `127.0.0.1:<port>`. Mitigations in place: the port is chosen randomly at
startup, the server lives only while the app process lives, it exposes no secret
(the proxy token is never returned by any page or endpoint), and destructive
actions require a POST that the attacker's app would have to guess the game id for.
The remaining exposure is reading locally stored games — the same data the user can
see. A per-install secret embedded in URL paths would raise the bar further.

## Network

- `android:usesCleartextTraffic="false"` with a network security config that permits
  cleartext **only** for `127.0.0.1`/`localhost`, because the UI itself is served
  over loopback.
- The proxy URL must be `https://` unless it is loopback (development).
- `INTERNET` permission is required for the loopback UI and for the proxy call.

## WebView

`MainActivity` disables JavaScript, JavaScript-opened windows, DOM storage, database
storage, file access, file-URL access, universal access from file URLs, geolocation
and cookies. `shouldOverrideUrlLoading` allows only `http://127.0.0.1:<port>/` and
`http://localhost:<port>/`; everything else is dropped rather than opened in an
external app.

Content access stays enabled for one reason: the system file chooser hands a
`content://` URI to the page's `<input type="file">`, and the WebView must read it.
The app itself never navigates to `content://` URLs.

## Storage

- Settings and games live in the app's private data directory (`filesDir`), not on
  shared storage, and `allowBackup=false` keeps them out of cloud backups.
- The proxy token is stored there in plain text. It is a low-value, revocable shared
  secret; the Gemini key is not. Moving it to the Android Keystore would require
  more Kotlin code than the JNI scope allows.
- Logs (`__android_log_print`) contain only lifecycle events and port numbers —
  never tokens, prompts, PGNs or engine output.

## Reporting

For a security issue in this repository, open an issue without exploit details, or
contact the maintainer privately.
