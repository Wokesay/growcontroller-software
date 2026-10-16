# API v1

As of 2026-10-15. REST with JSON under `/api/v1`, transport-neutral in the
core (`core/src/api.cpp`). The web app uses only this API. So whatever the
UI can do, an integration can do as well.

## Access

- `GET /info` is public: version, platform, whether a password is set,
  whether the first password needs a setup code (`setupCode`, #79) and
  the hub's language (`language`, `de` or `en`; a new hub is `en`), which
  a sign-in page without a choice of its own uses (SD-083).
- `POST /auth/setup {password, code}` sets the initial password, exactly
  once (otherwise 409). Where the platform armed a setup code (the Home
  Assistant app prints it to its log, SD-068), `code` from the JSON body
  must match it, in any case and with or without dashes, or the answer is
  403 `auth.code`, the same for a missing and a wrong code; wrong codes
  lock nobody out and are logged at most once per 15 min. Then at least 8
  characters (422). The checks run in this order, so nothing is hashed
  before the code is right; with lost credentials the answer is 423.
- `POST /auth/login {password}` sets the cookie `gc_session` (HttpOnly,
  SameSite=Strict). After 5 failed attempts the answer is 429 with a wait
  time (30 s, doubling up to 15 min). The count is per client (SD-051): an
  address that signed in or used its session since the hub last started
  has its own (at most 16); all others share one. Only counted failures
  are logged, not a try refused during the wait or before the first
  password.
- `POST /auth/logout`. `PUT /auth/password {old,new}` logs out all
  sessions; a wrong old password counts as a failed attempt and is logged
  (422, then 429). The device that changed the password keeps its count;
  all other devices share the count until they sign in again.
- `GET /auth/session` says whether the request has a session; with one it
  sends the cookie again for another 12 hours, so a page left open that
  asks every 10 minutes keeps its session (SD-064); a client with
  `Authorization: Bearer` gets the same cookie for its own token, which it
  may ignore. The hub ends a session
  after 12 hours without a request, on a restart and on a password change;
  with 16 sessions, a 17th sign-in ends the one used longest ago.
- Everything else requires a session, otherwise 401. A header
  `Authorization: Bearer <token>` comes first; without it the session
  comes from the cookie named exactly `gc_session`, and only when the
  `Cookie` header holds exactly one cookie of that name.
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
  If `auth.json` is there but cannot be read, it refuses with 423
  `auth.kept` first, whether or not a password is known to be set: the
  file is kept, and a restart that can read it again brings it back
  (SD-081).
- **Import** (`POST /config/import`) validates like every change, plus
  limits, phase parameters and calibration data; 409 while a job runs.
  Exception: an unknown area or mode in `system.control` is left out, as
  when the file is loaded, not refused. A `system.timezone` that is no zone
  name is refused with 422 `system.timezone` before anything else is checked
  (so other errors in the file are not listed with it, and a running job
  does not turn it into 409); when the saved file is loaded, such a zone
  falls back to `Europe/Berlin` and an event says so. A file without
  `system.language` sets the hub to German, as files were saved before
  English became the default (SD-083).

## Read

| Method | Path | Content |
|---|---|---|
| GET | `/state` | live state: ports, devices, readings with quality and reason (and `ageS`, `ageMs`, `ageOf`: what the age counts from, `reading`, `measurement` with a sign of life or `change`, SD-042), tank, controllers with control line and checklist, outputs, job, dosing, watchdog (headline and items as messages, with `stale`), functions (resolver), latches, stock (ml per bottle, `null` when unknown, below zero when more was booked than entered, SD-066), cultivation run; `time`: `secured`, `source` (`secured`, `continued` from the saved time, `unset`) and `operatingS` (PD-069) |
| GET | `/events/stream` | Server-Sent Events: `event: state` every 1 s |
| GET | `/config`, `/config/export` | configuration without secrets |
| GET | `/catalog` | catalog of the firmware; each device class says `externalCalibration` (calibrated outside the hub, set in code, R7) |
| GET | `/history?series=a,b&from&to&points` | series (`t`, `avg`, `min`, `max`, `stepS`); at most 16 series, none twice (else 422 `history.series_many`, `history.series_twice`), each id as for `/export.csv` |
| GET | `/events?type&from&to&limit` | events, newest first |
| GET | `/export.csv?series&from&to&lang` | CSV of the history; each series id holds only `a`–`z`, `0`–`9`, `.` and `_`, at most 64 characters (else 422 `history.series_id`, also for an empty id between or after commas; an empty list gives only the time column), at most 16 series and none twice as for `/history`: `lang=en` gives `time_utc` and the series ids separated by commas, with a decimal point (`growcontroller-history.csv`); `lang=de` gives `zeit_utc`, semicolons and a decimal comma (`growcontroller-verlauf.csv`); without `lang`, or with another value, the hub's language decides. A gap stays an empty field, never 0 (RAT-016) |
| GET | `/diagnostics` | diagnostic bundle (without hash, sessions, Wi-Fi, IP) |
| GET | `/changelog` | embedded CHANGELOG (Markdown) |
| GET | `/update` | version, available version with a summary |

## Set up

| Method | Path | |
|---|---|---|
| POST | `/setup/complete` | setup completed |
| PUT | `/system` | name, time zone (a name in the form of the time zone database such as `Europe/Berlin`, `Etc/GMT+1` or `UTC`: starting with a letter, letters, digits, `_`, `+`, `-` in parts separated by `/`, none empty, at most 64 characters; else 422 `system.timezone`), language (`de`/`en`), update channel and update check, cap per manual dose; `control` sets what the app is to do per area (`climate`, `light`, `irrigation`, `refill`, `circulation`, `dosing`): `"auto"`, `"outside"` or `null` to take a choice back, adding to the choices so far, anything else 422 `system.control`; recorded only, it feeds no control yet (SD-037) |
| POST / PATCH / DELETE | `/devices/{id}/accept`, `/devices/{id}` | accept, rename, remove |
| PUT / DELETE | `/roles/{rolle}` `{device, channel}` | assignment; for mains sockets only after the protection setting is confirmed (otherwise 502) |
| POST | `/roles/{rolle}/test` | switched output on for 3 s |
| POST | `/roles/{rolle}/switch` `{on}` | manual operation, with all locks (otherwise 409 with the reason) |
| PUT | `/tank` | name, usable volume, minimum level, water, volume without a level head |
| PUT | `/zone` | growing area: name, kind (`room`, `tent`, `greenhouse`) |
| POST / DELETE | `/canisters`, `/canisters/{id}`, `/canisters/{id}/stock` | canisters, stock (a stock entered below zero is refused with 422 `canister.stock`, an unchanged one is kept) |
| POST / DELETE | `/recipes`, `/recipes/template` `{id, map, lang}`, `/recipes/{id}` | recipes, whose steps may be `fixed` (a fixed addition, never one of a pair: `cfg.recipe.fixed_pair`, RAT-085; a step without it counts as not fixed, anything but true or false is 422 `recipe.fixed`), with the chart's `refEc` (mS/cm) and `ph` `[low, high]`: a missing key keeps them, `null` clears them, another type is 422 `recipe.ref_ec` or `recipe.ph`, an EC that does not fit the amounts or a range out of order is refused by the configuration check (`cfg.recipe.ref_far`, `cfg.recipe.ref_strong`, `cfg.recipe.ref_range`, `cfg.recipe.ph_band`, SD-057); a template brings its chart EC and pH range; template with a mapping part → canister (422 with `missing`, named in the language); `lang` `de` or `en` picks name, note and part names (default: the system language; other values: 422 `recipe.template.lang`), part names match in either language, the one in `lang` first |
| PATCH | `/functions/{id}` `{enabled, params}` | enabling only when set up (otherwise 409 with "what is missing") |
| POST | `/config/import` | validated, actuators off first |

Errors come as `{"error":{"key","text","args"},"errors":[…]}` (`args`
only when there are any). When the
configuration check refuses a change (422), `error` is the first of its
findings in `errors`, with its key and arguments, so a page can show it
in its own language. `errors` lists at most 50 findings; `more` counts
the rest. Every refusal has a key of its own and an English
text: those of the API, the sign-in, the configuration check, a
calibration and the import, also the transport's (`api.origin`,
`api.too_large`, `api.timeout`) and the simulator panel's (`sim.err.*`)
(SD-083).

Every text the hub sends is a message `{"key","text","args"}` (SD-032):
`key` is stable, `args` holds the values (numbers as numbers, a nested
message as an object) and `text` is English for every key in
`core/src/messages.cpp`. The web app shows German from
`web/src/lang/msg.ts`. Controller lines, checklist entries, the
watchdog (`headline`, each item's `label` and `text`), job messages,
events (`title`, `text`) and errors are messages; a reading's label is
the catalog label without a key until the catalog follows. An argument
can be a list (joined with ", "). Events written before keep their
plain text without a key. A latch's `why` in `/state` is a message (a
state saved before keeps its plain text; jump locks have no `why`). The
reasons of a reading and the checklists' entries follow (#73) and are
still German until then.

When the dosing of a job step cannot start or go on (`/mix/start`,
`/dose`, `/jobs/{id}/continue`, `/jobs/{id}/resume`,
`/pumps/{id}/calibrate`, `/pumps/{id}/prime`), the answer has the error
key `job.start_failed` and the cause as the message in `args.reason`,
for example `dose.too_small` or `act.stopped` in `args.reason.key`.
Checks of the request itself keep their own keys (for example
`job.busy`, `dose.hand_limit`, `job.state`); a mix started during a Stop
is refused with `act.stopped` itself. Job messages (`job.message`) use their own keys,
for example `job.dosing_step`, `job.dose_failed`, `job.pair_failed` and
`job.emergency_stop`.

## Operate

| Method | Path | |
|---|---|---|
| POST | `/mix/plan`, `/mix/start` `{recipe, waterL, mode, guided, confirmRepeat}` | preview, start |
| POST | `/jobs/{id}/continue`, `/abort`, `/resume`, `/result {ml}` | continue after stirring, abort, catch up a pair, calibration result (`flowMlPerMin`, `changed` when clearly different from before, `message`) |
| POST | `/dose {canister, ml}` | manual dose |
| POST | `/pumps/{id}/calibrate {seconds}`, `/pumps/{id}/prime {seconds}` | calibration, prime tubing |
| POST | `/probe {device, kind, action, reference}` | probe calibration: start, point, commit, cancel. For a sensor on its own schedule (Home Assistant, SD-041) `point` answers with `pending {since, reference}`: the point is taken from the sensor's next report; with none within its freshness limit the session in the state carries `failed` (`probe.no_raw`) |
| POST | `/latches/{id}/ack` | acknowledge a latch (a jump lock clears itself) |
| POST | `/stop`, `/resume`, `/maintenance {minutes}` | Stop, resume, maintenance mode |
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
