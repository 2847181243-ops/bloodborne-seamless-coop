// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * bbgame_probe.c -- M0-R2 minimal BB_GAME_LIB probe DLL (Windows x64).
 *
 * What it proves:
 *   1. the patched loader can load a foreign DLL after relocation/patches and
 *      before any game code runs;
 *   2. bbgame_init receives a sane BbGameHost (version/image/image_size);
 *   3. the image can be read, and (when the page is still writable) written back
 *      byte-identically, without changing game content;
 *   4. optionally, install_hook() accepts a guest offset and a later jump to the
 *      hook target is observable in the log.
 *
 * First-hand provenance (read 2026-10-12):
 *   [SRC-1] Yharnam-Hunters/bbport@8a2ed0ec9170:src/bbgame.h
 *   [SRC-2] Yharnam-Hunters/bbport@8a2ed0ec9170:src/probe.c    (load order,
 *           install_hook contract: FF 25 + 8-byte absolute target, >=14 bytes,
 *           executable segment check, -1 on refusal)
 *   [SRC-3] BetterCodeGoblin/bloodborne_pc_windows_port@29e59f5818bd:src/probe.c
 *           (the target loader this DLL is written for)
 *   [SRC-4] BetterCodeGoblin/bloodborne_pc_windows_port@29e59f5818bd:scripts/game_check.py
 *           (pinned executable)
 *
 * HONEST LIMITS -- read before using:
 *   - BB_PROBE_HOOK_MODE=skip (default) replaces the hooked function with a stub
 *     that logs and returns 0. That is a REAL BEHAVIOUR CHANGE for the hooked
 *     function. There is no trampoline in this mode.
 *   - BB_PROBE_HOOK_MODE=replay14 is opt-in and requires BB_PROBE_HOOK_ACK=replay14.
 *     It copies the overwritten instruction prefix (14 or 15 bytes) into an
 *     executable trampoline and resumes at image+offset+BB_PROBE_HOOK_REPLAY_LEN.
 *     It preserves GPRs and XMM0-15, but NOT the upper 128 bits of YMM and it
 *     cannot repair RIP-relative operands or branches that point inside the
 *     overwritten window. The operator must verify the prefix offline
 *     (Zydis from setup-windows.ps1, or objdump) before enabling this mode.
 *   - This DLL does NOT verify the pinned CUSA03173 v1.09 image SHA-256: by the
 *     time bbgame_init runs, relocations and patches have already rewritten the
 *     image, so the pinned hash cannot be recomputed here. The loader must fail
 *     closed instead (see LOADER-PATCH.md).
 *
 * Log format: one structured key=value line per event, LF-only, no BOM:
 *   ts=<UTC ISO-8601> seq=<n> pid=<n> tid=<n> event=<name> key=value ...
 *
 * Environment variables:
 *   BB_PROBE_LOG              log path (default %TEMP%\bbcoop-r2-probe.log)
 *   BB_PROBE_STDERR=1         mirror log lines to stderr
 *   BB_PROBE_WRITE_TEST=0     disable the byte-identical write-back test
 *   BB_PROBE_WRITE_OFFSET=<n> write-back offset (default 0)
 *   BB_PROBE_HOOK_OFFSET=<n>  guest offset to hook (unset = no hook)
 *   BB_PROBE_HOOK_SIZE=<n>    function size passed to install_hook (default 14)
 *   BB_PROBE_HOOK_MODE=skip|replay14 (default skip)
 *   BB_PROBE_HOOK_REPLAY_LEN=14|15 (only for replay14; default 14)
 *   BB_PROBE_HOOK_ACK=replay14 (mandatory ack for replay14)
 *
 * Return values from bbgame_init: 0 = success; non-zero = fail closed, the
 * loader must stop. Every failure writes event=bbgame_init.fail reason=<...>.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>

#include "bbgame.h"

#define PROBE_MAX_LOG_LINE 1024
#define PROBE_MAX_IMAGE    (512ull * 1024ull * 1024ull)
#define PROBE_HOOK_MIN_SIZE 14ull
#define PROBE_DEFAULT_LOG_FILE "bbcoop-r2-probe.log"

/* ---------------------------------------------------------------- state ---- */

static CRITICAL_SECTION g_log_lock;
static int g_lock_ready = 0;
static FILE *g_log = NULL;
static char g_log_path[MAX_PATH * 4];
static unsigned long long g_seq = 0;
static int g_stderr_mirror = 0;

static volatile LONG g_hook_hits = 0;
static int g_hook_installed = 0;
static const char *g_hook_mode = "none";
static void *bbgame_probe_trampoline = NULL; /* read by the replay stub */

/* ---------------------------------------------------------------- logging -- */

static void probe_utc(char *out, size_t cap) {
    FILETIME ft;
    ULARGE_INTEGER u;
    SYSTEMTIME st;
    unsigned ms;

    GetSystemTime(&st);
    GetSystemTimeAsFileTime(&ft);
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    ms = (unsigned)((u.QuadPart / 10000ull) % 1000ull);
    snprintf(out, cap, "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
             (unsigned)st.wYear, (unsigned)st.wMonth, (unsigned)st.wDay,
             (unsigned)st.wHour, (unsigned)st.wMinute, (unsigned)st.wSecond, ms);
}

static void probe_log(const char *event, const char *fmt, ...) {
    char line[PROBE_MAX_LOG_LINE];
    char ts[40];
    size_t len;
    int off;

    if (event == NULL) event = "?";
    probe_utc(ts, sizeof(ts));

    if (g_lock_ready) EnterCriticalSection(&g_log_lock);
    off = snprintf(line, sizeof(line),
                   "ts=%s seq=%llu pid=%lu tid=%lu event=%s",
                   ts, ++g_seq, (unsigned long)GetCurrentProcessId(),
                   (unsigned long)GetCurrentThreadId(), event);
    if (off < 0) off = 0;
    if ((size_t)off < sizeof(line) && fmt != NULL && fmt[0] != '\0') {
        va_list ap;
        line[off++] = ' '; /* keep event=<name> and key=value separated */
        va_start(ap, fmt);
        vsnprintf(line + (size_t)off, sizeof(line) - (size_t)off, fmt, ap);
        va_end(ap);
    }
    len = strlen(line);
    if (len + 1 < sizeof(line)) {
        line[len++] = '\n';
        line[len] = '\0';
    }
    if (g_log != NULL) {
        fwrite(line, 1, len, g_log);
        fflush(g_log);
    }
    if (g_stderr_mirror) {
        fwrite(line, 1, len, stderr);
        fflush(stderr);
    }
    if (g_lock_ready) LeaveCriticalSection(&g_log_lock);
}

static int env_is_true(const char *name, int fallback) {
    const char *v = getenv(name);
    if (v == NULL || v[0] == '\0') return fallback;
    if (strcmp(v, "0") == 0 || strcmp(v, "false") == 0 || strcmp(v, "no") == 0 || strcmp(v, "off") == 0) return 0;
    if (strcmp(v, "1") == 0 || strcmp(v, "true") == 0 || strcmp(v, "yes") == 0 || strcmp(v, "on") == 0) return 1;
    return fallback;
}

static int parse_u64(const char *text, unsigned long long *out) {
    char *end = NULL;
    unsigned long long v;
    if (text == NULL || text[0] == '\0') return 0;
    errno = 0;
    v = _strtoui64(text, &end, 0); /* accepts 0x... and decimal */
    if (errno != 0 || end == text || (end != NULL && *end != '\0')) return 0;
    *out = v;
    return 1;
}

static int protect_is_writable(DWORD p) {
    return p == PAGE_READWRITE || p == PAGE_WRITECOPY ||
           p == PAGE_EXECUTE_READWRITE || p == PAGE_EXECUTE_WRITECOPY;
}

static void hex16(const unsigned char *data, size_t n, char *out, size_t cap) {
    static const char digits[] = "0123456789abcdef";
    size_t i;
    if (cap < n * 2 + 1) n = (cap - 1) / 2;
    for (i = 0; i < n; ++i) {
        out[i * 2] = digits[data[i] >> 4];
        out[i * 2 + 1] = digits[data[i] & 0x0f];
    }
    out[n * 2] = '\0';
}

static int probe_open_log(void) {
    const char *env = getenv("BB_PROBE_LOG");
    char tmp[MAX_PATH + 1];

    if (env != NULL && env[0] != '\0') {
        snprintf(g_log_path, sizeof(g_log_path), "%s", env);
    } else {
        DWORD n = GetTempPathA((DWORD)sizeof(tmp), tmp);
        if (n == 0 || n >= sizeof(tmp)) snprintf(tmp, sizeof(tmp), ".");
        snprintf(g_log_path, sizeof(g_log_path), "%s%s", tmp, PROBE_DEFAULT_LOG_FILE);
    }
    g_log = fopen(g_log_path, "ab"); /* append, binary: LF only, no BOM */
    if (g_log == NULL) return 0;
    g_stderr_mirror = env_is_true("BB_PROBE_STDERR", 0);
    return 1;
}

/* ------------------------------------------------------------- hook stubs -- */

/*
 * Called from both stubs through the ordinary Windows x64 ABI. Kept external
 * (not static) so the top-level assembly trampoline can call it by name.
 */
void bbgame_probe_record(void) {
    LONG hits = InterlockedIncrement(&g_hook_hits);
    probe_log("hook.triggered", "count=%ld mode=%s", (long)hits, g_hook_mode);
}

/*
 * skip mode: entered by an absolute jmp from the game function entry, returns
 * to the original caller with RAX=0. The original function body never runs.
 */
static int64_t probe_skip_stub(void) {
    bbgame_probe_record();
    return 0;
}

/*
 * replay14 mode: an assembly stub that saves the guest argument registers,
 * logs, restores them and jumps to the executable trampoline. XMM/GPR saves
 * keep the original prologue replayable; see HONEST LIMITS above.
 */
void bbgame_probe_replay_stub(void);

__asm__(
    ".text\n"
    ".globl bbgame_probe_replay_stub\n"
    "bbgame_probe_replay_stub:\n"
    "    pushfq\n"
    "    push %rax\n"
    "    push %rbx\n"
    "    push %rcx\n"
    "    push %rdx\n"
    "    push %rsi\n"
    "    push %rdi\n"
    "    push %rbp\n"
    "    push %r8\n"
    "    push %r9\n"
    "    push %r10\n"
    "    push %r11\n"
    "    push %r12\n"
    "    push %r13\n"
    "    push %r14\n"
    "    push %r15\n"
    "    sub $0x128, %rsp\n"
    "    movdqu %xmm0, 0x20(%rsp)\n"
    "    movdqu %xmm1, 0x30(%rsp)\n"
    "    movdqu %xmm2, 0x40(%rsp)\n"
    "    movdqu %xmm3, 0x50(%rsp)\n"
    "    movdqu %xmm4, 0x60(%rsp)\n"
    "    movdqu %xmm5, 0x70(%rsp)\n"
    "    movdqu %xmm6, 0x80(%rsp)\n"
    "    movdqu %xmm7, 0x90(%rsp)\n"
    "    movdqu %xmm8, 0xa0(%rsp)\n"
    "    movdqu %xmm9, 0xb0(%rsp)\n"
    "    movdqu %xmm10, 0xc0(%rsp)\n"
    "    movdqu %xmm11, 0xd0(%rsp)\n"
    "    movdqu %xmm12, 0xe0(%rsp)\n"
    "    movdqu %xmm13, 0xf0(%rsp)\n"
    "    movdqu %xmm14, 0x100(%rsp)\n"
    "    movdqu %xmm15, 0x110(%rsp)\n"
    "    call bbgame_probe_record\n"
    "    movdqu 0x20(%rsp), %xmm0\n"
    "    movdqu 0x30(%rsp), %xmm1\n"
    "    movdqu 0x40(%rsp), %xmm2\n"
    "    movdqu 0x50(%rsp), %xmm3\n"
    "    movdqu 0x60(%rsp), %xmm4\n"
    "    movdqu 0x70(%rsp), %xmm5\n"
    "    movdqu 0x80(%rsp), %xmm6\n"
    "    movdqu 0x90(%rsp), %xmm7\n"
    "    movdqu 0xa0(%rsp), %xmm8\n"
    "    movdqu 0xb0(%rsp), %xmm9\n"
    "    movdqu 0xc0(%rsp), %xmm10\n"
    "    movdqu 0xd0(%rsp), %xmm11\n"
    "    movdqu 0xe0(%rsp), %xmm12\n"
    "    movdqu 0xf0(%rsp), %xmm13\n"
    "    movdqu 0x100(%rsp), %xmm14\n"
    "    movdqu 0x110(%rsp), %xmm15\n"
    "    add $0x128, %rsp\n"
    "    pop %r15\n"
    "    pop %r14\n"
    "    pop %r13\n"
    "    pop %r12\n"
    "    pop %r11\n"
    "    pop %r10\n"
    "    pop %r9\n"
    "    pop %r8\n"
    "    pop %rbp\n"
    "    pop %rdi\n"
    "    pop %rsi\n"
    "    pop %rdx\n"
    "    pop %rcx\n"
    "    pop %rbx\n"
    "    pop %rax\n"
    "    popfq\n"
    "    jmp *bbgame_probe_trampoline(%rip)\n"
);

/* ------------------------------------------------------------ probe phases -- */

static int probe_memory_read(const BbGameHost *host) {
    MEMORY_BASIC_INFORMATION mbi;
    unsigned char head[16];
    char hex[sizeof(head) * 2 + 1];
    size_t n = host->image_size < (uint64_t)sizeof(head) ? (size_t)host->image_size : sizeof(head);

    probe_log("probe.phase", "order=2 name=memory_read");
    memset(&mbi, 0, sizeof(mbi));
    if (VirtualQuery((LPCVOID)host->image, &mbi, sizeof(mbi)) == 0) {
        probe_log("mem.read.fail", "reason=VirtualQuery_failed error=%lu", (unsigned long)GetLastError());
        return 0;
    }
    if (mbi.State != MEM_COMMIT || mbi.Protect == 0 ||
        (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0) {
        probe_log("mem.read.fail", "reason=page_not_readable state=0x%lx protect=0x%lx",
                  (unsigned long)mbi.State, (unsigned long)mbi.Protect);
        return 0;
    }
    memset(head, 0, sizeof(head));
    memcpy(head, host->image, n);
    hex16(head, n, hex, sizeof(hex));
    probe_log("mem.read", "offset=0 size=%zu value=%s", n, hex);
    probe_log("image.elf_magic",
              "value=%s", (n >= 4 && head[0] == 0x7f && head[1] == 'E' && head[2] == 'L' && head[3] == 'F') ? "ok" : "unexpected");
    /* Relocations/patches already ran: the pinned image hash cannot be checked here. */
    probe_log("image.hash_check", "skipped=1 reason=image_is_post_relocation_post_patch");
    return 1;
}

static int probe_memory_write_back(const BbGameHost *host) {
    unsigned long long offset = 0;
    const char *env_off = getenv("BB_PROBE_WRITE_OFFSET");
    MEMORY_BASIC_INFORMATION mbi;
    unsigned char *p;
    uint64_t before = 0, after = 0;

    probe_log("probe.phase", "order=3 name=memory_write_back");
    if (!env_is_true("BB_PROBE_WRITE_TEST", 1)) {
        probe_log("mem.write_back", "skipped=1 reason=disabled_by_BB_PROBE_WRITE_TEST");
        return 1;
    }
    if (env_off != NULL && env_off[0] != '\0' && !parse_u64(env_off, &offset)) {
        probe_log("mem.write_back.fail", "reason=bad_BB_PROBE_WRITE_OFFSET value=%s", env_off);
        return 0;
    }
    if (offset + 8ull < offset || offset + 8ull > host->image_size) {
        probe_log("mem.write_back.fail", "reason=out_of_range offset=0x%llx image_size=%llu",
                  offset, (unsigned long long)host->image_size);
        return 0;
    }
    p = host->image + offset;
    memset(&mbi, 0, sizeof(mbi));
    if (VirtualQuery((LPCVOID)p, &mbi, sizeof(mbi)) == 0) {
        probe_log("mem.write_back.fail", "reason=VirtualQuery_failed error=%lu", (unsigned long)GetLastError());
        return 0;
    }
    if (mbi.State != MEM_COMMIT || !protect_is_writable(mbi.Protect)) {
        /* Not fatal here: install_hook may still prove writes (loader may use VirtualProtect). */
        probe_log("mem.write_back", "skipped=1 reason=page_not_writable state=0x%lx protect=0x%lx",
                  (unsigned long)mbi.State, (unsigned long)mbi.Protect);
        return 1;
    }
    memcpy(&before, p, 8);
    memcpy(p, &before, 8); /* byte-identical write-back: proves write access, no content change */
    memcpy(&after, p, 8);
    if (before != after) {
        probe_log("mem.write_back.fail", "reason=verify_mismatch offset=0x%llx before=0x%016llx after=0x%016llx",
                  offset, (unsigned long long)before, (unsigned long long)after);
        return 0;
    }
    probe_log("mem.write_back", "offset=0x%llx size=8 result=ok value=0x%016llx",
              offset, (unsigned long long)before);
    return 1;
}

static int probe_build_trampoline(const BbGameHost *host, unsigned long long offset,
                                  unsigned long long replay_len) {
    unsigned char *t;
    unsigned char *q;
    uintptr_t resume;
    const void *target;

    if (replay_len != 14 && replay_len != 15) {
        probe_log("hook.fail", "reason=replay_len_must_be_14_or_15 value=%llu", replay_len);
        return 0;
    }
    if (offset + replay_len > host->image_size) {
        probe_log("hook.fail", "reason=replay_out_of_range offset=0x%llx len=%llu image_size=%llu",
                  offset, replay_len, (unsigned long long)host->image_size);
        return 0;
    }
    t = (unsigned char *)VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (t == NULL) {
        probe_log("hook.fail", "reason=VirtualAlloc_failed error=%lu", (unsigned long)GetLastError());
        return 0;
    }
    memcpy(t, host->image + offset, (size_t)replay_len);
    q = t + replay_len;
    resume = (uintptr_t)(host->image + offset + replay_len);
    q[0] = 0xff;
    q[1] = 0x25;
    memset(q + 2, 0, 4);
    memcpy(q + 6, &resume, 8);
    FlushInstructionCache(GetCurrentProcess(), t, 64);
    bbgame_probe_trampoline = t;
    target = (const void *)&bbgame_probe_replay_stub;
    probe_log("hook.trampoline", "buffer=%p replay_len=%llu resume=0x%llx target=%p",
              (void *)t, replay_len, (unsigned long long)resume, target);
    return 1;
}

static int probe_setup_hook(const BbGameHost *host) {
    const char *env = getenv("BB_PROBE_HOOK_OFFSET");
    const char *env_size;
    const char *env_mode;
    const char *env_resume;
    const char *env_ack;
    unsigned long long offset = 0, size = PROBE_HOOK_MIN_SIZE, replay_len = 14;
    const void *target = NULL;

    probe_log("probe.phase", "order=4 name=hook_setup");
    if (env == NULL || env[0] == '\0') {
        probe_log("hook.not_requested", "value=0");
        return 1;
    }
    if (!parse_u64(env, &offset)) {
        probe_log("hook.fail", "reason=bad_BB_PROBE_HOOK_OFFSET value=%s", env);
        return 0;
    }
    env_size = getenv("BB_PROBE_HOOK_SIZE");
    if (env_size != NULL && env_size[0] != '\0' && !parse_u64(env_size, &size)) {
        probe_log("hook.fail", "reason=bad_BB_PROBE_HOOK_SIZE value=%s", env_size);
        return 0;
    }
    if (size < PROBE_HOOK_MIN_SIZE) {
        probe_log("hook.fail", "reason=size_lt_14 value=%llu", size);
        return 0;
    }
    if (offset + size < offset || offset + size > host->image_size) {
        probe_log("hook.fail", "reason=out_of_range offset=0x%llx size=%llu image_size=%llu",
                  offset, size, (unsigned long long)host->image_size);
        return 0;
    }

    env_mode = getenv("BB_PROBE_HOOK_MODE");
    if (env_mode == NULL || env_mode[0] == '\0') env_mode = "skip";

    if (strcmp(env_mode, "skip") == 0) {
        g_hook_mode = "skip";
        target = (const void *)&probe_skip_stub;
    } else if (strcmp(env_mode, "replay14") == 0) {
        env_ack = getenv("BB_PROBE_HOOK_ACK");
        if (env_ack == NULL || strcmp(env_ack, "replay14") != 0) {
            probe_log("hook.fail", "reason=replay14_requires_BB_PROBE_HOOK_ACK_replay14");
            return 0;
        }
        env_resume = getenv("BB_PROBE_HOOK_REPLAY_LEN");
        if (env_resume != NULL && env_resume[0] != '\0' && !parse_u64(env_resume, &replay_len)) {
            probe_log("hook.fail", "reason=bad_BB_PROBE_HOOK_REPLAY_LEN value=%s", env_resume);
            return 0;
        }
        if (!probe_build_trampoline(host, offset, replay_len)) return 0;
        g_hook_mode = "replay14";
        target = (const void *)&bbgame_probe_replay_stub;
    } else {
        probe_log("hook.fail", "reason=unknown_BB_PROBE_HOOK_MODE value=%s", env_mode);
        return 0;
    }

    if (host->install_hook == NULL) {
        probe_log("hook.fail", "reason=host_install_hook_is_null");
        return 0;
    }
    probe_log("hook.request", "offset=0x%llx size=%llu mode=%s target=%p",
              offset, size, g_hook_mode, target);
    {
        int result = host->install_hook(offset, size, target);
        if (result != 0) {
            probe_log("hook.install.fail",
                      "result=%d reason=loader_rejected (needs size>=14, target!=NULL, whole range in one executable segment)",
                      result);
            return 0;
        }
    }
    g_hook_installed = 1;
    probe_log("hook.install", "result=0 offset=0x%llx size=%llu mode=%s target=%p",
              offset, size, g_hook_mode, target);
    return 1;
}

/* ----------------------------------------------------------------- export --- */

__declspec(dllexport) int bbgame_init(const BbGameHost *host) {
    if (g_log == NULL) {
        if (!probe_open_log()) {
            fprintf(stderr, "bbgame_probe: cannot open BB_PROBE_LOG (%s), errno=%d\n",
                    g_log_path[0] ? g_log_path : "(unset)", errno);
            return 10;
        }
    }
    probe_log("probe.phase", "order=1 name=init_enter");
    probe_log("bbgame_init.enter", "dll=%p log=%s", (void *)(uintptr_t)&bbgame_init, g_log_path);
    probe_log("dll.abi", "host_struct_size=%zu api_version=%u", sizeof(BbGameHost), (unsigned)BBGAME_API_VERSION);

    if (host == NULL) {
        probe_log("bbgame_init.fail", "result=11 reason=host_null");
        return 11;
    }
    probe_log("host.version", "value=%u expected=%u", (unsigned)host->version, (unsigned)BBGAME_API_VERSION);
    if (host->version != BBGAME_API_VERSION) {
        probe_log("bbgame_init.fail", "result=12 reason=version_mismatch value=%u expected=%u",
                  (unsigned)host->version, (unsigned)BBGAME_API_VERSION);
        return 12;
    }
    if (host->image == NULL) {
        probe_log("bbgame_init.fail", "result=13 reason=image_null");
        return 13;
    }
    if (host->image_size == 0 || host->image_size > PROBE_MAX_IMAGE) {
        probe_log("bbgame_init.fail", "result=14 reason=image_size_invalid value=%llu max=%llu",
                  (unsigned long long)host->image_size, (unsigned long long)PROBE_MAX_IMAGE);
        return 14;
    }
    probe_log("host.image", "value=%p", (void *)host->image);
    probe_log("host.image_size", "value=%llu", (unsigned long long)host->image_size);

    if (!probe_memory_read(host)) {
        probe_log("bbgame_init.fail", "result=15 reason=memory_read_failed");
        return 15;
    }
    if (!probe_memory_write_back(host)) {
        probe_log("bbgame_init.fail", "result=16 reason=memory_write_back_failed");
        return 16;
    }
    if (!probe_setup_hook(host)) {
        probe_log("bbgame_init.fail", "result=17 reason=hook_setup_failed");
        return 17;
    }
    probe_log("bbgame_init.exit", "result=0 hooks_installed=%d hook_mode=%s", g_hook_installed, g_hook_mode);
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        InitializeCriticalSection(&g_log_lock);
        g_lock_ready = 1;
        OutputDebugStringA("bbgame_probe: DLL_PROCESS_ATTACH\n");
    } else if (reason == DLL_PROCESS_DETACH) {
        /*
         * Process teardown only: flush and let the OS reclaim the FILE and the
         * critical section. Closing/freeing them here could race a hook thread
         * that is still inside probe_log.
         */
        if (g_log != NULL) fflush(g_log);
    }
    return TRUE;
}
