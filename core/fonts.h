// Schriften von Brain Candy (Daten in fonts.cpp, erzeugt von tools/mkfonts.sh).
// Bitmap-Fassungen, abgeleitet von Montserrat und Amatic SC (SIL Open Font License 1.1, siehe licenses/).
#pragma once
#include "font.h"

namespace fonts {
extern const Font huge;    // 46 px Montserrat Black, nur Großbuchstaben, Ziffern und .!? (Überschriften)
extern const Font heavy;   // 27 px Montserrat Black („LVL.7“, Knöpfe)
extern const Font body;    // 19 px Montserrat Medium (Aufgabentext)
extern const Font label;   // 15 px Montserrat ExtraBold („Restart“, „Hints“)
extern const Font num;     // 22 px Montserrat ExtraBold (Zahlen in Knöpfen)
extern const Font hand;    // 32 px Amatic SC Bold (Handschrift: Tipps, Zurufe)
}  // namespace fonts
