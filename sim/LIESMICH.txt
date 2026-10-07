growcontroller – Simulator
==========================

Deutsch
-------
Der Simulator ist die komplette growcontroller-Software mit einem
nachgebildeten Hub, Dosierblock, Sensoren und Tank. Nichts wird installiert,
es braucht keine Hardware und kein Internet.

Starten:
  Windows:  growcontroller-simulator.exe doppelklicken.
            Meldet Windows „Der Computer wurde durch Windows geschützt“:
            „Weitere Informationen“ → „Trotzdem ausführen“.
  macOS:    Im Terminal in den entpackten Ordner wechseln (cd, dann den
            Ordner ins Terminal ziehen) und einmal ausführen:
              xattr -d com.apple.quarantine growcontroller-simulator
            dann doppelklicken oder ./growcontroller-simulator starten.
  Linux:    ./growcontroller-simulator

Der Browser öffnet sich mit http://127.0.0.1:8080 (bei belegtem Port 8081 …).
Passwort der Demo: demo-passwort

Die Daten liegen im Ordner „growcontroller-daten“ neben dem Programm.
Ordner löschen = Demo von vorn. Ist der Ordner nicht beschreibbar, läuft die
Demo nur im Speicher. Beenden: Fenster schließen oder Strg+C.

Weitere Optionen: growcontroller-simulator --help
  z. B. --scenario neu   (leerer Hub mit Setup-Assistent)
        --speed 60       (Zeitraffer)

English
-------
The simulator is the complete growcontroller software with a simulated hub,
dosing block, sensors and tank. Nothing is installed; no hardware and no
internet needed.

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
Delete the folder to start over. If the folder is not writable, the demo
runs in memory only. Stop: close the window or press Ctrl+C.

More options: growcontroller-simulator --help

Lizenzhinweise der enthaltenen Bibliotheken / third-party licenses:
THIRD_PARTY_LICENSES.txt
