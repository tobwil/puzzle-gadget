#!/bin/sh
# Erzeugt core/font_*.h aus tools/fonts (Montserrat, Amatic SC; beide SIL Open Font License 1.1).
set -e
cd "$(dirname "$0")/.."
M=tools/fonts/Montserrat
python3 tools/mkfont.py ${M}-Black.ttf 46 huge "ABCDEFGHIJKLMNOPQRSTUVWXYZÄÖÜ0123456789.!? " core/font_huge.h
python3 tools/mkfont.py ${M}-Black.ttf 27 heavy ascii core/font_heavy.h
python3 tools/mkfont.py ${M}-Medium.ttf 19 body ascii core/font_body.h
python3 tools/mkfont.py ${M}-ExtraBold.ttf 15 label ascii core/font_label.h
python3 tools/mkfont.py ${M}-ExtraBold.ttf 22 num ascii core/font_num.h
python3 tools/mkfont.py tools/fonts/AmaticSC-Bold.ttf 32 hand ascii core/font_hand.h
