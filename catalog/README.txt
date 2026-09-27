This folder is the app's local catalog.

It ships empty on purpose. FreeWili OG App Explorer v2 fetches the published
catalog at https://docs.freewili.com/og-apps/apps.json when it starts, so the
apps are already listed without anything being bundled here. (The fetched list
is cached in apps-cache.json, so later launches work offline; the Settings tab
can point the app at a different catalog, or clear it to stop fetching.)

Drop any FwOGapp <name>_main.uf2 into this folder and the App Explorer tab lists
it alongside the remote entries, reading its name, version and description out
of the image itself. An optional catalog.json beside them can override what is
shown. In-tree builds copy each *_main.uf2 here automatically after a
successful link (FWOG_CATALOG_DIR). Display application UF2s stay out: those
are not flashable on their own.

Flashing an OG app needs the OG display bootloader on the board first --
OG Bootloader Installer tab, "Install FreeWili OG Bootloader". One time per
board.
