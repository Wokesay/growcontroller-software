# History, events, export

As of 2026-10-10. Proposal by `software`, implemented in the prototype
(`history.*`, `events.*`).

## Series in three tiers

| Tier | Interval | Duration | Values | Storage per series (float) |
|---|---|---|---|---|
| L0 | 10 s | 24 h | mean | 34 KB |
| L1 | 1 min | 7 days | min/avg/max | 121 KB |
| L2 | 15 min | 1 year | min/avg/max | 420 KB |

- **Query:** a query uses the finest tier that still covers the period. It
  thins out to at most N points and keeps minimum and maximum.
- **Gaps stay gaps:** NaN, `null` in the API, a break in the chart, an
  empty field in the CSV. Rationale: RAT-006, RAT-016.
- **What is stored:** only valid values from sensor truth go into the
  history. An invalid value becomes a gap, not a wrong point.
- **Series:** every role with `series: true` (pH, EC, water temperature,
  level, climate) and the known tank volume.

**On a computer** (simulator, Home Assistant server; #68): the history is
a snapshot (`history.bin`, about 0.6 MB per series) plus a journal
(`history.log`) with one record per sampling round, appended every 10 s.

- The snapshot is written at every start, before anything is appended,
  and then once a day; it empties the journal, but only once it is on
  disk itself.
- After a restart the hub reads the snapshot and then the journal: per
  series only samples newer than that series in the snapshot, only the
  catalog's series, only plausible times. A journal that survived its
  snapshot (power loss in between) is not counted twice.
- Each record carries a checksum: a record torn by a power loss ends the
  replay and is never read as values; the snapshot at the start then
  replaces the journal.
- The event log works the same way (`events.json` plus `events.log`, one
  event per line, replayed by id). The journal never holds more events
  than the log keeps (5000); then a snapshot is taken early, so failed
  logins cannot fill the card.
- An idle hub with the demo setup hands about 90 KB of data an hour to
  the disk this way (tested below 0.4 MB in two hours); with the file
  system's 4 KB pages that is about 2 MB an hour. Before it was the whole
  history (about 4.6 MB) every 10 minutes.
- A version from before the journals ignores them: going back loses up
  to a day of history and events.
- The simulator holds its writes only while fast-forwarding (the 48 h
  prefill, "advance"): each file goes to disk once at the end, and a
  journal is never emptied behind its failed snapshot; what failed is
  given to the hub, which writes it again. Without a data folder (or one
  that cannot be created) everything stays in memory.

Not for the device yet: its 2 MB storage partition cannot hold the
snapshot of several series; there the ring buffer below is the plan.

**On the hub** (proposal by `software`, not implemented yet):

- The series go into a dedicated flash partition as a ring buffer: append
  to erased sectors; erase only on wrap-around.
- Values as scaled int16 plus a 4-byte timestamp. For about 12 channels
  this comes to about 4.7 MB, event log included.
- Partitions on 16 MB: 2 × 3 MB app (A/B), 1 MB LittleFS for the
  configuration, about 9 MB history.
- Wear is not critical: NOR flash lasts about 100,000 cycles; L0 lasts
  over 250 years by calculation.

**Time without a battery RTC:** SNTP, otherwise the browser's time at
login. Records without a reliable time are marked. Whether an RTC chip
goes on the board is decided by `hardware`.

## Event log

A ring buffer of 5,000 entries. Each entry has time, type, severity, title
and text, plus data.

| Type | Examples |
|---|---|
| `dose` | per job: canister, actual ml, target ml, actual run time, purpose (mix, manual, ec, ph) |
| `mix` | started, finished with amounts, interrupted (also by a restart), resumed |
| `control` | "pH corrected 6.29 → 5.88 with 2 doses", "EC topped up …" |
| `block` / `unblock` | jump lock with values, cleared |
| `alarm` | dry run, inlet emergency cut-off, control without effect |
| `tank` | inlet open/closed, refilled (calculated/measured) |
| `calibration` | pump calibrated, probe calibrated |
| `device` | detected, disconnected, back again ("Is it still on Part A?") |
| `config` | every change with its revision |
| `auth` | login, failed attempt, password change (security-relevant logging, CRA Annex I 2(l)) |
| `system`, `grow`, `measure` | start, emergency stop, maintenance mode, updates; cultivation run, phase, harvest; manual measurement |

The log thus replaces keeping a cultivation diary by hand.

## Export

- **CSV** per period: semicolon, decimal comma, empty fields instead of 0.
- **Settings** are saved and loaded as JSON. Loading is validated and
  switches all actuators off first.
- **Diagnostic bundle** for "Report a problem", see `SECURITY_MODEL.md`.

## Later

- **Companion** (Docker/NAS/Home Assistant add-on) for a long-term archive
  and for comparing cultivation runs by day of the run; optionally MQTT or
  Influx Line Protocol.
- **Summary at the end of a cultivation run:** water, ml and € of
  nutrients, phase durations, yield entered by hand. Rationale: RAT-016,
  RAT-064.
