#include "common/uf2_info.h"
#include "common/crc.h"

uint32_t fwog_uf2_info_crc(const fwog_uf2_info_t *info) {
    return fwog_crc32(info, offsetof(fwog_uf2_info_t, crc32));
}

/* True when s[0..n-1] contains a NUL, so s is safe to pass to a %s. Same
 * contract as app_meta.h's fwog_str_bounded(); kept local because this file
 * must not depend on app_meta.h, which describes a different record. */
static bool bounded(const char *s, size_t n) {
    for (size_t i = 0; i < n; i++)
        if (s[i] == '\0') return true;
    return false;
}

bool fwog_uf2_info_valid(const fwog_uf2_info_t *info) {
    /* Magic first: a record whose CRC is self-consistent but whose magic is
       wrong belongs to something else, and reading its fields would be a
       misinterpretation rather than a corruption. */
    if (info->magic0 != FWOG_UF2_INFO_MAGIC0) return false;
    if (info->magic1 != FWOG_UF2_INFO_MAGIC1) return false;
    /* An unrecognised version is rejected, never parsed optimistically: the
       fields may mean something else in a layout we do not know. */
    if (info->struct_version != FWOG_UF2_INFO_VERSION) return false;
    if (info->crc32 != fwog_uf2_info_crc(info)) return false;
    if (!bounded(info->name, sizeof info->name)) return false;
    if (!bounded(info->description, sizeof info->description)) return false;
    if (!bounded(info->build, sizeof info->build)) return false;
    return true;
}
