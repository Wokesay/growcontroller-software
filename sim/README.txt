growcontroller – Simulator
==========================

English
-------
The simulator is the complete growcontroller software with a simulated hub,
dosing block, sensors and tank. Nothing is installed; no hardware and no
internet are needed.

Start:
  Windows:  double-click growcontroller-simulator.exe.
            If Windows says "Windows protected your PC":
            "More info" → "Run anyway".
  macOS:    in Terminal, change to the unpacked folder (type cd, then drag
            the folder into Terminal) and run once:
              xattr -d com.apple.quarantine growcontroller-simulator
            then double-click or run ./growcontroller-simulator
  Linux:    ./growcontroller-simulator

Your browser opens http://127.0.0.1:8080 (8081 … if the port is busy).
Demo password: demo-passwort

Data is stored in the folder "growcontroller-daten" next to the program.
Delete the folder to start the demo over. If the folder is not writable,
the demo runs in memory only. Stop: close the window or press Ctrl+C.

More options: growcontroller-simulator --help
  for example --scenario neu   (empty hub with the setup wizard)
              --speed 60       (time-lapse)

Language: the app offers English and German; choose under Settings.

Third-party licenses of the bundled libraries: THIRD_PARTY_LICENSES.txt

Deutsch
-------
Der Simulator ist die komplette growcontroller-Software mit nachgebildetem
Hub, Dosierblock, Sensoren und Tank. Nichts wird installiert, es braucht
keine Hardware und kein Internet.

Starten:
  Windows:  growcontroller-simulator.exe doppelklicken. Meldet Windows
            „Der Computer wurde durch Windows geschützt“:
            „Weitere Informationen“ → „Trotzdem ausführen“.
  macOS:    im Terminal in den entpackten Ordner wechseln und einmal
              xattr -d com.apple.quarantine growcontroller-simulator
            ausführen, dann doppelklicken.
  Linux:    ./growcontroller-simulator

Der Browser öffnet http://127.0.0.1:8080. Passwort der Demo: demo-passwort
Die Daten liegen im Ordner „growcontroller-daten“ neben dem Programm;
Ordner löschen = Demo von vorn. Beenden: Fenster schließen oder Strg+C.
Sprache: Deutsch oder Englisch, wählbar in der App unter Einstellungen.
