// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * smoke_host.c -- local pre-flight harness for bbgame_probe.dll.
 *
 * This does NOT need Bloodborne, the port, or any game file. It loads the probe
 * DLL and fakes the BbGameHost exactly as the patched loader would, including a
 * minimal install_hook that writes the 14-byte absolute jump from SRC-2.
 *
 * Scenarios:
 *   A. skip mode: bbgame_init(version=1) -> 0, install_hook called, jumping to
 *      the hooked address logs hook.triggered and returns 0.
 *   B. replay14 mode: a synthetic 14-byte guest prologue is replayed from the
 *      trampoline and execution resumes at +14, returning 42; XMM0 is checked
 *      byte-for-byte across the hook to catch register clobbering.
 *   C. bbgame_init(version=2) -> non-zero, event=bbgame_init.fail.
 *
 * Build + run:  build.ps1 -SelfTest
 * Manual run:   smoke_host.exe <bbgame_probe.dll> <log path>
 *
 * Provenance: ABI from Yharnam-Hunters/bbport@8a2ed0ec9170:src/bbgame.h and the
 * hook contract from ...:src/probe.c (GPL-2.0-or-later).
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bbgame.h"

#define FAKE_IMAGE_SIZE (64ull * 1024ull)
#define FAKE_SKIP_OFFSET 0x100ull
#define FAKE_REPLAY_OFFSET 0x200ull

static unsigned char *g_image = NULL;
static unsigned long long g_image_size = FAKE_IMAGE_SIZE;
static int g_install_calls = 0;
static unsigned long long g_last_installed_offset = 0;

/* Minimal stand-in for the loader's install_hook (same contract as SRC-2). */
static int fake_install_hook(uint64_t offset, uint64_t size, const void *target) {
    unsigned char *p;
    uint64_t address;

    if (g_image == NULL || target == NULL || size < 14) return -1;
    if (offset > g_image_size || size > g_image_size - offset) return -1;
    p = g_image + offset;
    address = (uint64_t)(uintptr_t)target;
    p[0] = 0xff;
    p[1] = 0x25;
    memset(p + 2, 0, 4);
    memcpy(p + 6, &address, 8);
    g_install_calls++;
    g_last_installed_offset = offset;
    return 0;
}

static int file_contains(const char *path, const char *needle) {
    FILE *f;
    static char buffer[1 << 20];
    size_t n;

    f = fopen(path, "rb");
    if (f == NULL) return 0;
    n = fread(buffer, 1, sizeof(buffer) - 1, f);
    fclose(f);
    buffer[n] = '\0';
    return strstr(buffer, needle) != NULL;
}

static void expect_contains(const char *path, const char *needle, int *ok) {
    if (file_contains(path, needle)) {
        printf("PASS log contains: %s\n", needle);
    } else {
        printf("FAIL log missing : %s\n", needle);
        *ok = 0;
    }
}

static int set_env_pair(char *storage, size_t cap, const char *name, const char *value) {
    snprintf(storage, cap, "%s=%s", name, value);
    return _putenv(storage) == 0;
}

int main(int argc, char **argv) {
    const char *dll = (argc > 1) ? argv[1] : "bbgame_probe.dll";
    const char *log = (argc > 2) ? argv[2] : "smoke_probe.log";
    HMODULE module;
    BbGameInit init;
    BbGameHost host;
    int rc;
    int ok = 1;
    int calls_after_a;
    static char env_log[2048];
    static char env_write[64];
    static char env_hook[64];
    static char env_size[64];
    static char env_mode[64];
    static char env_ack[64];
    static char env_replay[64];

    /* Synthetic guest function at FAKE_REPLAY_OFFSET:
     *   B8 2A 00 00 00        mov eax, 42
     *   BA 44 33 22 11        mov edx, 0x11223344
     *   90 90 90 90           nop x4
     *   C3                    ret          <-- at +14, outside the 14-byte hook
     * First 14 bytes are whole instructions and position-independent, so
     * replay14 must resume at +14 and return 42 without touching the caller.
     */
    static const unsigned char guest_fn[15] = {
        0xB8, 0x2A, 0x00, 0x00, 0x00,
        0xBA, 0x44, 0x33, 0x22, 0x11,
        0x90, 0x90, 0x90, 0x90,
        0xC3
    };

    DeleteFileA(log);
    if (!set_env_pair(env_log, sizeof(env_log), "BB_PROBE_LOG", log) ||
        !set_env_pair(env_write, sizeof(env_write), "BB_PROBE_WRITE_TEST", "1")) {
        printf("WARN: _putenv failed; build.ps1 also sets the environment for the smoke run\n");
    }

    g_image = (unsigned char *)VirtualAlloc(NULL, (SIZE_T)g_image_size, MEM_COMMIT | MEM_RESERVE,
                                            PAGE_EXECUTE_READWRITE);
    if (g_image == NULL) {
        printf("FAIL: VirtualAlloc fake image, error=%lu\n", (unsigned long)GetLastError());
        return 1;
    }
    memset(g_image, 0, (size_t)g_image_size);
    memcpy(g_image, "\x7f" "ELF", 4);
    memcpy(g_image + FAKE_REPLAY_OFFSET, guest_fn, sizeof(guest_fn));

    module = LoadLibraryA(dll);
    if (module == NULL) {
        printf("FAIL: LoadLibraryA(%s), error=%lu (x64 DLL? same CRT as the loader?)\n",
               dll, (unsigned long)GetLastError());
        VirtualFree(g_image, 0, MEM_RELEASE);
        return 1;
    }
    init = (BbGameInit)(void *)GetProcAddress(module, BBGAME_INIT_SYMBOL);
    if (init == NULL) {
        printf("FAIL: GetProcAddress(%s) not found in %s\n", BBGAME_INIT_SYMBOL, dll);
        FreeLibrary(module);
        VirtualFree(g_image, 0, MEM_RELEASE);
        return 1;
    }

    /* ---------------- Scenario A: skip mode ------------------------------- */
    printf("--- Scenario A: BB_PROBE_HOOK_MODE=skip ---\n");
    if (!set_env_pair(env_hook, sizeof(env_hook), "BB_PROBE_HOOK_OFFSET", "0x100") ||
        !set_env_pair(env_size, sizeof(env_size), "BB_PROBE_HOOK_SIZE", "14") ||
        !set_env_pair(env_mode, sizeof(env_mode), "BB_PROBE_HOOK_MODE", "skip")) {
        printf("FAIL: could not set probe environment\n");
        ok = 0;
    }
    memset(&host, 0, sizeof(host));
    host.version = BBGAME_API_VERSION;
    host.image = g_image;
    host.image_size = g_image_size;
    host.install_hook = fake_install_hook;

    rc = init(&host);
    printf("%s bbgame_init(skip, version=1) = %d (expect 0)\n", rc == 0 ? "PASS" : "FAIL", rc);
    if (rc != 0) ok = 0;
    calls_after_a = g_install_calls;
    if (calls_after_a != 1 || g_last_installed_offset != FAKE_SKIP_OFFSET) {
        printf("FAIL install_hook: calls=%d offset=0x%llx (expect 1 / 0x100)\n",
               calls_after_a, (unsigned long long)g_last_installed_offset);
        ok = 0;
    } else {
        typedef int64_t (*HookedFn)(void);
        HookedFn fn = (HookedFn)(void *)(g_image + FAKE_SKIP_OFFSET);
        int64_t r = fn();
        printf("%s skipped hook returned %lld (expect 0)\n", r == 0 ? "PASS" : "FAIL", (long long)r);
        if (r != 0) ok = 0;
    }

    /* ---------------- Scenario B: replay14 mode --------------------------- */
    printf("--- Scenario B: BB_PROBE_HOOK_MODE=replay14 ---\n");
    if (!set_env_pair(env_hook, sizeof(env_hook), "BB_PROBE_HOOK_OFFSET", "0x200") ||
        !set_env_pair(env_size, sizeof(env_size), "BB_PROBE_HOOK_SIZE", "15") ||
        !set_env_pair(env_mode, sizeof(env_mode), "BB_PROBE_HOOK_MODE", "replay14") ||
        !set_env_pair(env_ack, sizeof(env_ack), "BB_PROBE_HOOK_ACK", "replay14") ||
        !set_env_pair(env_replay, sizeof(env_replay), "BB_PROBE_HOOK_REPLAY_LEN", "14")) {
        printf("FAIL: could not set replay environment\n");
        ok = 0;
    }
    rc = init(&host);
    printf("%s bbgame_init(replay14, version=1) = %d (expect 0)\n", rc == 0 ? "PASS" : "FAIL", rc);
    if (rc != 0) ok = 0;
    if (g_install_calls != 2 || g_last_installed_offset != FAKE_REPLAY_OFFSET) {
        printf("FAIL install_hook: calls=%d offset=0x%llx (expect 2 / 0x200)\n",
               g_install_calls, (unsigned long long)g_last_installed_offset);
        ok = 0;
    } else {
        typedef int64_t (*HookedFn)(void);
        HookedFn fn = (HookedFn)(void *)(g_image + FAKE_REPLAY_OFFSET);
        unsigned char sentinel[16], after[16];
        int64_t r;

        memset(sentinel, 0xAB, sizeof(sentinel));
        memset(after, 0, sizeof(after));
        __asm__ volatile("movdqu %0, %%xmm0" : : "m"(sentinel));
        r = fn();
        __asm__ volatile("movdqu %%xmm0, %0" : "=m"(after));
        printf("%s replayed guest function returned %lld (expect 42)\n", r == 42 ? "PASS" : "FAIL", (long long)r);
        if (r != 42) ok = 0;
        if (memcmp(sentinel, after, sizeof(sentinel)) != 0) {
            printf("FAIL replay stub clobbered XMM0\n");
            ok = 0;
        } else {
            printf("PASS replay stub preserved XMM0\n");
        }
    }

    /* ---------------- Scenario C: version mismatch ------------------------ */
    printf("--- Scenario C: wrong API version ---\n");
    host.version = 2;
    rc = init(&host);
    printf("%s bbgame_init(version=2) = %d (expect non-zero)\n", rc != 0 ? "PASS" : "FAIL", rc);
    if (rc == 0) ok = 0;

    expect_contains(log, "event=bbgame_init.enter", &ok);
    expect_contains(log, "event=host.version value=1", &ok);
    expect_contains(log, "event=mem.read", &ok);
    expect_contains(log, "event=mem.write_back", &ok);
    expect_contains(log, "event=mem.write_back offset=0x0 size=8 result=ok", &ok);
    expect_contains(log, "event=hook.install result=0", &ok);
    expect_contains(log, "event=hook.triggered count=1", &ok);
    expect_contains(log, "event=hook.trampoline", &ok);
    expect_contains(log, "event=hook.triggered count=2", &ok);
    expect_contains(log, "event=bbgame_init.exit result=0", &ok);
    expect_contains(log, "event=bbgame_init.fail", &ok);
    expect_contains(log, "reason=version_mismatch", &ok);

    FreeLibrary(module);
    VirtualFree(g_image, 0, MEM_RELEASE);
    printf("\n%s\n", ok ? "SMOKE PASS" : "SMOKE FAIL");
    return ok ? 0 : 1;
}