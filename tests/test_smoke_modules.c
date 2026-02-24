/*
 * test_smoke_modules.c -- smoke tests for the module loader end-to-end.
 *
 * Spawns the real openbc-server binary with crafted TOML configs and checks
 * exit codes + log output.  Links ZERO library objects -- standalone binary.
 *
 * Build rule (Makefile):
 *   $(BUILD)/tests/test_smoke_modules$(EXE): tests/test_smoke_modules.c
 *       $(CC) $(CFLAGS) -O1 $(LDFLAGS) -o $@ $< $(LDLIBS)
 */

#include "test_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#  include <windows.h>
#  define SMOKE_DLL_EXT ".dll"
#else
#  include <unistd.h>
#  include <signal.h>
#  include <sys/types.h>
#  include <sys/wait.h>
#  ifdef __APPLE__
#    define SMOKE_DLL_EXT ".dylib"
#  else
#    define SMOKE_DLL_EXT ".so"
#  endif
#endif

/* -------------------------------------------------------------------------
 * Platform-abstracted helpers
 * ---------------------------------------------------------------------- */

/* Server binary path -- relative to working directory (build/) */
#ifdef _WIN32
#  define SERVER_BIN "build\\openbc-server.exe"
#else
#  define SERVER_BIN "build/openbc-server"
#endif

/* Base port -- each test gets a unique port offset */
#define SMOKE_PORT_BASE 23200

typedef struct {
#ifdef _WIN32
    HANDLE hProcess;
    HANDLE hThread;
#else
    pid_t pid;
#endif
} smoke_proc_t;

/* Write a temp file with given content. Returns 0 on success. */
static int smoke_write_file(const char *path, const char *content)
{
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fputs(content, f);
    fclose(f);
    return 0;
}

/* Launch the server binary with --config, --log-file, --no-master, -p, -vv.
 * Returns 0 on success, -1 on spawn failure. */
static int smoke_spawn(smoke_proc_t *proc, const char *config,
                       const char *log, int port)
{
    char cmd[1024];
#ifdef _WIN32
    snprintf(cmd, sizeof(cmd),
             "%s --config %s --log-file %s --no-master -p %d -vv",
             SERVER_BIN, config, log, port);

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    memset(&pi, 0, sizeof(pi));

    if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE,
                        CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        return -1;
    }
    proc->hProcess = pi.hProcess;
    proc->hThread  = pi.hThread;
#else
    snprintf(cmd, sizeof(cmd),
             "%s --config %s --log-file %s --no-master -p %d -vv",
             SERVER_BIN, config, log, port);

    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        /* Child: exec the server */
        execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
        _exit(127);
    }
    proc->pid = pid;
#endif
    return 0;
}

/* Wait for process to exit (up to timeout_ms).
 * Returns exit code, or -1 on timeout/error. */
static int smoke_wait(smoke_proc_t *proc, int timeout_ms)
{
#ifdef _WIN32
    DWORD result = WaitForSingleObject(proc->hProcess, (DWORD)timeout_ms);
    if (result == WAIT_TIMEOUT) return -1;

    DWORD exit_code = 1;
    GetExitCodeProcess(proc->hProcess, &exit_code);
    CloseHandle(proc->hProcess);
    CloseHandle(proc->hThread);
    return (int)exit_code;
#else
    /* Poll with short sleeps up to timeout */
    int elapsed = 0;
    int status;
    while (elapsed < timeout_ms) {
        pid_t w = waitpid(proc->pid, &status, WNOHANG);
        if (w > 0) {
            if (WIFEXITED(status))
                return WEXITSTATUS(status);
            return -1;
        }
        usleep(50000); /* 50 ms */
        elapsed += 50;
    }
    return -1; /* timed out */
#endif
}

/* Send SIGTERM (POSIX) / TerminateProcess (Windows) to stop the server. */
static void smoke_stop(smoke_proc_t *proc)
{
#ifdef _WIN32
    TerminateProcess(proc->hProcess, 0);
#else
    kill(proc->pid, SIGTERM);
#endif
}

/* Search log file for needle. Returns 1 if found, 0 if not. */
static int smoke_log_contains(const char *log_path, const char *needle)
{
    FILE *f = fopen(log_path, "r");
    if (!f) return 0;

    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, needle)) {
            fclose(f);
            return 1;
        }
    }
    fclose(f);
    return 0;
}

/* Remove a temp file (best-effort). */
static void smoke_cleanup(const char *path)
{
    remove(path);
}

/* -------------------------------------------------------------------------
 * Test scenarios
 * ---------------------------------------------------------------------- */

TEST(smoke_no_modules)
{
    const char *toml = "build/tests/smoke_no_modules.toml";
    const char *log  = "build/tests/smoke_no_modules.log";
    int port = SMOKE_PORT_BASE;

    smoke_cleanup(log);
    int w = smoke_write_file(toml, "[server]\nport = 23200\n");
    ASSERT_EQ_INT(0, w);

    smoke_proc_t proc;
    int sp = smoke_spawn(&proc, toml, log, port);
    ASSERT_EQ_INT(0, sp);

    /* Let server start up */
#ifdef _WIN32
    Sleep(2000);
#else
    usleep(2000000);
#endif

    smoke_stop(&proc);
    int code = smoke_wait(&proc, 5000);
    ASSERT(code == 0 || code == -1); /* 0 on clean exit, -1 if killed */

    smoke_cleanup(toml);
    smoke_cleanup(log);
}

TEST(smoke_nonexistent_dll)
{
    const char *toml = "build/tests/smoke_nonexist_dll.toml";
    const char *log  = "build/tests/smoke_nonexist_dll.log";
    int port = SMOKE_PORT_BASE + 1;

    smoke_cleanup(log);
    char content[512];
    snprintf(content, sizeof(content),
             "[[modules]]\nname = \"bogus\"\ndll = \"nonexistent_module%s\"\n",
             SMOKE_DLL_EXT);
    int w = smoke_write_file(toml, content);
    ASSERT_EQ_INT(0, w);

    smoke_proc_t proc;
    int sp = smoke_spawn(&proc, toml, log, port);
    ASSERT_EQ_INT(0, sp);

    int code = smoke_wait(&proc, 5000);
    ASSERT_EQ_INT(1, code);

    ASSERT(smoke_log_contains(log, "ailed to load") ||
           smoke_log_contains(log, "ailed to open"));

    smoke_cleanup(toml);
    smoke_cleanup(log);
}

TEST(smoke_path_traversal)
{
    const char *toml = "build/tests/smoke_traversal.toml";
    const char *log  = "build/tests/smoke_traversal.log";
    int port = SMOKE_PORT_BASE + 2;

    smoke_cleanup(log);
    char content[512];
    snprintf(content, sizeof(content),
             "[[modules]]\nname = \"evil\"\ndll = \"../evil%s\"\n",
             SMOKE_DLL_EXT);
    int w = smoke_write_file(toml, content);
    ASSERT_EQ_INT(0, w);

    smoke_proc_t proc;
    int sp = smoke_spawn(&proc, toml, log, port);
    ASSERT_EQ_INT(0, sp);

    int code = smoke_wait(&proc, 5000);
    ASSERT_EQ_INT(1, code);

    ASSERT(smoke_log_contains(log, "traversal") ||
           smoke_log_contains(log, "raversal"));

    smoke_cleanup(toml);
    smoke_cleanup(log);
}

TEST(smoke_lua_only_starts)
{
    const char *toml = "build/tests/smoke_lua_only.toml";
    const char *log  = "build/tests/smoke_lua_only.log";
    int port = SMOKE_PORT_BASE + 3;

    smoke_cleanup(log);
    int w = smoke_write_file(toml,
        "[[modules]]\nname = \"luamod\"\nlua = \"mods/m.lua\"\n");
    ASSERT_EQ_INT(0, w);

    smoke_proc_t proc;
    int sp = smoke_spawn(&proc, toml, log, port);
    ASSERT_EQ_INT(0, sp);

    /* Let server start up */
#ifdef _WIN32
    Sleep(2000);
#else
    usleep(2000000);
#endif

    smoke_stop(&proc);
    int code = smoke_wait(&proc, 5000);
    ASSERT(code == 0 || code == -1); /* 0 on clean exit, -1 if killed */

    smoke_cleanup(toml);
    smoke_cleanup(log);
}

TEST(smoke_empty_module_aborts)
{
    const char *toml = "build/tests/smoke_empty_mod.toml";
    const char *log  = "build/tests/smoke_empty_mod.log";
    int port = SMOKE_PORT_BASE + 4;

    smoke_cleanup(log);
    int w = smoke_write_file(toml,
        "[[modules]]\nname = \"empty\"\n");
    ASSERT_EQ_INT(0, w);

    smoke_proc_t proc;
    int sp = smoke_spawn(&proc, toml, log, port);
    ASSERT_EQ_INT(0, sp);

    int code = smoke_wait(&proc, 5000);
    ASSERT_EQ_INT(1, code);

    ASSERT(smoke_log_contains(log, "no dll or lua") ||
           smoke_log_contains(log, "no dll") ||
           smoke_log_contains(log, "neither"));

    smoke_cleanup(toml);
    smoke_cleanup(log);
}

TEST(smoke_wrong_extension)
{
    const char *toml = "build/tests/smoke_wrong_ext.toml";
    const char *log  = "build/tests/smoke_wrong_ext.log";
    int port = SMOKE_PORT_BASE + 5;

    smoke_cleanup(log);
    int w = smoke_write_file(toml,
        "[[modules]]\nname = \"bad\"\ndll = \"mod.exe\"\n");
    ASSERT_EQ_INT(0, w);

    smoke_proc_t proc;
    int sp = smoke_spawn(&proc, toml, log, port);
    ASSERT_EQ_INT(0, sp);

    int code = smoke_wait(&proc, 5000);
    ASSERT_EQ_INT(1, code);

    ASSERT(smoke_log_contains(log, "extension") ||
           smoke_log_contains(log, "xtension"));

    smoke_cleanup(toml);
    smoke_cleanup(log);
}

/* -------------------------------------------------------------------------
 * Runner
 * ---------------------------------------------------------------------- */

int main(void)
{
    /* Ensure build/tests/ directory exists for temp files */
#ifdef _WIN32
    CreateDirectoryA("build", NULL);
    CreateDirectoryA("build\\tests", NULL);
#else
    (void)!system("mkdir -p build/tests");
#endif

    RUN(smoke_no_modules);
    RUN(smoke_nonexistent_dll);
    RUN(smoke_path_traversal);
    RUN(smoke_lua_only_starts);
    RUN(smoke_empty_module_aborts);
    RUN(smoke_wrong_extension);

    printf("%d/%d tests passed\n", test_pass, test_count);
    return test_fail > 0 ? 1 : 0;
}
