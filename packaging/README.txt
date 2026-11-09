Nimecraft - a single-player survival voxel sandbox
===================================================

RUNNING THE GAME

  Linux:    open a terminal in this folder and run  ./nimecraft
  Windows:  double-click nimecraft.exe

  The game opens on the title screen: Play shows your worlds; create one with a
  name, an optional seed, and a mode (Survival or Creative).

  Command-line options (they skip the title screen):
    --world NAME   play the world folder NAME (created if missing)
    --seed N       seed for a new world, 0..4294967295
    --rd N         render distance in columns, 4..16
    --creative     start a new world in Creative mode
    --smoke        a short automatic test run that saves nothing

LINUX APP MENU (optional)

  sh install.sh     copies the game to ~/.local/opt/nimecraft, adds the icon,
                    and adds "Nimecraft" to your desktop's app menu (no sudo).
                    Run it again from a newer release folder to upgrade.
  sh uninstall.sh   removes those three things again. Your worlds and settings
                    are kept.

WHERE SAVES LIVE

  Linux:    ~/.local/share/nimecraft   ($XDG_DATA_HOME/nimecraft if that is set)
  Windows:  %APPDATA%\Nimecraft
  Set the NIMECRAFT_HOME environment variable to use another folder.

  Each world is a folder under saves/; settings are in settings.ini. A world
  folder can be copied between Linux and Windows. The game saves every two
  minutes, when you choose "Save and Quit to Title", and when you close it.

CONTROLS

  W A S D          walk                      Mouse          look around
  Space            jump (swim up in water)   Ctrl + W       sprint
  Left mouse       mine a block / attack     Right mouse    place a block, use a
                                                            station, hold to eat
  Shift + Right    place a block against a station instead of opening it
  1-9, wheel       choose a hotbar slot      Q              drop one item
  E                inventory and crafting    Esc            pause menu
  F3               debug information         F4             skip time forward

  In the inventory: left click picks up or puts down a stack, right click
  takes half or puts one, Shift + click moves a stack across. Stations within
  3 blocks (workbench, kiln) unlock more recipes; Shift + click a recipe to
  craft many.

  Creative mode: double-tap Space to fly; Space rises, Shift sinks.

MUSIC

  Put .ogg files in an "assets/music" folder next to the game to hear them now
  and then while you play. Without that folder the game simply has no music.
