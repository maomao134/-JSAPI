# penfs — 词典笔文件操作 + 终端操作 JSAPI

给有道词典笔 A7 Pro（PenOS / falcon miniapp 框架）用的自定义原生 JSAPI，
在 miniapp 的 JS 里直接读写文件、列目录、执行 shell 命令。

- 原生库：`libs/libjsapi_penfs.so`（零链接依赖，纯 C，编译见下）
- JS 模块：`penfs`（文件操作）、`penshell`（终端操作）
- 演示应用：本仓库本身就是一个 miniapp（appid `8002048151526021`），
  左栏文件浏览器 / 终端命令面板，右侧输出区

## 一、在 JS 里使用

```js
import penfs from 'penfs'      // 默认导出是一个对象，包含全部 11 个方法
import penshell from 'penshell' // 默认导出是一个对象，包含 exec

// 也可以用命名导入：
import { readFile, listDir } from 'penfs'
import { exec } from 'penshell'
```

所有方法都**不抛异常**，统一返回结果对象（用 `Promise` 包裹见下文封装示例）：

| 方法 | 参数 | 成功返回 | 失败返回 |
|---|---|---|---|
| `penfs.readFile(path, maxLen?)` | 路径；最大字节（默认 1MB） | `{ok:true, data:string, size}` | `{ok:false, code, message}` |
| `penfs.writeFile(path, data, mode?)` | mode `'w'` 覆盖（默认）/`'a'` 追加 | `{ok:true, size}` | 同上 |
| `penfs.appendFile(path, data)` | — | `{ok:true, size}` | 同上 |
| `penfs.listDir(path)` | 目录；条目为目录时名字带 `/` 后缀 | `{ok:true, entries:string[]}` | 同上 |
| `penfs.exists(path)` | — | `{ok:true, exists:boolean}` | 同上 |
| `penfs.isDir(path)` | — | `{ok:true, isDir:boolean}` | 同上 |
| `penfs.deleteFile(path)` | 仅文件，目录用 `rmdir` | `{ok:true}` | 同上 |
| `penfs.mkdir(path)` | — | `{ok:true}` | 同上 |
| `penfs.rmdir(path)` | 仅空目录 | `{ok:true}` | 同上 |
| `penfs.rename(oldPath, newPath)` | — | `{ok:true}` | 同上 |
| `penfs.cwd()` | — | `{ok:true, path}` | 同上 |
| `penshell.exec(cmd, timeoutMs?)` | 命令；超时（默认 10s，0=不限） | `{ok:true, code, killed, stdout, stderr}` | `{ok:false, code, message}` |

`exec` 返回说明：`code` 为退出码（0-255），非正常退出为 `-1`；
`killed` 为 `true` 表示超时被杀（SIGKILL）；`stdout`/`stderr` 各自上限 64KB。

### 常用路径

- 用户可见目录：`/userdisk`、`/userdisk/Favorite`（U 盘模式下可见）
- miniapp 私有数据：`/userdisk/secondary/miniapp/data/mini_app/pkg/<appid>/data/`

### Promise 封装示例

```js
import penfs from 'penfs'

function prom(res) {
  return new Promise((resolve, reject) => {
    if (res.ok) { resolve(res) } else { reject(new Error(res.message || res.code)) }
  })
}

// 读文件
const { data, size } = await prom(penfs.readFile('/userdisk/Favorite/hello.txt'))

// 写文件（中文以 UTF-8 写入）
await prom(penfs.writeFile('/userdisk/Favorite/hello.txt', '你好，词典笔！\n'))

// 追加
await prom(penfs.appendFile('/userdisk/Favorite/hello.txt', '第二行\n'))

// 列目录（目录项带 / 后缀）
const { entries } = await prom(penfs.listDir('/userdisk'))
// => ['Favorite/', 'penfs-demo.txt', ...]

// 判断/创建/删除
await prom(penfs.mkdir('/userdisk/Favorite/新建目录'))
await prom(penfs.rename('/userdisk/Favorite/a.txt', '/userdisk/Favorite/b.txt'))
await prom(penfs.deleteFile('/userdisk/Favorite/b.txt'))
await prom(penfs.rmdir('/userdisk/Favorite/新建目录'))
```

### 执行 shell 命令示例

```js
import penshell from 'penshell'

const r = penshell.exec('df -h')
// r = { ok:true, code:0, killed:false,
//       stdout:'Filesystem  ...\n', stderr:'' }

const r2 = penshell.exec('sleep 5; echo done', 1000)
// r2 = { ok:true, code:-1, killed:true, stdout:'', stderr:'' }

const r3 = penshell.exec('cat /proc/meminfo | head -4')
// 管道由 /bin/sh -c 解析，支持完整 shell 语法
```

命令由 `/bin/sh -c <cmd>` 执行，支持管道、重定向、变量等完整 shell 语法。
`exec` 会**同步阻塞 JS 线程**直到命令结束或超时，所以页面建议给足够短的命令
或小超时值，避免卡 UI。

## 二、在你的项目里接入

1. **拷贝** `libs/libjsapi_penfs.so` 到你项目的 `libs/` 目录
   （打包时 aiot-cli 会自动把 `libs/` 打进 amr，设备安装后会自动重命名为
   `libjsapi_penfs_<hash>.so` 并加载）
2. 你的 `package.json` 的 `quickjs.version` 需为 `20200705`（设备宿主版本）
3. 在页面 JS 里 `import penfs from 'penfs'` 即可。构建时的
   “未找到模块 penfs,penshell” 警告可忽略（运行时由设备解析）
4. **页面生命周期钩子**（onShow/onLoad/onNewOptions 等）必须放在 Vue 组件的
   `methods` 里，并且在 `app.js` 的 `onLaunch` 里调用
   `$falcon.useDefaultBasePageClass(BasePage)`（BasePage 实现见
   `src/base-page.js`），框架才会把钩子转发到 Vue 实例；首次渲染建议用
   Vue 原生的 `mounted`（实例化即触发，不依赖前后台切换）

## 三、从源码构建 .so（Windows，无需 WSL/Docker）

要求：Arm GNU Toolchain（arm-none-eabi-gcc，Windows 版），本项目用的
`arm-gnu-toolchain-13.3.rel1-mingw-w64-i686-arm-none-eabi`。

```bat
cd native
build-native.bat        rem 编译 penjsapi.c -> ..\libs\libjsapi_penfs.so
```

要点（全部封装在 build-native.bat）：

```
arm-none-eabi-gcc -marm -mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard \
  -fPIC -fno-unwind-tables -O2 -Wall -I . -c penjsapi.c -o penjsapi.o
arm-none-eabi-gcc -shared -nostdlib -o ..\libs\libjsapi_penfs.so penjsapi.o
```

- `-nostdlib`：不链接任何 libc。所有符号（QuickJS API、registerCModuleLoader、
  open/read/fork/poll 等 POSIX 函数）在运行时由设备宿主进程全局符号表解析，
  必须全部存在，否则 dlopen 失败或调用时崩溃
- `-marm -mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard`：与设备 ABI
  （ARMv7l EABI hard-float）严格一致
- **不要使用 `JS_NewCFunction`**：SDK 头文件里它是 static inline，展开为对
  `JS_NewCFunction2` 的调用。设备上有两个宿主库导出 JS 符号：
  `libquickjs.so`（SDK 官方 QuickJS，导出 `JS_NewCFunction2` 和
  `JS_NewCFunctionData`）和 `libfalcon.so`（内部 QuickJS 分支，只导出
  `JS_NewCFunction2` 等 14 个符号、没有 `JS_NewCFunctionData`）。
  用 `JS_NewCFunction2` 可能绑定到 libfalcon 的分支实现（ABI 未验证）；
  用 `JS_NewCFunctionData` 则唯一绑定 libquickjs.so 的官方实现（真机已验证）。
  一律使用 `JS_NewCFunctionData`（见 penjsapi.c 的 `NEW_FUNC` 宏）
- 宿主不可用的符号（不要用）：`__JS_DupValue`、`__JS_NewFloat64`
  （`JS_NewInt32`/`JS_NewBool`/`JS_IsUndefined` 是 static inline，可安全使用）

## 四、构建 / 安装演示应用

```bat
build.bat      rem 检查 .so 后跑 aiot-cli -c -q -p 生成 .amr
install.bat    rem adb push + miniapp_cli install + start
```

要求 Node 18（框架与 Node 22+ 不兼容）。adb push 在 Git Bash 下注意
`MSYS_NO_PATHCONV=1`（否则 `/userdisk/...` 会被路径转换成 Git 安装目录）。

真机验证：

```
adb shell "miniapp_cli start 8002048151526021"
adb shell "miniapp_cli capture /userdisk/Favorite/shot.png"   # 截图
adb shell "cat /userdata/applog/DictPen_*.log"                # 设备日志（含 .so 的 dbg 追踪输出）
adb shell "cat /userdisk/secondary/miniapp/data/mini_app/pkg/8002048151526021/data/sharedpreferences/preferences.json"
```

## 五、限制与注意事项

- **同步阻塞**：`exec` 会阻塞当前 JS 线程（页面 UI 会卡住），命令要短或超时小。
  耗时任务建议用短命令 + 结果写文件 + 页面轮询的方式
- **输出上限**：stdout/stderr 各截断到 64KB；`readFile` 默认 1MB
- **文本按 UTF-8**：读写均为字节流，JS 字符串按 UTF-8 编解码，
  二进制文件会损坏，请勿读写非文本文件
- **无沙箱**：与系统内置模块一样，JSAPI 以宿主进程权限运行（root），
  能访问整个文件系统、执行任意命令 —— 只在你自己信任的应用里接入
- **超时精度为秒级**（`time()` 计时），短超时（<2s）误差较大
- `listDir` 基于 `glob`，文件名含特殊字符（`[` `?` `*` 等）可能被通配
- 删除、覆盖文件不可恢复，演示 UI 里长按文件条目即删除，请小心使用

## 六、目录结构

```
penfs/
├── native/
│   ├── penjsapi.c          # 原生库源码（penfs + penshell 两个 C 模块）
│   ├── build-native.bat    # 交叉编译脚本
│   └── quickjs/quickjs.h   # 从 iot-miniapp-sdk 拷贝的头文件（与设备宿主一致）
├── libs/
│   └── libjsapi_penfs.so   # 编译产物
├── src/
│   ├── utils/penapi.js     # JS 封装（Promise 化）
│   ├── utils/dbg.js        # storage 调试日志（真机 console.log 不可见）
│   └── pages/index/index.vue # 演示 UI
├── build.bat / install.bat
└── package.json            # appid 8002048151526021, quickjs 20200705
```

## 七、实现原理（简要）

1. `.so` 导出 `custom_init_jsapis()`；设备在应用启动时 `dlopen` 该 .so 并
   `dlsym` 这个入口调用它
2. 入口里调用 `registerCModuleLoader("penfs", loader)` 注册 QuickJS 模块加载器
3. 页面 JS `import penfs from 'penfs'` 触发 loader：`JS_NewCModule` +
   `JS_AddModuleExport`（加载时）+ `JS_SetModuleExport`（模块初始化时）
4. `penshell.exec` 用 fork + 双管道 + poll 实现，超时 SIGKILL，
   退出码按 waitpid status 解析
5. **宿主 miniapp 进程把 SIGCHLD 设为 SIG_IGN**（真机 /proc/<pid>/status
   已验证），直接 waitpid 会 ECHILD 拿不到退出码 —— exec 时先
   `signal(SIGCHLD, SIG_DFL)`，waitpid 后再恢复原设置
6. .so 里的 `dbg()` 追踪日志直接 `write(1/2, ...)`，宿主会捕获进
   设备日志（/userdata/applog/DictPen_*.log），调试新模块时很有用
