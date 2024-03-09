#include <config.h>

#include <glib.h>

#include "application.h"
#include "gsm_lingmosu.h"
#include "util.h"

gboolean (*lingmosu_exec) (const char *commandline);


static void
load_lingmosu (void)
{
  static gboolean init;

  if (init)
    return;

  init = TRUE;

  load_symbols ("liblingmosu.so.0",
                "lingmosu_exec", &lingmosu_exec,
                NULL);
}


gboolean
gsm_lingmosu_create_root_password_dialog (const char *command)
{
  return lingmosu_exec (command);
}


gboolean
procman_has_lingmosu (void)
{
  load_lingmosu ();
  return lingmosu_exec != NULL;
}
