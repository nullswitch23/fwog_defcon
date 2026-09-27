/* NEC 32-bit wire words for ir_comm_send() at 38 kHz (LSB-first packing).
 *
 * Public references: Arduino-IRremote NEC TV examples; IRremoteESP8266 NEC
 * tables. Power words for the first eight brands match OpticClick 001 k_lib.
 * Zero means unknown — blast skips it. Not a LIRC dump.
 */
#include "oc_codes.h"

const oc_brand_t k_oc_brands[] = {
    { "Samsung",  { 0xE0E040BFu, 0xE0E0F00Fu, 0xE0E048BFu, 0xE0E008F7u, 0xE0E0E01Fu, 0xE0E0D02Fu } },
    { "LG",       { 0x20DF10EFu, 0x20DF906Fu, 0x20DF00FFu, 0x20DF807Fu, 0x20DF40EFu, 0x20DFC03Fu } },
    { "Vizio",    { 0x04FB08F7u, 0x04FB906Fu, 0x04FB48BFu, 0x04FB00FFu, 0x04FBC03Fu, 0x04FB40BFu } },
    { "Hisense",  { 0x00FF02FDu, 0x00FFF00Fu, 0x00FF48BDu, 0x00FF08F7u, 0x00FF906Fu, 0x00FFD02Fu } },
    { "TCL",      { 0x00FF30CFu, 0u, 0u, 0u, 0u, 0u } },
    { "Insignia", { 0x04FB08F7u, 0x04FB906Fu, 0x04FB48BFu, 0x04FB00FFu, 0x04FBC03Fu, 0x04FB40BFu } },
    { "Roku",     { 0x5743C03Fu, 0u, 0u, 0u, 0u, 0u } },
    { "SharpNEC", { 0x7F8000FFu, 0u, 0u, 0u, 0u, 0u } },
    { "Toshiba",  { 0x02FD48B7u, 0x02FD08F7u, 0x02FD58A7u, 0x02FD7887u, 0x02FD906Fu, 0x02FDD02Fu } },
};

const unsigned k_oc_nbrands = (unsigned)(sizeof k_oc_brands / sizeof k_oc_brands[0]);

const char *oc_fn_name(oc_fn_t fn) {
    static const char *const k_names[OC_FN_COUNT] = {
        "POWER", "MUTE", "CH+", "CH-", "VOL+", "VOL-",
    };
    if ((unsigned)fn >= OC_FN_COUNT) return "?";
    return k_names[fn];
}
