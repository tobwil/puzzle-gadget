# puzzle-gadget: Brain Candy auf dem ESP32

Trickrätsel im Stil der iPhone-App **Brain Candy: Logic Puzzles** für das **Waveshare ESP32-S3-Touch-AMOLED-1.8 (V2)** und das **ESP32-S3-Touch-AMOLED-2.16**. Der Look entspricht der App: weißer Grund, Figuren in Graustufen wie ausgeschnittene Fotos, knallige Akzente in Cyan, Gelb und Pink, fette Montserrat-Schrift, Zurufe in Handschrift. Oben steht „LVL.7“ mit Cyan-Marker, unten liegen die grauen Knöpfe „Restart“ und „Hints“ mit dem Zähler im Cyan-Kreis.

![Übersicht](doc/uebersicht.png)

Englisch und Deutsch lassen sich auf dem Titelbild umschalten (EN/DE):

![Deutsch](doc/deutsch.png)

## Die Level

36 Rätsel für Erwachsene. Level 1–12 sind Trickrätsel, die die Fähigkeiten des Geräts nutzen. Level 13–36 sind harte Denkaufgaben: Denkfallen, Logik, Kopfrechnen, Gedächtnis und Geschick. Zahlenantworten kommen über eine Tastatur mit großen Tasten; Neustart und Tipp liegen dann als kleine Kreise in den unteren Ecken, damit man sie nicht aus Versehen trifft. Jede falsche Antwort sperrt die Eingabe länger (4 s, 8 s, 16 s, höchstens 30 s), damit sich Durchprobieren nicht lohnt.

| LVL | Aufgabe | Lösung (Spoiler) |
|---|---|---|
| 1 | Füttere den Dino | erst auspacken (schnell über das Bonbon wischen), dann ins Maul ziehen |
| 2 | Tippe auf das größte Tier | der winzige Elefant am Horizont, nicht die riesige Maus vorne |
| 3 | Wie viele Bonbons? | Becher wegschieben, darunter liegen zwei: 6 |
| 4 | Elefant in den Kühlschrank | Tür auf, Elefant rein, Tür zu |
| 5 | Lass das Küken schlüpfen | Finger lange auf dem Ei liegen lassen (Wärme, ohne Fortschrittsanzeige) |
| 6 | Dem Dino ist kalt | der gelbe „Deko“-Kreis ist die Sonne: zum Dino ziehen |
| 7 | Öffne das Portal | einmal mit dem Finger um den Ring fahren |
| 8 | Weck die Eule auf | Gerät schütteln (IMU) |
| 9 | Wo ist die Maus? | sie sitzt hinter dem Aufgabentext: Text wegziehen |
| 10 | Wünsch dir was! | Kerzen auspusten (Mikrofon) oder schnell drüberwischen |
| 11 | Ruf den Hund | auf den Tisch klopfen (IMU) oder rufen (Mikrofon) |
| 12 | Hilf der Fledermaus beim Einschlafen | Gerät auf den Kopf drehen oder sie unter den Ast hängen |
| 13 | Schläger und Ball kosten 1,10 € | 5 Cent |
| 14 | 5 Maschinen, 5 Minuten, 5 Teile | 5 Minuten |
| 15 | Seerosen verdoppeln sich, 48 Tage | 47 |
| 16 | Stroop-Sprint | 10 Schriftfarben in Folge in 12 s tippen |
| 17 | 10 schwarze, 10 weiße Socken im Dunkeln | 12 |
| 18 | Merk dir die Zahl (7 Ziffern, 2 s) | jedes Mal eine neue Zahl |
| 19 | 60 km hin mit 30, zurück mit 90 km/h | 45 km/h |
| 20 | Drei falsch beschriftete Kisten | aus GEMISCHT ziehen |
| 21 | Vater viermal so alt, in 20 Jahren doppelt | 40 |
| 22 | Kettenrechnung im Kopf | 7 × 6 – 9 : 3 + 15 × 2 = 52 |
| 23 | Kugel-Labyrinth | Gerät kippen, Löcher meiden (IMU) |
| 24 | Ritter und Schurken | 1 (A Schurke, B Ritter) |
| 25 | 1 kg Gurken, 99 % → 98 % Wasser | 500 g |
| 26 | Mittwoch + 100 Tage | 5 (Freitag) |
| 27 | Farbfolge nachtippen | 7 Farben merken |
| 28 | Schnecke im 10-m-Brunnen | Tag 8 |
| 29 | Welche Zahl fehlt? | 6 (magisches Quadrat) |
| 30 | Ziffer 1 von 1 bis 100 | 21 |
| 31 | Ziegenproblem | 67 % |
| 32 | Quadrate auf dem Schachbrett | 204 |
| 33 | Seil um den Äquator + 1 m | 16 cm |
| 34 | Zeiger übereinander zwischen 12 und 24 Uhr | 10 |
| 35 | Buchstaben der Antwort auf diese Frage | 4 („vier“) |
| 36 | 1, 11, 21, 1211, 111221, ? | 312211 |

## Wer spielt? Drei Altersstufen

Bei jedem Start und nach jedem Aufwecken fragt das Gerät „Wer spielt?“ (wird vorgelesen): **Klein** (3–5 Jahre, 36 Level), **Kind** (6–9 Jahre, 48 Level) oder **Erwachsen** (36 Level). Wer denselben Modus wählt, spielt dort weiter, wo er war. Jeder Modus hat eigenen Spielstand, eigene Tipps und eigene Schwierigkeit; Klein und Kind starten auf „Leicht“. Falsche Antworten sperren Kinder nur kurz (2 s, 3 s … höchstens 5 s), die Kleinen gar nicht.

**Klein (unter 5):** jede Aufgabe ist ganz direkt formuliert und wird vorgelesen, die Tipps auch. Keine Zahlentastatur, große Ziele, bei falschem Tippen sagt das Gerät, was es war („Das ist blau!“).

| LVL | Aufgabe |
|---|---|
| 1 | Gib dem Dino das Bonbon (schon ausgepackt) |
| 2 | Tippe auf das gelbe Bonbon |
| 3 | Schüttel das Gerät, dann wacht die Eule auf |
| 4 | Wie viele Bonbons? (3, Knöpfe 1–5) |
| 5 | Tippe auf den großen Elefanten |
| 6 | Schieb die Sonne zum Dino |
| 7 | Puste die Kerzen aus |
| 8 | Wo ist der Kreis? |
| 9 | Leg deinen Finger auf das Ei und warte (3,5 s) |
| 10 | Wie viele Mäuse? (4) |
| 11 | Ruf den Hund – ganz laut (oder klopfen) |
| 12 | Tippe auf das rote Bonbon |
| 13 | Fahr mit dem Finger rund um den Ring |
| 14 | Tippe auf die kleine Maus |
| 15 | Wo ist das Dreieck? |
| 16 | Häng die Fledermaus unten an den Ast |
| 17 | Wo ist die Eule? (drei Tiere) |
| 18 | Tippe auf das blaue Bonbon |
| 19 | Lass alle Ballons platzen |
| 20 | Wie viele Bonbons? (2) |
| 21 | Tippe auf den kleinen Elefanten |
| 22 | Bring die Maus zum Käse |
| 23 | Wo ist das Quadrat? |
| 24 | Kitzel den Dino (Finger hin und her) |
| 25 | Wo sind mehr Bonbons? |
| 26 | Wo ist der Hase? |
| 27 | Merk dir, wo das Bonbon ist (drei Becher) |
| 28 | Tippe auf das grüne Bonbon |
| 29 | Wie viele Sterne? (5) |
| 30 | Bring den Hund in seine Hütte |
| 31 | Tippe auf die große Maus |
| 32 | Lass nur die roten Ballons platzen |
| 33 | Streichel den Hund |
| 34 | Wo sind weniger Mäuse? |
| 35 | Wo ist die Fledermaus? |
| 36 | Was passt nicht dazu? (Maus zwischen Bonbons) |

**Kind (6–9):** Level 1–12 sind die Trickrätsel von oben, dann:

| LVL | Aufgabe | Lösung (Spoiler) |
|---|---|---|
| 13 | 5 Schafe, alle bis auf 3 laufen weg | 3 |
| 14 | 1 kg Federn oder 1 kg Steine? | GLEICH |
| 15 | Merk dir die Zahl (4 Ziffern, 3 s) | jedes Mal neu |
| 16 | Beine von 3 Hühnern und 2 Hunden | 14 |
| 17 | Den Zweiten überholt – welcher Platz? | 2 |
| 18 | 2 – 4 – 6 – ? – 10 | 8 |
| 19 | Farbfolge nachtippen | 4 Farben, langsamer |
| 20 | 1 Ei kocht 5 Minuten, 3 Eier im selben Topf? | 5 |
| 21 | Was wird nasser, je mehr es trocknet? | HANDTUCH |
| 22 | Kugel-Labyrinth | nur 2 Löcher |
| 23 | Wie viele Monate haben 28 Tage? | 12 |
| 24 | Toms Mutter hat drei Kinder: Mia, Leo und …? | TOM |
| 25 | Hahn legt ein Ei auf die Dachspitze | GAR NICHT |
| 26 | Hütchenspiel, Becher werden 4× vertauscht | dem Bonbon folgen |
| 27 | Was hat Zähne, kann aber nicht beißen? | KAMM |
| 28 | Beine einer Spinne | 8 |
| 29 | 5 – 10 – 15 – ? – 25 | 20 |
| 30 | 5 Äpfel, du nimmst 3: wie viele hast DU? | 3 |
| 31 | Merk dir die Zahl (5 Ziffern, 3 s) | jedes Mal neu |
| 32 | Mit welcher Hand rührst du den Kakao um? | LÖFFEL |
| 33 | Räder von 3 Bussen | 12 |
| 34 | Lisa > Tom > Ben: wer ist am kleinsten? | BEN |
| 35 | Ballons der Reihe nach 1–6 | Fehler: alle kommen zurück |
| 36 | 1 – 3 – 5 – ? – 9 | 7 |
| 37 | Heute Montag, übermorgen? | MITTWOCH |
| 38 | Farbfolge nachtippen | 5 Farben |
| 39 | Was gehört nicht dazu? | KAROTTE |
| 40 | 6 Hasen, 2 weg, 3 dazu | 7 |
| 41 | Stroop-Sprint | 6 Schriftfarben in 15 s |
| 42 | Ecken eines Würfels | 8 |
| 43 | 10 – 8 – 6 – ? – 2 | 4 |
| 44 | Welcher Vogel kann nicht fliegen? | PINGUIN |
| 45 | Kugel-Labyrinth | 3 Löcher |
| 46 | Verdoppelt plus 2 ergibt 10 | 4 |
| 47 | Papa 35, Mama 3 Jahre jünger | 32 |
| 48 | 1 – 2 – 4 – ? – 16 | 8 |

## Zufall: jedes Mal anders

- **Gemischt bei jedem Spiel:** Merkzahlen, Farbfolgen, Hütchenspiel und Stroop würfeln bei jedem Start neu (auf dem Gerät aus `esp_random()`). Farben, Formen, Tiere, Ballons, welche Seite mehr hat und wo das Andere liegt werden bei jedem Spiel neu angeordnet.
- **Zufallsmodus** (gelber Würfel auf dem Titelbild): endlos neue Aufgaben mit Serienzähler und Rekord je Altersstufe. Gemischt aus den festen Leveln der Stufe und frisch gewürfelten Aufgaben:
  - Klein: Farben, zählen (1–5), groß/klein, Formen, Tiere finden, mehr/weniger, Ballons, Becher, was passt nicht, ziehen, kitzeln – alles mit Ansage.
  - Kind: Rechnen bis 20 und kleines Einmaleins (auch als Geschichten), Zahlenreihen, Kettenrechnung, Zahl merken, Farbfolge, Hütchenspiel, Ballons der Reihe nach, Stroop, Labyrinth.
  - Erwachsen: Kopfrechnen mit Kniff (Prozent, Rabatt, Wurzeln, a² – b², Minuten), Zahlenreihen (auch Quadrat- und Fibonacci-Reihen), Kettenrechnung mit 5 Schritten, 7 Ziffern merken, Farbfolge, Hütchenspiel mit 8 Tauschen, Stroop, Labyrinth.
  - Tipps sind im Zufallsmodus gratis; >> würfelt eine neue Aufgabe (die Serie beginnt dann von vorn). Der Spielstand der Level bleibt unberührt.

## Einstellungen

Von oben nach unten wischen (überall, auch im Level) öffnet die Einstellungen: Helligkeit (4 Stufen), Lautstärke, Sprache, Wer spielt?, **Alles auf 0 zurücksetzen** (alle drei Modi, Intro von vorn) und **Ausschalten**. Die letzten beiden fragen nach („Wirklich? Nochmal tippen!“). Schließen mit FERTIG oder nach oben wischen.

**Pusten und Rufen** zählen nur, wenn es nah am Gerät laut ist: Der Pegel muss über −30 dBFS liegen (voll bei −12 dBFS) und deutlich über dem Grundrauschen. Gespräche im Raum (Büro: um −40 dBFS) lösen nichts aus. Die erste Sekunde nach LOS hört das Level noch nicht hin. Die Schwellen stehen oben in `micTask` (`firmware/main.cpp`); `mikro` zeigt den Spitzenpegel zum Nachjustieren.

Neue Sätze nachsprechen lassen, ohne die vorhandenen Clips zu ändern (gleiche Sätze teilen sich einen Clip): `HEYGEN_API_KEY=… python3 tools/mkvoice.py --fehlende`.

Tipps: Am Anfang gibt es 15, jedes erstmals gelöste Level bringt einen dazu. Ein Tipp kostet beim ersten Ansehen pro Level einen. Mit „>>“ lässt sich ein Level für 3 Tipps überspringen. Spielstand, Tipps, Sprache, Lautstärke, Schwierigkeit und Bestzeiten bleiben im NVS gespeichert.

**Schwierigkeit** (Knopf auf dem Titelbild):

| Stufe | Regeln |
|---|---|
| Leicht | Tipps kostenlos, nach 30 s erscheint der Tipp von selbst, Überspringen kostet 1 Tipp, die Aufgabenkarte zeigt, was helfen könnte (Finger, schütteln, kippen …) |
| Normal | wie oben beschrieben |
| Schwer | keine Tipps, kein Überspringen, Stoppuhr mit Bestzeit pro Level |

**Intro und Aufgabenkarte**: Beim ersten Start erklären drei Karten das Spiel (wieder aufrufbar mit „?“ auf dem Titelbild). Vor jedem Level zeigt eine Karte die Aufgabe groß, mit „LOS!“ geht es los.

**Ton und Sprache**: Das Lautsprecher-Symbol schaltet durch vier Stufen (aus, leise, mittel, laut). Das Gerät liest die Aufgabe auf der Karte vor und ruft beim Lösen „Bravo!“ & Co. Die Clips sind mit Tobias' HeyGen-Stimmklon gesprochen (`tools/mkvoice.py`); solange das Gerät spricht, wird das Mikrofon ignoriert.

Nach 90 s ohne Berührung wird das Display dunkler, nach 4 min geht es aus. Ein Tipp weckt es wieder (dieser erste Tipp zählt nicht als Spielzug).

**Tasten (AMOLED-2.16):**

| Taste | Wirkung |
|---|---|
| Power kurz | Display aus/an (das Spiel pausiert, Touch wird ignoriert) |
| Power lang (ca. 1,5 s) | Gerät ausschalten; einschalten wieder mit der Power-Taste (nach 6 s schaltet der Power-Chip notfalls selbst ab) |
| Taste an GPIO18 | Display aus/an |
| BOOT | bleibt frei (zum Flashen) |

## Bauen und flashen

PlatformIO (`pip install platformio`), Gerät per USB-C anstecken:

```bash
pio run -t upload
```

Für das **Waveshare ESP32-S3-Touch-AMOLED-2.16** (480×480, Touch CST9220, Mikrofone am ES7210) gibt es eine eigene Umgebung. Das Spiel liegt dort mittig mit weißem Rand:

```bash
pio run -e amoled216 -t upload
```

Zum Display gehen nur die 16-Pixel-Bänder, die sich seit dem letzten Bild geändert haben (das Panel hat keinen TE-Ausgang; ein ständig neu geschriebenes Vollbild flackerte).

Die Partitionstabelle `partitions.csv` hat eine große App (15,9 MB) statt zwei OTA-Hälften, ohne SPIFFS, weil die Sprach-Clips eingebettet sind.

Die Plattform ist auf pioarduino `55.03.313` (Arduino-Core 3.3) festgelegt. Die aktuelle „stable“ bringt Core 4.0 mit, damit kompiliert GFX Library 1.6.8 nicht mehr.

Serielle Befehle (115200 Baud, `pio device monitor`):

| Befehl | Wirkung |
|---|---|
| `status` | Bildschirm, Level, gelöste Level, Tipps, Neigung, Mikrofonpegel |
| `level N` | Level N starten (Erwachsen 1–36, Kind 1–48, Klein 1–36) |
| `stufe N` | Schwierigkeit: 0 leicht, 1 normal, 2 schwer |
| `laut N` | Lautstärke 0 (stumm) bis 3 |
| `modus klein`, `modus kind`, `modus erwachsen` | Altersstufe wählen |
| `menu` | Einstellungen öffnen |
| `zufall` | Zufallsmodus starten (gibt die Lösung aus) |
| `mikro` | Spitzenpegel des Mikrofons seit dem letzten Aufruf |
| `sag KEY` | Sprach-Clip abspielen, z. B. `sag de_t05` |
| `tipp X Y`, `wisch X0 Y0 X1 Y1` | Berührung nachstellen |
| `klopf N` | N Schläge auf den Tisch nachstellen |
| `reset` | Spielstand löschen |
| `log` | fps und Rechenzeiten alle 2 s |

## Simulator

```bash
make -C sim && sim/bcsim --tour /tmp/bilder
```

`--tour` spielt jedes Level in beiden Sprachen auf seinem Lösungsweg durch (mit ein paar falschen Versuchen vorweg). Dabei prüft es, dass das Level gelöst wird, speichert Bilder (PPM) und meldet jeden Text, der über den Rand oder in die runden Display-Ecken ragt.

Mit Fenster (braucht SDL2, z. B. `brew install sdl2`): `make -C sim sdl && sim/bcsim`. Maus = Finger, X halten = schütteln, M halten = pusten/rufen, K = auf den Tisch klopfen, U halten = Gerät umdrehen, P = Bildschirmfoto.

## Aufbau

```
core/        Vektor-Renderer (368×448, kantengeglättet, bandweise), Schriften, Synthesizer, Klopf-/Schüttel-Erkennung
             (aus esp32-kids-adhs übernommen; neu: Hintergrundfarbe in Scene::bg)
game/        game.cpp: Intro, Titel, Levelauswahl, Aufgabenkarte, Kopfzeile, Tipps, Überspringen, Schwierigkeit, Erfolgsbild, Energiesparen
             levels.cpp: die 36 Level (je eine Klasse mit reset/tick/draw; Denkfragen über Quiz + QuizSpec, Zahlentastatur NumPad mit Sperre Lock)
             art.cpp: Schnörkel, Funkeln, Symbole und die Figuren (Dino, Maus, Elefant, Eule, Fledermaus, Hund, Ei …)
firmware/    Gerät: Rendern auf Kern 1, Display/Eingabe/Audio/Mikrofon auf Kern 0; voice.bin + voice_index.h (Sprach-Clips)
lib/amoled18 Board-Schicht: Display (CO5300), Touch (CST816 bzw. CST9220 auf dem 2.16), IMU (QMI8658), Audio (ES8311, Mikrofone am ES7210 auf dem 2.16)
sim/         Simulator (Tour ohne Fenster, interaktiv mit SDL2)
tools/       mkfont.py (Schrift → Bitmap-Header, Pillow), mkfonts.sh, fonts/, mkvoice.py (Sprach-Clips über HeyGen oder macOS say)
```

Ein neues Level ist eine Klasse in `game/levels.cpp`: `task()` und `hint()` (EN/DE), `reset()`, `tick()` bekommt pro Frame Finger, Neigung, Schütteln, Klopfen und Mikrofonpegel, `draw()` zeichnet. `h.solve()` meldet die Lösung, `h.nope()` und `h.say()` zeigen Zurufe. Dann `LEVELS` in `game/ui.h` hochzählen, `makeLevel()` ergänzen und in `sim/main.cpp` den Lösungsweg für die Tour eintragen.

Schriften neu erzeugen: `tools/mkfonts.sh` (braucht Pillow). Sprach-Clips neu erzeugen (nach geänderten Aufgabentexten): `make -C sim && HEYGEN_API_KEY=… python3 tools/mkvoice.py` (HeyGen, Modell `heygen-voice-1-turbo-free`, Stimme über `HEYGEN_VOICE_ID`) oder ohne Schlüssel `python3 tools/mkvoice.py --say` (macOS-Stimmen).

## Lizenzen

- Schriften: Montserrat und Amatic SC, SIL Open Font License 1.1, siehe [licenses/](licenses/). Die Bitmap-Fassungen in `core/font_*.h` sind daraus abgeleitet.
- ES8311-Treiber (`lib/amoled18/es8311*`): Espressif, Apache-2.0.
- ES7210-Treiber (`lib/amoled18/es7210*`, `audio_hal.h`): Espressif MIT License, aus den Waveshare-Beispielen zum AMOLED-2.16.
- Sprach-Clips (`firmware/voice.bin`): Tobias' eigene Stimme (HeyGen-Stimmklon „tobias_pitch“).
