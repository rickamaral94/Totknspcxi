// Where TOTKDroid keeps its files (runtime/paths.c).
//
// Data root: everything the app writes or the user brings - extracted/ (game files), save/,
// cache/ (shaders / pipelines), fonts/ (imported), larecomp.ini. rt_paths_init() picks it and makes it the
// working directory, so the rest of the runtime opens those by their relative names. In order:
//   1. LARECOMP_DATA_ROOT (tests, scratch installs)
//   2. dev / portable mode: the starting directory already has extracted/ (a source checkout: `./build/totkrecompandroid
//      extracted`, the test scripts) - used as is
//   3. macOS: ~/Library/Application Support/com.mamcjr.totkrecompandroid (the .app bundle itself is never written to)
//      Windows: the folder of the .exe (portable, no installer); refuses to start, with a message, if not writable
//      Linux: $XDG_DATA_HOME/totkrecompandroid (~/.local/share/totkrecompandroid)
//      Android: the app's external files folder (Android/data/<package>/files)
//
// Game versions: that root is the main version's (rt.h GAME_V_MAIN, Tears of the Kingdom 1.4.3). Others would each run in a folder
// of their own inside it, v<version> (game files, saves, settings - android/run_ver.sh's test runs use the same ones):
// the one the start screen chose (game_version.txt in the root) becomes the data root instead, its settings and saves
// starting as copies of the main version's (services/fs.c, LARECOMP_SAVE_SEED). There are none yet (paths.c
// other_version), but the mechanism stays from BOTWRecompAndroid (1.0.0, 1.6.0 and 1.8.2 side by side).
//
// Resources: files that ship with the app (helper programs, the window icon) - next to the executable (the
// Windows folder, the .app's Contents/MacOS), in the .app's Contents/Resources, or in the source tree.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RT_BUNDLE_ID "com.mamcjr.totkrecompandroid"

void rt_paths_init(void);         // first thing in main(), after plat_init
const char *rt_data_root(void);   // absolute
bool rt_dev_mode(void);           // dev / portable mode (rule 2 above): e.g. a source checkout

// A shipped file: `name` next to the executable or in Contents/Resources, else `dev_rel` in the source tree (the
// starting directory or the executable's parent; NULL = shipped builds only). Absolute path in out; false = none.
bool rt_resource(const char *name, const char *dev_rel, char *out, size_t n);

// Game versions, from main's build id (its NSO header's bytes 0x40-0x47: runtime/loader.c; 0 = not one this app runs)
#define GAME_V_100 100          // Tears of the Kingdom 1.0.0 (the base game, no update): beside 1.4.3, in v100, recompiled only
#define GAME_V_142 142          // Tears of the Kingdom 1.4.2 (the update v655360, SDK 20.5.6): beside 1.4.3, in v142, recompiled only
#define GAME_V_143 143          // Tears of the Kingdom 1.4.3 (the update v720896, SDK 20.5.22; MacNativeGames' version)
#define GAME_V_MAIN GAME_V_143  // the one in the data root itself (1.4.2, the port's first, was dropped 2026-10-25; back beside it)
int game_version_of_build(const uint8_t id[8]);
const char *game_version_name(int v);                     // "1.4.3", "1.4.2"; "?" for one this app doesn't run
static inline int game_is_totk(int v) { return v == GAME_V_100 || v == GAME_V_142 || v == GAME_V_143; }
// A main build id an import found to be a version (TOTK 1.0.0: the base package's CNMT says version 0), kept in the main
// data root (game_builds.txt) so the loader and the setup know it from then on
bool game_version_register(const uint8_t id[8], int v);
int rt_game_version(void);                                // the one whose folder is the data root
bool rt_select_game_version(int v);                       // the one the next start runs (the app restarts into it)
bool rt_version_root(int v, char *out, size_t n);         // its folder (absolute)
int rt_game_dir_version(const char *game_dir);            // whose extracted/ folder game_dir is; 0: none
// Where its game code library is loaded from: another version's own (Android: <internal files>/v<version>, as external
// storage is mounted noexec; elsewhere its extracted/). false for the main version, whose library is looked for as
// before (runtime/gamelib.c: next to the executable, then extracted/).
bool rt_version_game_lib(int v, char *out, size_t n);

// The only way the app deletes game files. Removes the named entry of game_dir (an extracted/ folder) only if
// game_dir is <data root>/extracted itself or a game version's (symlinks resolved; a symlinked extracted/ is refused)
// and the entry is one the setup creates (exefs, romfs, romfs.bin, leftover folders) and not a link. The folder the
// user picked never gets here. true = removed or not there.
bool rt_remove_game_file(const char *game_dir, const char *name);
// All the game files set up there (rt_remove_game_file on each, then the empty folder). Saves and settings stay.
bool rt_remove_game_files(const char *game_dir);
// The same before a new setup there, except the game code's compiled objects (.gamebuild): runtime/gamebuild.c keeps
// them only when they were compiled from the same game code by the same builder, so the folder picked again after a
// build the app didn't finish goes on from there (issue #25: it started over)
bool rt_remove_game_files_for_setup(const char *game_dir);
// Bytes taken by the extracted game files in game_dir (links not followed).
unsigned long long rt_game_files_size(const char *game_dir);

typedef struct SDL_Window SDL_Window;
void rt_window_icon(SDL_Window *w);  // the shipped icon.png, if any

#ifdef __cplusplus
}
#endif
