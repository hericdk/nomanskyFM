#!/usr/bin/env python3
"""No Man Sky FM: the ship seen from above (the close-up in space) -> build/gen/nms_nave_cima.h
  assets/nave-cima.png: nose up, transparent round it (cut out of the user's picture of the four views); the longest side
  48; as tools/gen_nave.py. Drawn turned to the ship's heading by ui_game.c (KNOB 3's closest zoom)."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent))
import gen_nave

if __name__ == '__main__':
    gen_nave.main(sys.argv[1], 'nave-cima', 'NMS_NC', 'NMS_NAVE_CIMA', 48)
