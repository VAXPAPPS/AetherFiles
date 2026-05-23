#pragma once
#include <glib-object.h>
#include "../domain/file_repository.h"

#define AETHER_TYPE_GIO_FILE_REPOSITORY (aether_gio_file_repository_get_type())
G_DECLARE_FINAL_TYPE(AetherGioFileRepository, aether_gio_file_repository, AETHER, GIO_FILE_REPOSITORY, GObject)

AetherGioFileRepository *aether_gio_file_repository_new(void);
