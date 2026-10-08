# API v1

As of 2026-10-06. REST with JSON under `/api/v1`, transport-neutral in the
core (`core/src/api.cpp`). The web app uses only this API. So whatever the
UI can do, an integration can do as well.

## Access

- `GET /info` is public: version, platform, whether a password is set.
- `POST /auth/setup {password}` sets the initial password, exactly once
  (otherwise 409). At least 8 characters.
- `POST /auth/login {password}` sets the cookie `gc_session` (HttpOnly,
  SameSite=Strict). After 5 failed attempts the answer is 429 with a wait
  time (30 s, doubling up to 15 min).
- `POST /auth/logout`. `PUT /auth/password {old,new}` logs out all
  sessions.
- Everything else requires a session (cookie or `Authorization: Bearer`),
  otherwise 401.
- Security headers on the server: CSP `default-src 'self'`,
  `X-Frame-Options: DENY`, `nosniff`, `no-referrer`.
- **Origin:** the `Host` header must be an IP, `localhost` or a name in the
  home network (`.local`, `.lan`, `.home.arpa`, `.internal`, `.fritz.box`;
  against DNS rebinding). Write requests from a browser are accepted only
  from the same origin (`Sec-Fetch-Site`, otherwise `Origin` against
  `Host`), otherwise 403 `api.origin`.
- **Errors:** malformed input 400 `api.bad_input`, internal error 500
  `api.internal`; the server keeps running. If the password is lost
  (`auth.json` is missing although a password was set), `/auth/setup`
  refuses with 423 `auth.lost`: a factory reset on the device is needed.
- **Import** (`POST /config/import`) validates like every change, plus
  limits, phase parameters and calibration data; 409 while a job runs.

## Read

| Method | Path | Content |
|---|---|---|
| GET | `/state` | live state: ports, devices, readings with quality and reason, tank, controllers with control line and checklist, outputs, job, dosing, watchdog (headline and items as messages, with `stale`), functions (resolver), latches, stock, cultivation run; `time`: `secured`, `source` (`secured`, `continued` from the saved time, `unset`) and `operatingS` (PD-069) |
| GET | `/events/stream` | Server-Sent Events: `event: state` every 1 s |
| GET | `/config`, `/config/export` | configuration without secrets |
| GET | `/catalog` | catalog of the firmware |
| GET | `/history?series=a,b&from&to&points` | series (`t`, `avg`, `min`, `max`, `stepS`) |
| GET | `/events?type&from&to&limit` | events, newest first |
| GET | `/export.csv?series&from&to` | CSV |
| GET | `/diagnostics` | diagnostic bundle (without hash, sessions, Wi-Fi, IP) |
| GET | `/changelog` | embedded CHANGELOG (Markdown) |
| GET | `/update` | version, available version with a summary |

## Set up

| Method | Path | |
|---|---|---|
| POST | `/setup/complete` | setup completed |
| PUT | `/system` | name, time zone, language (`de`/`en`), update channel and update check, cap per manual dose |
| POST / PATCH / DELETE | `/devices/{id}/accept`, `/devices/{id}` | accept, rename, remove |
| PUT / DELETE | `/roles/{rolle}` `{device, channel}` | assignment; for mains sockets only after the protection setting is confirmed (otherwise 502) |
| POST | `/roles/{rolle}/test` | switched output on for 3 s |
| POST | `/roles/{rolle}/switch` `{on}` | manual operation, with all locks (otherwise 409 with the reason) |
| PUT | `/tank` | name, usable volume, minimum level, water, volume without a level head |
| PUT | `/zone` | growing area: name, kind (`room`, `tent`, `greenhouse`) |
| POST / DELETE | `/canisters`, `/canisters/{id}`, `/canisters/{id}/stock` | canisters, stock |
| POST / DELETE | `/recipes`, `/recipes/template` `{id, map, lang}`, `/recipes/{id}` | recipes; template with a mapping part → canister (422 with `missing`, named in the language); `lang` `de` or `en` picks name, note and part names (default: the system language; other values: 422 `recipe.template.lang`), part names match in either language, the one in `lang` first |
| PATCH | `/functions/{id}` `{enabled, params}` | enabling only when set up (otherwise 409 with "what is missing") |
| POST | `/config/import` | validated, actuators off first |

Errors come as `{"error":{"key","text"},"errors":[…]}`.

Every text the hub sends is a message `{"key","text","args"}` (SD-032):
`key` is stable, `args` holds the values (numbers as numbers, a nested
message as an object) and `text` is English for every key in
`core/src/messages.cpp`. The web app shows German from
`web/src/lang/msg.ts`. Controller lines, checklist entries and the
watchdog (`headline`, each item's `label` and `text`) are messages; a
reading's label is the catalog label without a key until the catalog
follows. The other texts follow under #18 and are still German until
then.

## Operate

| Method | Path | |
|---|---|---|
| POST | `/mix/plan`, `/mix/start` `{recipe, waterL, mode, guided, confirmRepeat}` | preview, start |
| POST | `/jobs/{id}/continue`, `/abort`, `/resume`, `/result {ml}` | continue after stirring, abort, catch up a pair, calibration result |
| POST | `/dose {canister, ml}` | manual dose |
| POST | `/pumps/{id}/calibrate {seconds}`, `/pumps/{id}/prime {seconds}` | calibration, prime tubing |
| POST | `/probe {device, kind, action, reference}` | probe calibration: start, point, commit, cancel |
| POST | `/latches/{id}/ack` | acknowledge a latch (a jump lock clears itself) |
| POST | `/stop`, `/resume`, `/maintenance {minutes}` | emergency stop, resume, maintenance mode |
| POST | `/measure {ph, ec}` | manual measurement |
| POST | `/grow/start`, `/grow/next`, `/grow/harvest`, `/grow/complete` | cultivation run |
| POST | `/update/check`, `/update/install`, `/update/rollback` | install only while no dosing runs (409) |

## Simulator only

`GET /sim`, `POST /sim/{speed|plug|unplug|cap|uncap|fault|water|probe|reboot|scenario|advance|net_add|net_remove}`
(see `SIMULATOR.md`). `net_add {class, loads:[{load, watts}]}` creates a
socket on the Wi-Fi; for sockets, `fault` knows `offline`, `readonly`,
`ignore` and `none`. These paths do not exist on the device.

## Open

- Generate an OpenAPI file from this document.
- Tokens for integrations, with roles (read, operate, admin).
- `If-Match` on `revision` against concurrent editing.
- Evaluate WebSocket instead of SSE (`architekt` recommends WebSocket,
  `software` SSE). The prototype uses SSE: simpler, readable with curl,
  and it reconnects automatically.
