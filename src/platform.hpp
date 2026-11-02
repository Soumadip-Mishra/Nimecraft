// The only OS-specific module (Global Constraints, docs/plan/README.md).
#pragma once

#include <filesystem>

// Folder for saves and settings, created if missing:
// - NIMECRAFT_HOME, if set and non-empty;
// - Linux: $XDG_DATA_HOME/nimecraft if XDG_DATA_HOME is an absolute path (relative or empty is
//   ignored, per the XDG spec), else $HOME/.local/share/nimecraft;
// - Windows: %APPDATA%\Nimecraft (read with _wgetenv, so non-ASCII folders work).
// Throws std::runtime_error when HOME (Linux) or APPDATA (Windows) is needed but is not an
// absolute path, and std::filesystem::filesystem_error when the folder cannot be created.
std::filesystem::path user_data_dir();
