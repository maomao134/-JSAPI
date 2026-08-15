/**
 * 文件终端工具箱 - 应用入口
 * 自定义 JSAPI 演示：penfs（文件操作）+ penshell（终端操作）
 */
import { BasePage } from './base-page.js'

class App extends $falcon.App {
  constructor() {
    super()
  }

  onLaunch(options) {
    super.onLaunch(options)
    // 设置页面基类：让 .vue 页面的 onShow/onLoad 等生命周期回调被转发到
    // Vue 实例（this.$root.onShow()）。不设置则页面钩子不会触发。
    $falcon.useDefaultBasePageClass(BasePage)
  }

  onShow() {
    super.onShow()
  }

  onHide() {
    super.onHide()
  }

  onDestroy() {
    super.onDestroy()
  }
}

export default App
