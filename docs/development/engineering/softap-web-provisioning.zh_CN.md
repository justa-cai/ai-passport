<p align="right">
  <a href="softap-web-provisioning.md">English</a> · <strong>简体中文</strong>
</p>

# SoftAP 网页配网

应用需要从用户处拿到 Wi-Fi 凭据、而蓝牙配网(见
[wifi-provisioning.md](wifi-provisioning.md))不适用时(例如为了内存关掉了
蓝牙)使用本模式。设备开一个开放热点,手机连入后在网页提交凭据,设备
先测试再落盘。参考实现在 `feature/music-player` 分支的
`main/music_wifi.c` + `main/music_httpd.c`。

## 生命周期

决策在可主机测试的状态机 `main/music_net_sm.c`,执行在网络任务里。
状态:`AP_WAIT`(热点开,配网页服务中)→ `STA_TESTING`(候选凭据连接
中)→ `ONLINE`(拿到 IP)。连续失败三次或测试超时回落 `AP_WAIT`;启动
时已有凭据则完全跳过热点。

**必须**:表单提交的凭据在 STA 测试拿到 IP 之前只是*候选*;那一刻才
覆盖 NVS 里的已存凭据(命名空间 `musicdl`,键 `ssid`/`pass`)。
**禁止**:连接测试成功前就把表单凭据写进 NVS —— 一次手误会把设备困在
坏凭据的启动循环里。

## HTTP 服务

一个 `esp_http_server` 实例按模式整体换 handler 集;配网页与遥控页
从不共存。AP 模式(默认 `WIFI_AP` netif 上的 `192.168.4.1`):
`GET /` 表单页、`POST /api/wifi`(urlencoded,请求体上限 512 字节,拒绝
`%00`)、`GET /api/status` 供页面轮询直到 `online`。POST 的响应可能被
拆断 —— AP→STA 切换会在应答中途断掉手机侧连接;页面要以轮询、而不是
POST 应答,来判定结果。

表单解析不允许就地改请求体:把某个字段值后面的 `&` 覆写为终止符会毁
掉其后所有字段(这个 bug 上过一次线;修复后的 `form_field` 把解码值
拷贝输出,请求体保持 `const`)。

## 安全边界

热点开放(页面本身不含敏感数据),遥控页在局域网内无鉴权。两者都是
写明的 v1 取舍:局域网内任何人都能控制播放。需要更强隔离的部署可以在
设备屏幕上显示 PIN 并给页面加校验,作为后续项。

## 验证

主机测试覆盖状态机全转移表,包括候选不覆盖已存、回落热点
(`tests/test_music_net_sm.c`)。模拟器验证覆盖全流程:热点起 → 页面
就绪 → 真实 POST → STA 加入模拟器 AP → 拿到 IP → 凭据落盘 → 服务切到
遥控页。
