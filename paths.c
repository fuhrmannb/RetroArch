/*  RetroArch - A frontend for libretro.
 *  Copyright (C) 2011-2016 - Daniel De Matteis
 * 
 *  RetroArch is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  RetroArch is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with RetroArch.
 *  If not, see <http://www.gnu.org/licenses/>.
 */

#include <retro_miscellaneous.h>
#include <compat/strl.h>
#include <file/file_path.h>
#include <lists/string_list.h>
#include <string/stdstring.h>
#include <retro_assert.h>
#include <retro_stat.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_NETPLAY
#include "network/netplay/netplay.h"
#endif

#include "dirs.h"
#include "paths.h"

#include "configuration.h"
#include "command.h"
#include "content.h"
#include "dynamic.h"
#include "movie.h"
#include "file_path_special.h"

#include "core.h"
#include "msg_hash.h"
#include "retroarch.h"
#include "runloop.h"
#include "verbosity.h"
#include "tasks/tasks_internal.h"

#define MENU_VALUE_NO_CORE 0x7d5472cbU

/* For --subsystem content. */
static struct string_list *subsystem_fullpaths          = NULL;

char subsystem_path[PATH_MAX_LENGTH]                    = {0};

static char path_default_shader_preset[PATH_MAX_LENGTH] = {0};
static char path_main_basename[PATH_MAX_LENGTH]         = {0}
;
static char path_content[PATH_MAX_LENGTH]               = {0};
static char current_savefile_dir[PATH_MAX_LENGTH]       = {0};
static char path_libretro[PATH_MAX_LENGTH]              = {0};
static char path_config_file[PATH_MAX_LENGTH]           = {0};
static char path_config_append_file[PATH_MAX_LENGTH]    = {0};
/* Config file associated with per-core configs. */
static char path_core_options_file[PATH_MAX_LENGTH]     = {0};

void path_set_redirect(void)
{
   char current_savestate_dir[PATH_MAX_LENGTH] = {0};
   uint32_t global_library_name_hash           = 0;
   bool check_global_library_name_hash         = false;
   rarch_system_info_t      *info              = NULL;
   global_t                *global             = global_get_ptr();
   const char *old_savefile_dir                = dir_get_savefile();
   const char *old_savestate_dir               = dir_get_savestate();

   runloop_ctl(RUNLOOP_CTL_SYSTEM_INFO_GET, &info);

   if (info && info->info.library_name &&
         !string_is_empty(info->info.library_name))
      global_library_name_hash =
         msg_hash_calculate(info->info.library_name);

   /* Initialize current save directories
    * with the values from the config. */
   strlcpy(current_savefile_dir,
         old_savefile_dir,
         sizeof(current_savefile_dir));
   strlcpy(current_savestate_dir,
         old_savestate_dir,
         sizeof(current_savestate_dir));

   check_global_library_name_hash = (global_library_name_hash != 0);
#ifdef HAVE_MENU
   check_global_library_name_hash = check_global_library_name_hash &&
      (global_library_name_hash != MENU_VALUE_NO_CORE);
#endif

   if (check_global_library_name_hash)
   {
      settings_t *settings = config_get_ptr();

      /* per-core saves: append the library_name to the save location */
      if (settings->sort_savefiles_enable
            && !string_is_empty(old_savefile_dir))
      {
         fill_pathname_join(
               current_savefile_dir,
               old_savefile_dir,
               info->info.library_name,
               sizeof(current_savefile_dir));

         /* If path doesn't exist, try to create it,
          * if everything fails revert to the original path. */
         if(!path_is_directory(current_savefile_dir)
               && !string_is_empty(current_savefile_dir))
         {
            path_mkdir(current_savefile_dir);
            if(!path_is_directory(current_savefile_dir))
            {
               RARCH_LOG("%s %s\n",
                     msg_hash_to_str(MSG_REVERTING_SAVEFILE_DIRECTORY_TO),
                     old_savefile_dir);

               strlcpy(current_savefile_dir,
                     old_savefile_dir,
                     sizeof(current_savefile_dir));
            }
         }
      }

      /* per-core states: append the library_name to the save location */
      if (settings->sort_savestates_enable
            && !string_is_empty(old_savestate_dir))
      {
         fill_pathname_join(
               current_savestate_dir,
               old_savestate_dir,
               info->info.library_name,
               sizeof(current_savestate_dir));

         /* If path doesn't exist, try to create it.
          * If everything fails, revert to the original path. */
         if(!path_is_directory(current_savestate_dir) &&
               !string_is_empty(current_savestate_dir))
         {
            path_mkdir(current_savestate_dir);
            if(!path_is_directory(current_savestate_dir))
            {
               RARCH_LOG("%s %s\n",
                     msg_hash_to_str(MSG_REVERTING_SAVESTATE_DIRECTORY_TO),
                     old_savestate_dir);
               strlcpy(current_savestate_dir,
                     old_savestate_dir,
                     sizeof(current_savestate_dir));
            }
         }
      }
   }

   /* Set savefile directory if empty based on content directory */
   if (string_is_empty(current_savefile_dir))
   {
      strlcpy(current_savefile_dir, path_main_basename,
            sizeof(current_savefile_dir));
      path_basedir(current_savefile_dir);
   }

   if (global)
   {
      if(path_is_directory(current_savefile_dir))
         strlcpy(global->name.savefile, current_savefile_dir,
               sizeof(global->name.savefile));

      if(path_is_directory(current_savestate_dir))
         strlcpy(global->name.savestate, current_savestate_dir,
               sizeof(global->name.savestate));

      if (path_is_directory(global->name.savefile))
      {
         fill_pathname_dir(global->name.savefile, path_main_basename,
               file_path_str(FILE_PATH_SRM_EXTENSION),
               sizeof(global->name.savefile));
         RARCH_LOG("%s \"%s\".\n",
               msg_hash_to_str(MSG_REDIRECTING_SAVEFILE_TO),
               global->name.savefile);
      }

      if (path_is_directory(global->name.savestate))
      {
         fill_pathname_dir(global->name.savestate, path_main_basename,
               file_path_str(FILE_PATH_STATE_EXTENSION),
               sizeof(global->name.savestate));
         RARCH_LOG("%s \"%s\".\n",
               msg_hash_to_str(MSG_REDIRECTING_SAVESTATE_TO),
               global->name.savestate);
      }

      if (path_is_directory(global->name.cheatfile))
      {
         fill_pathname_dir(global->name.cheatfile, path_main_basename,
               file_path_str(FILE_PATH_STATE_EXTENSION),
               sizeof(global->name.cheatfile));
         RARCH_LOG("%s \"%s\".\n",
               msg_hash_to_str(MSG_REDIRECTING_CHEATFILE_TO),
               global->name.cheatfile);
      }
   }
}

void path_set_basename(const char *path)
{
   char *dst          = NULL;

   path_set_content(path);

   strlcpy(path_main_basename,     path, sizeof(path_main_basename));

#ifdef HAVE_COMPRESSION
   /* Removing extension is a bit tricky for compressed files.
    * Basename means:
    * /file/to/path/game.extension should be:
    * /file/to/path/game
    *
    * Two things to consider here are: /file/to/path/ is expected
    * to be a directory and "game" is a single file. This is used for
    * states and srm default paths.
    *
    * For compressed files we have:
    *
    * /file/to/path/comp.7z#game.extension and
    * /file/to/path/comp.7z#folder/game.extension
    *
    * The choice I take here is:
    * /file/to/path/game as basename. We might end up in a writable
    * directory then and the name of srm and states are meaningful.
    *
    */
   path_basedir(path_main_basename);
   fill_pathname_dir(path_main_basename, path, "", sizeof(path_main_basename));
#endif

   if ((dst = strrchr(path_main_basename, '.')))
      *dst = '\0';
}

struct string_list *path_get_subsystem_list(void)
{
   return subsystem_fullpaths;
}

const char *path_get_current_savefile_dir(void)
{
   char *ret = current_savefile_dir;

   /* try to infer the path in case it's still empty by calling
   path_set_redirect */
   if (string_is_empty(ret) && !content_does_not_need_content())
      path_set_redirect();

   return ret;
}

void path_set_special(char **argv, unsigned num_content)
{
   unsigned i;
   union string_list_elem_attr attr;
   global_t   *global   = global_get_ptr();

   /* First content file is the significant one. */
   path_set_basename(argv[0]);

   subsystem_fullpaths = string_list_new();
   retro_assert(subsystem_fullpaths);

   attr.i = 0;

   for (i = 0; i < num_content; i++)
      string_list_append(subsystem_fullpaths, argv[i], attr);

   /* We defer SRAM path updates until we can resolve it.
    * It is more complicated for special content types. */

   if (!retroarch_override_setting_is_set(RARCH_OVERRIDE_SETTING_STATE_PATH))
      fill_pathname_noext(global->name.savestate, path_main_basename,
            file_path_str(FILE_PATH_STATE_EXTENSION),
            sizeof(global->name.savestate));

   if (path_is_directory(global->name.savestate))
   {
      fill_pathname_dir(global->name.savestate, path_main_basename,
            file_path_str(FILE_PATH_STATE_EXTENSION),
            sizeof(global->name.savestate));
      RARCH_LOG("%s \"%s\".\n",
            msg_hash_to_str(MSG_REDIRECTING_SAVESTATE_TO),
            global->name.savestate);
   }
}

static bool path_init_subsystem(void)
{
   unsigned i, j;
   const struct retro_subsystem_info *info = NULL;
   rarch_system_info_t             *system = NULL;
   global_t                        *global = global_get_ptr();

   runloop_ctl(RUNLOOP_CTL_SYSTEM_INFO_GET, &system);

   if (!system)
      return false;

   if (path_is_subsystem_empty())
      return false;

   /* For subsystems, we know exactly which RAM types are supported. */

   info = libretro_find_subsystem_info(
         system->subsystem.data,
         system->subsystem.size,
         path_get_subsystem());

   /* We'll handle this error gracefully later. */

   if (info)
   {
      unsigned num_content = MIN(info->num_roms,
            path_is_subsystem_empty() ?
            0 : subsystem_fullpaths->size);

      for (i = 0; i < num_content; i++)
      {
         for (j = 0; j < info->roms[i].num_memory; j++)
         {
            union string_list_elem_attr attr;
            char path[PATH_MAX_LENGTH] = {0};
            char ext[32] = {0};
            const struct retro_subsystem_memory_info *mem =
               (const struct retro_subsystem_memory_info*)
               &info->roms[i].memory[j];

            snprintf(ext, sizeof(ext), ".%s", mem->extension);

            if (path_is_directory(dir_get_savefile()))
            {
               /* Use SRAM dir */
               /* Redirect content fullpath to save directory. */
               strlcpy(path, dir_get_savefile(), sizeof(path));
               fill_pathname_dir(path,
                     subsystem_fullpaths->elems[i].data, ext,
                     sizeof(path));
            }
            else
            {
               fill_pathname(path, subsystem_fullpaths->elems[i].data,
                     ext, sizeof(path));
            }

            attr.i = mem->type;
            string_list_append((struct string_list*)savefile_ptr_get(), path, attr);
         }
      }
   }

   /* Let other relevant paths be inferred from the main SRAM location. */
   if (!retroarch_override_setting_is_set(RARCH_OVERRIDE_SETTING_SAVE_PATH))
      fill_pathname_noext(global->name.savefile,
            path_main_basename,
            file_path_str(FILE_PATH_SRM_EXTENSION),
            sizeof(global->name.savefile));

   if (path_is_directory(global->name.savefile))
   {
      fill_pathname_dir(global->name.savefile,
            path_main_basename,
            file_path_str(FILE_PATH_SRM_EXTENSION),
            sizeof(global->name.savefile));
      RARCH_LOG("%s \"%s\".\n",
            msg_hash_to_str(MSG_REDIRECTING_SAVEFILE_TO),
            global->name.savefile);
   }

   return true;
}

void path_init_savefile(void)
{
   bool should_sram_be_used = rarch_ctl(RARCH_CTL_IS_SRAM_USED, NULL) 
      && !rarch_ctl(RARCH_CTL_IS_SRAM_SAVE_DISABLED, NULL);
#ifdef HAVE_NETPLAY
   global_t *global         = global_get_ptr();

   should_sram_be_used = should_sram_be_used &&
      (!netplay_driver_ctl(RARCH_NETPLAY_CTL_IS_DATA_INITED, NULL)
       || !global->netplay.is_client);
#endif

   if (should_sram_be_used)
      rarch_ctl(RARCH_CTL_SET_SRAM_ENABLE_FORCE, NULL);
   else
      rarch_ctl(RARCH_CTL_UNSET_SRAM_ENABLE, NULL);

   if (rarch_ctl(RARCH_CTL_IS_SRAM_USED, NULL))
   {
      command_event(CMD_EVENT_AUTOSAVE_INIT, NULL);
      return;
   }

   RARCH_LOG("%s\n",
         msg_hash_to_str(MSG_SRAM_WILL_NOT_BE_SAVED));
}

static void path_init_savefile_internal(void)
{
   path_deinit_savefile();

   path_init_savefile_new();

   if (!path_init_subsystem())
      path_init_savefile_rtc();
}

void path_set_names(const char *path)
{
   global_t *global = global_get_ptr();

   path_set_basename(path);

   if (!retroarch_override_setting_is_set(RARCH_OVERRIDE_SETTING_SAVE_PATH))
      fill_pathname_noext(global->name.savefile, path_main_basename,
            file_path_str(FILE_PATH_SRM_EXTENSION), sizeof(global->name.savefile));

   if (!retroarch_override_setting_is_set(RARCH_OVERRIDE_SETTING_STATE_PATH))
      fill_pathname_noext(global->name.savestate, path_main_basename,
            file_path_str(FILE_PATH_STATE_EXTENSION), sizeof(global->name.savestate));

   fill_pathname_noext(global->name.cheatfile, path_main_basename,
         file_path_str(FILE_PATH_CHT_EXTENSION), sizeof(global->name.cheatfile));

   path_set_redirect();
}

void path_fill_names(void)
{
   global_t *global = global_get_ptr();

   path_init_savefile_internal();
   bsv_movie_set_path(global->name.savefile);

   if (string_is_empty(path_main_basename))
      return;

   if (string_is_empty(global->name.ups))
      fill_pathname_noext(global->name.ups, path_main_basename,
            file_path_str(FILE_PATH_UPS_EXTENSION),
            sizeof(global->name.ups));

   if (string_is_empty(global->name.bps))
      fill_pathname_noext(global->name.bps, path_main_basename,
            file_path_str(FILE_PATH_BPS_EXTENSION),
            sizeof(global->name.bps));

   if (string_is_empty(global->name.ips))
      fill_pathname_noext(global->name.ips, path_main_basename,
            file_path_str(FILE_PATH_IPS_EXTENSION),
            sizeof(global->name.ips));
}

/* Core file path */

const char *path_get_subsystem(void)
{
   return subsystem_path;
}

const char *path_get_basename(void)
{
   return path_main_basename;
}

char *path_get_core_ptr(void)
{
   return path_libretro;
}

const char *path_get_core(void)
{
   return path_libretro;
}

bool path_is_core_empty(void)
{
   return !path_libretro[0];
}

size_t path_get_core_size(void)
{
   return sizeof(path_libretro);
}

void path_set_core(const char *path)
{
   strlcpy(path_libretro, path, sizeof(path_libretro));
}

void path_set(enum rarch_path_type type, const char *path)
{
   switch (type)
   {
      case RARCH_PATH_SUBSYSTEM:
         strlcpy(subsystem_path, path, sizeof(subsystem_path));
         break;
      default:
      case RARCH_PATH_NONE:
         break;
   }
}

void path_clear_subsystem(void)
{
   *subsystem_path = '\0';
}

void path_clear_core(void)
{
   *path_libretro = '\0';
}

void path_clear_default_shader_preset(void)
{
   *path_default_shader_preset = '\0';
}

/* Config file path */

bool path_is_subsystem_empty(void)
{
   if (string_is_empty(subsystem_path))
      return true;

   return false;
}

bool path_is_config_empty(void)
{
   if (string_is_empty(path_config_file))
      return true;

   return false;
}

void path_set_config(const char *path)
{
   strlcpy(path_config_file, path, sizeof(path_config_file));
}

const char *path_get_config(void)
{
   if (!path_is_config_empty())
      return path_config_file;

   return NULL;
}

bool path_get_default_shader_preset(char **preset)
{
   if (!preset)
      return false;
   *preset       = (char*)path_default_shader_preset;
   return true;
}

void path_clear_config(void)
{
   *path_config_file = '\0';
}

void path_clear_basename(void)
{
   *path_main_basename = '\0';
}

/* Core options file path */

bool path_is_core_options_empty(void)
{
   if (string_is_empty(path_core_options_file))
      return true;

   return false;
}

void path_clear_core_options(void)
{
   *path_core_options_file = '\0';
}

bool path_set_default_shader_preset(const char *preset)
{
   if (!preset)
      return false;
   strlcpy(path_default_shader_preset, preset,
         sizeof(path_default_shader_preset));
   return true;
}

void path_set_core_options(const char *path)
{
   strlcpy(path_core_options_file, path, sizeof(path_core_options_file));
}

const char *path_get_core_options(void)
{
   if (!path_is_core_options_empty())
      return path_core_options_file;

   return NULL;
}

/* Append config file path */

bool path_is_config_append_empty(void)
{
   if (string_is_empty(path_config_append_file))
      return true;

   return false;
}

void path_clear_config_append(void)
{
   *path_config_append_file = '\0';
}

void path_clear_content(void)
{
   *path_content = '\0';
}

bool path_set_content(const char *path)
{
   if (!path)
      return false;
   strlcpy(path_content, path, sizeof(path_content));
   return true;
}

void path_set_config_append(const char *path)
{
   strlcpy(path_config_append_file, path, sizeof(path_config_append_file));
}

bool path_get_content(char **fullpath)
{
   if (!fullpath)
      return false;
   *fullpath       = (char*)path_content;

   return true;
}

const char *path_get_config_append(void)
{
   if (!path_is_config_append_empty())
      return path_config_append_file;

   return NULL;
}

void path_clear_all(void)
{
   path_clear_config();
   path_clear_config_append();
   path_clear_core_options();

   path_clear_basename();
}

enum rarch_content_type path_is_media_type(const char *path)
{
   char ext_lower[PATH_MAX_LENGTH] = {0};

   strlcpy(ext_lower, path_get_extension(path), sizeof(ext_lower));

   string_to_lower(ext_lower);

   switch (msg_hash_to_file_type(msg_hash_calculate(ext_lower)))
   {
#ifdef HAVE_FFMPEG
      case FILE_TYPE_OGM:
      case FILE_TYPE_MKV:
      case FILE_TYPE_AVI:
      case FILE_TYPE_MP4:
      case FILE_TYPE_FLV:
      case FILE_TYPE_WEBM:
      case FILE_TYPE_3GP:
      case FILE_TYPE_3G2:
      case FILE_TYPE_F4F:
      case FILE_TYPE_F4V:
      case FILE_TYPE_MOV:
      case FILE_TYPE_WMV:
      case FILE_TYPE_MPG:
      case FILE_TYPE_MPEG:
      case FILE_TYPE_VOB:
      case FILE_TYPE_ASF:
      case FILE_TYPE_DIVX:
      case FILE_TYPE_M2P:
      case FILE_TYPE_M2TS:
      case FILE_TYPE_PS:
      case FILE_TYPE_TS:
      case FILE_TYPE_MXF:
         return RARCH_CONTENT_MOVIE;
      case FILE_TYPE_WMA:
      case FILE_TYPE_OGG:
      case FILE_TYPE_MP3:
      case FILE_TYPE_M4A:
      case FILE_TYPE_FLAC:
      case FILE_TYPE_WAV:
         return RARCH_CONTENT_MUSIC;
#endif
#ifdef HAVE_IMAGEVIEWER
      case FILE_TYPE_JPEG:
      case FILE_TYPE_PNG:
      case FILE_TYPE_TGA:
      case FILE_TYPE_BMP:
         return RARCH_CONTENT_IMAGE;
#endif
      case FILE_TYPE_NONE:
      default:
         break;
   }

   return RARCH_CONTENT_NONE;
}

void path_deinit_subsystem(void)
{
   if (subsystem_fullpaths)
      string_list_free(subsystem_fullpaths);
   subsystem_fullpaths = NULL;
}
