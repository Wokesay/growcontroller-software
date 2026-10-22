<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->
# Getting started

For testers of the prototype releases. Plan about 30 minutes: 5 to start the
simulator, 15 for the tour, the rest for your notes. Then please tell us
what you found: [FEEDBACK.md](FEEDBACK.md).

## What growcontroller is, and what it cannot do yet

growcontroller is software for a hydroponic grow controller. It mixes
nutrient solution, keeps pH and EC on target and refills the tank. Light,
climate and watering come later; for now their pages switch only when
you tap. It is meant to run on a small
hub of its own, with its own web app: no cloud, no account. What this
version cannot do yet:

- Control (mixing, dosing, pH and EC, refill) runs only in the
  simulator, a digital twin on your computer. There is no firmware
  release for the hub yet.
- The Home Assistant app only reads sensors. It switches nothing.
- A mix doses the recipe's millilitres per litre. Mixing to a target EC
  comes with a later prototype.
- More in the release notes of the newest release, section "Known
  limitations": [Releases](https://github.com/Wokesay/growcontroller-software/releases).

> **Safety.** Do not use it to control real pumps, valves, mains-powered
> devices or chemicals such as pH adjusters: a fault can cause
> overdosing, overflow, water damage or fire ([README](../README.md)).
> For testing only: this version does not meet the requirements of the
> EU Cyber Resilience Act (CRA).

## The simulator in five steps

It runs the complete software with a simulated hub and tank.

1. **Download** the file for your system from the
   [release page](https://github.com/Wokesay/growcontroller-software/releases).
   You do not need the other files there. `<version>` is the release's
   number, for example `0.1.0-proto.5`.
   - Windows: `growcontroller-simulator-<version>-windows-x64.zip`
   - macOS: `growcontroller-simulator-<version>-macos-universal.zip`
   - Linux: `growcontroller-simulator-<version>-linux-x64.zip`
2. **Unpack** the whole zip file into a folder of its own. Nothing is
   installed.
3. **Start** it.
   - Windows: double-click `growcontroller-simulator.exe`. If Windows
     says "Windows protected your PC": "More info", then "Run anyway".
   - macOS: in Terminal, type `cd` and a space, drag the unpacked folder
     into the window, press Enter, and run once
     `xattr -d com.apple.quarantine growcontroller-simulator`. Then
     double-click the program or run `./growcontroller-simulator`.
   - Linux: in that folder, run `./growcontroller-simulator`.
4. **Sign in.** Your browser opens http://127.0.0.1:8080 (8081 or higher
   if the port is busy; the window shows the address). The password is
   `demo-passwort`. It exists only in the simulator. The demo speaks
   English; German is under Settings › Display and info › Language.
5. **Stop or start over.** Close the window or press Ctrl+C. The demo is
   kept in the folder `growcontroller-daten` next to the program. To
   start over, stop the simulator and delete that folder. Tried an
   earlier version in the same place? Delete it first, or the old demo
   comes back, in German.

Options: run the program in a terminal in its folder, for example
`./growcontroller-simulator --scenario neu` for an empty hub with the
setup wizard (Windows: `.\growcontroller-simulator.exe --scenario neu`).
`--help` lists them all. Started with an option, it does not open your
browser (open the address it shows, or add `--open`) and keeps nothing
after it stops. More:
[`sim/README.txt`](../sim/README.txt), [SIMULATOR.md](SIMULATOR.md).

## The Home Assistant app

It shows the sensors you already have in Home Assistant and switches
nothing. You need Home Assistant OS or Supervised (on a Raspberry Pi
with a 64-bit system, or on a PC) and an administrator account.

1. In your Home Assistant profile, switch on **Advanced mode**: the app
   is experimental, and the store lists such apps only then.
2. Open **Settings › Apps**, the store, the menu at the top right (⋮),
   **Repositories**, and add
   `https://github.com/Wokesay/growcontroller-software`.
3. Install **growcontroller**, start it and choose **Open web UI**.
   Switch on **Show in sidebar** to find it there later. It uses your
   Home Assistant login, with no password of its own. Only
   administrators can open it; that needs Supervisor 2026.10.2 or newer,
   so update an older one first.
4. A short **Introduction** asks for language, name, colour scheme, one
   sensor per measurement, and per area whether the app should do it
   automatically one day. It switches nothing, whatever you choose.
   Sensors can be changed later under **Devices › Assignment**.

You should see your readings on the **Overview**, and **Display only** in
the top bar where the simulator has STOP. Direct access on port 8099 is
closed unless you open it; leave it closed for testing. Read the app's
[documentation](../ha/app/DOCS.md) before you open port 8099 or update
the app.
More: [HOME_ASSISTANT.md](HOME_ASSISTANT.md), [COMPATIBILITY.md](COMPATIBILITY.md).

## A tour of the demo, about 15 minutes

Each step says what to do and what you should see. Something different?
Note it for your [feedback](FEEDBACK.md).

1. **Sign in (1 min).** Enter `demo-passwort` and choose **Sign in**.
   The sign-in page says this version is for testing only. After signing
   in you should see the **Overview**.
2. **The overview (2 min).**
   - The coloured bar at the top of the Overview is the monitoring. You
     should see its verdict, for example "Everything OK (… checks)".
   - The tank's card has tiles for pH, EC and water temperature, with
     small curves; pH and EC also show their target.
   - The **Control** card has a row each for EC, pH, refill and
     circulation, with a line such as "Resting: EC on target". Tap the EC
     row. You should see a checklist of why it is dosing now, or why not.
3. **Mix a batch, then stop (3 min).**
   - Open **Mixing**. Keep the recipe, choose **New batch** and enter
     `20` under **Water in the tank**. You should see the plan before
     anything doses: each bottle with ml/L, amount and duration.
   - Choose **Start mixing**. You should see **Running now** and the
     first bottle dosing.
   - While it doses, choose **STOP** at the top right, then **Stop
     everything**. You should see "Stopped: everything off", **Resume** in
     the top bar, and **Last mix** with "Cancelled by the Stop".
   - Choose **Resume**. You should see "Automation resumed". The mix
     does not start again by itself.
4. **The Simulator panel (4 min).** Choose **Simulator** at the bottom
   left (on a phone, bottom right); **Close** hides the panel again.
   - Under **Speed**, choose 60×. You should see waiting times run out
     60 times faster.
   - Under **Faults**, choose **pH jump**. On the Overview, you should
     see the pH tile say the value rose by about 2, the pH row turn
     **Blocked**, and a jump lock in **Latest events**. Choose **Sensor
     head normal**: the value falls back, which counts as a second jump.
     15 steady minutes later (15 s at 60×) the lock lifts.
   - Next to **Pump 1**, choose **Block**, then mix 10 L on **Mixing**
     (if it asks **Mix this tank again?**, choose **Mix anyway**). You
     should see the mix stop as **Interrupted**, with the reason. Choose
     **Unblock**, then **Complete …** on the mix.
   - Choose **Power cut / restart**, then sign in again. You should see
     "Hub started" in **Latest events**: the restart switched everything
     off except the fans. A mix that was running is reported, not resumed.
5. **History and CSV (1 min).** Open **History** and choose **24 h**.
   You should see curves for pH, EC, water temperature, volume and the
   room climate.
   Dashed lines are doses, the green strip is the target range, gaps are
   missing values. Under **Events**, try **Doses** and **Alarms**. Choose
   **CSV**: you should get a file that opens in a spreadsheet.
6. **Calibrate a pump (1 min).** Open **Devices**, choose **Calibrate**
   next to a pump, then **Start pump**. When it stops, take the
   simulator's **Use … ml** under **How much is in the measuring cup?**
   and choose **Save**. You should see "Pump calibrated" and the new
   "calibrated: … ml/min" next to the pump.
7. **Start over with an empty hub (3 min).**
   - In the Simulator panel, under **Scenario**, choose **Empty (stage
     0)**. This clears the demo. You should see the sign-in page asking
     for your own password: enter one of at least 8 characters twice and
     choose **Set password**.
   - The setup wizard leads through **Basics**, **Devices**, **Tank**,
     **Nutrients**, **Calibrate** and **Done**, with **Next** after each.
     Under **Devices**, choose **Add all …**; under **Nutrients**, pick a
     template; under **Calibrate**, calibrate each pump as in step 6.
   - You should see **Setup complete**. Choose **Go to overview**.
   - To get the demo back, choose **Demo (ready to use)** under
     **Scenario** and sign in with `demo-passwort`. Closed the simulator
     before that? It starts again with your empty hub and your password;
     delete `growcontroller-daten` to get the demo back.
8. **Language and dark mode (30 s).** Open **Settings**, card **Display
   and info**. Under **Colour scheme**, choose **Dark**: the page turns
   dark at once. Under **Language**, choose **Deutsch**: the page turns
   German, while the names the demo gave its devices and recipes stay
   English. Back: Einstellungen › Darstellung und Info › Sprache ›
   **English**.
9. **On a phone (optional).** The simulator listens only on this
   computer (127.0.0.1). For a phone on your own Wi-Fi, stop the
   simulator, then start it in a terminal with
   `--host 0.0.0.0 --password <a password of your own>`. On the phone,
   open `http://<your computer's IP address>:<the port the window
   shows>`. The IP address: Windows `ipconfig`; macOS System Settings ›
   Wi-Fi › Details; Linux `hostname -I`. If your firewall asks, allow
   your private network only, and do this only at home, never on public
   or shared Wi-Fi. You should see a bar at the bottom with **Overview**,
   **Mixing**, **Tank**, **History** and **More**, and STOP as an icon.
   Stop the simulator when you are done.
