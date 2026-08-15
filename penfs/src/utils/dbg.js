/**
 * 设备调试日志：写 storage 的 penfs-dbg 键（真机上 console.log 无处可查）。
 * 读取：adb shell "cat /userdisk/secondary/miniapp/data/mini_app/pkg/<appid>/data/sharedpreferences/preferences.json"
 */
const DBG_KEY = 'penfs-dbg'
const dbgBuffer = []

export function dbgLog(msg) {
  try {
    dbgBuffer.push({ t: Date.now(), m: String(msg) })
    if (dbgBuffer.length > 300) {
      dbgBuffer.splice(0, dbgBuffer.length - 300)
    }
    $falcon.jsapi.storage.setStorage({ key: DBG_KEY, data: JSON.stringify(dbgBuffer) })
  } catch (err) {
    /* 调试日志失败不影响功能 */
  }
}
