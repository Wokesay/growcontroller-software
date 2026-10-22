Doseph – Simulator
==================

English
-------
The simulator is the complete Doseph software with a simulated hub,
dosing block, sensors and tank. Nothing is installed; no hardware and no
internet are needed.

Start:
  Windows:  double-click doseph-simulator.exe.
            If Windows says "Windows protected your PC":
            "More info" → "Run anyway".
  macOS:    in Terminal, change to the unpacked folder (type cd, then drag
            the folder into Terminal) and run once:
              xattr -d com.apple.quarantine doseph-simulator
            then double-click or run ./doseph-simulator
  Linux:    ./doseph-simulator

Your browser opens http://127.0.0.1:8080 (8081 … if the port is busy).
Demo password: demo-passwort

Data is stored in the folder "doseph-data" next to the program. If you
used the simulator before it was called Doseph, it keeps using your
"growcontroller-daten" folder as long as there is no "doseph-data".
Delete that folder to start the demo over. If the folder cannot be created,
or it or a file in it cannot be read, the demo runs in memory only and
leaves the folder as it is.
Stop: close the window or press Ctrl+C.

More options: doseph-simulator --help
  for example --scenario neu   (empty hub with the setup wizard)
              --speed 60       (time-lapse)

Language: the app offers English and German; choose under Settings.

License: free software under the GNU Affero General Public License,
version 3 or later (LICENSE.txt). Source code:
https://github.com/Wokesay/doseph
The running app links the exact version under Settings.
Third-party licenses of the bundled libraries: THIRD_PARTY_LICENSES.txt

Deutsch
-------
Der Simulator ist die komplette Doseph-Software mit nachgebildetem
Hub, Dosierblock, Sensoren und Tank. Nichts wird installiert, es braucht
keine Hardware und kein Internet.

Starten:
  Windows:  doseph-simulator.exe doppelklicken. Meldet Windows
            „Der Computer wurde durch Windows geschützt“:
            „Weitere Informationen“ → „Trotzdem ausführen“.
  macOS:    im Terminal in den entpackten Ordner wechseln und einmal
              xattr -d com.apple.quarantine doseph-simulator
            ausführen, dann doppelklicken.
  Linux:    ./doseph-simulator

Der Browser öffnet http://127.0.0.1:8080. Passwort der Demo: demo-passwort
Die Daten liegen im Ordner „doseph-data“ neben dem Programm. Hast du den
Simulator schon vor dem Namen Doseph benutzt, nimmt er weiter deinen
Ordner „growcontroller-daten“, solange es kein „doseph-data“ gibt. Lösch
diesen Ordner, um die Demo von vorn zu beginnen. Lässt sich der Ordner nicht anlegen oder
er oder eine Datei darin nicht lesen, läuft die Demo nur im Speicher und
lässt den Ordner, wie er ist. Beenden: Fenster schließen oder Strg+C.
Sprache: Deutsch oder Englisch, wählbar in der App unter Einstellungen.
Lizenz: freie Software unter der GNU Affero General Public License,
Version 3 oder später (LICENSE.txt). Quellcode:
https://github.com/Wokesay/doseph
Die App verlinkt die genaue Version unter Einstellungen.
