// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * bbgame.h -- ABI-compatible re-declaration of the BB_GAME_LIB host interface.
 *
 * Provenance (first-hand sources read 2026-10-12):
 *   [SRC-1] Yharnam-Hunters/bbport@8a2ed0ec9170:src/bbgame.h   (GPL-2.0-or-later)
 *   [SRC-2] Yharnam-Hunters/bbport@8a2ed0ec9170:FORK.md        ("game library plug-in point")
 *
 * This file is a rewrite for the M0-R2 preparation pack. Only the interface
 * (symbol names, field order, types and semantics needed for ABI compatibility)
 * is reproduced; upstream comments and implementation code are not copied.
 *
 * License note: the interface is derived from GPL-2.0-or-later source, so this
 * file is marked GPL-2.0-or-later. The repository's own licensing route is a
 * Human Owner decision (see REF-007 section 5 in this repository's docs).
 *
 * Semantics (same as upstream, do not change field order):
 *   - version   : BBGAME_API_VERSION the loader speaks. bbgame_init must refuse
 *                 anything != 1 (fail closed).
 *   - image     : host address of guest offset 0 (the relocated flat image).
 *   - image_size: size of that flat image in bytes (max p_vaddr + p_memsz).
 *   - install_hook(offset, size, target): replace the function entry at guest
 *                 offset (ELF p_vaddr) with an absolute jump to target.
 *                 Returns 0 on success, -1 if size < 14, target is NULL, or the
 *                 whole [offset, offset+size) range is not inside one executable
 *                 segment.
 *   - bbgame_init is called by the loader after mapping, relocating and patching
 *     the image and before any game code runs. Return 0 to continue; return
 *     non-zero to stop the loader (fail closed).
 */
#ifndef BBCOOP_BBGAME_H
#define BBCOOP_BBGAME_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BBGAME_API_VERSION 1u
#define BBGAME_INIT_SYMBOL "bbgame_init"

typedef struct BbGameHost {
    uint32_t version;          /* BBGAME_API_VERSION */
    unsigned char *image;      /* host address of guest offset 0 */
    uint64_t image_size;       /* size of the flat guest image in bytes */
    int (*install_hook)(uint64_t offset, uint64_t size, const void *target);
} BbGameHost;

typedef int (*BbGameInit)(const BbGameHost *host);

#if defined(__x86_64__) || defined(_M_X64)
#  if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Static_assert(offsetof(BbGameHost, image) == 8,      "ABI: image must be at offset 8");
_Static_assert(offsetof(BbGameHost, image_size) == 16, "ABI: image_size must be at offset 16");
_Static_assert(offsetof(BbGameHost, install_hook) == 24, "ABI: install_hook must be at offset 24");
_Static_assert(sizeof(BbGameHost) == 32, "ABI: BbGameHost must be 32 bytes on x86-64");
#  endif
#endif

#ifdef __cplusplus
}
#endif

#endif /* BBCOOP_BBGAME_H */
