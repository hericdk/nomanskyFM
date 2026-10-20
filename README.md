# No Man Sky FM

**A space adventure for the M-VAVE FM-1.** One hand flies the ship with two knobs, the other makes the music. Land on planets, mine, fight robots, warp between stars, and play the soundtrack of your own journey. Current version **v0.5**, based on **SLOOP 2.7.1**.

- **Site, installer and 3D simulator (the real firmware in your browser, with sound):** https://hericdk.github.io/nomanskyFM/
- **Install on your FM-1:** open the site in Chrome or Edge, connect the FM-1 by USB and press install. The package is also in [`release/nmsfm-0.5.fwsc`](release/).
- **License:** GPL-3.0-only, like SLOOP and Felucca.

## The shortcuts that exist only here

On HOME the FM-1 is the game.

| Control | What it does |
|---|---|
| **KNOB 1** | turn the ship · on a planet: walk |
| **KNOB 2** | speed · on a planet: walk |
| **KNOB 3** | zoom: wide, near, close-up, cabin |
| **KNOB 4** | the selected track's volume |
| **OCT− held** | boost (1.2 s: pulse jump) · on a planet: run |
| **OCT− held, standing still** | on a planet: call the ship |
| **OCT− twice** | auto boost (another click: off) |
| **OCT+ held** | land · dock · laser (let go: collect) · take off by the ship |
| **OCT− + OCT+** | the warp map |
| **HOME held** | on a planet: call the ship · in space: the menu |
| **HOME twice** | radar |

**A quick press keeps the usual function.** Only holding, double tapping or pressing together does something new: tapped once, OCT− and OCT+ still change the octave, HOME goes home, PLAY plays, REC records, and every other button does what it always did on SLOOP.

## What is unique to No Man Sky FM

| Feature | What it does |
|---|---|
| **The opening** | Your ship flies in over the stars with its own soft synthwave theme, then arrives in space. A first-time HOW TO FLY box explains the controls. |
| **Planets** | Every planet different, in all eight colours of the game: seas, rivers, hills, rings. The three nearest wait at the edge of the screen with their distance in light-years. |
| **On the ground** | Walk, run, mine resources with the laser (let go to collect), fight the sentinels: they chase you, three hits and they blow up. Hostile planets shoot back. |
| **Call the ship, take off** | The ship comes down beside you; a pixel-art film carries you through the clouds back to space. |
| **Pursuit** | The robots you fought follow you into space as red ships, shooting. Boost hard and you lose them. Radar: a blue ring of light. |
| **Close-up and cabin** | Zoom in to the ship among trails of light, and once more to sit inside it, the planet ahead in the window. |
| **Warp** | A galactic map of star systems, a tunnel of light, a new system. |
| **Every screen over the game** | Pages, layers, REC, the menu: the game goes on behind them through a mesh of pixels. |
| **Key light effects** | Menu · KEY LED: the light a key makes (waves, rain, comet, fireworks). |
| **DRUM SOUND page** | GLO · DRUM SOUND: a drum's TUNE and DECAY, in this firmware's look. |
| **REC · QUANT** | KNOB 4 on the REC screen: QUANT ON / OFF, to record off the grid. |
| **3D simulator** | The real firmware compiled to WebAssembly, in a 3D model of the FM-1. |

Merged from SLOOP 2.6 → 2.7.1: the sound on both cores of the FM-1, 15 arp modes and KBD HOLD, MIDI 3 (FX channel, drum map, remote, clock out), SAVE SECTION A–D and favourite sounds, drum TUNE and DECAY, looping samples, REC QUANT off, knob acceleration, USB rate.

## Changes

- **v0.5:** battles in space (a boss in 30 s, little ships that keep coming), pirate raids, escorts called by holding keys, races on planets and in space (gears, pixel dashboard), a hangar with five ships and upgrades, quests and ships that talk to you, REC keeps the arp and the punch-in FX (saved with the project: format 7, older projects still load), a take-off and close-up drawn from a 3D model of the ship.
- **v0.4:** launch version, based on SLOOP 2.7.1.

## Use it in your own work

You are welcome to use these features, screens and ideas in your own firmware or project, **as long as you give proper credit**: a line in your README **or** in your presentation video, for example:

> Features from No Man Sky FM by Hericdk (github.com/hericdk/nomanskyFM)

The code is GPL-3.0-only, so any firmware you distribute that is based on it must also ship its source under the same licence. The credit above is the author's request, on top of the licence.

## What is in this repository

| Folder | Content |
|---|---|
| `index.html`, `assets/`, `media/`, `sim/` | The site: installer, 3D simulator (`sim/`) and the feature clips. |
| `release/` | The firmware package (`nmsfm-0.5.fwsc`). |
| `firmware/` | The complete source of that firmware (C, JieLi AC79), as the GPL requires. `sh firmware/build.sh` builds the package (see `firmware/BUILDING.md`). |

**Rescue:** hold OCT− alone while switching the FM-1 on to enter the USB rescue mode and reinstall any firmware. Installing custom firmware is at your own risk.

## Credits

- **No Man Sky FM:** Hericdk ([Instagram](https://www.instagram.com/hericdk/) · [YouTube](https://www.youtube.com/@_heric))
- **SLOOP** by [isod89](https://github.com/isod89/sloop-fm1), the firmware No Man Sky FM is built on
- **Felucca** by Leo Kuroshita ([@kurogedelic](https://github.com/kurogedelic)), Hügelton Instruments, the base of SLOOP
- Inspired by No Man's Sky: a fan-made demake, a thank-you to Hello Games and the game's community.
- Font: Pixelify Sans (SIL OFL). Sampled sounds: CC0 recordings (asset terms in `firmware/LICENSING.md`).

Not affiliated with Hello Games or M-VAVE. No Man's Sky, M-VAVE and FM-1 are trademarks of their respective owners.
