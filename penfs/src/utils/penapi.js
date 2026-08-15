/**
 * penapi - 词典笔自定义 JSAPI 的 JS 封装层
 *
 * 底层原生模块由应用包内 libs/libjsapi_penfs.so 提供，
 * 运行时注册为 QuickJS C 模块 'penfs' 与 'penshell'，直接 import：
 *   import penfs from 'penfs'
 *   import penshell from 'penshell'
 *
 * 原生方法为同步调用，返回结果对象 {ok:true, ...} 或 {ok:false, code, message}；
 * 本封装统一转成 Promise，失败时 reject(Error(message))。
 */
import penfs from 'penfs'
import penshell from 'penshell'

function prom(res) {
  if (res && res.ok) {
    return Promise.resolve(res)
  }
  return Promise.reject(new Error(res ? (res.message || res.code || '调用失败') : 'jsapi 不可用'))
}

/* ==================== 文件操作 penfs ==================== */
export const fs = {
  /** 读文件（UTF-8 文本）。maxLen 可选，默认 1MB，超出截断 */
  readFile(path, maxLen) {
    return prom(penfs.readFile(path, maxLen === undefined ? undefined : maxLen))
  },
  /** 覆盖写文件，返回 {ok, size} */
  writeFile(path, data) {
    return prom(penfs.writeFile(path, data, 'w'))
  },
  /** 追加写文件，返回 {ok, size} */
  appendFile(path, data) {
    return prom(penfs.appendFile(path, data))
  },
  /** 列目录，返回 {ok, entries:['a/', 'b.txt']}，目录名带 / 后缀 */
  listDir(path) {
    return prom(penfs.listDir(path))
  },
  /** 是否存在 */
  async exists(path) {
    const r = await prom(penfs.exists(path))
    return r.exists
  },
  /** 是否目录 */
  async isDir(path) {
    const r = await prom(penfs.isDir(path))
    return r.isDir
  },
  /** 删除文件（目录请用 rmdir） */
  deleteFile(path) {
    return prom(penfs.deleteFile(path))
  },
  /** 创建目录 */
  mkdir(path) {
    return prom(penfs.mkdir(path))
  },
  /** 删除空目录 */
  rmdir(path) {
    return prom(penfs.rmdir(path))
  },
  /** 重命名/移动 */
  rename(oldPath, newPath) {
    return prom(penfs.rename(oldPath, newPath))
  },
  /** 当前工作目录 */
  async cwd() {
    const r = await prom(penfs.cwd())
    return r.path
  }
}

/* ==================== 终端操作 penshell ==================== */
export const shell = {
  /**
   * 执行 shell 命令（经 /bin/sh -c 执行）
   * @param {string} cmd 命令字符串
   * @param {number} timeoutMs 超时毫秒数，默认 10000，0 表示不限时（秒级精度）
   * @returns {Promise<{ok, code, killed, stdout, stderr}>}
   *   code:   退出码 0-255，异常退出为 -1
   *   killed: 是否因超时被杀
   *   stdout/stderr: 各最多 64KB，超出截断
   */
  exec(cmd, timeoutMs) {
    return prom(penshell.exec(cmd, timeoutMs === undefined ? undefined : timeoutMs))
  }
}

export default { fs, shell }
