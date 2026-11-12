# Music credits

**Since Round 16e the pack holds only `synthwave.mp3`** (added by the user; source/licence not recorded yet). The
Pixabay tracks below are no longer used; their files are in `old_pixabay/`.

The background music of the game is made of tracks from Pixabay (https://pixabay.com/music/), used under the
Pixabay Content License ("Free for use"; free to use in a project like this one without attribution, but not public
domain: the tracks must not be redistributed or sold on their own). Credit is given anyway:

| Intensity tiers | Track | Artist | Page |
|---|---|---|---|
| 0 and 1 (calm) | "Space" (2:40, 75 s of it used) | leberch | https://pixabay.com/music/ambient-space-626368/ |
| 2 and 3 (intense) | "Space" (4:24, 75 s of it used) | Sub_Clair | https://pixabay.com/music/ambient-space-627616/ |
| 3 (most intense, Round 16d) | `synthwave.mp3` (2:13, whole track) | added by the user; source/licence not recorded yet | - |

Round 16d: with three tracks the pack keeps 150 s of each (`MUSIC_WINDOW=150`): tiers 0-1 leberch, tier 2 Sub_Clair,
tier 3 synthwave.

Only a part of each track is stored (`make music`: the most energetic 75 s after compression, 4-bit ADPCM at 15.625 kHz, mono), to save flash.
The original MP3 files in this folder are the user's downloads; `tools/music_pack.py` turns them into the flash image.
`music_tier*.wav` are PC previews of the earlier synthesized tune (`tools/music_render.py`).
