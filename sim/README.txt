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
Delete the folder to start the demo over. If the folder cannot be created,
or it or a file in it cannot be read, the demo runs in memory only and
leaves the folder as it is. Started again while it runs, the program
opens the running demo in the browser instead of starting a second copy.
Close the demo before you delete the folder.
Stop: close the window or press Ctrl+C.

More options: growcontroller-simulator --help
  for example --scenario neu   (empty hub with the setup wizard)
              --speed 60       (time-lapse)

Language: the demo starts in English. The app offers English and German;
choose German under Settings › Display and info › Language. A demo folder
saved by an older version keeps its language and its German names, also
when you switch the language; delete "growcontroller-daten" to get the
English demo.

License: free software under the GNU Affero General Public License,
version 3 or later (LICENSE.txt). Source code:
https://github.com/Wokesay/growcontroller-software
The running app links the exact version under Settings.
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
Ordner löschen = Demo von vorn. Lässt sich der Ordner nicht anlegen oder
er oder eine Datei darin nicht lesen, läuft die Demo nur im Speicher und
lässt den Ordner, wie er ist. Startest du den Simulator noch einmal,
während er läuft, öffnet sich die laufende Demo im Browser, statt dass
eine zweite startet. Vor dem Löschen des Ordners die Demo beenden.
Beenden: Fenster schließen oder Strg+C.
Sprache: Die Demo startet auf Englisch. Auf Deutsch umstellen in der App
unter Settings › Display and info › Language, auf Deutsch heißt das
Einstellungen › Darstellung und Info › Sprache. Ein Demo-Ordner einer
älteren Version behält seine Sprache und seine deutschen Namen; löschst
du „growcontroller-daten“, startet die englische Demo.
Lizenz: freie Software unter der GNU Affero General Public License,
Version 3 oder später (LICENSE.txt). Quellcode:
https://github.com/Wokesay/growcontroller-software
Die App verlinkt die genaue Version unter Einstellungen.
