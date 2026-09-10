#include "cheats.h"
#include "config_ini.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

CustomCheat g_custom_cheats[MAX_CUSTOM_CHEATS];
int g_custom_cheat_count = 0;

static inline void write_memory_byte_bank(GBContext* ctx, uint8_t bank, uint16_t addr, uint8_t val) {
    if (!ctx || !ctx->wram) return;
    if (addr >= 0xC000 && addr <= 0xCFFF) {
        ctx->wram[addr - 0xC000] = val;
    } else if (addr >= 0xD000 && addr <= 0xDFFF) {
        uint8_t b = (bank == 0) ? ((ctx->wram_bank == 0) ? 1 : (ctx->wram_bank & 0x07)) : (bank & 0x07);
        ctx->wram[b * 0x1000 + (addr - 0xD000)] = val;
    } else if (addr >= 0xFF80 && addr <= 0xFFFE) {
        ctx->hram[addr - 0xFF80] = val;
    }
}

static inline uint8_t read_memory_byte_bank(GBContext* ctx, uint8_t bank, uint16_t addr) {
    if (!ctx || !ctx->wram) return 0;
    if (addr >= 0xC000 && addr <= 0xCFFF) {
        return ctx->wram[addr - 0xC000];
    } else if (addr >= 0xD000 && addr <= 0xDFFF) {
        uint8_t b = (bank == 0) ? ((ctx->wram_bank == 0) ? 1 : (ctx->wram_bank & 0x07)) : (bank & 0x07);
        return ctx->wram[b * 0x1000 + (addr - 0xD000)];
    } else if (addr >= 0xFF80 && addr <= 0xFFFE) {
        return ctx->hram[addr - 0xFF80];
    }
    return 0;
}

static inline void write_memory_byte(GBContext* ctx, uint16_t addr, uint8_t val) {
    write_memory_byte_bank(ctx, 0, addr, val);
}

static inline uint8_t read_memory_byte(GBContext* ctx, uint16_t addr) {
    return read_memory_byte_bank(ctx, 0, addr);
}

static void apply_gameshark_code(GBContext* ctx, const char* code_str) {
    // Format: 01XXYYZZ -> write byte XX to 0xZZYY (active bank / fixed WRAM)
    // Format: 9BXXYYZZ -> write byte XX to 0xZZYY in WRAM bank B
    if (!code_str) return;

    // Clean any whitespace, tabs, or dashes (e.g. "9230-91D4" or "910A C1C3")
    char clean[32];
    int len = 0;
    for (int i = 0; code_str[i] && len < 31; ++i) {
        char ch = code_str[i];
        if (ch != ' ' && ch != '-' && ch != '\t') {
            clean[len++] = ch;
        }
    }
    clean[len] = '\0';
    if (len != 8) return;

    uint8_t bank = 0;
    if (clean[0] == '0' && clean[1] == '1') {
        bank = 0;
    } else if (clean[0] == '9') {
        char bank_str[2] = { clean[1], '\0' };
        bank = (uint8_t)strtoul(bank_str, NULL, 16);
    } else {
        return; // Unsupported code type
    }

    char byte_str[3] = { clean[2], clean[3], '\0' };
    char addr_lo_str[3] = { clean[4], clean[5], '\0' };
    char addr_hi_str[3] = { clean[6], clean[7], '\0' };

    uint8_t val = (uint8_t)strtoul(byte_str, NULL, 16);
    uint8_t lo = (uint8_t)strtoul(addr_lo_str, NULL, 16);
    uint8_t hi = (uint8_t)strtoul(addr_hi_str, NULL, 16);

    uint16_t addr = (uint16_t)((hi << 8) | lo);
    write_memory_byte_bank(ctx, bank, addr, val);
}

void cheats_apply_frame(GBContext* ctx) {
    if (!ctx || !ctx->wram) return;

    // Verify gameplay state has initialized (Barry knife flag at $C3A4, or character HP initialized at $C3B9..$C3BB)
    bool game_active = ((ctx->wram[0x03A4] & 0x40) != 0) ||
                       (ctx->wram[0x03B9] != 0) ||
                       (ctx->wram[0x03BA] != 0) ||
                       (ctx->wram[0x03BB] != 0);
    if (!game_active) {
        // Still apply custom cheats if user explicitly added any
        for (int i = 0; i < g_custom_cheat_count; ++i) {
            if (g_custom_cheats[i].enabled) {
                apply_gameshark_code(ctx, g_custom_cheats[i].code);
            }
        }
        return;
    }

    // 1. Infinite Health for Barry, Leon, Lucia
    if (g_app_config.cheat_infinite_health) {
        // Character 0 (Barry) - Max HP: 100 ($64)
        write_memory_byte(ctx, 0xC3B9, 100);
        // Character 1 (Leon) - Max HP: 100 (default base 60, restore to full)
        write_memory_byte(ctx, 0xC3BA, 100);
        // Character 2 (Lucia) - Max HP: 120 ($78)
        write_memory_byte(ctx, 0xC3BB, 120);

        // Clear poison status flags & timers
        write_memory_byte(ctx, 0xC3BC, read_memory_byte(ctx, 0xC3BC) & ~0x08);
        write_memory_byte(ctx, 0xC3BD, read_memory_byte(ctx, 0xC3BD) & ~0x08);
        write_memory_byte(ctx, 0xC3BE, read_memory_byte(ctx, 0xC3BE) & ~0x08);
        write_memory_byte(ctx, 0xC3D0, 0);
        write_memory_byte(ctx, 0xC3D1, 0);
        write_memory_byte(ctx, 0xC3D2, 0);

        // Also update Bank 2 $D491 (used by inventory UI health display)
        write_memory_byte_bank(ctx, 2, 0xD491, 0x30);
    }

    // 2. Infinite Ammo for all weapons
    if (g_app_config.cheat_infinite_ammo) {
        write_memory_byte(ctx, 0xC3C1, 99); // Handgun ammo
        write_memory_byte(ctx, 0xC3C2, 99); // Shotgun ammo
        write_memory_byte(ctx, 0xC3C3, 99); // Grenade Launcher ammo
        write_memory_byte(ctx, 0xC3C4, 99); // Assault Rifle ammo
        write_memory_byte(ctx, 0xC3C5, 99); // Rocket Launcher ammo
    }

    // 3. One-Hit Kill in Battle
    if (g_app_config.cheat_one_hit_kill) {
        // Active battle enemies are referenced by entity page high-byte at $C228..$C22B.
        // Enemy HP is stored at entity offset 0x46 (bank_00d.asm:1971, 6873).
        for (int i = 0; i < 4; ++i) {
            uint8_t page = read_memory_byte(ctx, 0xC228 + i);
            if (page >= 0xD0 && page <= 0xDF) {
                uint16_t base = (uint16_t)(page << 8);
                // Clamp in both Bank 1 (battle engine) and Bank 4 (entity storage)
                uint8_t hp1 = read_memory_byte_bank(ctx, 1, base + 0x46);
                if (hp1 > 1) {
                    write_memory_byte_bank(ctx, 1, base + 0x46, 1);
                }
                uint8_t hp4 = read_memory_byte_bank(ctx, 4, base + 0x46);
                if (hp4 > 1) {
                    write_memory_byte_bank(ctx, 4, base + 0x46, 1);
                }
            }
        }

        // Scan all entity slots $D000..$DF00 in Bank 1 and Bank 4, excluding reticle/target entities
        uint8_t reticle_p = read_memory_byte(ctx, 0xC22C);
        uint8_t target_p = read_memory_byte(ctx, 0xC22D);
        uint8_t zone1_p = read_memory_byte(ctx, 0xC22E);
        uint8_t zone2_p = read_memory_byte(ctx, 0xC22F);
        for (int slot = 0; slot < 16; ++slot) {
            uint16_t base = 0xD000 + (slot * 0x100);
            uint8_t page = (uint8_t)(base >> 8);
            if (page == reticle_p || page == target_p || page == zone1_p || page == zone2_p) {
                continue;
            }
            uint8_t hp1 = read_memory_byte_bank(ctx, 1, base + 0x46);
            if (hp1 > 1) {
                write_memory_byte_bank(ctx, 1, base + 0x46, 1);
            }
            uint8_t hp4 = read_memory_byte_bank(ctx, 4, base + 0x46);
            if (hp4 > 1) {
                write_memory_byte_bank(ctx, 4, base + 0x46, 1);
            }
        }
    }

    // 4. Freeze Combat Reticle / Always Perfect Hit
    if (g_app_config.cheat_freeze_reticle) {
        bool in_battle = (ctx->rom_bank == 0x0D || ctx->rom_bank == 0x0E);

        if (in_battle) {
            uint8_t reticle_p = read_memory_byte(ctx, 0xC22C);
            // Target center X position (between sweep bounds 0x50 and 0x94)
            uint8_t target_p = read_memory_byte(ctx, 0xC22D);
            uint8_t target_x = 0x72; // Midpoint between 0x50 and 0x94 (114 decimal)
            if (target_p >= 0xD0 && target_p <= 0xDF) {
                uint16_t tbase = (uint16_t)(target_p << 8);
                uint8_t tx = read_memory_byte_bank(ctx, 1, tbase + 0x8F);
                if (tx >= 0x50 && tx <= 0x94) {
                    target_x = tx;
                } else {
                    tx = read_memory_byte_bank(ctx, 1, tbase + 0x32);
                    if (tx >= 0x50 && tx <= 0x94) {
                        target_x = tx;
                    }
                }
            }

            // Lock mirrored screen position and entity positions
            write_memory_byte(ctx, 0xC2E9, target_x);
            if (reticle_p >= 0xD0 && reticle_p <= 0xDF) {
                uint16_t rbase = (uint16_t)(reticle_p << 8);
                write_memory_byte_bank(ctx, 1, rbase + 0x8F, target_x);
                // 12.4 fixed-point coordinates in offset 0x32..0x33
                write_memory_byte_bank(ctx, 1, rbase + 0x32, (uint8_t)((target_x & 0x0F) << 4));
                write_memory_byte_bank(ctx, 1, rbase + 0x33, (uint8_t)((target_x >> 4) & 0x0F));

                // Screen sprite coordinates (Call_00d_400b mapping: x + 0xB0)
                uint8_t a = (uint8_t)(target_x + 0xB0);
                uint8_t e = (uint8_t)((a & 0x0F) << 4);
                uint8_t d = (uint8_t)((a & 0xF0) >> 4);
                write_memory_byte_bank(ctx, 1, rbase + 0x3A, e);
                write_memory_byte_bank(ctx, 1, rbase + 0x3B, d);

                uint8_t cbcd = read_memory_byte(ctx, 0xCBCD);
                if (cbcd >= 0xD0 && cbcd <= 0xDF) {
                    write_memory_byte_bank(ctx, 1, (cbcd << 8) + 0x3A, e);
                    write_memory_byte_bank(ctx, 1, (cbcd << 8) + 0x3B, d);
                }
                uint8_t cbce = read_memory_byte(ctx, 0xCBCE);
                if (cbce >= 0xD0 && cbce <= 0xDF) {
                    write_memory_byte_bank(ctx, 1, (cbce << 8) + 0x3A, e);
                    write_memory_byte_bank(ctx, 1, (cbce << 8) + 0x3B, d);
                }
            }

            // Lock combat hit result:
            // $CBD3: hit check outcome (0x00 = perfect/critical hit, 0xFF = miss)
            write_memory_byte(ctx, 0xCBD3, 0x00);
            // $CBD4: hit success indicator (1 = hit, 3 = miss)
            write_memory_byte(ctx, 0xCBD4, 0x01);
            write_memory_byte(ctx, 0xCBD5, 0x00);
            // Set bit 3 in $CBCB (Critical hit flag used by battle engine)
            write_memory_byte(ctx, 0xCBCB, read_memory_byte(ctx, 0xCBCB) | 0x08);

            // Clear 9-byte hit map at $CBE0..$CBE8 so any sample evaluates as a hit
            for (int k = 0; k < 9; ++k) {
                write_memory_byte(ctx, 0xCBE0 + k, 0x00);
            }
        }

        // Result of reticle hit check indicator: 2 = Perfect / Critical Hit
        write_memory_byte(ctx, 0xC186, 2);
    }

    // 5. Unlock All Weapons
    if (g_app_config.cheat_all_weapons) {
        // Weapon ownership bitmask array at $C399 ($C3A4..$C3A6)
        write_memory_byte(ctx, 0xC3A4, read_memory_byte(ctx, 0xC3A4) | 0x40); // Knife ($2F)
        write_memory_byte(ctx, 0xC3A5, read_memory_byte(ctx, 0xC3A5) | 0x55); // Handgun ($30), Ammo ($31), Shotgun ($32), Grenade Launcher ($33)
        write_memory_byte(ctx, 0xC3A6, read_memory_byte(ctx, 0xC3A6) | 0x15); // Assault Rifle ($34), Rocket Launcher ($35), Special ($36)

        // Weapon inventory slots ($C3BF..$C3C5)
        write_memory_byte(ctx, 0xC3BF, 0xFF); // Knife (infinite)
        write_memory_byte(ctx, 0xC3C0, 0xFF); // Handgun (infinite/owned)
        if (read_memory_byte(ctx, 0xC3C1) == 0) write_memory_byte(ctx, 0xC3C1, 30); // Handgun ammo
        if (read_memory_byte(ctx, 0xC3C2) == 0) write_memory_byte(ctx, 0xC3C2, 10); // Shotgun ammo
        if (read_memory_byte(ctx, 0xC3C3) == 0) write_memory_byte(ctx, 0xC3C3, 6);  // Grenade Launcher ammo
        if (read_memory_byte(ctx, 0xC3C4) == 0) write_memory_byte(ctx, 0xC3C4, 30); // Assault Rifle ammo
        if (read_memory_byte(ctx, 0xC3C5) == 0) write_memory_byte(ctx, 0xC3C5, 4);  // Rocket Launcher ammo
    }

    // 6. Infinite First Aid Sprays & Items
    if (g_app_config.cheat_infinite_items) {
        // Item ownership bitmask array at $C399 ($C3A7..$C3A8)
        write_memory_byte(ctx, 0xC3A7, read_memory_byte(ctx, 0xC3A7) | 0x40); // Green Herb ($3B)
        write_memory_byte(ctx, 0xC3A8, read_memory_byte(ctx, 0xC3A8) | 0x55); // Red Herb ($3C), First Aid Spray ($3D), Body Armor ($3E), Heavy Armor ($3F)

        // Item quantities
        write_memory_byte(ctx, 0xC3CB, 9); // Green Herbs
        write_memory_byte(ctx, 0xC3CC, 9); // Red Herbs
        write_memory_byte(ctx, 0xC3CD, 9); // First Aid Sprays
        write_memory_byte(ctx, 0xC3CE, 9); // Body Armor
        write_memory_byte(ctx, 0xC3CF, 9); // Heavy Armor
    }

    // 7. Custom User GameShark Codes
    for (int i = 0; i < g_custom_cheat_count; ++i) {
        if (g_custom_cheats[i].enabled) {
            apply_gameshark_code(ctx, g_custom_cheats[i].code);
        }
    }
}

bool cheats_add_gameshark_code(const char* name, const char* code_str, bool enabled) {
    if (!code_str || g_custom_cheat_count >= MAX_CUSTOM_CHEATS) return false;
    CustomCheat* c = &g_custom_cheats[g_custom_cheat_count++];
    snprintf(c->name, sizeof(c->name), "%s", (name && name[0]) ? name : "Custom Cheat");
    snprintf(c->code, sizeof(c->code), "%s", code_str);
    c->enabled = enabled;
    return true;
}

void cheats_remove_custom(int index) {
    if (index < 0 || index >= g_custom_cheat_count) return;
    for (int i = index; i < g_custom_cheat_count - 1; ++i) {
        g_custom_cheats[i] = g_custom_cheats[i + 1];
    }
    g_custom_cheat_count--;
}
