#ifndef GB_FS_H
#define GB_FS_H

/* FatFs walk + hex dump/restore on main USB CDC. Used by GlassBak and
 * KitHome's GlassBak tile. */

void gb_fs_dump(void);
void gb_fs_poll_restore(void);

#endif
