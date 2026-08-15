/*
 * penjsapi.c - 有道词典笔自定义 JSAPI（文件操作 + 终端操作）
 *
 * 向 QuickJS 注册两个 C 模块：
 *   penfs    - 文件操作：readFile/writeFile/appendFile/listDir/exists/isDir/
 *              deleteFile/mkdir/rmdir/rename/cwd
 *   penshell - 终端操作：exec（执行 shell 命令，捕获 stdout/stderr/退出码，支持超时）
 *
 * 工作原理：
 *   本 .so 以「零链接依赖」方式编译（-nostdlib，不链接任何 libc）。
 *   所有符号在运行时由设备宿主进程解析：
 *     - QuickJS API     <- /usr/lib/libquickjs.so、libfalcon.so
 *     - registerCModuleLoader <- libfalcon.so
 *     - POSIX/文件函数  <- 设备系统的 uClibc（/lib/libc.so.1）
 *   运行时通过 dlsym("custom_init_jsapis") 找到入口，注册模块加载器；
 *   之后 JS 里 import xxx from 'penfs' / 'penshell' 即触发加载。
 *
 * 编译（Windows，Arm GNU Toolchain）：
 *   arm-none-eabi-gcc -marm -mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard \
 *     -fPIC -fno-unwind-tables -fno-asynchronous-unwind-tables -O2 -Wall \
 *     -I native -c native/penjsapi.c -o native/penjsapi.o
 *   arm-none-eabi-gcc -shared -nostdlib -o libs/libjsapi_penfs.so native/penjsapi.o
 * （详见 native/build-native.bat）
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "quickjs/quickjs.h"

/* ==================== 宿主动态符号声明（运行时解析） ==================== */

/* libfalcon.so 导出：注册 C 模块加载器 */
extern void registerCModuleLoader(const char *moduleName,
                                  JSModuleDef *(*loader)(JSContext *ctx, const char *name));

/* 设备 uClibc 导出（ARM Linux EABI 硬浮点，全部在 dlopen 时解析） */
extern int open(const char *path, int flags, ...);
extern int read(int fd, void *buf, unsigned int count);
extern int write(int fd, const void *buf, unsigned int count);
extern int close(int fd);
extern int unlink(const char *path);
extern int rename(const char *oldpath, const char *newpath);
extern int access(const char *path, int mode);
extern int mkdir(const char *path, unsigned int mode);
extern int rmdir(const char *path);
extern char *getcwd(char *buf, unsigned int size);
extern void *opendir(const char *path);
extern int closedir(void *dir);

extern int pipe(int fds[2]);
extern int fork(void);
extern int execv(const char *file, char *const argv[]);
extern int waitpid(int pid, int *status, int options);
extern int dup2(int oldfd, int newfd);
extern int kill(int pid, int sig);
extern void _exit(int status);
extern long time(long *t);

struct pollfd { int fd; short events; short revents; };
extern int poll(struct pollfd *fds, unsigned long nfds, int timeout);

/* glob_t（uClibc-ng 布局：前 9 个成员 + 填充对齐） */
typedef struct {
    size_t gl_pathc;
    char **gl_pathv;
    size_t gl_offs;
    int gl_flags;
    void (*gl_closedir)(void *);
    void (*gl_readdir)(void *);
    void *(*gl_opendir)(const char *);
    int (*gl_lstat)(const char *, void *);
    int (*gl_stat)(const char *, void *);
    char _pad[28];
} pen_glob_t;
extern int glob(const char *pattern, int flags, void *errfunc, pen_glob_t *pglob);
extern void globfree(pen_glob_t *pglob);

extern void *malloc(size_t size);
extern void free(void *ptr);

/* 宿主 miniapp 进程 SIGCHLD 被设为 SIG_IGN（/proc/<pid>/status SigIgn 已验证），
 * 直接 waitpid 会 ECHILD 拿不到退出码。exec 时临时改回 SIG_DFL 再恢复。 */
typedef void (*pen_sighandler_t)(int);
extern pen_sighandler_t signal(int signum, pen_sighandler_t handler);

/* ==================== ARM Linux 内核 ABI 常量 ==================== */

#define O_RDONLY   0
#define O_WRONLY   1
#define O_CREAT    0x40
#define O_TRUNC    0x200
#define O_APPEND   0x400

#define F_OK 0

#define POLLIN  0x0001
#define POLLERR 0x0008
#define POLLHUP 0x0010

#define SIGKILL 9
#define SIGCHLD 17
#define SIG_DFL ((pen_sighandler_t)0)
#define SIG_IGN ((pen_sighandler_t)1)
#define GLOB_MARK 0x02
#define GLOB_NOMATCH 3

#define WIFEXITED(s)  (((s) & 0x7f) == 0)
#define WEXITSTATUS(s) (((s) >> 8) & 0xff)

/* 追踪日志：写 fd2/fd1，宿主进程会捕获到设备日志（/userdata/applog/DictPen_*.log）。
 * 用 write() 而不是 printf —— printf 需要 FILE* 全局状态，零链接场景不可靠。 */
static void dbg(const char *s)
{
    int n = (int)strlen(s);
    write(2, s, (unsigned int)n);
    write(2, "\n", 1);
    write(1, s, (unsigned int)n);
    write(1, "\n", 1);
}

/* 创建 JS 函数对象。
 * 注意：不能用 JS_NewCFunction —— 它是 static inline，展开为对宿主
 * JS_NewCFunction2 的调用，而设备宿主并未导出该符号（会导致进程崩溃）。
 * 必须用 JS_NewCFunctionData（已验证宿主导出）。
 * 函数签名少两个参数（magic/func_data）不影响 ARM AAPCS 调用：多余的实参被忽略。 */
#define NEW_FUNC(fn, nargs) \
    JS_NewCFunctionData(ctx, (JSCFunctionData *)(fn), (nargs), 0, 0, NULL)

/* ==================== JS 结果对象构造 ==================== */

/* {ok:true} / {ok:false, code, message} */
static JSValue new_result(JSContext *ctx, int ok)
{
    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "ok", JS_NewBool(ctx, ok));
    return obj;
}

static JSValue fail(JSContext *ctx, const char *code, const char *msg)
{
    JSValue obj = new_result(ctx, 0);
    JS_SetPropertyStr(ctx, obj, "code", JS_NewString(ctx, code));
    JS_SetPropertyStr(ctx, obj, "message", JS_NewString(ctx, msg));
    return obj;
}

static void set_prop_int(JSContext *ctx, JSValue obj, const char *key, int v)
{
    JS_SetPropertyStr(ctx, obj, key, JS_NewInt32(ctx, v));
}

static void set_prop_bool(JSContext *ctx, JSValue obj, const char *key, int v)
{
    JS_SetPropertyStr(ctx, obj, key, JS_NewBool(ctx, v));
}

static void set_prop_str(JSContext *ctx, JSValue obj, const char *key, const char *v)
{
    JS_SetPropertyStr(ctx, obj, key, JS_NewString(ctx, v));
}

static void set_prop_strlen(JSContext *ctx, JSValue obj, const char *key,
                            const char *v, size_t len)
{
    JS_SetPropertyStr(ctx, obj, key, JS_NewStringLen(ctx, v, len));
}

/* 取 argv[i] 为 C 字符串（malloc 拷贝，调用方 free；失败返回 NULL 并填 errmsg） */
static char *arg_to_cstr(JSContext *ctx, JSValueConst v, const char **errmsg)
{
    size_t len = 0;
    const char *p = JS_ToCStringLen(ctx, &len, v);
    if (!p) {
        *errmsg = "参数无法转换为字符串";
        return NULL;
    }
    char *s = (char *)malloc(len + 1);
    if (!s) {
        JS_FreeCString(ctx, p);
        *errmsg = "内存不足";
        return NULL;
    }
    memcpy(s, p, len);
    s[len] = '\0';
    JS_FreeCString(ctx, p);
    return s;
}

static int arg_to_int(JSContext *ctx, JSValueConst v, int *out)
{
    int32_t n = 0;
    if (JS_ToInt32(ctx, &n, v) < 0)
        return -1;
    *out = (int)n;
    return 0;
}

/* ==================== penfs：文件操作 ==================== */

/* readFile(path, maxLen?) -> {ok, data, size}   maxLen 默认 1MB */
static JSValue f_readFile(JSContext *ctx, JSValueConst this_val,
                          int argc, JSValueConst *argv)
{
    (void)this_val;
    const char *emsg = NULL;
    if (argc < 1)
        return fail(ctx, "EINVAL", "缺少 path 参数");
    char *path = arg_to_cstr(ctx, argv[0], &emsg);
    if (!path)
        return fail(ctx, "EINVAL", emsg);

    int max = 1024 * 1024;
    if (argc > 1 && !JS_IsUndefined(argv[1])) {
        if (arg_to_int(ctx, argv[1], &max) < 0 || max < 0) {
            free(path);
            return fail(ctx, "EINVAL", "maxLen 参数无效");
        }
    }

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        free(path);
        return fail(ctx, "ENOENT", "无法打开文件");
    }
    char *buf = (char *)malloc((size_t)max + 1);
    if (!buf) {
        close(fd);
        free(path);
        return fail(ctx, "ENOMEM", "内存不足");
    }
    int total = 0;
    while (total < max) {
        int n = read(fd, buf + total, (unsigned int)(max - total));
        if (n <= 0)
            break;
        total += n;
    }
    close(fd);

    JSValue r = new_result(ctx, 1);
    set_prop_strlen(ctx, r, "data", buf, (size_t)total);
    set_prop_int(ctx, r, "size", total);
    free(buf);
    free(path);
    return r;
}

/* writeFile(path, data, mode?)  mode: "w" 覆盖(默认) / "a" 追加 -> {ok, size} */
static JSValue f_writeFile(JSContext *ctx, JSValueConst this_val,
                           int argc, JSValueConst *argv)
{
    (void)this_val;
    const char *emsg = NULL;
    if (argc < 2)
        return fail(ctx, "EINVAL", "需要 path 和 data 参数");
    char *path = arg_to_cstr(ctx, argv[0], &emsg);
    if (!path)
        return fail(ctx, "EINVAL", emsg);

    size_t dlen = 0;
    const char *data = JS_ToCStringLen(ctx, &dlen, argv[1]);
    if (!data) {
        free(path);
        return fail(ctx, "EINVAL", "data 无法转换为字符串");
    }

    int flags = O_WRONLY | O_CREAT | O_TRUNC;
    if (argc > 2 && !JS_IsUndefined(argv[2])) {
        char *mode = arg_to_cstr(ctx, argv[2], &emsg);
        if (!mode) {
            JS_FreeCString(ctx, data);
            free(path);
            return fail(ctx, "EINVAL", emsg);
        }
        if (mode[0] == 'a')
            flags = O_WRONLY | O_CREAT | O_APPEND;
        free(mode);
    }

    int fd = open(path, flags, 0644);
    if (fd < 0) {
        JS_FreeCString(ctx, data);
        free(path);
        return fail(ctx, "EPERM", "无法打开文件写入");
    }
    int total = 0;
    while (total < (int)dlen) {
        int n = write(fd, data + total, (unsigned int)(dlen - (size_t)total));
        if (n <= 0)
            break;
        total += n;
    }
    close(fd);
    JS_FreeCString(ctx, data);

    JSValue r = new_result(ctx, 1);
    set_prop_int(ctx, r, "size", total);
    free(path);
    return r;
}

/* appendFile(path, data) -> {ok, size} */
static JSValue f_appendFile(JSContext *ctx, JSValueConst this_val,
                            int argc, JSValueConst *argv)
{
    JSValueConst nargv[3];
    JSValue mode = JS_NewString(ctx, "a");
    nargv[0] = (argc > 0) ? argv[0] : JS_UNDEFINED;
    nargv[1] = (argc > 1) ? argv[1] : JS_UNDEFINED;
    nargv[2] = mode;
    JSValue r = f_writeFile(ctx, this_val, 3, nargv);
    JS_FreeValue(ctx, mode);
    return r;
}

/* listDir(path) -> {ok, entries:[名字数组]，目录名带 / 后缀} */
static JSValue f_listDir(JSContext *ctx, JSValueConst this_val,
                         int argc, JSValueConst *argv)
{
    (void)this_val;
    const char *emsg = NULL;
    if (argc < 1)
        return fail(ctx, "EINVAL", "缺少 path 参数");
    char *path = arg_to_cstr(ctx, argv[0], &emsg);
    if (!path)
        return fail(ctx, "EINVAL", emsg);

    size_t len = strlen(path);
    char *pattern = (char *)malloc(len + 4);
    if (!pattern) {
        free(path);
        return fail(ctx, "ENOMEM", "内存不足");
    }
    memcpy(pattern, path, len);
    if (len > 0 && path[len - 1] == '/') {
        pattern[len] = '*';
        pattern[len + 1] = '\0';
    } else {
        pattern[len] = '/';
        pattern[len + 1] = '*';
        pattern[len + 2] = '\0';
    }

    pen_glob_t g;
    memset(&g, 0, sizeof(g));
    int rc = glob(pattern, GLOB_MARK, NULL, &g);
    free(pattern);
    if (rc != 0 && rc != GLOB_NOMATCH) {
        free(path);
        return fail(ctx, "ELIST", "目录无法读取");
    }

    JSValue arr = JS_NewArray(ctx);
    size_t skip = (len > 0 && path[len - 1] == '/') ? len : len + 1;
    size_t i;
    for (i = 0; i < g.gl_pathc; i++) {
        JS_SetPropertyUint32(ctx, arr, (uint32_t)i, JS_NewString(ctx, g.gl_pathv[i] + skip));
    }
    if (rc == 0)
        globfree(&g);

    JSValue r = new_result(ctx, 1);
    JS_SetPropertyStr(ctx, r, "entries", arr);
    free(path);
    return r;
}

/* exists(path) -> {ok, exists} */
static JSValue f_exists(JSContext *ctx, JSValueConst this_val,
                        int argc, JSValueConst *argv)
{
    (void)this_val;
    const char *emsg = NULL;
    if (argc < 1)
        return fail(ctx, "EINVAL", "缺少 path 参数");
    char *path = arg_to_cstr(ctx, argv[0], &emsg);
    if (!path)
        return fail(ctx, "EINVAL", emsg);
    int ex = (access(path, F_OK) == 0);
    free(path);
    JSValue r = new_result(ctx, 1);
    set_prop_bool(ctx, r, "exists", ex);
    return r;
}

/* isDir(path) -> {ok, isDir} */
static JSValue f_isDir(JSContext *ctx, JSValueConst this_val,
                       int argc, JSValueConst *argv)
{
    (void)this_val;
    const char *emsg = NULL;
    if (argc < 1)
        return fail(ctx, "EINVAL", "缺少 path 参数");
    char *path = arg_to_cstr(ctx, argv[0], &emsg);
    if (!path)
        return fail(ctx, "EINVAL", emsg);
    void *d = opendir(path);
    int isd = (d != NULL);
    if (d)
        closedir(d);
    free(path);
    JSValue r = new_result(ctx, 1);
    set_prop_bool(ctx, r, "isDir", isd);
    return r;
}

/* deleteFile(path) -> {ok}（仅文件；目录请用 rmdir） */
static JSValue f_deleteFile(JSContext *ctx, JSValueConst this_val,
                            int argc, JSValueConst *argv)
{
    (void)this_val;
    const char *emsg = NULL;
    if (argc < 1)
        return fail(ctx, "EINVAL", "缺少 path 参数");
    char *path = arg_to_cstr(ctx, argv[0], &emsg);
    if (!path)
        return fail(ctx, "EINVAL", emsg);
    int rc = unlink(path);
    free(path);
    if (rc != 0)
        return fail(ctx, "EPERM", "删除失败（不存在或无权限）");
    return new_result(ctx, 1);
}

/* mkdir(path) -> {ok} */
static JSValue f_mkdir(JSContext *ctx, JSValueConst this_val,
                       int argc, JSValueConst *argv)
{
    (void)this_val;
    const char *emsg = NULL;
    if (argc < 1)
        return fail(ctx, "EINVAL", "缺少 path 参数");
    char *path = arg_to_cstr(ctx, argv[0], &emsg);
    if (!path)
        return fail(ctx, "EINVAL", emsg);
    int rc = mkdir(path, 0777);
    free(path);
    if (rc != 0)
        return fail(ctx, "EPERM", "创建目录失败");
    return new_result(ctx, 1);
}

/* rmdir(path) -> {ok} */
static JSValue f_rmdir(JSContext *ctx, JSValueConst this_val,
                       int argc, JSValueConst *argv)
{
    (void)this_val;
    const char *emsg = NULL;
    if (argc < 1)
        return fail(ctx, "EINVAL", "缺少 path 参数");
    char *path = arg_to_cstr(ctx, argv[0], &emsg);
    if (!path)
        return fail(ctx, "EINVAL", emsg);
    int rc = rmdir(path);
    free(path);
    if (rc != 0)
        return fail(ctx, "EPERM", "删除目录失败（不存在/非空/无权限）");
    return new_result(ctx, 1);
}

/* rename(oldPath, newPath) -> {ok} */
static JSValue f_rename(JSContext *ctx, JSValueConst this_val,
                        int argc, JSValueConst *argv)
{
    (void)this_val;
    const char *emsg = NULL;
    if (argc < 2)
        return fail(ctx, "EINVAL", "需要 oldPath 和 newPath 参数");
    char *oldp = arg_to_cstr(ctx, argv[0], &emsg);
    if (!oldp)
        return fail(ctx, "EINVAL", emsg);
    char *newp = arg_to_cstr(ctx, argv[1], &emsg);
    if (!newp) {
        free(oldp);
        return fail(ctx, "EINVAL", emsg);
    }
    int rc = rename(oldp, newp);
    free(oldp);
    free(newp);
    if (rc != 0)
        return fail(ctx, "EPERM", "重命名失败");
    return new_result(ctx, 1);
}

/* cwd() -> {ok, path} */
static JSValue f_cwd(JSContext *ctx, JSValueConst this_val,
                     int argc, JSValueConst *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    char buf[4096];
    char *p = getcwd(buf, sizeof(buf));
    if (!p)
        return fail(ctx, "ECWD", "获取当前目录失败");
    JSValue r = new_result(ctx, 1);
    set_prop_str(ctx, r, "path", p);
    return r;
}

/* ==================== penshell：终端操作 ==================== */

/* exec(cmd, timeoutMs?) -> {ok, code, killed, stdout, stderr}
 * code: 退出码（0-255），非正常退出为 -1；killed: 是否超时被杀
 * timeoutMs 默认 10000，0 表示不超时（秒级精度） */
static JSValue f_exec(JSContext *ctx, JSValueConst this_val,
                      int argc, JSValueConst *argv)
{
    (void)this_val;
    const char *emsg = NULL;
    if (argc < 1)
        return fail(ctx, "EINVAL", "缺少 cmd 参数");
    char *cmd = arg_to_cstr(ctx, argv[0], &emsg);
    if (!cmd)
        return fail(ctx, "EINVAL", emsg);

    int timeoutMs = 10000;
    if (argc > 1 && !JS_IsUndefined(argv[1])) {
        if (arg_to_int(ctx, argv[1], &timeoutMs) < 0 || timeoutMs < 0) {
            free(cmd);
            return fail(ctx, "EINVAL", "timeoutMs 参数无效");
        }
    }

    int outfd[2], errfd[2];
    if (pipe(outfd) != 0 || pipe(errfd) != 0) {
        free(cmd);
        return fail(ctx, "EPIPE", "无法创建管道");
    }

    /* 宿主进程 SIGCHLD=SIG_IGN，waitpid 会 ECHILD；临时改回 SIG_DFL */
    pen_sighandler_t prev_sigchld = signal(SIGCHLD, SIG_DFL);
    int pid = fork();
    if (pid < 0) {
        signal(SIGCHLD, prev_sigchld);
        close(outfd[0]); close(outfd[1]);
        close(errfd[0]); close(errfd[1]);
        free(cmd);
        return fail(ctx, "EFORK", "fork 失败");
    }
    if (pid == 0) {
        /* 子进程：重定向 stdout/stderr 到管道后执行 /bin/sh -c cmd */
        dup2(outfd[1], 1);
        dup2(errfd[1], 2);
        close(outfd[0]); close(outfd[1]);
        close(errfd[0]); close(errfd[1]);
        char *shargv[4];
        shargv[0] = (char *)"/bin/sh";
        shargv[1] = (char *)"-c";
        shargv[2] = cmd;
        shargv[3] = NULL;
        execv("/bin/sh", shargv);
        _exit(127);
    }

    close(outfd[1]);
    close(errfd[1]);

    char outbuf[65536], errbuf[65536];
    int outLen = 0, errLen = 0;
    int eofOut = 0, eofErr = 0, killed = 0;
    int status = 0;

    struct pollfd pfds[2];
    pfds[0].fd = outfd[0];
    pfds[0].events = POLLIN;
    pfds[0].revents = 0;
    pfds[1].fd = errfd[0];
    pfds[1].events = POLLIN;
    pfds[1].revents = 0;

    long t0 = time(NULL);
    while (!(eofOut && eofErr)) {
        int t = -1;
        if (timeoutMs > 0) {
            long elapsed = (time(NULL) - t0) * 1000;
            if (elapsed >= (long)timeoutMs) {
                killed = 1;
                kill(pid, SIGKILL);
                break;
            }
            t = timeoutMs - (int)elapsed;
        }
        int n = poll(pfds, 2, t);
        if (n < 0)
            break;
        if (n == 0) {
            killed = 1;
            kill(pid, SIGKILL);
            break;
        }
        if (pfds[0].revents) {
            if ((pfds[0].revents & POLLIN) &&
                outLen < (int)sizeof(outbuf) - 1) {
                int r = read(outfd[0], outbuf + outLen,
                             (unsigned int)(sizeof(outbuf) - 1 - (size_t)outLen));
                if (r > 0)
                    outLen += r;
            }
            if (pfds[0].revents & (POLLHUP | POLLERR)) {
                eofOut = 1;
                pfds[0].fd = -1;
                pfds[0].events = 0;
            }
        }
        if (pfds[1].revents) {
            if ((pfds[1].revents & POLLIN) &&
                errLen < (int)sizeof(errbuf) - 1) {
                int r = read(errfd[0], errbuf + errLen,
                             (unsigned int)(sizeof(errbuf) - 1 - (size_t)errLen));
                if (r > 0)
                    errLen += r;
            }
            if (pfds[1].revents & (POLLHUP | POLLERR)) {
                eofErr = 1;
                pfds[1].fd = -1;
                pfds[1].events = 0;
            }
        }
    }
    waitpid(pid, &status, 0);
    signal(SIGCHLD, prev_sigchld);   /* 恢复宿主原有 SIGCHLD 设置 */
    close(outfd[0]);
    close(errfd[0]);

    int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    JSValue r = new_result(ctx, 1);
    set_prop_int(ctx, r, "code", code);
    set_prop_bool(ctx, r, "killed", killed);
    set_prop_strlen(ctx, r, "stdout", outbuf, (size_t)outLen);
    set_prop_strlen(ctx, r, "stderr", errbuf, (size_t)errLen);
    free(cmd);
    return r;
}

/* ==================== 模块注册 ==================== */

static int penfs_init(JSContext *ctx, JSModuleDef *m)
{
    dbg("[penfs] penfs_init enter");
    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "readFile",   NEW_FUNC(f_readFile, 2));
    JS_SetPropertyStr(ctx, obj, "writeFile",  NEW_FUNC(f_writeFile, 3));
    JS_SetPropertyStr(ctx, obj, "appendFile", NEW_FUNC(f_appendFile, 2));
    JS_SetPropertyStr(ctx, obj, "listDir",    NEW_FUNC(f_listDir, 1));
    JS_SetPropertyStr(ctx, obj, "exists",     NEW_FUNC(f_exists, 1));
    JS_SetPropertyStr(ctx, obj, "isDir",      NEW_FUNC(f_isDir, 1));
    JS_SetPropertyStr(ctx, obj, "deleteFile", NEW_FUNC(f_deleteFile, 1));
    JS_SetPropertyStr(ctx, obj, "mkdir",      NEW_FUNC(f_mkdir, 1));
    JS_SetPropertyStr(ctx, obj, "rmdir",      NEW_FUNC(f_rmdir, 1));
    JS_SetPropertyStr(ctx, obj, "rename",     NEW_FUNC(f_rename, 2));
    JS_SetPropertyStr(ctx, obj, "cwd",        NEW_FUNC(f_cwd, 0));
    dbg("[penfs] penfs_init obj done");

    JS_SetModuleExport(ctx, m, "default", obj);
    JS_SetModuleExport(ctx, m, "readFile",   NEW_FUNC(f_readFile, 2));
    JS_SetModuleExport(ctx, m, "writeFile",  NEW_FUNC(f_writeFile, 3));
    JS_SetModuleExport(ctx, m, "appendFile", NEW_FUNC(f_appendFile, 2));
    JS_SetModuleExport(ctx, m, "listDir",    NEW_FUNC(f_listDir, 1));
    JS_SetModuleExport(ctx, m, "exists",     NEW_FUNC(f_exists, 1));
    JS_SetModuleExport(ctx, m, "isDir",      NEW_FUNC(f_isDir, 1));
    JS_SetModuleExport(ctx, m, "deleteFile", NEW_FUNC(f_deleteFile, 1));
    JS_SetModuleExport(ctx, m, "mkdir",      NEW_FUNC(f_mkdir, 1));
    JS_SetModuleExport(ctx, m, "rmdir",      NEW_FUNC(f_rmdir, 1));
    JS_SetModuleExport(ctx, m, "rename",     NEW_FUNC(f_rename, 2));
    JS_SetModuleExport(ctx, m, "cwd",        NEW_FUNC(f_cwd, 0));
    dbg("[penfs] penfs_init exit ok");
    return 0;
}

static JSModuleDef *penfs_module_load(JSContext *ctx, const char *name)
{
    JSModuleDef *m;
    dbg("[penfs] module_load enter");
    if (!name || strcmp(name, "penfs") != 0)
        return NULL;
    m = JS_NewCModule(ctx, name, penfs_init);
    if (!m)
        return NULL;
    JS_AddModuleExport(ctx, m, "default");
    JS_AddModuleExport(ctx, m, "readFile");
    JS_AddModuleExport(ctx, m, "writeFile");
    JS_AddModuleExport(ctx, m, "appendFile");
    JS_AddModuleExport(ctx, m, "listDir");
    JS_AddModuleExport(ctx, m, "exists");
    JS_AddModuleExport(ctx, m, "isDir");
    JS_AddModuleExport(ctx, m, "deleteFile");
    JS_AddModuleExport(ctx, m, "mkdir");
    JS_AddModuleExport(ctx, m, "rmdir");
    JS_AddModuleExport(ctx, m, "rename");
    JS_AddModuleExport(ctx, m, "cwd");
    dbg("[penfs] module_load exit ok");
    return m;
}

static int penshell_init(JSContext *ctx, JSModuleDef *m)
{
    dbg("[penfs] penshell_init enter");
    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "exec", NEW_FUNC(f_exec, 2));
    JS_SetModuleExport(ctx, m, "default", obj);
    JS_SetModuleExport(ctx, m, "exec", NEW_FUNC(f_exec, 2));
    dbg("[penfs] penshell_init exit ok");
    return 0;
}

static JSModuleDef *penshell_module_load(JSContext *ctx, const char *name)
{
    JSModuleDef *m;
    dbg("[penfs] penshell_module_load enter");
    if (!name || strcmp(name, "penshell") != 0)
        return NULL;
    m = JS_NewCModule(ctx, name, penshell_init);
    if (!m)
        return NULL;
    JS_AddModuleExport(ctx, m, "default");
    JS_AddModuleExport(ctx, m, "exec");
    dbg("[penfs] penshell_module_load exit ok");
    return m;
}

/* 运行时 dlsym 的入口：dlopen 后由 libfalcon 调用 */
void custom_init_jsapis(void)
{
    dbg("[penfs] custom_init_jsapis enter");
    registerCModuleLoader("penfs", penfs_module_load);
    dbg("[penfs] penfs loader registered");
    registerCModuleLoader("penshell", penshell_module_load);
    dbg("[penfs] custom_init_jsapis exit ok");
}
