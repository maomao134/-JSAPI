<template>
  <div class="root">
    <div class="left">
      <div class="tabs">
        <div class="tab" :class="mode === 'fs' ? 'tab on' : 'tab'" @click="switchMode('fs')">
          <text class="tabtxt">文件</text>
        </div>
        <div class="tab" :class="mode === 'sh' ? 'tab on' : 'tab'" @click="switchMode('sh')">
          <text class="tabtxt">终端</text>
        </div>
      </div>
      <div class="fspath" v-if="mode === 'fs'">
        <div class="upbtn" @click="goUp"><text class="btntxt">..</text></div>
        <div class="pathbox"><text class="pathtxt">{{ path }}</text></div>
        <div class="upbtn" @click="refresh"><text class="btntxt">刷</text></div>
        <div class="upbtn" @click="writeDemo"><text class="btntxt">+</text></div>
      </div>
      <scroller class="fslist" v-if="mode === 'fs'">
        <div class="entry" v-for="(e, i) in entries" :key="i" @click="onEntry(e)" @longpress="onLongEntry(e)">
          <text class="entrytxt">{{ e }}</text>
        </div>
        <div class="entry" v-if="entries.length === 0"><text class="entrytxt dim">(空目录)</text></div>
      </scroller>
      <scroller class="cmdlist" v-else>
        <div class="cmd" v-for="(c, i) in presets" :key="i" @click="runPreset(i)">
          <text class="cmdtxt">{{ c[0] }}</text>
        </div>
      </scroller>
    </div>
    <div class="right">
      <div class="status"><text class="statustxt">{{ status }}</text></div>
      <scroller class="out">
        <text class="outtxt">{{ output }}</text>
      </scroller>
    </div>
  </div>
</template>

<script>
import { fs, shell } from '../../utils/penapi.js'
import { dbgLog } from '../../utils/dbg.js'

function joinPath(base, name) {
  if (base === '/') {
    return '/' + name
  }
  return base + '/' + name
}

export default {
  data() {
    return {
      mode: 'fs',
      path: '/userdisk',
      entries: [],
      output: '',
      status: '就绪',
      loaded: false,
      presets: [
        ['系统信息', 'uname -a'],
        ['主机名', 'cat /etc/hostname'],
        ['磁盘占用', 'df -h'],
        ['内存', 'cat /proc/meminfo | head -4'],
        ['进程', 'ps | head -12'],
        ['userdisk 目录', 'ls -la /userdisk'],
        ['写测试文件', 'echo hello-penfs > /userdisk/Favorite/penfs-test.txt && cat /userdisk/Favorite/penfs-test.txt'],
        ['sleep 3 超时测试', 'sleep 3; echo done']
      ]
    }
  },
  mounted() {
    /* Vue 原生钩子：页面实例化即触发，保证首次渲染就有目录列表 */
    dbgLog('index mounted')
    if (!this.loaded) {
      this.loaded = true
      this.refresh()
    }
  },
  methods: {
    /* 注意：框架从实例上查找生命周期回调，onShow 必须放 methods 里 */
    onShow() {
      dbgLog('index onShow')
      if (!this.loaded) {
        this.loaded = true
        this.refresh()
      }
    },
    switchMode(m) {
      this.mode = m
      if (m === 'sh') {
        this.status = '终端模式：点左侧命令执行'
      } else {
        this.refresh()
      }
    },
    refresh() {
      dbgLog('refresh path=' + this.path)
      fs.listDir(this.path).then((r) => {
        this.entries = r.entries
        this.status = this.path + ' 共 ' + this.entries.length + ' 项'
        dbgLog('listDir ok n=' + this.entries.length)
      }).catch((e) => {
        this.entries = []
        this.status = '读取失败: ' + e.message
        dbgLog('listDir fail ' + e.message)
      })
    },
    goUp() {
      if (this.path === '/') {
        return
      }
      const p = this.path.replace(/\/+$/, '')
      const i = p.lastIndexOf('/')
      this.path = i <= 0 ? '/' : p.slice(0, i)
      this.refresh()
    },
    onEntry(e) {
      if (e.endsWith('/')) {
        this.path = joinPath(this.path, e.slice(0, -1))
        this.refresh()
        return
      }
      const full = joinPath(this.path, e)
      fs.readFile(full, 4096).then((r) => {
        this.output = '--- ' + full + ' (' + r.size + ' 字节) ---\n' + r.data
        this.status = full + '  ' + r.size + ' 字节'
        dbgLog('readFile ok ' + full + ' size=' + r.size)
      }).catch((err) => {
        this.output = '读取失败: ' + err.message
        this.status = '读取失败'
        dbgLog('readFile fail ' + err.message)
      })
    },
    onLongEntry(e) {
      if (e.endsWith('/')) {
        this.status = '目录请用 rmdir() 删除'
        return
      }
      fs.deleteFile(joinPath(this.path, e)).then(() => {
        this.status = '已删除 ' + e
        dbgLog('deleteFile ok ' + e)
        this.refresh()
      }).catch((err) => {
        this.status = '删除失败: ' + err.message
        dbgLog('deleteFile fail ' + err.message)
      })
    },
    writeDemo() {
      const f = joinPath(this.path, 'penfs-demo.txt')
      fs.writeFile(f, '你好，词典笔！\nhello penfs ' + Date.now() + '\n').then(() => {
        this.status = '已写入 ' + f
        dbgLog('writeFile ok ' + f)
        this.refresh()
      }).catch((err) => {
        this.status = '写入失败: ' + err.message
        dbgLog('writeFile fail ' + err.message)
      })
    },
    runPreset(i) {
      const c = this.presets[i]
      this.status = '执行中: ' + c[1]
      const t0 = Date.now()
      shell.exec(c[1], 8000).then((r) => {
        const ms = Date.now() - t0
        const head = '$ ' + c[1] + '  [exit=' + r.code + ' ' + ms + 'ms' + (r.killed ? ' 超时' : '') + ']'
        this.output = head + '\n' + r.stdout + (r.stderr ? '\n[stderr]\n' + r.stderr : '')
        this.status = 'exit=' + r.code + '  ' + ms + 'ms'
        dbgLog('exec ok ' + c[1] + ' code=' + r.code + ' out=' + r.stdout.length)
      }).catch((err) => {
        this.output = '执行失败: ' + err.message
        this.status = '执行失败'
        dbgLog('exec fail ' + err.message)
      })
    }
  }
}
</script>

<style scoped>
.root {
  width: 1024px;
  height: 240px;
  background-color: #0a0f1e;
  flex-direction: row;
}
.left {
  width: 330px;
  height: 240px;
  background-color: #16213e;
}
.tabs {
  height: 44px;
  flex-direction: row;
}
.tab {
  flex: 1;
  height: 44px;
  justify-content: center;
  align-items: center;
}
.on {
  background-color: #0f3460;
}
.tabtxt {
  font-size: 18px;
  color: #e8f0fe;
}
.fspath {
  height: 40px;
  flex-direction: row;
  align-items: center;
}
.upbtn {
  width: 36px;
  height: 32px;
  margin-left: 4px;
  background-color: #0f3460;
  border-radius: 6px;
  justify-content: center;
  align-items: center;
}
.btntxt {
  font-size: 16px;
  color: #e8f0fe;
}
.pathbox {
  flex: 1;
  height: 32px;
  margin-left: 6px;
  margin-right: 4px;
  justify-content: center;
}
.pathtxt {
  font-size: 13px;
  color: #9fb4d8;
}
.fslist {
  width: 330px;
  height: 156px;
}
.entry {
  height: 32px;
  padding-left: 10px;
  justify-content: center;
}
.entrytxt {
  font-size: 15px;
  color: #c5d3e8;
}
.dim {
  color: #5a6a8c;
}
.cmdlist {
  width: 330px;
  height: 196px;
}
.cmd {
  height: 36px;
  margin-left: 8px;
  margin-right: 8px;
  margin-top: 4px;
  background-color: #0f3460;
  border-radius: 6px;
  justify-content: center;
  align-items: center;
}
.cmdtxt {
  font-size: 14px;
  color: #e8f0fe;
}
.right {
  flex: 1;
  height: 240px;
  padding: 8px;
}
.status {
  height: 28px;
  justify-content: center;
}
.statustxt {
  font-size: 14px;
  color: #f5c542;
}
.out {
  width: 678px;
  height: 188px;
  background-color: #0d1b2a;
  border-radius: 8px;
  padding: 8px;
}
.outtxt {
  font-size: 13px;
  line-height: 18px;
  color: #c5d3e8;
}
</style>
