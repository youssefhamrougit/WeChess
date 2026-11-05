# Gemini proxy

WeChess asks Gemini for prose only. Evaluations, best moves, principal variations
and classifications always come from Stockfish. The proxy exists so that the
Gemini API key can live on a server instead of inside an APK.

## 1. Build and run

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DWECHESS_WITH_CURL=ON
cmake --build build -j"$(nproc)" --target wechess_proxy

export GEMINI_API_KEY="$(cat /run/secrets/gemini_key)"   # never commit this
export WECHESS_PROXY_TOKEN="$(openssl rand -hex 24)"     # >= 16 characters
export GEMINI_MODEL=gemini-2.5-flash
export GEMINI_MODELS=gemini-2.5-flash,gemini-2.5-pro     # allowlist
export REQUESTS_PER_MINUTE=20
export BIND_ADDRESS=127.0.0.1     # put a TLS terminator in front
export PORT=8787
./build/wechess_proxy
```

Environment reference:

| Variable | Meaning |
| --- | --- |
| `GEMINI_API_KEY` | Google AI Studio key. Required; the proxy exits without it. |
| `WECHESS_PROXY_TOKEN` | Shared app token (≥16 chars). Required. |
| `GEMINI_MODEL` | Default model when the client does not ask for one. |
| `GEMINI_MODELS` | Comma-separated allowlist of models the proxy will forward. |
| `GEMINI_API_BASE` | Upstream base URL override, used for the mock test double. |
| `REQUESTS_PER_MINUTE` | Per-client rate limit. |
| `BIND_ADDRESS`, `PORT` | Listen address and port. |

## 2. API

`POST /v1/explain`

```http
Authorization: Bearer <WECHESS_PROXY_TOKEN>
Content-Type: application/json

{"model": "gemini-2.5-flash", "prompt": "..."}
```

Success: `200 {"text": "..."}`
Failure: `4xx/5xx {"error": "..."}` with `401` for a bad token, `429` when rate
limited, `503` when the upstream key is missing or Gemini is unreachable.

`GET /healthz` returns `200 ok` for load balancers.

## 3. Deploy

TLS termination is not optional for remote use: put the proxy behind Caddy, nginx
or a cloud load balancer, because the app refuses plain-http proxy URLs except on
loopback.

Minimal Caddyfile:

```
wechess-proxy.example.com {
    reverse_proxy 127.0.0.1:8787
}
```

systemd unit sketch:

```ini
[Service]
ExecStart=/opt/wechess/wechess_proxy
EnvironmentFile=/etc/wechess/proxy.env
DynamicUser=yes
NoNewPrivileges=yes
PrivateTmp=yes
ProtectSystem=strict
Restart=on-failure
```

Keep the env file mode `0600`, owned by the service user, and never inside the repo.

## 4. Configure the app

WeChess → Settings:

- **Proxy URL**: `https://wechess-proxy.example.com`
- **App token**: the `WECHESS_PROXY_TOKEN` value
- **Model**: one of the allowlisted models
- **Enable Gemini explanations**: on

Only the app token is stored on the device. The Gemini key is not.

Then on any move: **Explain this move** → a wait page → the explanation appears in
the review panel. If the proxy is down, offline or misconfigured, the panel shows
the specific reason and the Stockfish review stays fully usable.

## 5. Testing without a key

`GEMINI_API_BASE` points the proxy at a local mock, and `test_gemini` does the same
from the app side:

```bash
GEMINI_API_BASE=http://127.0.0.1:9099 \
GEMINI_API_KEY=dummy WECHESS_PROXY_TOKEN=0123456789abcdef \
./build/wechess_proxy
```

## 6. Rotation

1. Generate a new token, update the service environment, restart the proxy.
2. Paste the new token into WeChess settings.

Rotating the token does not require a new APK, and the Gemini key is untouched.
To rotate the Gemini key, change it in AI Studio, update the env file, restart.
