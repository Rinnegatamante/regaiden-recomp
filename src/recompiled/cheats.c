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

    // Verify gameplay state has initialized (Barry starting knife flag at $C3A4, or character HP initialized at $C3B9)
    bool game_active = ((ctx->wram[0x03A4] & 0x40) != 0) || (ctx->wram[0x03B9] != 0);
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
        // In battle, combat entities are stored in WRAM Bank 4 from $D000 to $DF00
        for (int slot = 0; slot < 16; ++slot) {
            uint16_t base = 0xD000 + (slot * 0x100);
            uint8_t flags = read_memory_byte_bank(ctx, 4, base + 0x00);
            // bit 0 = active, bit 3 = dead
            if ((flags & 0x09) == 0x01) {
                uint8_t hp = read_memory_byte_bank(ctx, 4, base + 0x21);
                if (hp > 1) {
                    write_memory_byte_bank(ctx, 4, base + 0x21, 1);
                }
            }
        }
    }

    // 4. Freeze Combat Reticle / Always Perfect Hit
    if (g_app_config.cheat_freeze_reticle) {
        // Result of reticle hit check: 2 = Perfect / Critical Hit
        write_memory_byte(ctx, 0xC186, 2);
        // Center the reticle oscillation pointer
        write_memory_byte(ctx, 0xC195, 0x00);
        write_memory_byte(ctx, 0xC196, 0x40);
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
