#ifndef _GSM_GSM_LingmoSU_H_
#define _GSM_GSM_LingmoSU_H_

#include <glib.h>

gboolean
gsm_lingmosu_create_root_password_dialog (const char *message);

gboolean
procman_has_lingmosu (void) G_GNUC_CONST;

#endif /* _GSM_GSM_LingmoSU_H_ */
