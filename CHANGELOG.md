# Changelog

All notable changes to growcontroller-software. Format according to
[Keep a Changelog 1.1.0](https://keepachangelog.com/en/1.1.0/), versions
according to [SemVer](https://semver.org/). Security-relevant changes are
listed in every release under "Security".

## [Unreleased]

### Added
- **A prototype says it is for testing only** (SD-096, from #250): the
  sign-in page (also when the first password is set), the Home Assistant
  app's introduction and Settings › Display and info say "For testing
  only: this version does not meet the requirements of the EU Cyber
  Resilience Act (CRA)" whenever the version is a pre-release
  (`-proto`, `-beta`) or cannot be read; a Stable release shows nothing.
  The README says so too, and the release notes of a pre-release start
  with it.
- **No watering on water not fit for it** (#298, SD-093, RAT-086): the
  watering pump no longer switches on, by hand, over the API or for a
  test, and a running one goes off, when the tank's pH is below 4.0 or
  above 8.5, or its EC is more than 1.0 mS/cm above the target EC (the
  higher of this phase's and the last phase's; with EC control off and
  no phase naming a target, above 5.0; never above 5.0). The band is wide on purpose: it catches a
  pump that ran too long or dosed too much, while uncorrected tap water
  and plain water for flushing typically stay fit. An assigned pH or EC
  probe without a usable reading, or in a calibration, blocks too; a
  probe not assigned, or calibrated outside the hub, is not checked.
  With a probe checked, watering also waits after every run of a dosing
  pump and after every start until a dose has mixed in (at least 4 min,
  longer in a large tank), and a run that starts stops the pump. There is no override. A refused
  switch-on says why and when to try again, and an event says why the
  pump went off.
- **Mixing to an EC without measuring** (#143, SD-057, step 5, SD-098):
  without a probe, or without circulation, the mix page can now scale a
  recipe to a target EC from the water's EC, taken from a meter or
  entered as 0 for reverse-osmosis water. The hub works from the larger
  of the effect the recipe's chart EC implies and what EC control learned
  for it, scales every part (but a fixed one) by one factor, and never
  doses more than the recipe's own amounts: a target above that is not
  met, and the plan says what EC to expect and how to get more. Without a
  chart EC that fits the amounts, a target in the EC target's range or
  the water's EC (never taken as 0), it is refused with the reason; the
  recipe's amounts can always be mixed. It is offered while EC control
  is off or locked: switched on, EC control takes the tank to its own
  target after the mix, and the page says so. The plan says it runs
  without measuring, warns of water above 0.4 mS/cm on a new fill and,
  with pH control on, of the EC the pH correction adds, or that the pH
  is corrected by hand when the EC to expect is below pH control's EC
  lock; a water EC above the EC target's range is refused as a likely
  µS/cm. On a new fill the probe's reading can be taken over as the
  water's EC. The mix card names the target and the EC to expect.
- **Each recipe learns its own EC effect** (#143, SD-057, step 4,
  SD-092): EC control learned one effect for whatever recipe it dosed;
  a phase moving to another recipe started from it. Now each recipe
  learns its own from EC control's clean rounds on it, and a round
  learns into the recipe it dosed. A recipe that has learned nothing
  starts from the start effect, or from the larger effect its chart EC
  implies, so a chart EC only ever makes a round smaller; the cap of a
  round converts with the larger of that start and the learned effect,
  so neither a low chart EC nor a small learned effect widens it. An
  edit that changes a recipe's parts or their ratio keeps its learned
  effect only at or above the recipe's new start, scaled up first by
  parts that are fixed or removed now, and a chart EC raised above it
  forgets it; a round on that recipe that settles across the edit learns
  nothing. An import does the same for a recipe the hub had; a recipe it
  leaves out or brings new, or one deleted or created, starts afresh.
  After the update, every recipe whose start it does not undercut starts
  from the effect learned before. A recipe's ID must be there and used
  once. `/state` names the effect of EC control's recipe and of each
  recipe (`effects.ecByRecipe`).
- **A mix mixes in before it ends** (#143, SD-057, step 3): with a
  circulation pump, a mix now ends only once its nutrients have mixed in
  (as long as EC or pH control waits after a dose, whichever is longer,
  at least about 100 s per 20 L in the tank, at most 60 min); the mix
  card shows the minutes left, offers no button to skip or cancel it, and
  says so when the pump is not running (off or unknown for 10 s). EC and
  pH control and refill decide only after that. A mix ended, stopped, or cut short by a failure,
  an internal error or a restart with nutrients in makes EC and pH
  control wait for the circulation pump and settle first, at least 15 min
  and as long as the longer of their settle times; EC decides first, now
  also when both waits end together. So does a mix whose pump did not run
  while it mixed in. Their line says why while the pump does not run, and
  such a wait logs no correction. Stirring by hand now waits at most 30
  minutes after a part (SD-089): then the mix stops, names the parts of a pair
  still missing with their amounts, and can go on ("Resume mix") or be
  ended ("End mix"); a mix cancelled with a pair half in names them too.
- **Fixed amounts in a recipe** (#143, SD-057, step 2, RAT-085): a
  bottle that is not one of a pair can be marked "Feste Menge" / "Fixed
  amount", for an addition the maker gives a fixed amount per litre for,
  such as a cleanser. It goes in per litre of fresh water in every mix, as
  all parts do today, but EC control never doses or scales it: its rounds
  go to the other parts in their ratio, and the effect it learns, like the
  chart EC's check, counts only those. Marking a part of EC control's
  recipe this way scales the learned effect up, so the next round doses
  less, never more; a round running then doses no part that is fixed now
  and learns nothing, and one left with nothing to dose settles or ends. The recipe card and the mix plan mark it; the bottle
  editor says before saving that such a bottle cannot join a pair. A
  recipe of fixed amounts only cannot raise the EC, and the editor and
  the control line say so. EC control's reasons for not dosing (tank
  volume or effect unknown, no amounts, EC not below the target) now come
  in English too.
- **A recipe keeps its chart's EC and pH range, and a grow proposes them**
  (#143, SD-057, step 1): the recipe editor has "Chart EC" and "pH
  range", and the recipe templates fill them in (2.1 and 2.3 mS/cm
  from the Athena chart, 1.6 for the general two-part start values;
  5.8–6.2). The grow dialog proposes each phase's target EC from its
  recipe and its target pH from the middle of the range, instead of fixed
  numbers; a value you type stays when you pick another recipe, and an
  empty field takes the value under Functions and says so. An EC that
  does not fit the amounts (outside a quarter to four times the usual
  effect, likely a typo or a very strong concentrate) or a pH range out of
  order is named before saving, with the EC that would fit, and refused
  by the hub. Without a grow, the overview names the target ranges that
  apply. Mixing does not change yet, but a grow started with the
  proposals controls EC to the recipe's chart EC (2.1 for the Athena
  template, where the dialog proposed 1.4 before); mixing to the target
  follows in the next steps.
- **Firmware for two boards** (SD-043, M1): the firmware is built for
  the ESP32-S3-DevKitC-1-N32R16V (32 MB flash, 16 MB PSRAM), and for the
  N8R8 (8 MB flash) as the fallback with smaller apps and a shorter
  history. Each board has its own profile and partition table in
  `firmware/boards/`. CI builds both. Per board it keeps the images to
  flash, `flash_args` for esptool, a manifest for flashing from the
  browser with ESP Web Tools, and their SHA-256 checksums. Each image goes
  to its own offset: flashing the same board with an unchanged partition
  table keeps NVS and the data partitions, unless you choose to erase.
  NVS gets 64 KB for the encrypted secrets to come. The architecture
  check now also rejects eFuse-burning options in a board profile. The
  firmware is still a skeleton and has not run on a board yet.
- **Home Assistant: a sign of life per sensor** (SD-042, SD-047): Home Assistant
  does not report a value again while it stays the same, so a calm water
  temperature looked "1 min" old. Under Devices › Assignment,
  "Lebenszeichen" (sign of life) picks for a sensor the channel of its
  device's sign-of-life text (`n=<counter>|<code>=<s>|…`, the age of each
  channel's last measurement) or a timestamp sensor. The age then counts
  from the last measurement, shown green up to 30 s, yellow from 31 s and
  red from 60 s, and the value is outdated after 60 s without a
  measurement. A text older than 60 s makes all its sensors outdated and
  shows in the monitoring as one problem titled with the readings it
  affects, with the sign of life's entity ID at the end (#107). Without a
  sign of life the age now says "unverändert seit"
  (unchanged for), as the app sees only the last change. A sign of life
  that is unavailable, unreadable, misses the channel, lies in the future
  or dates the measurement before the sensor's own report makes the value
  outdated; it never falls back to the longer learned limit. The same
  text dated again keeps its first time, also after an outage, so it goes
  out of date 60 s later. Which sign of life belongs to a sensor rests
  on your pick: the list marks the sensor's own device and another one.
  `docs/HOME_ASSISTANT.md` has an ESPHome example.
- **Home Assistant: a pH probe's voltage is calibrated in the app**
  (SD-038, SD-041): a pH probe connected without amplification that
  reports its voltage (mV or V) instead of a pH is suggested first in the
  sensor picker, marked as a raw value, and calibrated under Devices ›
  Assignment › "Kalibrieren" with pH 7 and pH 4 buffers, with the hub's
  own check (it accepts 47–74 mV per pH step, 80–125 % of the ideal). Then
  its pH is a valid reading, checked for freshness (the limit learned for
  the sensor) and a stuck signal; the app still switches nothing. Each
  step waits for the sensor's next report. A refused calibration says what
  is wrong: buffers swapped, an amplifier in between, or a worn probe. A
  pH from Home Assistant in pH stays display only. Level and EC as raw
  values follow.
- **Hardware compatibility list** (SD-049, `docs/COMPATIBILITY.md`): what
  works with growcontroller, by connection: ESP32 boards, sensors in Home
  Assistant (for example what DFRobot's analog pH and EC boards, Atlas
  Scientific EZO circuits and a DS18B20 give today), the carrier board's
  inputs and outputs, wireless sensors and smart plugs, each Tested,
  Expected, Not yet or No, with warnings where a wrong choice is risky
  (pH and EC probes without isolation in one tank, pumps too large for
  pH− in a small tank). The README says which way fits you: the ESP32-S3
  on its own, or with Home Assistant on top. The issue form "Device or
  sensor" takes test reports for the list.
- **Docs for newcomers** (`docs/DEMO.md`, `docs/FAQ.md`): a guide to the
  demo (which download for which system, checking it, starting it on
  Windows, macOS and Linux, how the older download differs, what to do
  when it does not start, a short tour with a pH jump,
  time-lapse, a mix cancelled halfway, a power cut and the first-time
  setup, the command-line options, building it yourself on Linux, macOS,
  Windows or a Raspberry Pi, and what feedback helps); a FAQ on status,
  hardware, safety and its limits, how the project is built and what
  SD-, PD- and RAT- numbers mean; `SUPPORT.md` (where to ask what) and
  `CODE_OF_CONDUCT.md`. The README starts with what the software does, a
  screenshot, the demo in three steps and a status table, and says how
  the project is made (one maintainer with an AI coding assistant, AI
  review agents, the maintainer's approval for releases and gateway
  logic); `CONTRIBUTING.md` names the ways to help without code and what
  an outside contributor needs. A feedback form for Discussions (category
  "Feedback"); the bug report form asks for system and browser, offers
  "Home Assistant app" as the hardware and says "report number" as the
  app does.

### Changed
- **Plan: the demo goes public after 0.1.0-proto.5** (SD-099): the demo
  starting in English (#302) is a precondition of proto.5, and the demo
  is announced to a wider public only once proto.5 is released.
- **The simulator's demo speaks English** (SD-095): a new demo starts
  with the hub's language English, so the sign-in page and the app are
  English in a browser that has not chosen a language, and the demo's
  devices, bottles, recipes, area and grow have English names ("Dosing
  block 1", "Part A", "Part B", "Veg", "Bloom", "Room 1", "Grow 1" …;
  the bottle and recipe IDs follow, for example `part-a` and `veg`).
  German is chosen under Settings › Display and info › Language, as
  `sim/README.txt` and `docs/SIMULATOR.md` say. The simulator's own
  messages on the console and its `--help` are English too, and so is
  its stand-in for updates under Settings › Updates: the sample of what
  is new, and the messages when you install or roll back, now by key in
  the page's language. A demo folder saved by an older version keeps its
  German and its German names, as a hub keeps its stored language
  (SD-083); delete it for the English demo. The demo password stays
  `demo-passwort`.
- **The mix plan's, a dose split's and a pH dose's messages in both
  languages** (#143, SD-057, step 5): the hub sends them in English with a
  key, the web app shows them in German by that key. For API clients: a
  dose split's reason inside a mix plan's error or an EC round's reason is
  now wrapped as `mix.part` with the split's own message in `args.reason`,
  and a recipe step without an amount is `mix.amount_missing` instead of
  `mix.amount` (`docs/API.md`).
- **Control details are decided in the software session; the product
  offers a safety frame, not complete protection** (#298, SD-091, from
  PD-317 and PD-318): the product side keeps the guideline, costs,
  hardware and three baseline rules: a crash or restart stops whatever
  could cause harm, a hardware time limit for the pumps, and no watering
  when pH and EC say the water is not fit. Locks, releases, thresholds,
  sequences and which devices and pumps are allowed are decided here as
  SDs; earlier product decisions on them are proposals, to be simplified
  along the guideline, and what was recorded from them stays until a
  later decision changes it. The guideline wants any pump and device to
  be usable; the software follows in later changes. Product questions
  are about what the product is and offers (`CLAUDE.md`,
  `docs/WORKFLOW.md`). Two baseline rules are not met yet: board v0.1
  has no hardware time limit for the pumps, and watering on water that
  is not fit is not blocked yet (that follows in a later change for
  #298).
- **Core tests on every core** (#260): `tools/ci.sh`, and with it CI's
  `checks` job, runs the C++ tests with `tools/core_tests.sh`: each test
  case in a process and a temporary folder of its own, as many at once
  as there are cores, the scenarios first. On four cores they took
  about 9 min in a local run instead of about 34 in one process. A test
  case that fails, crashes, does not end or runs other than one case
  fails the run; its output is shown after all have ended, with the
  command to run it again. A passing run names the five slowest cases.
  `build/gc_tests` alone still runs everything in one process, as the
  app image's job does.
- **The catalog in the page's language** (#143, #18, SD-083 point 3):
  the names and texts of roles, device classes, functions and their
  settings, and of the Home Assistant app's sensor classes, show in the
  page's language, also inside messages and in the event log. The
  catalog is English; the web app shows German by each entry's key. An
  event saved before keeps its German names. A device accepted without a
  name of its own stores none and is named by its class in the page's
  language; a stored name that is only its class's German name, which a
  device accepted before got, counts as none, and the file keeps it. For
  API users: `/catalog` is English; a role's, a device class's, a
  capability's, a function's and a parameter's name inside a message's
  `args` is a message with a key (`cat.role.<id>`, `cat.class.<id>`,
  `cat.cap.<id>`, `cat.fn.<id>`, `cat.param.<function>.<key>`) instead
  of a German string; a device without a name of its own has an empty
  `name` in `/state` and is named by its class in events; the list of
  devices not responding (`watch.devices.offline`, `names`) is a list.
- **English first** (#143, #18, SD-014, SD-083): a new hub is set to
  English. The first-password page starts with the language choice, set
  to English; what you pick there carries into setup, whose first field
  is the language, as in the Home Assistant app's introduction. A browser
  where you have not picked a language no longer follows the browser's
  own language: it shows the hub's language, from the sign-in page on. A
  hub you already use keeps its language through the update, and so does
  an older settings file without a language when imported (German).
  Skipping the Home Assistant app's introduction still saves the language
  you picked.
- **The CSV export in the page's language** (RAT-016, SD-083): from a
  German page as before, with semicolons and a decimal comma
  (`growcontroller-verlauf.csv`); from an English page with commas and a
  decimal point (`growcontroller-history.csv`). Gaps stay empty in both.
  A series name in the export's address holds only lower-case letters,
  digits, dots and underscores.
- **English file names:** the settings file is now
  `growcontroller-config.json` (was `growcontroller-konfiguration.json`),
  the diagnostic bundle `growcontroller-diagnostics-….json` (was
  `growcontroller-diagnose-….json`).
- **API:** a refused change's `error` is now its first finding, with
  `key`, `text` and `args` (the whole list stays in `errors`); `/info`
  names the hub's language (`language`); `/export.csv` takes `lang=de`
  or `lang=en` and otherwise uses the hub's.
- **Reasons and checklists in the page's language** (#143, #73, SD-083):
  why a reading is not valid (not assigned, no answer, stale, frozen,
  outside its range, a jump lock), what a function still needs and its
  short state on Functions and in setup now come in the page's language.
  A jump lock names only causes that can be true (#162, RAT-041,
  RAT-042), and no longer says "without dosing": a pH fall says to
  measure by hand and raise pH yourself, as the hub only lowers it, and
  names the hub's own pH dose as one possible cause; a pH rise names work
  at the tank and the probe; a level jump asks about filling or draining
  by hand without maintenance mode; a reading of the grow area names work
  near the plants, one of the tank work at the tank. Each says the lock
  lifts after 15 min steady, in at most 200 characters. An event saved
  before these texts is shown as it was saved, also a reason inside it.
  The simulator's port lines (a pump plugged straight into the hub) and
  a device's own error text inside a refusal or a failed dose are
  English; the device's text has no key. For
  API users: a function's `checks[].text` is now a message object (`req.*`
  keys) instead of a string; its `summary` names the first open item and
  how many more (`fn.needs_setup`, `fn.needs_setup_more`,
  `fn.unavailable`, `fn.unavailable_more`) and splits "switched on"
  (`fn.on`) from "ready, switched off" (`fn.ready`); a pH rise, a pH fall,
  a level jump and a jump in the grow area have keys of their own
  (`truth.jump_ph`, `truth.jump_ph_fall`, `truth.jump_level`,
  `truth.jump_zone`), three soft requirements' reasons too (`req.why.*`),
  `req.present` is now `req.assigned`, a level sensor's calibration
  too (`req.calibrate_level`, `req.calibrated_level`), and values in a
  reading's reason are rounded to the reading's decimals and carry their
  unit (`unit`).
- **Refusals in the page's language** (#143, #18, SD-083): a refusal of
  the hub now comes with a key of its own and an English text, and an
  English page shows English, a German page German: signing in and
  changing the password, adding devices and assigning roles, bottles,
  recipes and templates, starting a mix, manual doses, pump calibration
  and tube priming, probe and tank-curve calibration, grow cycles,
  maintenance mode, updates, the configuration check and the import, and
  the simulator panel. Still German until its work follows: the
  reasons a mix plan or a dose split gives (with SD-057). For API users, some keys are new where
  one key had several texts: `device.not_detected` (was `device.unknown`
  when adding a device), `config.busy`, `config.newer`, `job.state_amount`,
  `job.state_resume`, `job.dosing` (EC or pH control dosing, before a
  manual dose, a calibration or priming), `recipe.template.unknown`,
  `auth.wrong_old`, `update.busy_back`, `probe.reference_range`,
  `probe.points_max`, `probe.points_two` and `probe.points_one` (were
  `probe.points`), `cfg.role.place_tank`, `cfg.params`, `cfg.phase.type`,
  `cfg.phase.range`, `cfg.calibration.offered`, and `curve.*` (was
  `probe.curve`) and `calib.*` for a tank curve or a calibration a probe
  step or an import refuses; the simulator panel's `sim.err.*` (was
  `sim`).
- **History and CSV export:** a request names at most 16 series and none
  twice; each series id follows the CSV export's rule.
- **A refused change lists at most 50 findings**; `more` counts the rest.
- **The menu is grouped by topic** (#143, SD-056): below Overview and
  History come small headings for the nutrient solution (mixing, tank
  and control, recipes and bottles, watering), the room (climate,
  light) and the system (devices, functions, settings). Watering is now
  in the same group as the tank it draws from, and recipes in the same
  group as mixing. On a phone the bottom bar stays as it was, and "More"
  shows the same headings.
- **Climate, light and watering say they work by hand** (#143, SD-056):
  each page is marked "Manual mode" ("Handbetrieb" in German). The hub
  switches on only when you tap and controls nothing by itself, apart
  from exhaust and circulation fans, which come back on after a power
  cut. So keep your own timer or controller running: a light on a hub
  plug goes off at every restart, and only a pump the hub switches is
  protected from running dry. With nothing assigned, the page says what
  to connect and where, and how to add a smart plug if none is added
  yet. The Home Assistant app is unchanged, as it switches nothing
  there.
- **Plan: factory reset with BOOT held 15 s and a confirmation; an
  access reset** (#133, SD-080, from PD-197 to PD-199 and PD-217 to
  PD-219). To reset the ESP32-S3 to its factory state, you hold BOOT for
  15 s. It first switches off the smart plugs with a water or CO2 role:
  irrigation and circulation pump, inlet and inlet pump, humidifier,
  dehumidifier, CO2 and tank heater. Light, fans, room heater and air
  pump keep their state, and a drain with its own pump stays on. The
  LEDs show whether every one of these plugs reported "off". Only a
  short press on BOOT within 30 s then erases; without it nothing is
  erased. The web app and our flash page switch off the same plugs
  first. The reset erases Wi-Fi, the password, settings, calibrations,
  history and the passwords of adopted plugs, and keeps the label's key.
  If you only lost access, the access reset keeps your data: hold BOOT
  long (proposed: 8 s), set a new password in the home network within
  15 min and confirm it with a short press on BOOT. Home Assistant must
  then be paired again. Before you pass a device on, use the factory
  reset. Built in a later change.
- **Plain words instead of developer terms** (#143, SD-055): in German,
  locks now say "Freigabe nötig" and "Freigeben", as the help and the
  English texts do, instead of "Gerastet" and "quittieren". The lock list
  on Tank & control names the reading of a jump lock instead of an
  internal key. Control lines, checklists, events and hints no longer say
  "Sensorwahrheit", "Aktor-Gateway", "Notgrenze", "watchdog" or "job" /
  "Auftrag"; the Overview no longer speaks of "Stufe 0", and Settings no
  longer shows a rule number. Events say "Change no. 7" instead of
  "Settings revision 7", and no longer name internal files when a saved
  file cannot be read. Message keys stay, so on a German page events saved
  before show the new words; on an English page they keep the old ones.
- **"Stop" instead of "emergency stop"** (SD-045, PD-172; #143): the
  button, its question, the banner, Monitoring, the event log and every
  message call the function "Stop", in German "Stopp" or "Alles stoppen",
  never "emergency stop" or "Not-Halt". Examples: "Stop everything?" /
  "Alles stoppen?", "Stop active." / "Stopp aktiv.", "Stop cleared" /
  "Stopp aufgehoben". The Home Assistant app's documentation says the
  same. The message keys stay, so on a German page events saved before
  show the new words too; on an English page they keep the old ones. A
  mix refused during a Stop now says so in both languages; for API
  clients its error key is `act.stopped` instead of `job.stopped`.
- **Sessions claim an issue before working on it** (SD-078): parallel
  sessions had picked up the same issue. Before a session starts, it
  fetches the issue again and leaves it alone if a live claim or an open
  pull request covers it. Otherwise it claims it with a comment naming
  the session and its branch and the label `in-progress`; of two claims
  the first comment wins. A multi-part issue is claimed per part, a claim
  with an open pull request never goes stale, and any other claim quiet
  for 24 hours may be taken over (`CLAUDE.md`, `docs/WORKFLOW.md`).
- **Plan: the Home Assistant app moves behind Ingress** (#190, SD-077,
  from PD-225; replaces SD-068's setup code for the app). From
  0.1.0-proto.5 on, the app opens in Home Assistant's sidebar with Home
  Assistant's login, and the setup code goes away. Port 8099 is closed by
  default. A bookmark to it works again once the port is opened in the
  app's network settings and the app has a password for direct access
  (set by a Home Assistant administrator in the app; a password from
  0.1.0-proto.4 is kept); without one, port 8099 only says where the app
  is now. The ESP keeps its own login. Built in a later change.
- **Product questions are issues here** (#190, SD-072, from PD-226;
  replaces SD-069): the software session files each product question it meets,
  with its software basis, the options and its recommendation, as an
  English issue labelled `product-question`. The product repository's
  session picks it up and decides it there (PD). The software session no
  longer accesses the product repository.
- **A security advisory for every fixed vulnerability** (#190, SD-079,
  from PD-220): a flaw that someone without the needed rights can
  trigger gets a GitHub Security Advisory, with a CVE from GitHub or the
  CVE of the component it comes from. It is published with the release
  that fixes it; if that release takes longer than 30 days after the fix
  is on `main`, it is published anyway, with a workaround. The Security
  section names per item the affected versions, impact, severity and
  remedy (`SECURITY.md`, `docs/RELEASE.md`). At the latest from the
  first device offered or lent, actively exploited vulnerabilities and
  severe incidents are reported under CRA Art. 14; until then we do so
  voluntarily.
- **Triage template (not active) only for people with a role** (#29):
  `docs/templates/claude-triage.yml` starts only for an issue opened by
  the owner, a member or a collaborator, or for "@claude" in their
  comment on an issue, not on a pull request. Anyone else's issue waits
  until someone with a role, in practice the owner, comments "@claude".
  It passes the workflow's own token, which its permissions limit, and
  its checkout is pinned by commit SHA like the active workflows; a test
  keeps all of this. `docs/WORKFLOW.md` lists what to do before
  activating it.
- **Release acceptance in two parts** (SD-065, `docs/RELEASE.md`): the
  project owner tests what a release brings with the release branch's
  server against a real Home Assistant before saying "mergen", and the
  update with a backup made first, a fresh install, the restart, the
  lockout per device and the backup with the published app right after
  it is published. The app's documentation in Home Assistant and the
  release notes ask you to back up the app before updating; if the
  published app fails, a fixing release follows at once.
- **The circulation pump's dry-run lock releases itself once the tank is
  refilled** (SD-022, SD-067, #27). A lock set because the level fell below
  the minimum while the pump ran now lifts by itself when a valid level has
  stayed at or above the minimum plus a margin (the larger of 0.5 L and
  2 % of the tank) for 5 minutes, and the event log says so. Maintenance
  mode pauses this; the 5 minutes start when it ends. It still holds until
  you tap "Release", and says why, if switching off was not confirmed or
  the pump ran again after it was off, if the level was low again within
  an hour of the last self-release, if the level jumped while locked, and
  for a lock saved by an older version. Filling by hand? Start maintenance
  mode first, or the jump holds the lock.
  The inlet's emergency cut-off is unchanged. A lock set or released is
  now saved at once.
- **Decision numbers:** the decision on access from outside the home
  network is now SD-059. It (#123) and the sign-in lockout (#122) had both
  been recorded as SD-051 on the same day; SD-051 stays the lockout.
- **Plan: 0.1.0-proto.5 for testers** (SD-094 to SD-097): testers start
  from the docs for newcomers, "Report a problem" opens GitHub's bug
  form for everyone, the demo speaks English, and a prototype says on the
  sign-in page and under Settings that it is for testing only and does
  not meet the EU Cyber Resilience Act. A fixed defect gets a security
  advisory only if someone without the needed rights can trigger it
  (PD-320); the other fixes in Security are labelled "not exploitable by
  an attacker".
- **Plan: polish before 0.1.0-proto.5** (SD-055 to SD-057): the next
  prototype release comes after a block of fixes from a review: the phone
  layout, a message when Stop or a switch fails, plain words instead of
  developer terms, "Stopp" instead of "Not-Halt", and English complete.
  The menu will be grouped by topic (nutrient solution, room, system), and
  climate, light and watering will say "manual" while they only switch by
  hand. Mixing will aim at the phase's target EC, with the recipe giving
  the ratio and order of its parts: to the target with an EC probe, as a
  fixed pattern from litres and recipe or a fixed EC with the water's
  measured EC (for soaking, for example), or with stirring by hand
  between the parts.
- **Plan: dosing modules with a fixed rate** (SD-066, SD-084, #132): after
  0.1.0-proto.5, as part of the work for the carrier board, growcontroller
  will support dosing modules that each run at one fixed rate and keep
  their calibration on a chip in the module. "Fine" (about 12 ml/min) is
  for nutrients, additives and pH in tanks up to about 150 L. "Large"
  (about 220 ml/min) is recommended from about 100 L. Every dose has a cap
  derived from the tank's usable volume, on top of the caps per litre of
  what is in the tank and below the fixed caps in code. A
  long dose runs as runs of at most 60 s, and the board ends every run
  after about 90 s on its own. Priming, calibration and test runs go into
  a cup, never into the tank, and every shop module is calibrated at the
  factory. A mix still does not start when a canister's stock does not
  cover it, and a warning comes when the stock will not cover the next
  mix. Besides the tank, you will be able to mix into a bucket or watering
  can: nutrients only, no pH. Some details are still to be decided
  (#282, #284). The simulator docs now mark how mixing time grows with
  the tank as an assumption.
- **Plan: the hub's own pH dose and the jump lock** (SD-085, #162): the
  hub's own pH doses will get a fixed cap per litre, derived from the jump
  threshold at the strongest measured effect. A lock in the direction of
  the hub's own dose will reset the learned pH effect. Until it is
  measured how far an EC round moves pH, an EC round will explain a pH
  change from its start. A pH jump lock still lifts by itself after
  15 min steady.
- **Plan: access from outside, Home Assistant, fans and EC** (SD-052 to
  SD-054, SD-059, SD-060): at setup you will choose whether the ESP32-S3's
  web app can be reached only in the home network (preselected), viewed
  from outside, or viewed and controlled from outside. The Home Assistant
  window counts as outside. "Stop" works from outside at both outside
  levels; calibration, limits, updates and acknowledging a latch stay in
  the home network. The ESP32-S3 reads only its own sensors and sends
  their values to Home Assistant. Fans are only switched on and off, must
  start again in their last setting, and are switched on when a climate
  value is outdated or invalid; a dehumidifier with its own hygrostat
  keeps power, other climate devices switch off; a Bluetooth climate
  sensor is outdated after three missed reports, never sooner than 120 s.
  You will choose whether the ESP32-S3 compensates EC for the water
  temperature (2 % per °C) or not, with a hint how far off EC can be
  without it; with compensation, EC and pH wait while the water
  temperature sensor fails. The pH calibration on the board's input will
  refuse a probe whose zero point is more than 30 mV off and ask to clean
  or replace it; pH correction then waits for your confirmation.
- **You stay signed in while you use the app** (SD-064): a page left
  open, such as a phone on the wall, renews its sign-in every 10 minutes
  and when it wakes up, so a Stop from it still works the next day.
  Before, the browser dropped the sign-in 12 hours after signing in, even
  while you used the app. A sign-in still ends after 12 hours without
  use, when the hub restarts and when the password changes; a page left
  open says so as soon as it next asks. A Stop from the app still needs a
  signed-in session. More under Security.
- **Plan: access from outside through a relay** (SD-062,
  `docs/ROADMAP.md` M6): if you switch on access from outside at setup or
  later, your ESP32-S3 connects out over its Wi-Fi to a relay. You scan a
  QR code, put the app's icon on your home screen and sign in with your
  password, from anywhere; no router settings, no open port. The
  encryption ends on your ESP32-S3, so the relay is built not to read
  your data. At "View from outside" you see values and history, can
  press "Stop" and acknowledge messages; a latch is acknowledged only at
  home. The
  project runs a relay free of charge, and you can run your own. A
  direct way through a port forward is not offered. Whether an app comes
  into the stores is decided later.
- **Partition tables: room for factory data** (SD-076): both boards get a
  read-only 16 KB partition `fctry` before the first app, for the serial
  number and the setup network's key from the label of pre-flashed
  devices. Nothing else moves: flashing a board you already have keeps
  its settings and data, unless you choose to erase. The installer never
  writes it and a factory reset will keep it; erasing the whole flash
  wipes it.
- **Plan: pre-flashed devices, setup without a cable, support period**
  (SD-076): until a series with hardened firmware, you flash the DevKit
  yourself in the browser; later it can come pre-flashed. Wi-Fi is then
  set up through the ESP32-S3's own setup network with the key from the
  label; on a configured device it opens only when you hold BOOT for
  3 s, for at most 10 min. A factory reset (see the entry on BOOT held
  15 s) erases settings, calibration values, passwords, Wi-Fi and
  history; the label's key stays. A pre-flashed device asks for
  its first password in the home network within 15 min after it joins.
  Every board with our firmware gets security updates for 7 years from
  its delivery, until December (delivered 2028: until at least 12/2035);
  `SECURITY.md` says so, and the web app will show the date. Any
  computer on a USB port of the DevKit can install other firmware
  without a button press, so unplug USB in operation.
- **Plan: one release for every package, Home Assistant as a window on
  the ESP32-S3** (SD-048, SD-050, `docs/ROADMAP.md`): firmware, Home
  Assistant app and simulator keep one version and one release. From M3
  on, Home Assistant shows all values and states of the ESP32-S3, and the
  Home Assistant app opens the ESP's web app in Home Assistant, after a
  security review of the design; access from outside is chosen at setup
  (SD-059); control stays on the ESP. A step-by-step guide for flashing
  follows with the first firmware release (SD-049).
- **Plan: freshness, then the ESP32-S3** (SD-042 to SD-045,
  `docs/ROADMAP.md`): next comes freshness by a sign of life per sensor
  (SD-042): a sensor is stale after 60 s without a measurement, not
  without a change. Then the ESP32-S3, which runs control and safety on
  its own (SD-045). Its firmware stays our own ESP-IDF firmware and will
  speak the ESPHome native API to Home Assistant itself, off by default
  and encrypted (SD-043). It gets its Wi-Fi over USB right after flashing
  in the browser; later changes go over a setup network that opens only
  after a long press on BOOT, with a key per device (SD-044). Raw level
  and EC from Home Assistant, the combined history view and the stop menu
  wait until the ESP foundation stands.
- Build tools of the web app: TypeScript 7.0.2 for the type check and
  Playwright 1.64.0 for the end-to-end tests (proposed by Dependabot). The
  app itself does not change.
- The web app runs on Preact 11.0.1 with the icons of lucide-preact
  1.53.0 (proposed by Dependabot). Nothing visible changes.
- Build tool of the web app: Vite 8.3.3 (proposed by Dependabot); 8.3.3
  closes three advisories of Vite's development server
  (GHSA-rq7h-c2jc-7f22, GHSA-vfpm-58rq-9qcg, GHSA-9jrq-w75r-8gcw), which our
  builds and tests never run. It needs
  lightningcss, which is under MPL-2.0; the license check now allows
  MPL-2.0 for lightningcss as a build tool, which is never shipped
  (SD-046). The code of the app does not change; Vite 8 bundles it with
  Rolldown, so the built files differ.
- CI and release workflows: `actions/upload-artifact` 7.0.2,
  `actions/download-artifact` 8.0.2 and `actions/setup-node` 7.1.0, pinned
  by commit SHA as before (proposed by Dependabot).
- **Home Assistant app on Alpine 3.24** (proposed by Dependabot): the
  server in the app is now linked against musl 1.2.6 and the runtime of
  GCC 15.2 instead of musl 1.2.5 and GCC 14.2 from Alpine 3.22. The image
  ships musl 1.2.6's license notice.
- **License check: the app imports only runtime dependencies** (#102):
  every package the web app imports must be listed under `dependencies`.
  A package listed as a build tool would be bundled into the app but
  checked only as a build tool, without a notice in
  `THIRD_PARTY_NOTICES.md`.
- **Tests on the app image's toolchain** (#112): CI now also builds and
  runs the core tests with the Home Assistant app image's own toolchain
  (musl and the GCC of its Alpine base, its packages and build options,
  checked against the Dockerfile), on amd64 and aarch64, and runs the
  server that ships against a stand-in Home Assistant: a pH of 6.2, an
  unavailable sensor and 5 µS/cm must arrive as 6.2, no value and
  0.005 mS/cm, aged by Home Assistant's clock and timestamps.
- **Type check of the end-to-end tests** (#101): `tools/ci.sh` and CI now
  also type-check `web/e2e`, `web/playwright.config.ts` and
  `web/vite.config.ts` (`npm run typecheck:node`, `web/tsconfig.node.json`).
  Until now Playwright only stripped their types, so a type error showed
  up only when its path ran. Node's types (`@types/node`, MIT, a build
  tool only) follow the Node that CI runs, 22.

### Fixed
- **The mix plan says what to do next; the diagnostic package's "Not
  included" follows the page's language** (#73, SD-055): a pump not
  calibrated says to calibrate it under Devices, a bottle without a pump
  where to choose one, a bottle running short to refill it and mark it
  full under Recipes & bottles, and a recipe whose bottle is gone says to
  edit the recipe. EC control names a bottle without a pump, or with a
  pump the hub does not know, as such instead of as a pump to calibrate.
  What the diagnostic package leaves out ("Not included: password hash,
  …") came in German on an English page; it is named in the page's
  language now. For API clients: every message of a mix plan that names
  a bottle, and an EC round's `mix.part`, `mix.no_pump` and
  `mix.pump_offline`, carry its ID as `canister`, and the package's
  `redacted` lists keys.
- **Maintenance mode says what it does** (#297, RAT-042): its entry in
  the event log and the overview no longer say that locks and sensor
  checks all stay active. While it lasts, readings may jump without
  locking, and monitoring does not assess pH, EC and water temperature;
  missing, frozen or impossible values are still reported, and every
  lock stays on. Next to the button that ends it, a hint says to end it
  once the work at the tank is done and the readings are steady: after
  that, a jump locks the reading until it has stayed steady for 15 min.
- **The smart-plug safety note names the fans that come back on** (#266):
  it said that every outlet stays off after a power cut. The outlets of
  the exhaust and circulation fans come back on by themselves (PD-050),
  unless a Stop is active (PD-076), and the note now says so.
- **The Home Assistant server names a token file it cannot read** (#213):
  a `gc_ha_server --token-file` that was missing, could not be read (no
  permission, a folder it may not enter, a link that loops), was a folder
  or held no token said only "No token: --token-file FILE or GC_HA_TOKEN",
  even with `GC_HA_TOKEN` set, which sent you looking for the token rather
  than the file. Now it names the file and why, as `--config` does. The
  file named still wins over `GC_HA_TOKEN`, and `--token-file ""` (an unset
  shell variable) is refused instead of quietly taking `GC_HA_TOKEN`. A
  token given in place of its file is refused without showing it, since
  the output may go to a log. Both files are opened once, so a named pipe
  works as `--config` too. A `--config` that is a folder now says so
  instead of "the mapping must be a JSON object". `tools/ha_server.test.mjs`
  now also starts it with a `--data` it cannot create and a `--web` it
  cannot read (a link that loops; a folder it may not enter, not as root).
- **A missing saved state on a hub that ran before keeps everything
  stopped** (#238, SD-086): when `state.json` was gone (deleted, or a
  damaged entry the flash reports as not found) but the hub's events were
  there, the hub started as a new one: not stopped, with no locks or
  latches, so a Stop in force was lost and control started again without
  Resume. Now it starts as with a broken state (stopped, a second mix
  right after the start needs confirmation, a dry-run latch in the first
  hour holds) and says "Saved state missing".
- **An unknown saved state keeps the inlet shut until a person releases
  it** (#273, SD-087): a start whose saved state was broken, unreadable or
  missing stayed stopped, but an inlet latch from before was lost with it,
  so after Resume, also from afar, the refill could fill on its own. Now
  such a start also latches the inlet with the reason "Saved state
  unknown" when a valve is assigned, or when the settings are broken,
  unreadable or gone too. Resume brings back the fans and control; water
  flows only after Release. The start's alarm, the refill line and a
  refused switch by hand name the reason, and after Resume the app says
  the inlet stays shut until released under Tank & control. A state file
  kept unread (SD-081) is not written over for this latch; a real cut-off
  of the inlet replaces its reason and is kept.
- **The first fill after the inlet is released waits for the level to
  settle** (#288, SD-088): a person who released the inlet latch could
  have the refill open in the very next second, planned from the level
  before the release, though they had just been asked to check the tank.
  Bailing it then could end in a false "did not rise" latch. Now the first
  fill after a release waits 3 min, as after a Stop, and plans from the
  level read then.
- **Three gaps around the refill** (#289, SD-090): while a fill that is
  due waited for the level to settle (after a Stop, a start, a release or
  irrigation), EC or pH could start a round that the fill then diluted;
  now they wait too. While a due fill waited for a round to settle, EC or
  pH started the next round of their correction at once, so the fill
  waited for all of it; now no new round starts, and the line reads
  "Waiting: refill first". Switching the refill off and on again let a fill open
  at once from a reading taken while someone worked at the tank; now it
  waits for the level to settle, as after a Stop. And an inlet latch
  released during a Stop, maintenance or with the refill off kept the
  count of fills not judged, so the next such fill could latch the inlet
  at once; now a latch resets it however it is released.
- **A cold refill no longer locks the water temperature** (#272,
  RAT-042): while the inlet is open and for 5 min after it closes, the
  hub now counts a change of the water temperature as expected, as it
  already did for EC, pH and the level. Before, fresh water much colder
  than a small tank could move the water temperature by more than 3 °C
  within 5 min, and the hub raised a jump lock and an alarm for a change
  it had caused itself. In tests, the simulator can now give the inlet
  water a temperature of its own.
- **Settings files: whole numbers only.** Restoring settings refuses a
  file, with the reason, when a count or index in it is not a whole
  number in its range: the current phase of a running grow cycle (it has
  to be one of its phases), a phase's days, a device's channel and the
  schema version. A time in the file that the hub cannot have written
  reads as not set. "Next phase" checks the current phase too and says
  how to go on: finish the cycle and start a new one. A hub whose saved
  settings already hold such a phase or such days keeps running; while
  that cycle runs, a change says the same, and once it is finished
  nothing is blocked.
- **A refused change says why in the page's language** (#18): when the
  configuration check refused a recipe, for example because the chart EC
  does not fit the amounts, a German page showed the reason in English.
  It now shows it in German.
- **A large refill no longer latches the inlet** (#155, SD-082): a
  refill planned longer than the valve may stay open ("Ventil höchstens
  offen", 15 min by default) ran into that limit, which shut the inlet as
  a fault and waited for a person, after every refill from a nearly empty
  tank. Now one fill keeps the valve open at most a minute less than the
  limit (half of it, if that is shorter), and the rest follows in steps
  with a 10 min pause between them, each step named in the refill's line
  ("Füllt: Etappe 1 von 2 …"). Each fill waits until the level reading
  has been undisturbed for 3 min, for example after irrigation (there at
  most 30 min) or maintenance.
  EC and pH wait from the first opening until the last step is checked;
  a mix or hand dose meanwhile ends the steps.
  The limit itself is unchanged and still closes a valve the hub opened.
  The refill no longer takes over an inlet opened by hand, and says that
  such a valve locks after the limit.
- **The web SBOM lists preact** (#201, SD-075): the release built
  `sbom-web.cdx.json` with `npm sbom --omit dev`, which drops every
  package a build tool depends on, peers included, so it left out
  `preact`, the app's core library. The web SBOMs of 0.1.0-proto.2 to
  proto.4 lack `preact` 10.29.8 (`pkg:npm/preact@10.29.8`, MIT); no
  advisory known on 2026-10-15 affects that version. The SBOM now lists exactly the runtime packages of
  `web/package-lock.json`, the packages the build lets into the app, as
  npm describes them, and a test fails when it differs from them.
- **The simulator and the Home Assistant server no longer end at the start
  on a bad path or value** (#213): `gc_ha_server --config` with a file it
  cannot read (below a folder it may not enter, a link that loops, no
  permission) now says so and exits 2, instead of crashing or saying the
  file holds no mapping. In `gc_sim_server`, a `--port`, `--speed` or
  `--prefill` that is no number now says so and exits 2 instead of
  crashing, and one out of range (port 1–65535, time-lapse 0–600 as in the
  simulator panel, history ahead 0–1000 h) is refused instead of taken;
  both servers name the range. A start without arguments that finds
  neither its own folder nor the working folder runs in memory only and
  says so. A data folder it cannot create is no longer named as its data
  folder. The port search stops at 65535. The download's smoke test also
  starts it plainly with a data folder it cannot read (SD-074) and with no
  folder for the data, on macOS and Linux (#214).
  `tools/ha_server.test.mjs` now also starts `gc_ha_server` with a
  `--config` below a folder it may not enter (not as root).
- **The hub keeps a file it cannot read** (#211, SD-081): a file of the
  hub that is there but cannot be read (no permission, for example root's
  files after a start with `sudo`, or a read error of the disk or flash)
  was taken as missing: the hub started without the Stop that was in
  force and without its settings or password, and later saved over the
  file. Now it starts as with a broken file and does not write over it,
  a snapshot and its journal together. One alarm per kind of data says
  what this means: a run-time state it cannot read keeps everything
  stopped and is written anew at the first Stop or Resume or a new lock;
  settings it cannot read give the factory settings; a password file it
  cannot read refuses a first password over it and no longer advises a
  factory reset; a job it cannot read counts as a mix cut off by the
  restart, and the next job writes it anew. A run-time state that is
  broken or cannot be read also makes a second mix right after the start
  need confirmation and holds a dry-run latch in the first hour for a
  person. This applies to the Home Assistant server, which also prints
  such a file at the start with the reason, and, through its storage, to
  the device. The hub's files are listed in one place (#212).
- **The simulator keeps a data folder it cannot read** (SD-074): when
  `--data`, or the folder next to the program on a start without
  arguments, could not be read (a folder it may not enter, for example
  one an earlier start with `sudo` made, or a link that loops), it ended
  at the start; started by a double click, its window just closed. When
  only files in it could not be read (for example root's files after a
  start with `sudo`), it started without its configuration and password
  and later saved over them. Now it says "Data cannot be read (…).
  Running in memory only." and runs in memory only. It reads, writes and
  removes nothing in that folder, so data of an earlier start stays as
  it was.
- **After a Stop, an interrupted control round mixes in before the next decision**
  (#180, SD-073): a Stop that cut an EC or pH round, while it dosed or
  waited for the dose to mix in, only reset the controller. After Resume
  it could start the next round right away, on a reading the cut dose had
  not mixed into, since the circulation pump is off while stopped; too
  much EC can only be undone by diluting. Now, as after a restart, the cut
  round first waits for the circulation pump and then settles with it
  running, for its settle time but at least 15 minutes, while the other
  controller and refill wait. Refill is not held while the round waits for
  the pump, which may need the level back; the line says why ("Blocked:
  circulation pump not running – after the Stop, mixing comes first", or,
  when the hub refuses the pump, "Blocked: circulation pump can't run –
  after the Stop, mixing comes first") (RAT-036, RAT-052, RAT-004,
  RAT-047).
- **The simulator starts with a web app folder it cannot read**: when
  `--web` pointed into a folder the simulator may not enter, or through a
  link that loops, it ended at the start ("filesystem error: status").
  Now such a folder counts as no web app: it says why, starts with the
  embedded web app or the "not built" page, and the API answers
  (`tools/sim_server.test.mjs`), as the Home Assistant server does since
  #80.
- **Editing a bottle out of its pair no longer blocks later saves**
  (#152): clearing or changing part A's pair label, or making it a pH
  bottle, left part B alone with the label, and the hub then refused
  saving a recipe, the tank, a zone or a function until the label was
  cleared by hand, as removing a bottle did before #147. Now the bottle
  left behind is no longer paired; the bottle editor says so before you
  save, and the event log afterwards. With a new label, part A is that
  pair's first bottle until its partner joins: a recipe, also one from a
  template, may already hold it; other saves wait for the partner, as
  before. An edit that would leave a recipe with only half of a pair
  (for example a third bottle joining a pair the recipe holds two of) is
  refused and nothing is saved. The hub now lets a recipe hold both
  bottles before the second gets the label, so two bottles can be paired
  when a recipe holds only one of them: put the other in first.
- **A bottle can go into an existing recipe** (#159): the recipe editor
  listed only the recipe's own bottles, so a bottle added later could
  only go into a new recipe. It now lists every nutrient bottle, the
  recipe's own first: an amount puts one in, numbered in its row's place,
  and clearing it takes it out. Only bottles with an amount get a number
  and the order buttons, which pass over the others; each value stays
  with its bottle when the order changes (before, a value could show in
  the wrong row), and the focus moves with the row. Each amount field is
  named for screen readers, and on a phone the rows no longer run past
  the screen with long names. A long name without spaces now breaks in
  the bottle list and as a recipe card's title too, instead of running
  past the card.
- **The recipe and bottle editors say before saving what the hub would
  refuse** (#159), with the way out, in German and English, and Save
  stays off until it is fixed: a pair only partly in a recipe (naming
  the bottles missing), a recipe without a name or an amount, an amount
  below 0, a bottle without a name, a pair label that would leave a
  recipe with part of a pair, and a pH bottle that a recipe still holds.
  A screen reader reads each change. Before, the hub refused after Save
  with a German-only message.
- **An unknown stock no longer turns "full" when a bottle is saved**
  (#184): any save of a bottle whose stock was unknown (a new name, a
  colour, a pump, a first size), for example after importing a backup
  onto a new hub, set its stock to its size without a word. Its low-stock
  and below-zero warnings then came late, and a mix check passed that the
  bottle could not cover. The stock now stays unknown until an amount is
  entered or "Bottle changed (full)" is confirmed (RAT-015); a new bottle
  whose amount is left empty stays unknown too. The bottle editor and
  the setup no longer fill in 1000 ml for a bottle without a size, a new
  bottle's amount follows its size until the amount itself is changed,
  and the overview no longer counts an unknown stock as enough. Where
  the hub does not know a stock, the bottle list and the overview say
  so ("unknown" instead of "?", a dashed bar) and how to enter it, and
  the amount field's hint fits the bottle's state.
- **A new bottle no longer takes over an old stock** (#161): importing a
  backup without a bottle kept that bottle's stock, and a new bottle of
  the same name created without a stock, as the setup wizard does, then
  started with it instead of its size. An import now keeps only the
  stock of the bottles the hub had and the file keeps, and a new bottle
  starts with the stock entered, else its size, else unknown.
- **A removed bottle's stock is saved at once** (#152): until the next
  regular save, a power cut brought it back, and a new bottle of the same
  name then showed the old stock.
- **Removing one bottle of a pair no longer blocks every later save**
  (#147): the other bottle kept its pair label on its own, and the hub
  then refused saving a recipe, the tank, a zone or a function, binding a
  role, importing a configuration or starting a grow ("Paar AB hat nur
  einen Kanister") until the label was cleared by hand. Now the bottle left
  behind is no longer paired; the removal dialog and the event say so. If
  a bottle is already left with a pair label of its own, clear its "Pair"
  field under Recipes › Bottles.
- **Home Assistant: only plain numbers are values** (#71): a state such as
  `0x6`, ` 6.2`, `+6.2` or `.5` was read as a number; now it gives no
  value instead of a guess, and the device says why ("is no plain
  number"). The setup now takes "calibrated outside the hub" from the hub
  instead of guessing it from what a device measures. Text from Home
  Assistant in names and faults is checked to be well-formed UTF-8 by the
  adapter itself, too.
- **Removing a bottle asks first** (#120): under Recipes › Bottles the
  remove button took a bottle away at once, with its stock, and on a phone
  it sits next to "Edit". Now "Remove bottle?" ("Kanister entfernen?")
  names it and what follows (its stock goes; its pump, if it has one,
  becomes free and stays calibrated if it was); only "Remove" removes it.
  A bottle a recipe still uses says "Bottle still in use", names the
  recipes and how to take it out (both bottles of a pair, since the hub
  refuses half a pair) and, when exactly one recipe uses it, offers "Edit
  recipe" (with several: "Got it"), instead of offering a removal the hub
  refuses.
- **Dialogs and the keyboard** (#105): a dialog now takes the focus when it
  opens, Tab stays inside it (also a folded list's heading, and back in
  when the focus has fallen out), the focus returns to the button that
  opened it when that is known (after removing a bottle: to "+ Bottle"),
  and a screen reader names it by its title; a confirmation (removing a
  bottle, mixing a tank again) reads out its consequence.
- **Durations and ages stay on one line** (#106): "vor 45 s" / "45 s ago"
  and the hub's texts with seconds, minutes and hours ("seit 75 s nichts")
  no longer break between the number and the unit; other numbers with
  units (ml, ml/L) follow (#148).
- **A stock below zero is reported** (RAT-015, RAT-071, SD-066): when more
  was booked from a bottle than its stock, the hub set the stock to 0 and
  showed "0 ml left – change the bottle soon", which hid that more had
  been booked than entered and that doses after that went in short. Now
  the stock stays below zero ("-2 ml by the hub's count – bottle empty?
  Change it, or enter the amount left"; German "Rechnerisch -2 ml …"), the
  event log says each time it falls below zero what to do (empty? change
  it; otherwise enter the amount left under Recipes & bottles; calibrate
  the pump again only if it keeps happening), and a mix says the stock is
  used up. Entering the amount left or tapping "Bottle changed" clears it.
  Less than half a millilitre below zero raises no alarm. An amount left
  entered below zero is refused, and the bottle editor sends it only when
  you changed it, so a dose booked while the editor is open is kept.
- **A pH jump is caught while the hub's own pH dose settles** (RAT-042,
  RAT-044): after its own pH dose, the hub took any pH change as expected
  for the whole wait after the dose plus a minute (6 min by default), so a
  disturbed probe did not lock the reading, and pH control then dosed pH−
  on the wrong value. Now the reading locks, and pH control doses nothing
  until the lock lifts after 15 min of calm. More under Security.
- **Mixing twice is caught after a long mix too** (RAT-003): another mix
  within 30 minutes needs your confirmation, because dosing again doubles
  the nutrients. The 30 minutes counted from the start of the last mix, so
  after a mix that took longer, for example a guided one stirred by hand or
  a large tank, a second mix right after it started without asking. They
  now count from when the last mix ended: finished, stopped, failed or cut
  off by a restart. A mix that stopped part way can no longer be replaced
  by a new one through the API; resume or end it first. A prime, a pump
  calibration or a hand dose started meanwhile ends it and reports what is
  in the tank, where before it dropped the mix without a trace.
- **The hub checks the time zone:** it stored any text sent as the time
  zone, and only the app checked it. Now saving or importing one that is
  not a name like `Europe/Berlin` (starting with a letter; letters, digits,
  `_`, `+`, `-` in parts separated by `/`, none empty; at most 64 characters) is refused
  with its reason, and nothing else of the change is taken over; the app
  checks the same form at the field. A zone saved before that is no such
  name falls back to `Europe/Berlin` when the hub starts, an event says so,
  and the configuration stays readable: one that was not text at all made
  the hub start with factory settings. Nothing in the hub uses the zone
  yet; light and watering times will.
- **Stop is never silent** (SD-055): a Stop the hub does not confirm
  within 5 seconds, or that gets no connection, says so in its dialog
  ("Stopp nicht bestätigt – keine Antwort vom Hub") with what to do in an
  emergency (unplug the hub; with smart plugs, also switch off the devices
  on them directly), and offers to try again; while sending, the button
  says so. A refusal from the hub starts with the same words and gives its
  reason. A Stop that arrives after all closes the dialog; one that fails
  after the dialog was closed shows a message. The time limit also works in
  browsers before iOS 16. A failed Resume says why instead of nothing.
  When the session has run out, the sign-in page says that what was just
  clicked was not carried out.
- **No raw error texts:** an action without an answer says "Der Hub
  antwortet nicht …" instead of "TypeError: Failed to fetch", any other
  unexpected failure says it did not work, everywhere in the app (switching
  functions and outputs, devices, Home Assistant, the simulator panel,
  calibration, sign-in); a switch or choice that fails says so too (in
  Settings the update channel, the update check and the language failed
  without a word); a file that is no saved configuration says so.
- **Values that stop coming show:** after 10 seconds without new values
  from the live stream or the polls, a phone shows a bar under the top bar
  that stays in view while scrolling, and the badge on a wider screen says
  offline; while the polls stand in for the stream, neither does. A stream
  that is open but brings nothing is polled, until it brings values again. A device row with calibrate
  buttons puts them below the name on a phone, which no longer breaks
  letter by letter.
- **German mix page:** the hub's closing note on the plan ("pH zuletzt …")
  shows in German, not in English; the plan's errors and warnings go
  through the same translation, though some are still German on the
  English page (#18). The top-up note says plainly that the amounts are only
  for the fresh water.
- **History chart:** the time axis follows the page's language and the
  24-hour clock (it showed "3pm" and "10/11/26"); ticks a day apart show the
  date, and closer ones show it at midnight.
- **Time zone:** the setup proposes the browser's time zone on the first
  run and keeps the chosen one when you go back or open it again later,
  instead of overwriting it. Setup and Settings refuse a time zone that does
  not exist at the field and save it in its usual spelling
  ("europe/berlin" becomes "Europe/Berlin"). The setup's first step is
  called "Grundlagen", its text names its five steps, and Done has no
  number, also in the Home Assistant introduction.
- **Phone: the app opens at 100 % and needs no zooming out** (#33): on an
  iPhone, Safari zoomed in when the password field got the focus, because
  the fields were smaller than 16 px, and stayed zoomed after signing in.
  Fields are 16 px now. At 320 px nothing tested is wider than the screen
  any more: the bottle table under Recipes becomes cards with larger
  buttons, long device and Home Assistant sensor IDs wrap, the "new
  devices" note puts its button below the text, and "Weiter" (next) in the
  setup no longer runs off the screen. A phone test at 320 px checks the
  pages of the menu in German and English, every setup step, and the Home
  Assistant app's introduction, devices, assignment and sign-of-life
  window with long sensor IDs.
- **Event log: a device is "back" only after it was gone** (#69, #115):
  when every device stopped answering at once, for example while Home
  Assistant restarted, the log said "getrennt" (not responding) for each
  of them but never "wieder da" (is back). Now each device you added logs
  that it is back. A start logs no "back", also for devices that answer
  later than the first ones, such as Home Assistant's integrations
  reconnecting one after another or the hub's bus devices. So a pump asks
  whether it is still on its bottle only when it was unplugged and plugged
  in again while the hub runs. "Back" now names the device as "not
  responding" does: its name, else its kind (before, "not responding"
  named a device without a name by its ID). In German, "getrennt" now
  says what to check (cable, power or Wi-Fi), and the pump's question
  names the bottle as English does.
- **Calibrating a probe:** closing the window half way no longer leaves the
  calibration running on the hub, and opening it again starts at the first
  buffer instead of showing a step the hub did not have. The window now
  says when the hub ended the calibration (a restart, a long pause) and
  offers to start again after a refused save.
- **The web app ships no code of Vite:** Vite added its modulepreload
  polyfill to the built app, while the third-party notices list Vite as
  used for building only. The app preloads nothing, so the polyfill is off,
  and the build now fails if code of Vite shows up in the app again.

### Security
Affected: every published version (0.1.0-proto.2 to 0.1.0-proto.4),
fixed in this release. Each item names its affected versions, impact,
severity and remedy (SD-079). Four are vulnerabilities: someone without
the needed rights, a device in the network or a crafted file, could
trigger them. Each gets a GitHub Security Advisory (GHSA), published
with this release; we request a CVE from GitHub for the three of our own,
and the fourth refers to AMD's CVE-2025-62626. Four are safety and robustness
fixes: normal use, a faulty probe or someone with the password triggers
them, so they get no advisory.

Vulnerabilities, each with an advisory:

- **Nobody else can set the Home Assistant app's first password** (#79,
  SD-068): until the owner set it, any device in the home network could
  set it first, see the names of Home Assistant's sensors and the values of
  those it could pick, change the app's settings, and lock the owner out
  until the app was reinstalled. The app now prints a one-time setup code to its log, which
  only Home Assistant's administrators, and apps they gave that right, can
  read, and the first password needs it. The code has 80 bits, is kept
  in memory only and is used up by the password; a restart makes a new
  one. Wrong codes lock nobody out and are logged at most once per 15
  minutes.
  - Affected: the Home Assistant app, 0.1.0-proto.2 to 0.1.0-proto.4.
  - Impact: needs a device in the home network before the owner sets the
    first password; the Home Assistant app switches nothing.
  - Severity: medium, CVSS 3.1 6.4
    (`CVSS:3.1/AV:A/AC:H/PR:N/UI:N/S:U/C:L/I:L/A:H`), CWE-1188.
  - Remedy: update to 0.1.0-proto.5. The update keeps a password that is
    already set: if you did not set it yourself, reinstall the app or
    restore a backup made before that password was set, then set the
    password or update at once. On an older version, set the password
    right after installing.
  - Advisory: a GHSA, published with this release; a CVE requested from GitHub.
- **Someone guessing the password no longer locks everyone out** (SD-051):
  the lockout after 5 failed attempts counted for all devices together, so
  a device in the network that, after about ten wrong passwords, sent one
  every 15 minutes kept every other one from signing in, the owner's phone
  included, and in the simulator from "Stop" unless it was still signed
  in. Now a device that has signed in
  since the hub last started has its own count, so guessing from another
  address no longer locks it out; all devices that never signed in share
  one count, so guessing from many of them stays as slow as from one. The
  address comes from the connection, never from a header. A wrong old
  password when changing it counts as a failed attempt and is logged; the
  device that changed the password keeps its own count. Only counted
  failures are written to the event log: neither a try refused during the
  wait nor one before the first password is.
  - Affected: the Home Assistant app and the simulator, 0.1.0-proto.2 to
    0.1.0-proto.4.
  - Impact: needs a device in the home network (for the simulator only
    when it was started with `--host`); a device already signed in kept
    its session and, in the simulator, "Stop".
  - Severity: medium, CVSS 3.1 4.3
    (`CVSS:3.1/AV:A/AC:L/PR:N/UI:N/S:U/C:N/I:N/A:L`), CWE-645.
  - Remedy: update to 0.1.0-proto.5. Until then, keep the devices you
    need signed in; "Sign-in failed" in the event log shows when someone
    guesses, not from which device, so look for an unknown device in your
    router's list of connected devices.
  - Advisory: a GHSA, published with this release; a CVE requested from GitHub.
- **Random numbers no longer come through RDSEED** (AMD-SB-7055,
  CVE-2025-62626): the Home Assistant app and the simulator took the
  random numbers for session tokens and password salts from
  `std::random_device`, which on Linux x86-64 tries the processor's RDSEED
  instruction first. AMD Zen 5 processors without AMD's fix can return
  zeros there while reporting success, so on such a host part of a session
  token could be zero and easier to guess. They now come from the
  operating system (`getentropy()`), as does the setup code. Not affected:
  aarch64 (a Raspberry Pi), macOS, Windows, and Zen 5 hosts with AMD's
  microcode or BIOS fix. Nothing to do after the update: the restart ends
  every old session, and salts need not be secret.
  - Affected: the Home Assistant app on amd64 and the simulator on Linux
    x86-64, 0.1.0-proto.2 to 0.1.0-proto.4.
  - Impact: needs a Linux x86-64 host with an AMD Zen 5 processor without
    AMD's fix (for the simulator also a start with `--host`), and even
    there a session token or salt is only partly predictable.
  - Severity: medium, CVSS 3.1 4.2
    (`CVSS:3.1/AV:A/AC:H/PR:N/UI:N/S:U/C:L/I:L/A:N`), CWE-331.
  - Remedy: update to 0.1.0-proto.5. Until then, install AMD's microcode
    or BIOS fix.
  - Advisory: a GHSA, published with this release; no CVE of its own, it
    refers to CVE-2025-62626.
- **An import could show a pH calibrated in Home Assistant as checked**
  (SD-041): a crafted backup could give a Home Assistant pH sensor the
  class of the hub's own pH probe, with a calibration; its values then
  showed as checked by the hub ("Valid", without "Calibrated elsewhere")
  and counted as usable until the app restarted. Now a device whose
  reported class differs from the configured one has no value, and an
  import gives a calibration only to a class that offers it.
  - Affected: the Home Assistant app, 0.1.0-proto.2 to 0.1.0-proto.4.
  - Impact: a crafted backup; the owner must import it, which needs the
    password, and the Home Assistant app cannot switch anything.
  - Severity: low, CVSS 3.1 3.3
    (`CVSS:3.1/AV:L/AC:L/PR:N/UI:R/S:U/C:N/I:L/A:N`), CWE-807.
  - Remedy: update to 0.1.0-proto.5. Until then, import only backups you
    made yourself.
  - Advisory: a GHSA, published with this release; a CVE requested from GitHub.

Safety and robustness fixes, no vulnerability:

- **An abandoned probe calibration no longer keeps jump checks off:** a
  calibration window closed without saving left the calibration open on
  the hub until a restart; meanwhile jumps of that probe were taken as the
  calibration (RAT-042). A calibration now ends when its window closes and
  with its device; one left for 30 min without a step no longer explains
  jumps, while pH and EC control keep waiting until it is saved or
  cancelled, since the probe may still be in a buffer.
  - Affected: the simulator, 0.1.0-proto.2 to 0.1.0-proto.4; in the Home
    Assistant app only through a crafted import.
  - Impact: concerns the hub's own probes, which only the simulator has
    so far.
  - Severity: low; no CVSS, as it is not a vulnerability.
  - Remedy: update to 0.1.0-proto.5. Until then, restart the simulator
    after a calibration you did not finish.
- **A new sign-in ends the session used longest ago:** with 16 sessions
  open, a 17th sign-in ended a random one of them, so the phone someone was
  using could be signed out and had to sign in again before it could
  reach "Stop" in the simulator. Now the session used longest ago ends. Found through a test of the
  lockout (SD-051) that failed in about 1 of 16 runs.
  - Affected: the Home Assistant app and the simulator, 0.1.0-proto.2 to
    0.1.0-proto.4.
  - Impact: needs 16 other sign-ins within 12 hours, each with the
    password, and whoever has the password can already stop, switch or
    change it.
  - Severity: low; no CVSS, as it is not a vulnerability.
  - Remedy: update to 0.1.0-proto.5.
- **A disturbed pH probe is no longer taken as the hub's own pH dose**
  (RAT-042, RAT-044): the hub announced its own pH dose to the jump check
  until a minute after the dose's wait, against RAT-042, which keeps own pH
  doses unannounced because they are sized to move pH by at most 0.3, far
  below the 1.0 of a jump. A jump in that time became the new reference,
  and pH control dosed on it. The jump lock now holds then as at any other
  time. It can now also trip when an own dose moves pH far more than
  planned, which stops pH control until 15 min of calm, the safe direction;
  that dose's effect is then not learned, which a follow-up addresses. A
  jump in the last minutes of another announcement (after a mix or hand
  dose, the inlet, an EC round or a probe calibration, and in the 10 s
  after maintenance mode was ended early) is still taken as explained;
  that stays open.
  - Affected: the simulator, 0.1.0-proto.2 to 0.1.0-proto.4.
  - Impact: concerns the hub's own probes, which only the simulator has
    so far; there, pH control dosed twice on the jumped reading.
  - Severity: low; no CVSS, as it is not a vulnerability.
  - Remedy: update to 0.1.0-proto.5.
- **Ending maintenance mode early switches jump checks back on** (#292,
  RAT-042): while maintenance mode lasted, the hub announced a change of
  every reading until the end of the window the user had asked for, and
  ending or shortening it kept those announcements. A user who started
  it for 240 min and ended it after 5 min left the jump lock off for
  every sensor for almost four more hours. Maintenance now announces a
  change only 10 s ahead, renewed on every tick, never past its end:
  ended early, a jump locks again within 10 s; a window that runs out
  behaves as before.
  - Affected: the Home Assistant app and the simulator, 0.1.0-proto.2 to
    0.1.0-proto.4.
  - Impact: a reading that jumped in that time was not locked: it showed
    as valid (or as calibrated elsewhere) and went into the history, and
    no jump alarm was logged. In the simulator, pH and EC control could
    dose on it within their caps per dose, and a level that jumped could
    let the circulation pump's dry-run latch release itself and the pumps
    and the inlet act on it. The Home Assistant app switches nothing.
  - Severity: low; no CVSS, as it is not a vulnerability.
  - Remedy: update to 0.1.0-proto.5. Until then, after ending or
    shortening maintenance mode, restart the app or the simulator ("Power
    cut / restart") while no mix or dose runs.

A protective cut-off added:

- **A refill after which the level did not rise latches the inlet**
  (#155, SD-082, RAT-031): no refill checked that water arrived. With an
  empty supply, a leak, a stuck valve or a level sensor that does not
  follow, the inlet opened again after every pause for as long as the
  level read below the start level. Now, at the end of each fill's pause,
  the level must have risen by at least half of what flowed in by the set
  rate; otherwise the inlet is latched until a person releases it, and
  the alarm names what to check. A fill whose rise cannot be judged (a
  rise too small while the irrigation pump ran or its state was not
  known, no valid or fresh level, a level calibration, a fill cut by a
  restart) ends its steps; fills not confirmed are added up and never
  fill past the target or stay open longer than one fill may before they
  are judged, and two not judged in a row, also across refills, latch
  the inlet too. A Stop, maintenance, switching the refill off or a level
  calibration keeps what flowed unconfirmed until the level reading has
  moved by 1 L, so a stuck level gets no second full fill through them;
  a level calibration ends a fill, and the refill waits while it runs. A pending check
  survives a restart. Residual risk: a
  level reading wrongly low still gets fills up to the target or one
  fill's open time before the latch; with start level and target less
  than 4 L apart, a level that hovers at the start level without
  following can let small fills repeat; an inlet on a socket that keeps
  running while the hub loses power or hangs is not counted for that
  time; an irrigation pump that draws more than half a fill in the pause
  of two refills in a row, or a level curve imported (not calibrated)
  while a refill waits to be checked, latches a sound inlet; and an irrigation pump the hub does
  not know can make a correct fill look like a missing rise: bind it as
  the irrigation pump. After the update, check that the inlet rate under
  Functions is about right: one set at more than twice the real rate now
  latches every refill.

A protective cut-off relaxed on purpose:

- **A large refill no longer ends in the open-time latch** (#155,
  SD-082): a fill planned longer than the valve may stay open used to be
  cut by that limit, which latched the inlet. That also stopped a large
  refill from a level reading wrongly low after one fill. A fill now
  stops a minute before the limit and the rest follows in steps, also
  above the start level; that case is caught by the rise check above
  instead. The limit itself still closes and latches a valve the hub
  opened.
- **A protective lock that releases itself, deliberately** (SD-067, #27):
  the circulation pump's dry-run lock, when caused by a low level, now
  lifts by itself after the tank is refilled (see Changed). This relaxes a
  protective cut-off on purpose, as SD-022 asks. It stays locked until a
  person releases it if switching off failed or the pump ran again, if the level was low again
  within an hour of the last self-release, if the level jumped while
  locked, and for a lock saved by an older version or with anything else
  stored with it; the inlet, EC and pH locks never release themselves.
  Residual risk: a level sensor that reads a refilled tank that is not
  can release the lock while the tank is still low; a jump of more than
  20 L in 5 minutes holds it, a slow drift, a small tank, a wrong reading
  during a refill or right after a restart do not (SD-067).

Hardening, without a vulnerability behind it:

- **CI and the app image build with Node 22.23.4** (SD-070, SD-071): the
  Node that builds the web app and checks its packages' registry
  signatures moves from 22.23.3 to Node's security release of
  2026-10-20: OpenSSL 3.5.9, new root certificates and, for Node 22,
  CVE-2026-105481, CVE-2026-105482, CVE-2026-106069, CVE-2026-106072,
  CVE-2026-106073, CVE-2026-106074 and CVE-2026-111497
  (https://nodejs.org/en/blog/vulnerability/october-2026-security-releases).
  No Node code ships, and npm stays 10.9.9; every package it fetches is
  checked against its sha512 in the lockfile and its registry signature,
  so none of these could have changed a published artifact. On amd64,
  the app image's `node` image now unpacks the build from `nodejs.org`,
  checked against the Node release keys, instead of an unofficial build.
  No published version affected.
- **The session comes only from a cookie named exactly `gc_session`**
  (Home Assistant app, simulator and firmware): the servers now split the
  browser's `Cookie` header into its name=value pairs and take the session
  only from the pair of that name, and only when there is exactly one such
  pair; two cookies of that name count as none. This happens in one place
  in the core (`gc::sessionToken`).
- **Three security fixes are guarded against coming back** (#234); each
  check fails when its fix is taken out:
  - `tools/arch_check.sh` refuses `std::random_device` and the RDSEED
    and RDRAND instructions in our own code of the core, the simulator,
    the Home Assistant server and the firmware. The one exception is the
    Windows branch of `sim::randomBytes`. `std::random_device` tries
    RDSEED first on Linux x86-64 (AMD-SB-7055). On the code of
    0.1.0-proto.4 the check names both places that used it. The test of
    `sim::randomBytes` now also checks that it fills the buffer to the
    end.
  - A scenario test leaves a calibration of the hub's own pH probe open
    for 45 min. A jump must then lock the reading, and pH control must
    not dose on it (RAT-042). So far the test of that fix covered only a
    probe from Home Assistant.
  - A test imports a backup that gives a pH calibrated in Home Assistant
    the class of the hub's own pH head, with a calibration. That was the
    way in up to 0.1.0-proto.4. The reading must stay without a value,
    and the app must not offer a calibration for it (RAT-025, SD-041).
- **The web build checks every module the bundler puts into the app**
  (#190, PD-228, SD-075): the app's code is to come only from our
  sources and the runtime packages of the lockfile, and the build
  searched the output only for three markers of Vite.
  Rolldown's runtime helpers or a build-time package leave no such marker
  in minified code. The build now looks at every module of every chunk
  and fails on one that is neither our source nor a runtime package of
  the lockfile, and when it checked nothing. The app holds none today,
  and it builds the same byte for byte as without the check.
- **The signature check runs with a pinned npm, and says why it fails**
  (#163, SD-070, SD-071): the npm that checks the web app's packages
  against their registry signatures (SD-058) came with whatever Node the
  runner image had, and in the Home Assistant app image with Alpine's.
  Every job that sets up Node now runs the one named in `.nvmrc`, raised
  by hand with a review and compared with Node's security releases at
  every release; the app image runs the same Node from the official
  `node` image, pinned by digest, and prints the versions. When a job
  with the check fails, a step that runs only then shows the end of npm's
  own log of the check and asks npm again for its reasons; until now the
  output named only the package, so a failure on 2026-10-14 that passed
  on re-run could not be explained. The check itself is unchanged.
- **A Stop holds even when booking the running dose fails** (RAT-036):
  a Stop booked the running dose and ended the job before it switched the
  inlet, the circulation pump and the sockets off. An error while booking
  left them on and the Stop unsaved, and the app showed an internal error.
  Now the Stop takes effect, everything goes off and the Stop is saved
  first; the gateway refuses any start meanwhile. A dose that cannot be
  booked is dropped, so nothing of it runs or is booked later, and the log
  says "Stop: dose not fully counted" with the bottle, what to
  check and the reason. No path to such an error is known today.
- **Sessions last while they are in use** (SD-064): the browser kept the
  sign-in cookie 12 hours from signing in, a fixed upper limit even for a
  page in use; now a page in use renews it (`GET /auth/session` sends it
  again) and there is no fixed longest lifetime. A session still ends
  after 12 hours without a request, on a restart and on a password change.
  Residual risk: a session once obtained (a lost or shared phone, a
  browser left signed in, a cookie read off the unencrypted home network
  until HTTPS) stays valid as long as it is used at least every 12 hours;
  changing the password ends every session at once.
- **The Home Assistant app no longer runs as root** (#80): the server ran
  in its container with full administrator rights (root, with Docker's
  default capabilities), and Home Assistant starts every app without a
  seccomp filter, so a flaw in its web server would have given an attacker
  root in the container. Now, at every start and before it opens its port,
  it hands its data folder and what is in it to an account of its own
  (user 65532; for a link, the link itself, never the file it points to; a
  data folder that is a link is refused) and switches to that account for
  good: no root, no capabilities, no new privileges; then it makes the
  folder private again. Nothing to do after the update. If it cannot, the
  app does not start and its log says why and what to do; your data is
  kept. The app also brings an AppArmor profile (`ha/app/apparmor.txt`):
  besides the basic system files every confined program may read, the
  server reads only its web app and `/etc/hosts`, writes only its data
  folder and uses only TCP over IPv4. CI now runs the app image the way
  the Supervisor starts it, in that profile, also as a new install and
  after a restart, fails if the profile refuses anything, and checks that
  the profile loads with Debian 12's older AppArmor. Found on the way: a
  web folder the server may not read stopped it at the start; now it
  starts without the web app and says so in its log. Residual risk:
  docker-init, which the Supervisor starts first, stays root next to the
  server with the profile's capabilities (chown, dac_override, kill,
  setgid, setuid); it runs nothing but the server, which can no longer
  signal it. A host without AppArmor runs the app without the profile.
  From the merge until the release, a new install gets the previous image
  in the new profile, since Home Assistant takes the profile from the
  repository.
- **Registry signatures of the web app's packages** (#100, SD-058):
  wherever the web app's packages are installed (CI, the simulator
  packages, the release build, the Home Assistant app image),
  `npm audit signatures` now runs right after the install, before any of
  them runs. A missing or invalid npm registry signature or an invalid
  provenance attestation fails the build; packages without provenance
  pass. Every package in the lockfile must come from its own record on
  the npm registry. This proves the registry's signed record of each
  package; it does not protect against a release from a hijacked
  maintainer account, against which the 7-day wait for updates and the
  lockfile's hashes stay the guards.
- **Provenance must not drop** (#128, SD-063): before the web app's
  packages are installed, CI finds for every package in the lockfile the
  highest lower version that was published with provenance. If the locked
  version has none, or has it from another source repository, the build
  fails: that is the typical sign of a release published by hand from a
  stolen token, which the registry signs too. A case accepted after
  review is listed with its exact problem and the reason; today two:
  `@rolldown/pluginutils`, which moved to the `rolldown/plugins`
  repository, and `undici-types` 6.21.0, from a stretch of 2024 releases
  by the same maintainer without provenance.

## [0.1.0-proto.4] – 2026-10-11

Fourth prototype: more from the first test of the Home Assistant app.
Control still runs only in the simulator; the Home Assistant app only
reads sensors and switches nothing. No compatibility promises.

- **New:** after the first sign-in, the Home Assistant app gives a short
  introduction: name, language, colour scheme, a sensor for each
  measurement, and for each area (climate, light, watering …) whether the
  app should take it over one day or leave it to Home Assistant. A sensor
  the app does not suggest can now be chosen under "Show other sensors"
  ("Andere Sensoren anzeigen"), with a search, if its unit fits the
  measurement.
- **Fixed:** the climate, light and watering pages in the Home Assistant
  app no longer show switches that switch nothing; they show your choice
  for the area instead. The history no longer shows an empty tile for the
  mixed volume.
- **Please note:** the app still switches nothing, also where you choose
  "Automatic" ("Automatisch"): keep your Home Assistant automations and
  timers on. If you already use the app, the introduction opens once after
  this update; you can skip it and open it again under Settings. A choice
  made now will not switch anything after a later update without being
  asked again.

### Added
- **Home Assistant: choose a sensor the app does not suggest** (SD-036):
  the sensor picker has "Show other sensors" ("Andere Sensoren anzeigen")
  with a search by name or entity ID. A sensor is offered for a
  measurement its unit fits (% for humidity, ppm for CO2, L for level, no
  unit for pH). One that Home Assistant says measures something else, such
  as a phone battery, is listed apart with the reason and cannot be
  chosen; a meter that only counts up is never taken as a level. For a
  measurement without a suggestion, a sensor can be chosen right from the
  assignment page.
- **Home Assistant: a short introduction after the first sign-in** (SD-037):
  name, time zone (from the browser), language and colour scheme, a sensor
  per measurement, and per area (climate, light, watering, refill,
  circulation, dosing) whether the app is to do it automatically
  ("Automatic") or it is "Handled elsewhere" ("Außerhalb der Steuerung").
  The app switches nothing yet and says so; the choice is recorded. It can
  be skipped and opened again from Settings.
- **API:** `PUT /system {control}` and `system.control` in the
  configuration and its export (`docs/API.md`); `GET /ha/candidates`
  reports `otherFits` and `otherCount`, and `?others=1` adds the other
  sensors (`docs/HOME_ASSISTANT.md`).

### Changed
- **Home Assistant: no switches that switch nothing** (SD-037): the
  climate, light and watering pages show what was chosen for the area,
  changeable there in one tap, instead of switch buttons.
- **German wording** (SD-037): the web app says "manuell" (manually) and
  "manuelle Gabe" (manual dose) instead of "von Hand" (by hand) and
  "Handgabe"; "von Hand" stays only where you do something yourself, such
  as measuring pH.
- **History: a curve shows only where it can have values** (SD-039): in
  the Home Assistant app the volume from mixes is no longer an empty tile,
  and a note says that more curves appear once a sensor is assigned.

### Security
No vulnerability was fixed, so no version is affected and no GHSA or CVE
was issued. What changes for security:

- **Home Assistant: the app can list more of your sensors, but only what
  it needs** (SD-036): for "Show other sensors" a signed-in session now
  also receives other numeric sensors from Home Assistant, only when the
  picker asks for them and at most 1000: name, entity ID and unit, Home
  Assistant's device class and the reason when one cannot be chosen, and a
  value only for those that can. Text, dates, energy, power and any other
  unit are left out by the app's server. Before, a session received only
  the suggested sensors. A sensor can be chosen only if it is on this list
  and fits the measurement.
- **The choice per area is recorded, not used** (SD-037): only the six
  areas and "auto" or "outside" are accepted, anything else is refused
  (422 `system.control`); an import leaves out unknown entries. It feeds
  no control; the actuator gateway and the protective cut-offs are
  unchanged. A choice made while the app switches nothing is no consent to
  switch: the first version that lets it feed control asks again.

## [0.1.0-proto.3] – 2026-10-11

Third prototype: fixes from the first test of the Home Assistant app on a
Raspberry Pi. Control still runs only in the simulator; the Home Assistant
app only reads sensors and switches nothing. No compatibility promises.

- **New:** pH, EC and level from Home Assistant now have a history and a
  trend line on their tiles. In the Home Assistant app the top bar says
  "Display only" ("Nur Anzeige") instead of offering an emergency stop.
- **Fixed:** sensors from Home Assistant no longer show as outdated while
  they keep reporting; VPD now shows when temperature and humidity report
  at different times; the emergency stop question is no longer cut off on
  a phone.
- **Please note:** the Home Assistant app switches nothing and has no
  emergency stop: in an emergency, switch off in Home Assistant or at the
  device. A sensor that stops reporting shows as outdated after 30 min at
  the latest; one that reports only on a change can show as outdated while
  its value stays the same, so let it report at least every 10 min.

### Added
- **Home Assistant: pH, EC and level have a history** (SD-039): values
  calibrated in Home Assistant that pass every check are recorded,
  exported and shown as a trend on their tiles, now marked "Calibrated
  elsewhere" ("anderswo kalibriert"); control still never uses them.

### Fixed
- **Home Assistant: sensors no longer show as outdated while they report**
  (SD-035): the app learns how often each sensor reports and calls a
  value outdated only after about three of its usual gaps: never sooner
  than 5 min, and at the latest after 30 min; right after the start,
  10 min. A sensor that is "unavailable" in Home Assistant still shows at
  once. The hub's own probes keep their fixed limits (60 s for pH, EC and
  level, 120 s for temperature, humidity and CO2). A sensor that reports
  only on a change needs `force_update: true` (Home Assistant 2024.4 or
  later) to stay valid while its value stays the same.
- **Home Assistant: VPD now shows when air temperature and humidity report
  at different times** (SD-035): it is computed while both are valid;
  many sensors report only on a change. The hub's own climate head still
  pairs its readings within 60 s.

### Security
Affected: 0.1.0-proto.2, fixed in 0.1.0-proto.3 (the cut-off dialog also
existed in 0.1.0-proto.1, which was never published). Severity: low; both
are about what the web app shows about the emergency stop, and no
published download switches anything outside the simulator. Neither is a
vulnerability: nothing could be exploited, bypassed or switched, so no
GHSA or CVE was issued.

- **The emergency stop question is no longer cut off on a phone:** opened
  from the top bar, it was cut off at the top and the page behind it was
  not dimmed (most visible on a phone). Every window in the app now opens
  over the whole page.
- **Home Assistant: no emergency stop button that would stop nothing**
  (SD-040): the app switches nothing, so the top bar says "Display only"
  ("Nur Anzeige") and tells you to switch off in Home Assistant or at the
  device. A stop set by the previous version can be cleared, and the
  overview no longer claims that everything is off.

## [0.1.0-proto.2] – 2026-10-10

Second prototype and the first published release: simulator downloads for
Windows, macOS and Linux, and a Home Assistant app that only reads
sensors. Control runs only in the simulator; the firmware for the
ESP32-S3 is a skeleton without a bus. No compatibility promises.

- **New:** try it without hardware with the simulator download; a Home
  Assistant app that shows your sensors (it only reads and switches
  nothing); the app in English too (some device messages are still
  German); Climate, Light and Irrigation pages with readings and switching
  by hand; setup in five steps with recipe templates.
- **Fixed:** your changes are saved at once, and far less is written to
  the memory card; a damaged entry in the event list can no longer break
  the app.
- **Please note:** this is a prototype; do not connect it to real pumps or
  plugs. The Home Assistant app is experimental: switch on advanced mode
  in your Home Assistant profile to find it, open it by IP address or
  `homeassistant.local`, and set its password right away. Its connection
  is not encrypted, so use it only in your own network.

### Added
- **growcontroller as a Home Assistant app** (SD-034,
  `docs/HOME_ASSISTANT.md`): switch on advanced mode in your Home Assistant
  profile, add this repository in Home Assistant's app store (formerly
  add-on store) and install growcontroller on a Raspberry Pi (64-bit) or
  PC; open its web UI on port 8099 by IP address or `homeassistant.local`,
  set a password right away and pick the sensors. Nothing to configure, no
  token to make: `gc_ha_server --app` reaches Home Assistant through the
  Supervisor. Still read-only: it switches nothing. Home Assistant
  downloads a ready-made image of a few MB
  (`ghcr.io/wokesay/growcontroller-ha`) that holds only the server, the
  web app and the license texts. CI builds and starts it for `amd64` and
  `aarch64` on every code change, next to a stand-in for the Supervisor;
  the release workflow rehearses the push in every dry run and publishes
  and attests it for a tag (`docs/RELEASE.md`). Please note: the web app
  is plain HTTP on port 8099 for your own network only, and the access
  the app gets from the Supervisor is that of Home Assistant's
  administrator, of which growcontroller only reads the states.
- **Home Assistant address with a path** (`docs/HOME_ASSISTANT.md`): the
  mapping file's `url` may carry a plain path in front of the API, such as
  `http://supervisor/core`, which is how a Home Assistant add-on reaches
  Home Assistant. The whole address is checked once and the connection is
  made from its checked parts: the host is a plain name, an IPv4 address or
  an IPv6 address in `[ ]` (no `user@`, `?`, `#`), the port 1–65535, the
  path letters, digits, `-` and `_` (no dot segments, escapes, queries or
  empty segments); an address with `/api` in it is refused with a hint.
- **Home Assistant: choose sensors in the web app instead of listing
  them** (`docs/HOME_ASSISTANT.md`): `gc_ha_server` reads all states in one
  request and finds the sensors it can use by Home Assistant's device class
  and unit (pH, EC, temperature, humidity, CO2, level); everything else is
  dropped at once. Under Devices › Assignment the user picks one sensor per
  measurement from a list with name, entity ID and live value; one tap
  selects, adds and assigns it, and "Don't use a sensor" takes it away
  again; a failed change keeps the former sensor. The picks live in the
  hub's configuration, and the mapping file needs only Home Assistant's
  address. A unit the hub does not convert exactly (TDS in ppm) is listed
  without a value. The overview and the picker say when Home Assistant
  does not answer or refuses access.
- **Home Assistant, read-only spike** (`docs/HOME_ASSISTANT.md`): a
  server (`gc_ha_server`) runs the core next to Home Assistant, reads
  mapped sensor entities (pH, EC, water and air temperature, level in L,
  humidity, CO2) through its REST API, runs them through the sensor truth
  and serves the web app. It switches nothing and only reads states from
  Home Assistant. pH, EC and level from it are shown but are no values for
  control, since the hub has not checked a calibration done in Home
  Assistant (RAT-025); the hub offers no calibration of its own for them.
  A missing unit (except pH) or an unreadable report time gives no value.
  A refused token stops reading until restart, so Home Assistant does not
  ban the computer. Their tiles say "display only" with the value's age
  instead of "not calibrated", the monitoring says "calibrated outside the
  hub", and the setup step does not ask for a probe calibration. The
  setup for the hub's own dosing hardware is not offered on a read-only
  hub.
- **Fan sockets come back on after a power loss** (PD-050, SD-028):
  binding a socket to the exhaust or circulation fan sets it to "on after
  power loss"; every other socket stays "off". After a restart the hub
  leaves fans as they are. A socket that loses its fan role (unassigned,
  moved, device removed, configuration imported) goes back to "off"; the
  hub reads the setting back and reports a socket that keeps "on". The
  catalog marks such roles with `afterPowerLoss: "on"`; the core accepts
  it only for the exhaust and the circulation fan, as continuous loads
  without a maximum run time. Fan sockets bound with an older version
  report "The plug's safety setting is missing or differs" and need to be
  assigned again.
- **The emergency stop survives a restart** (PD-076, SD-028): it is kept
  in the run-time state; after a power loss or restart everything stays
  off, fans included, and the hub reports "Emergency stop still active" until
  someone resumes. During the stop the fan sockets are set to "off after
  power loss"; resume restores their setting.
- **Internal error keeps the fans running** (PD-077): the safe state after
  a caught error switches everything off except the fans on sockets.
- **Secured time and the continued clock** (PD-069, PD-073, SD-028): the
  hub knows whether its time is secured (in the firmware after the first
  network time sync). It saves its time and an operating time with its
  run-time state. Without a secured time after a start it continues from
  the saved time (a newer event moves it forward by at most 1 h) and
  reports "Clock not synced" after 2 min; when the time is secured later
  it reports the jump. `GET /api/v1/state` shows `time` (`secured`,
  `source`, `operatingS`). Intervals within one start use operating time; across a
  restart they use wall time only if both moments were secured, otherwise
  operating time, so an outage never stretches them (prepared for the
  dosing intervals that follow). A clock step keeps the remaining time of
  jump locks, maintenance and controller pauses; a jump lock never holds
  longer than 15 min after a step. An unreadable run-time state keeps the
  hub stopped (copy in `state.broken.json`) until someone resumes.
- **Simulator:** a power cut can last a while (`outageMin`, the world runs
  on without power) and leave the hub without a secured time
  (`timeSecured: false`); `mainsLost: false` restarts only the hub. The
  action `time` secures the time later, takes it away again or steps it
  (`stepS`). The socket fault `crash` makes the hub's tick fail, to test
  the internal-error path.
- **Public repository** (PD-055 to PD-062, SD-026): a safety notice and a
  trademark notice in the README; `SECURITY.md` takes reports only through
  GitHub private vulnerability reporting, with targets of 7 days
  (acknowledgement), 30 days (first assessment) and at most 90 days to
  disclosure.
- **Quick checks** (`.github/workflows/checks.yml`): architecture rules,
  `reuse lint` and the dependency license check run on every PR, also for
  docs-only changes; the full CI skips its heavy jobs for them (PD-059,
  SD-030).
- **Own repository with history:** the software moved from the product
  repository into `Wokesay/growcontroller-software` with its 17 commits;
  every moved commit carries `LICENSE`.
- **Licensing:** AGPL-3.0-or-later for code, docs, catalog and templates
  (`LICENSE`, `LICENSES/`, `REUSE.toml`, an SPDX header in every source
  file); an additional permission under AGPL §7 for the binary-only ESP-IDF
  libraries (`ADDITIONAL_PERMISSION.md`); `THIRD_PARTY_NOTICES.md`. CI runs
  `reuse lint` and a license check of all npm dependencies
  (`tools/check_licenses.mjs`).
- **Source code link** (AGPL §13): the login page and Settings › Display
  and info link to the source code of exactly the running build (the
  commit, set at build time). CI and release builds fail without a
  revision; otherwise the app says the version is unknown.
- **Rationale register** `docs/RATIONALE.md`: one entry per domain rule
  (RAT-001 … RAT-084) with rule, reason, evidence, implementation and
  tests. Code and docs cite only these IDs.
- **Decision log** `docs/DECISIONS.md` starts: SD-001 to SD-026 record the
  product decisions that apply to the software; SD-027 is the first
  decision about this repository itself.
- Simulator download for Windows, macOS (Apple silicon and Intel) and
  Linux: one file with the web app embedded; a double-click starts the demo
  and opens the browser, and the data is stored next to the program. If the
  port is busy, it takes the next free one; if the folder is not writable,
  the demo runs in memory only. Releases attach the packages automatically,
  with the license notices of the bundled libraries
  (`THIRD_PARTY_LICENSES.txt`, also in the web package; with the
  third-party parts in nlohmann/json and the Apache-2.0 text). A firmware
  image goes into a release only once it is signed offline
  (`docs/RELEASE.md`); CI still builds the firmware on every run.
- **Setup in five steps** (Start, Devices, Tank, Nutrients, Calibrate): a
  fixed bar with Back, Skip and Next; completed steps can be clicked;
  explanations (ⓘ) for usable volume, minimum level, bottle, pair and
  calibration; Nutrients starts with a template and its preview, and
  existing recipes are visible.
- **Recipe templates** with a preview, mapped to your own bottles instead of
  matching by name: Two-part nutrient, "Vegetative wk 1–4" and "Flowering
  wk 1–2" per the Athena Blended Feed Program A01.004. API:
  `POST /recipes/template` takes `map` (part → bottle), reports missing
  parts in `missing` and rejects one bottle for two parts; a pair from the
  template carries over to bottles without a pair of their own, as "AB2"
  and so on if the name is already taken. The manufacturer source is also
  available in English (`sourceEn`).
- **Growing area (schema v2):** setup asks where the plants are (room,
  tent, greenhouse), with a name of your own. In the data model this is a
  zone with its own roles (`zone.*`, previously `tent.*` on the tank); the
  configuration is migrated at startup. API: `PUT /zone`. For third-party
  API clients: `/roles/tent.*` is now `/roles/zone.*`; series recorded
  before the update keep the old name.
- **pH and EC as one head or two:** new device classes "Sensor head pH"
  and "Sensor head EC" (with water temperature) next to the combined pH/EC
  head. Measurement roles are assigned only when exactly one device fits.
  Calibrate and Devices show the calibrations per head from the catalog; in
  the simulator both variants can be plugged in, and faults hit the
  matching head. Which hardware variant will exist is open (PD to follow);
  the software supports both.
- **Switchable sockets (Shelly, local), in the simulator:** sockets (Plug S
  Gen3, Power Strip 4 Gen4) appear in setup and under Devices. For each
  outlet you say what is plugged in (circulation pump, light, exhaust fan,
  circulation fan, humidifier, dehumidifier, irrigation pump, inlet) and
  test it (on for 3 s). When a socket is added, the hub sets every outlet
  to "off after power loss"; when an outlet is assigned, the hub writes the
  profile's protection setting into the device (auto-off for humidifier,
  irrigation pump and inlet) and assigns it only once the read-back
  matches; before every switch-on it checks the setting again. On the
  device, discovery via mDNS, login and RPC follow. Which sockets are
  recommended is open (PD to follow). API: `POST /roles/{rolle}/test`,
  `POST /roles/{rolle}/switch {on}` (manual mode).
- **Validation:** a switched output serves only one role, and the channel
  must be within the device's range; the setting "valve open at most"
  allows no more than 25 min.
- **Climate, Light and Irrigation areas:** own pages in the navigation with
  readings and switched outputs, including manual mode; the overview shows
  the room climate and all assigned switched outputs. Automatic control
  (light schedule, climate, irrigation schedule) does not exist yet.
- **VPD:** air VPD without a leaf offset; if an input value is missing,
  there is a gap instead of 0 (Rationale: RAT-017). Formula FAO-56 eq. 11.
  The two values may be at most 60 s apart (own rule, assumption). Shown in
  the history together with air temperature, humidity and CO2.
- **Simulator:** simple room climate (light and heating warm the room, the
  exhaust fan exchanges air, humidifier and dehumidifier, evaporation while
  the light is on; assumptions); climate and CO2 heads deliver values. The
  demo has a climate head and a power strip with light, exhaust fan,
  circulation fan and humidifier. An outlet without Wi-Fi keeps powering
  its load (room climate, circulation). New fault `stuck`: the outlet
  rejects switching commands and keeps its state; the device's auto-off and
  power loss still take effect. The simulator counts the switch-ons per
  outlet (`switchOns`), so tests reliably detect short pulses.
- **German and English:** every page in both languages (see "The app's
  texts move into the language tables" and "The hub speaks English with
  keys" below). The language is stored on the hub and can be chosen per
  browser.

### Security
No published version is affected: this is the first release published
from this repository (0.1.0-proto.1 was never published as a download or
an image), and no GHSA or CVE was issued. The first entry also fixes
builds of 0.1.0-proto.1 from source; the others harden what is new in
this release.

- **A new password is on disk when the call returns** (#68): before, the
  simulator and the Home Assistant server wrote `auth.json` only with the
  next save every 10 s, so a power cut right after the first setup could
  open the setup to the network again. If it cannot be written, setup and
  password change answer "Not saved" and change nothing. Data files are
  created for the owner only, and the event journal never grows past the
  5000 events the log keeps, so failed logins cannot fill the card.
- **Home Assistant trial reads only what it needs:** Home Assistant's
  state list is read event by event and only sensors, with a few fields
  each, are built, so people, locations and other entities are never kept;
  a kept name is cut to 256 bytes while reading and any other longer field
  is dropped, so a cut never makes an ID valid or changes a value; deep
  nesting stops the read;
  names from Home Assistant are cleaned of control and invisible format
  characters, entity IDs are limited to 255 characters, and the picking
  routes need a signed-in session.
- **A damaged event cannot break the app** (SD-032): the web app reads
  the hub's messages defensively (only its own table keys, only strings
  as text, at most four nested levels). If a page or its event list
  still fails to draw, it shows a notice with a reload button while
  navigation and STOP stay usable. The hub caps the key and text of
  events it loads.
- **Release hardening** (#29): `release.yml` checks that a release tag
  points at a commit on `main`; every file is built in jobs without write
  access and without a cache; the publishing job only checks the files
  against their checksums, attests their build provenance (check with
  `gh attestation verify` naming the release workflow and tag, see
  `docs/RELEASE.md`), pushes and attests the Home Assistant app image and
  creates the release. A second SBOM lists
  the C++ libraries of the simulator. All actions are pinned by commit SHA
  and updated by Dependabot after a 7-day cooldown; a test keeps them
  GitHub's own and pinned. Pull requests that change the release
  machinery run the release as a dry run. The ESP-IDF build image is
  pinned by digest, and CI installs `reuse` only from requirements files
  with hashes (`tools/requirements-reuse*.txt`, kept current by
  Dependabot): every dependency as a wheel, `reuse` itself built from its
  hashed source with its hashed build backend.
- **Safety profiles in the actuator gateway** for sockets and 12 V
  outputs: `dauer` (continuous), `puls` (pulse), `kompressor`
  (compressor).
  - Humidifier and dehumidifier never run at the same time; if the state
    of the other device is unknown, this one stays off (Rationale: RAT-034,
    R5).
  - Dehumidifier: 5 min pause after switching off, also after an emergency
    stop and a restart (Rationale: RAT-034).
  - Maximum run time for the humidifier (5 min, assumption), the irrigation
    pump (10 min, assumption) and the inlet (25 min, Rationale: RAT-079);
    the device switches off by itself shortly after.
  - Irrigation pump only above the minimum level, switched off when the
    level drops below it while running; locked when the level is
    unreadable (deviation from RAT-068; since decided in PD-031, which adds
    a limited emergency dose and irrigation without a level sensor, not
    implemented yet).
  - Humidifier: if a humidity sensor is assigned, only with a valid value
    below 85 % (assumption).
  - Reassigning or removing first switches the old output off; if that
    fails, the event log shows "may still be on".
  - Protective cut-offs (dry run of the circulation pump, inlet emergency
    limit, irrigation pump, maximum run time): if switching off fails, the
    hub reports "may still be on" instead of "off", once per reason; the
    inlet distinguishes level, emergency limit and open time. The hub keeps
    trying as long as the reason or the latch persists, and reports "now
    off" as soon as the output reads as off. Before, the log said
    "off", and for the circulation pump and the inlet it added a new entry
    on every cycle.
  - If the circulation pump runs or the inlet is open although the latch
    has not been released yet (switching off failed, button on the
    device), the hub switches it off again.
  - Maximum run time: if the device has already switched off by itself
    (auto-off), there is no further switching attempt. After reassigning,
    tracking of the old output ends without an all-clear. An emergency stop
    ends the retries at the maximum run time (open; fallback: auto-off in
    the device).
  - Unassigning a role: if the output does not switch off, the log shows
    "may still be on" (as with reassigning).
  - Emergency stop and restart switch off all switched roles; circulation
    fan and exhaust fan do not run on (Rationale: RAT-036).
  - Heaters are not available as a role yet: only once the latching
    emergency shutdown exists (Rationale: RAT-060).

### Changed
- **The overview points to devices waiting to be accepted:** a banner
  names how many were detected and links to Devices.
- **Probe calibration accepts only the kinds a device class offers**
  (RAT-025): a pH calibration on an EC head, for example, is refused with
  `probe.not_offered` instead of starting a calibration session that
  paused control on that device for 10 min.
- **The hub speaks English with keys** (#18, SD-032): controller lines,
  their checklists and the monitoring (headline and assessments) come
  from the hub as a key, the values and an English text. A German page
  shows them in German, with numbers in the page language; a single
  passing check now reads "1 Prüfung" instead of "1 Prüfungen". Events,
  job messages and errors follow the same way: the history stores key
  and values, so it switches language too; older events keep their text.
  For API clients: `watchdog.headline` and each item's
  `label` and `text` in `/state`, and each event's `title` and `text`
  in `/events`, are now `{key, text, args}` instead of a string; a few line keys are new (`ph.start`, `circ.on_dosing`,
  `circ.on_always`, `circ.on_interval`, `circ.latched`, `circ.off`).
  When the dosing of a job step cannot start or go on, the answer now
  has the error key `job.start_failed` and the cause in `args.reason`
  (before: `mix.start`, `job.step`, `dose.start`, `cal.start`,
  `prime.start`, and for `/dose` the cause's own key such as
  `dose.too_small`); checks of the request itself keep their keys. The job message keys
  `job.dosing` and `job.stopped` are now `job.dosing_step` and
  `job.emergency_stop`, and a single failed dose reports
  `job.dose_failed`. A pump calibration's result also returns `changed`
  and `message`. Going back to an older version keeps the events but
  shows them without titles, since it cannot read the new form.
  Events name a released latch, the kind of a sensor calibration, the
  removed role and the trigger of an emergency stop in words instead of
  internal IDs; a cancelled mix reads "Mix cancelled". The calibration
  window shows the hub's result, including the warning to check the
  tubing when the rate changed clearly, and then offers to calibrate
  again; after a failed run, closing the window cancels the run. A pump
  that reports a stored rate of 0 no longer counts as a previous rate.
  The actuator gateway and the doser too: why an output or pump may
  not run (`act.*`), how a dose ended (`dose.*`), switch-off reports and
  protective cut-offs carry keys, so dose events, "may still be on" and
  dry-run or inlet cut-offs read in the page language; details a device
  reports itself (for example why a smart plug refused), the names of
  outputs from the catalog (for example "Zulaufventil") and a sensor's own
  reason still show in German. A latch's `why` is now a message; a state saved
  before keeps its text, while going back to an older version shows such
  a reason as "[object Object]" on the Tank page. For API clients, three
  refusals of `/roles/{role}/switch` got their own keys: a refusing
  output `act.output_refused` (was `act.bus`), humidity too high
  `act.humidifier.rh_high`, and the watering pump's `act.irrigation.level`
  split into `act.irrigation.min_missing` and `act.irrigation.low`.
- **The app's texts move into the language tables** (#18): every page,
  the shared widgets and the simulator panel show their own texts in
  English when English is chosen, and so do the hub's messages (above).
  Numbers use
  a decimal point on an English page and a comma on a German one, also
  in input fields and range hints, and a field shows its value again
  when the language changes (PD-035). The issue that "Report a problem"
  prepares is always in English, as the repository is (PD-034). German
  stays the default for a new hub. The buttons that
  reorder a recipe now name the bottle for screen readers, and in English
  releasing a latch reads "Release" everywhere.
- **Merging gateway changes** (SD-033): a technical change to the
  actuator gateway or to protective cut-offs that changes no logic is
  merged once `reviewer`, `qa` and `security` accept it; logic changes
  there still wait for the project owner's "mergen".
- **CI** (SD-030): the full CI runs on every pull request and push to
  `main`; for docs-only changes it skips its heavy jobs. A summary check
  `ci-ok` lets GitHub block a merge while the full CI is red or still
  running.
- **Product decisions** (PD-100, SD-029): the software is worked on in a
  session of its own with only this repository; product decisions are
  written only in the product repository's session. Requirements arrive
  here as issues, product questions are named instead of decided, and SD
  and RAT IDs are still assigned here (`CLAUDE.md`, `docs/WORKFLOW.md`).
- **Repository rules** (SD-027): pull requests are merged by squash only;
  `main` accepts changes only through pull requests with green quick
  checks; `v*` release tags can only be set by the project owner.
- **Recipe templates from a manufacturer chart** are named after the
  phase with the source behind it ("Vegetative wk 1–4 (per Athena
  A01.004)") and say "manufacturer data, not binding; not affiliated with
  the manufacturer" with edition and date of the source: in the template
  dialog of the recipe page, in recipes created from it and in the
  template cards of the setup wizard (PD-030, PD-062). The source line is
  easier to read, and long recipe names wrap on phones.
- **CI:** caches compiler output (ccache, saved only on `main`) for the
  core and the firmware; every job has a time limit; only superseded PR
  runs are cancelled.
- The README no longer mentions selling hardware (PD-061); `CLAUDE.md`
  names the project owner neutrally (PD-057).
- **English:** docs, README, CONTRIBUTING, SECURITY, templates, agents,
  workflows and `CLAUDE.md` are in English; docs, templates and packaging
  files have English names (for example `docs/CONCEPT.md`,
  `docs/INVARIANTS.md`, `sim/README.txt`, `tools/package.sh`). Code
  comments and test names follow in a separate PR.
- **Contributions** need no sign-off any more (no DCO, no CLA);
  inbound = outbound under the AGPL.
- "Report a problem" and the issue templates point to this repository; the
  label of a bug report is `bug`.
- Decisions: E1, E2, E7, E8 and E9 are decided as PD-013 to PD-017 in the
  product repository; the plant automation concept refers to PD-018 to
  PD-021 (scope and languages, sockets, power loss, pH/EC heads). The
  behaviour after a power loss (PD-020) is implemented in part: fans keep
  their state (PD-050); light and irrigation follow.
- Embedded texts (catalog, changelog) as byte arrays, so the core also
  compiles with MSVC.
- Catalog version 2 (roles `zone.*`, separate pH and EC heads),
  configuration schema 2.
- Naming: "Port 1–8" on the hub, "Pump 1–6" on the dosing block, "sensor
  head", "Room" instead of "Tent", tagline "Plant automation". Ports show
  the device's icon.
- The app uses the full screen width; tiles wrap, and setup tables become
  cards on phones.
- Messages say "minimum level", "Port n" and "Pump n on the dosing block".

### Fixed
- **What you change is saved at once, and far less is written** (#68):
  a password, a configuration change, a picked sensor and a STOP are on
  disk when the click returns (written atomically, with `fsync` on a
  computer); before, the simulator and the Home Assistant server saved
  every 10 s, so a power cut could undo them. History and events are now
  appended as they come and written whole only after the start and once a
  day (`docs/HISTORY.md`); an idle hub wrote about 55 MB in two hours and
  now hands about 0.2 MB to the disk, so an SD card lasts. After a restart
  history and events come back from the snapshot plus the journal; a
  record torn by a power loss is never read as a value. When the data
  folder takes nothing, the hub logs one alarm instead of going on
  silently in memory, and writes the files again until they are on disk,
  the STOP first; a failed write leaves no temporary file that would fill
  the card. The Home Assistant server does not start with a data folder
  it cannot write.
- **Escape closes a window right after it opens:** windows listened for
  the key only after the next frame, so an Escape pressed at once was lost.
- **Recipes from a template keep the language of the page** (#32): a
  recipe created from a template was always stored with the German name
  and note, also in English. The recipes page now sends its language
  (`lang`), the hub's system language is the default; the list of missing
  bottles uses that language, and bottles named "Part A"/"Part B" match
  the template's parts.
- Reading tiles: when a sensor failed, the notice and the chart stuck out
  of the tile.
- Demo: the phase "Blüte" (flowering) pointed to a missing recipe. IDs now
  transliterate umlauts ("Blüte" → "bluete").
- Recipes that use the same bottle twice are rejected.
- Names (hub, area, tank, device, bottle, recipe, cultivation run) are cut
  to 40 bytes without splitting a character; before, an umlaut at the
  limit made the configuration unreadable.
- When you add a device, the hub assigns only that device's measurement
  roles; a role you unassigned on purpose stays unassigned.
- Simulator: "freeze value" keeps the last value instead of "no value".
- `PUT /system`: if one field is rejected, the others do not change
  either.
- Simulator: on macOS the server binds without `SO_REUSEADDR`; a data
  folder that is not writable is remembered ("in memory only") instead of
  forgotten; the packaging script also detects Windows when run locally.

### Removed
- **No collection box** (PD-078, SD-031): every sensor head and the
  dosing block hang directly on a hub port. The device issue form no
  longer offers the collection box, the catalog attaches the climate and
  CO2 heads to a hub port, and the docs no longer list it as a
  connection.
- The recipe templates `athena_pro_veg` and `ab_basic` (replaced by the
  templates from the manufacturer chart, see "Recipe templates" above).

## [0.1.0-proto.1] – 2026-10-06

First prototype (not published as a release). Runs in the simulator; the
firmware for the ESP32-S3 is a skeleton without a bus. No compatibility
promises.

### Added
- **Core (C++17, platform-neutral):** catalog with capabilities, device
  classes, roles, functions and templates; configuration with migration
  and validation in three stages; resolver with setup states and
  checklists; phases provide parameters.
- **Sensor truth:** freshness, frozen values, plausibility, jump lock
  (survives a restart), calibration of pH, EC and the level curve;
  announced changes explain jumps (Rationale: RAT-042).
- **Dosing:** mixing by recipe with A:B as a pair, manual dose, pump
  calibration and tube priming; partial runs with actual run time and a
  booking per run; a job ID per attempt; "catch up" after a block.
- **Control:** EC and pH (Rationale: RAT-053 and others, see
  `docs/RATIONALE.md`), refilling via the inlet, circulation pump; states
  idle, controlling, waiting, blocked, latched; a checklist per controller.
- **Watchdog** that only evaluates (OK, problem, neutral).
- **History** in three tiers (10 s, 1 min, 15 min), event log, CSV export.
- **API** `/api/v1` with a live channel (SSE) and simulator endpoints.
- **Web app:** Overview, Mixing, Tank & control, History, Recipes &
  bottles, Devices, Functions, Settings, setup wizard in 8 steps; light and
  dark, mobile.
- **Simulator** as a digital twin with the scenarios `neu` (new, empty
  hub), `stufe1` (stage 1) and `demo`, fault injection, time-lapse and
  restart.
- **Firmware skeleton** for the ESP32-S3 (ESP-IDF): core, web app in the
  image, API, storage, 12 V outputs, A/B partitions.
- **Tests:** unit and scenario tests (doctest, also with ASan/UBSan),
  end-to-end tests (Playwright), architecture check
  `tools/arch_check.sh`, CI for simulator, web app and firmware.
- **Updates and changelog** in the app (a mock-up in the simulator).

### Security
- Mandatory first password, no default password; PBKDF2-HMAC-SHA256 with
  salt; lockout after 5 failed attempts; session as an HttpOnly/SameSite
  cookie.
- Security headers (CSP, `X-Frame-Options`, `nosniff`).
- Actuators only via the actuator gateway with fixed limits in the code;
  the configuration (also via import or a phase) can only tighten them.
  After a restart everything is off; no dosing without a calibration
  value.
- Doser with a deadline per run; an abort books what has already run; bus
  job IDs are unique across restarts.
- Origin check against CSRF and DNS rebinding; constant-time hash
  comparison; if the password is lost, setup over the network is locked.
- Broken inputs and files lead to 400/500 or to "everything off" with an
  alarm, not to a crash. Simulator: scenario reset only when logged in.
- Not included yet: HTTPS, signed OTA, Secure Boot
  (`docs/SECURITY_MODEL.md`).
