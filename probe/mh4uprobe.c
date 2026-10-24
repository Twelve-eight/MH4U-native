/* mh4uprobe: milestone M0 of DEVELOP.md (section 9). Read-only.
 *
 * Forge PC 0.7 plugin for the recompiled MHGU (1.4.0) PC build. For every upstream hook site
 * (probe/sites.h, generated from MH4U-port-to-MHGU module/build.py + gear_hooks.txt):
 *   1. read the 1.4.0 word at main + off and compare it with sites.h (a wrong base or a
 *      different game build shows up as mismatches);
 *   2. sort the site into the buckets of DEVELOP.md 3.5:
 *        B1  the site itself is a recompiled block start (hook directly);
 *        B2  an earlier block start B exists within 0x400 bytes and B..site holds no branch and
 *            no pc-relative access (hook B, replay the prefix);
 *        B3  neither (re-export with a coverage root, or override the function).
 * Nothing is written to game memory and no hook is created. Output: forge.log and
 * <mods>/0100770008DD8000/MH4U/mh4u_sites.txt.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#define _CRT_SECURE_NO_WARNINGS
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>

#include "forge_pc.h"
#include "sites.h"

#ifdef _WIN32
#include <direct.h>
#define probe_mkdir(p) _mkdir(p)
#else
#include <sys/stat.h>
#define probe_mkdir(p) mkdir(p, 0777)
#endif

FORGE_PLUGIN_DEFINE_API

#define PLUGIN "mh4uprobe"
#define WALK_LIMIT 0x400u
#define LINE_MAX 1024

static FILE* s_report;

static void out(ForgeLogLevel level, const char* fmt, ...)
{
    char buf[LINE_MAX];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    forge_api->log(level, PLUGIN, buf);
    if (s_report) {
        fputs(buf, s_report);
        fputc('\n', s_report);
    }
}

static bool open_report(void)
{
    const char* mods = forge_api->mods_dir();
    char dir[1024];
    char path[1024];

    if (!mods || !mods[0]) {
        return false;
    }
    snprintf(dir, sizeof(dir), "%s/MH4U", mods);
    probe_mkdir(dir); /* exists with an installed plugin; EEXIST is fine */
    snprintf(path, sizeof(path), "%s/mh4u_sites.txt", dir);
    s_report = fopen(path, "w");
    return s_report != NULL;
}

/* Why an ARM word makes a replayed prefix address-dependent, or NULL. Same classes as
 * MHGU/forge/itembox-v7 pc_hazard, condensed: any branch, any pc operand, any pc write. */
static const char* hazard(uint32_t w)
{
    const uint32_t cls = (w >> 25) & 0x7u;
    const uint32_t rn = (w >> 16) & 0xFu;
    const uint32_t rd = (w >> 12) & 0xFu;
    const uint32_t rm = w & 0xFu;

    if ((w >> 28) == 0xFu) {
        return "unconditional space";
    }
    switch (cls) {
    case 0x5u:
        return "b/bl";
    case 0x4u:
        return (rn == 0xFu || (w & 0x8000u)) ? "ldm/stm pc" : NULL;
    case 0x2u:
    case 0x3u:
        if (cls == 0x3u && (w & 0x10u)) {
            return NULL; /* media */
        }
        if (rn == 0xFu) {
            return "pc-relative load/store";
        }
        if (rd == 0xFu) {
            return "load into pc";
        }
        return (cls == 0x3u && rm == 0xFu) ? "pc offset" : NULL;
    case 0x0u:
    case 0x1u:
        if ((w & 0x0FFFFFF0u) == 0x012FFF10u || (w & 0x0FFFFFF0u) == 0x012FFF30u) {
            return "bx/blx register";
        }
        if (rd == 0xFu || rn == 0xFu || (cls == 0x0u && rm == 0xFu)) {
            return "pc operand";
        }
        return NULL;
    default:
        return (rn == 0xFu) ? "coprocessor pc" : NULL; /* 110 / 111 */
    }
}

static void probe(void)
{
    const uint32_t base = forge_api->mem_getModuleBase("main");
    const uint32_t size = forge_api->mem_getModuleSize("main");
    unsigned matched = 0, b1 = 0, b2 = 0, b3 = 0;
    size_t i;

    if (!forge_api->is_block) {
        out(FORGE_LOG_ERROR, "loader has no is_block (Forge PC 0.7 required)");
        return;
    }
    out(FORGE_LOG_INFO, "[1] main base %#010x, size %#x; upstream assumed 0x704000", base, size);
    if (base == 0 || size == 0) {
        out(FORGE_LOG_ERROR, "[1] FAIL: module \"main\" not found");
        return;
    }

    for (i = 0; i < MH4U_SITE_COUNT; i++) {
        const Mh4uSite* s = &kMh4uSites[i];
        const uint32_t abs = base + s->off;
        uint32_t word = 0;
        const bool readable = s->off + 4u <= size && forge_api->mem_read(abs, &word, 4);
        const bool match = readable && word == s->word;
        const char* why = NULL;
        uint32_t blk = 0;
        uint32_t d;

        matched += match ? 1u : 0u;
        if (forge_api->is_block(abs)) {
            b1++;
            out(FORGE_LOG_INFO, "[2] %-24s main+%#08x word %08X %s B1", s->name, s->off, word,
                match ? "ok" : "MISMATCH");
            continue;
        }
        for (d = 4; d <= WALK_LIMIT && abs >= base + d; d += 4) {
            if (forge_api->is_block(abs - d)) {
                blk = abs - d;
                break;
            }
        }
        if (blk != 0) {
            uint32_t a;
            for (a = blk; a < abs && why == NULL; a += 4) {
                uint32_t w = 0;
                why = forge_api->mem_read(a, &w, 4) ? hazard(w) : "unreadable";
            }
        }
        if (blk != 0 && why == NULL) {
            b2++;
            out(FORGE_LOG_INFO, "[2] %-24s main+%#08x word %08X %s B2 (B main+%#x, %u words)",
                s->name, s->off, word, match ? "ok" : "MISMATCH", blk - base, (abs - blk) / 4u);
        } else {
            b3++;
            out(FORGE_LOG_WARN, "[2] %-24s main+%#08x word %08X %s B3 (%s)", s->name, s->off, word,
                match ? "ok" : "MISMATCH",
                blk == 0 ? "no block start within 0x400 bytes" : why);
        }
    }
    out(FORGE_LOG_INFO, "[3] summary: words matched %u/%u; B1 %u, B2 %u, B3 %u", matched,
        (unsigned)MH4U_SITE_COUNT, b1, b2, b3);
}

FORGE_EXPORT void forge_onLoad(ForgePluginInfo* info)
{
    info->required_ver.major = FORGE_PC_VERSION_MAJOR;
    info->required_ver.minor = FORGE_PC_VERSION_MINOR;
    info->required_ver.patch = FORGE_PC_VERSION_PATCH;
    snprintf(info->name, sizeof(info->name), "MH4U probe (read-only, M0)");
}

FORGE_EXPORT void forge_onInit(void)
{
    if (!open_report()) {
        forge_api->log(FORGE_LOG_WARN, PLUGIN, "cannot write MH4U/mh4u_sites.txt, logging only");
    }
    out(FORGE_LOG_INFO, "# mh4uprobe: %u upstream sites, read-only", (unsigned)MH4U_SITE_COUNT);
    probe();
    if (s_report) {
        fclose(s_report);
        s_report = NULL;
    }
}

FORGE_EXPORT void forge_onUnload(void)
{
    /* nothing to restore: the plugin never wrote game memory */
}
