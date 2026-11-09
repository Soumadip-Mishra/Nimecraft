<div align="center">

# Nimecraft

**A single-player survival voxel sandbox with an endless world.**
Mine, craft, build, and survive the night.

![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C?logo=cplusplus&logoColor=white)
![raylib 6.0](https://img.shields.io/badge/raylib-6.0-black)
![Platforms](https://img.shields.io/badge/platform-Linux%20%7C%20Windows-4c8eda)
![Version](https://img.shields.io/badge/version-1.0.0-3fb950)

</div>

---

## Features

- **Endless world.**
  Terrain streams in around you, 256 blocks tall, with seven biomes: Meadow, Forest, Pine Taiga, Desert, Snowfield, Highlands, and Ocean.
- **Caves, ores, and geodes.**
  Dig for coal, copper, tin, iron, silver, and quartz at the depths where each one hides.
- **Real progression.**
  Climb the tool tiers from Wood to Stone, Copper, Bronze, and Iron, with Silver as a weapon side branch.
- **Crafting stations.**
  Workbench, Kiln, Storage Crate, and Bedroll, with a recipe list that reacts to what you carry and what stands nearby.
- **Survival.**
  Health, hunger, fall damage, drowning, and a Grave Cairn that keeps your items when you die.
- **Day and night.**
  A 20-minute cycle with smooth lighting, torches, and lanterns.
- **Six creatures.**
  Tusker, Fluffhorn, and Hopper roam by day, while Gloomwalker, Thornspitter, and Cave Crawler hunt in the dark.
- **A living world.**
  Flowing water, growing plants, crops, and falling blocks.
- **Creative mode.**
  Fly, build freely, and take no damage.
- **Synthesized sound.**
  All effects are generated in code, with optional ambient music.
- **Multiple worlds.**
  Create worlds by name, seed, and mode, with autosave every two minutes.

---

## Download and install

Packages are built for 64-bit (x86-64) Linux and Windows.
Get them from the [Releases page](https://github.com/Soumadip-Mishra/Nimecraft/releases).

> If the Releases page is empty, no release has been published yet.
> Build the game yourself with the steps in [Build from source](#build-from-source).

### Linux

1. Download `Nimecraft-1.0.0-Linux.tar.gz`.
2. Unpack it:
   ```sh
   tar xzf Nimecraft-1.0.0-Linux.tar.gz
   cd Nimecraft-1.0.0-Linux
   ```
3. Start the game:
   ```sh
   ./nimecraft
   ```
4. Optional: add Nimecraft to your app menu (no `sudo` needed):
   ```sh
   sh install.sh
   ```
   The game is copied to `~/.local/opt/nimecraft`, and a "Nimecraft" entry with an icon appears in your menu.
   Run `install.sh` again from a newer release to upgrade.
   Run `sh uninstall.sh` to remove it.
   Your worlds and settings are kept.

### Windows

1. Download `Nimecraft-1.0.0-win64.zip`.
2. Right-click the file, choose **Extract All**, and pick a folder.
3. Open the extracted folder and double-click `nimecraft.exe`.

Windows may show a warning for an unsigned program the first time.
Choose **More info**, then **Run anyway**, if you trust the download.

---

## Controls

| Input | Action |
|---|---|
| `W` `A` `S` `D` | Walk |
| Mouse | Look around |
| `Space` | Jump, or swim up |
| `Ctrl` + `W` | Sprint |
| Left mouse | Mine a block or attack |
| Right mouse | Place a block, use a station, hold to eat |
| `Shift` + right mouse | Place a block against a station instead of opening it |
| `1`-`9` or wheel | Choose a hotbar slot |
| `Q` | Drop one item |
| `E` | Inventory and crafting |
| `Esc` | Pause menu |
| `F3` | Debug information |

In the inventory, left click picks up or puts down a stack, right click takes half or places one, and `Shift` + click moves a stack across.
Stations within 3 blocks unlock more recipes.
In Creative mode, double-tap `Space` to fly.

---

## Command-line options

These skip the title screen.

| Option | Meaning |
|---|---|
| `--world NAME` | Play the world folder `NAME` (created if missing) |
| `--seed N` | Seed for a new world, 0 to 4294967295 |
| `--rd N` | Render distance in columns, 4 to 16 |
| `--creative` | Start a new world in Creative mode |
| `--smoke` | A short automatic test run that saves nothing |

---

## Where your saves live

| System | Folder |
|---|---|
| Linux | `~/.local/share/nimecraft` (or `$XDG_DATA_HOME/nimecraft`) |
| Windows | `%APPDATA%\Nimecraft` |

Set the `NIMECRAFT_HOME` environment variable to use another folder.
Each world is a folder under `saves/`, and a world folder can be copied between Linux and Windows.

---

## Build from source

You need a C++20 compiler, CMake, and Make.
The build is driven through the Makefile, which wraps the CMake presets.

```sh
git clone https://github.com/Soumadip-Mishra/Nimecraft.git
cd Nimecraft
make run
```

| Command | What it does |
|---|---|
| `make` | Build the release game |
| `make run` | Build and run the release game |
| `make test` | Run all tests (ASan and UBSan) |
| `make tsan` | Run all tests under ThreadSanitizer |
| `make win` | Cross-build for Windows with MinGW and test under Wine |
| `make coverage` | Run the coverage gate |

To produce the release packages yourself:

```sh
make release
cmake --build build/release --target package
```

The archive appears in `build/release/`.

---

## Credits

Built with [raylib](https://www.raylib.com/).
All block, item, and creature names, textures, and sounds are original to Nimecraft.
