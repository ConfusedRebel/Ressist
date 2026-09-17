#ifndef WINDOWCMD_PARSER_H
#define WINDOWCMD_PARSER_H

#include "model.h"

typedef enum {
    WINDOWCMD_ERROR_INVALID_ARGUMENT,
} WindowcmdError;

#define WINDOWCMD_ERROR (windowcmd_error_quark())

GQuark windowcmd_error_quark(void);
gboolean parse_window_command(const gchar *text,
                             const WindowSnapshot *target,
                             ParsedAction *out,
                             GError **error);

#endif
