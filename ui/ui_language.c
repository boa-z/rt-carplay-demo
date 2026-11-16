/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "ui_language.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

LV_FONT_DECLARE(carplay_cjk_16);
LV_FONT_DECLARE(carplay_cjk_24);

static bool chinese;
static const struct
{
    const char *en, *zh;
} words[] = {
    { "Settings", "设置" },
    { "SETTINGS", "设置" },
    { "Display", "显示" },
    { "Display mode", "显示模式" },
    { "CARPLAY DISPLAY", "显示模式" },
    { "Full screen", "全屏" },
    { "Fusion", "融合" },
    { "Local instruments", "本地仪表" },
    { "Full screen 1024 x 600", "全屏 1024 × 600" },
    { "Video + meters 768 x 600", "融合 768 × 600" },
    { "Apply", "应用" },
    { "Done", "完成" },
    { "Back", "返回" },
    { "Back to settings", "返回设置" },
    { "Home", "主页" },
    { "Resume CarPlay", "返回 CarPlay" },
    { "Vehicle instruments", "车辆仪表" },
    { "Devices", "设备" },
    { "Retry", "重试" },
    { "Diagnostics", "诊断" },
    { "Audio", "音频" },
    { "Audio unavailable", "音频不可用" },
    { "Language", "语言" },
    { "English", "English" },
    { "中文", "中文" },
    { "Reconnecting...", "正在重连…" },
    { "Selected", "已选择" },
    { "Close device/audio tools first.", "请先关闭设备或音频工具" },
    { "1024 x 600", "1024 × 600" },
    { "768 x 600 + meters", "768 × 600 + 仪表" },
    { "Instruments only", "仅显示仪表" },
    { "USB and wireless", "有线和无线均适用" },
    { "Resolution changes reconnect CarPlay.", "切换分辨率时会重连" },
    { "Settings reset at reboot.", "重启后恢复默认设置" },
    { "Wireless discovery: ON", "无线发现：开" },
    { "Wireless discovery: OFF", "无线发现：关" },
    { "Microphone: MUTED", "麦克风：静音" },
    { "Microphone: ENABLED", "麦克风：开" },
    { "Microphone not enabled", "麦克风未启用" },
    { "Microphone mute setting applied.", "麦克风已设置" },
    { "Connection setting applied. Settings reset at reboot.", "连接设置已更新" },
    { "Wait until the connection is idle, then try again.", "请在连接空闲时操作" },
    { "FIRMWARE & UPDATES", "固件更新" },
    { "Installed firmware", "当前固件" },
    { "Check for updates", "检查更新" },
    { "Install update", "安装更新" },
    { "Not checked yet. Connect Ethernet, then check for updates.", "请连接以太网后检查更新" },
    { "Checking the release server...", "正在检查更新…" },
    { "SYSTEM", "系统" },
    { "CONNECTIONS", "连接" },
    { "01  Discover your phone", "01  发现" },
    { "02  Verify and connect", "02  连接" },
    { "03  Open CarPlay", "03  CarPlay" },
    { "Ready when you are.", "等待连接" },
    { "Pair your iPhone.", "配对 iPhone" },
    { "Trust this display.", "请确认信任" },
    { "Making the connection.", "正在连接" },
    { "Opening CarPlay.", "正在打开 CarPlay" },
    { "Check display handoff.", "等待画面" },
    { "Getting ready again.", "正在断开" },
    { "Let's reconnect.", "正在重连" },
    { "Your drive, connected.", "CarPlay" },
    { "Connect USB or pair Bluetooth.", "连接 USB 或蓝牙配对" },
    { "Connect your iPhone with USB.", "通过 USB 连接 iPhone" },
    { "Confirm pairing on your iPhone.", "请在 iPhone 上确认配对" },
    { "Unlock iPhone and tap Trust.", "解锁 iPhone 并确认信任" },
    { "Joining Wi-Fi...", "正在连接 Wi-Fi…" },
    { "Preparing USB...", "正在准备 USB…" },
    { "Waiting for video...", "等待视频…" },
    { "See Diagnostics.", "请查看诊断" },
    { "Closing session...", "正在关闭连接…" },
    { "Pairing storage error.", "配对保存失败" },
    { "Retrying automatically...", "正在自动重试…" },
    { "Starting...", "正在启动…" },
    { "AUTO CONNECT", "自动连接" },
    { "VERIFYING", "正在验证" },
    { "CONNECTING", "正在连接" },
    { "ALMOST READY", "等待视频" },
    { "DISPLAY CHECK", "等待画面" },
    { "RETURNING", "正在返回" },
    { "RECONNECTING", "正在重连" },
    { "LAUNCHER 02", "CarPlay" },
    { "Starting USB service", "USB 启动中" },
    { "Phone detected / queued", "设备等待中" },
    { "Phone detected", "已发现设备" },
    { "Ready for a cable", "等待 USB" },
    { "Paused in Settings", "已暂停" },
    { "Standby after USB session", "待机" },
    { "Phone connected", "设备已连接" },
    { "Discoverable as rt-carplay", "可发现：rt-carplay" },
    { "Starting Bluetooth / Wi-Fi", "蓝牙 / Wi-Fi 启动中" },
    { "Session connected", "已连接" },
    { "One phone at a time", "单设备连接" },
    { "Waiting before automatic retry", "等待重试" },
    { "Automatic discovery is on", "自动发现" },
    { "WIRED USB\n%s", "有线 USB\n%s" },
    { "WIRELESS\n%s", "无线\n%s" },
    { "Bluetooth: rt-carplay  |  CarPlay PIN: %s\nWi-Fi: %s", "蓝牙 rt-carplay  |  PIN %s\nWi-Fi：%s" },
    { "preparing", "准备中" },
    { "paused", "已暂停" },
    { "VEHICLE", "车辆" },
    { "SIMULATED DATA", "模拟数据" },
    { "VEHICLE PROVIDER", "车辆数据" },
    { "DATA UNAVAILABLE", "无车辆数据" },
    { "GEAR  %s", "挡位  %s" },
    { "SOC  %u%%", "电量  %u%%" },
    { "GEAR  --", "挡位  --" },
    { "SOC  --", "电量  --" },
    { "CONNECTED", "已连接" },
    { "CLOSING", "正在关闭" },
    { "CONNECTION ERROR", "连接异常" },
    { "WAITING FOR PHONE", "等待设备" },
    { "WIRELESS", "无线" },
    { "FULLSCREEN", "全屏" },
    { "FUSION", "融合" },
    { "LOCAL", "本地仪表" },
    { "Switching...", "正在切换…" },
    { "CONNECTION DIAGNOSTICS", "连接诊断" },
    { "VEHICLE NETWORK", "网络" },
    { "Waiting for a status snapshot...", "等待状态…" },
    { "Waiting for Ethernet status...", "等待以太网状态…" },
    { "Check now", "检查" },
    { "Unavailable", "不可用" },
    { "No adapter", "无网卡" },
    { "Cable disconnected", "未连接网线" },
    { "Getting address", "获取地址中" },
    { "No gateway", "无网关" },
    { "Checking...", "检查中…" },
    { "Not checked", "未检查" },
    { "Internet verified", "网络正常" },
    { "Check expired", "检查已过期" },
    { "Check failed", "检查失败" },
    { "Ethernet: %s", "以太网：%s" },
    { "Ethernet: starting...", "以太网启动中…" },
    { "Never", "无" },
    { "Detected", "已发现" },
    { "Not detected", "未发现" },
    { "Up", "已连接" },
    { "Down", "未连接" },
    { "Running", "运行中" },
    { "Passed", "通过" },
    { "Failed / expired", "失败 / 已过期" },
    { "Not run", "未执行" },
    { "Active", "开启" },
    { "Inactive", "关闭" },
    { "SAVED DEVICES", "已保存设备" },
    { "Refresh", "刷新" },
    { "Reading pairing files...", "读取中…" },
    { "Cancel", "取消" },
    { "Forget record", "删除记录" },
    { "Forget Bluetooth phones", "清除蓝牙设备" },
    { "Forgetting selected record and restarting receiver...", "正在清除并重启连接…" },
    { "Numbers match / Allow", "确认配对" },
    { "Cancel pairing", "取消配对" },
    { "Allow Bluetooth pairing?", "允许蓝牙配对？" },
    { "%06u\n\nConfirm this number on your iPhone.", "%06u\n\n请在 iPhone 确认此号码" },
    { "%06u\n\nEnter on your iPhone.", "%06u\n\n请在 iPhone 输入此号码" },
    { "SPEAKER & MICROPHONE TEST", "音频测试" },
    { "PCM tone", "PCM 声音" },
    { "PCM silence", "PCM 静音" },
    { "AAC tone", "AAC 声音" },
    { "AAC fixture", "AAC 声音" },
    { "Mic meter", "麦克风电平" },
    { "Mic level", "麦克风电平" },
    { "Mic loopback", "麦克风回放" },
    { "Both", "双声道" },
    { "Left", "左声道" },
    { "Right", "右声道" },
    { "Volume -", "音量 -" },
    { "Volume +", "音量 +" },
    { "Mute / unmute", "静音 / 取消" },
    { "Preparing audio tests...", "音频准备中…" },
    { "Input L", "输入左" },
    { "Input R", "输入右" },
    { "Start / restart", "开始" },
    { "Stop", "停止" },
    { "Ready", "就绪" },
    { "Stopping...", "停止中…" },
    { "MUTED", "静音" },
    { "Selected: %s | %u Hz | %s | Volume %u%% %s", "%s | %u Hz | %s | 音量 %u%% %s" },
    { "%s | Error %d\nInput L/R: %u / %u", "%s | 错误 %d\n输入 左/右：%u / %u" },
    { "Forget %s?\n\n%s", "删除 %s？\n\n%s" },
    { "All Bluetooth phones will need to pair again.", "所有蓝牙设备需重新配对" },
    { "All CarPlay phones will need to pair again.", "所有 CarPlay 设备需重新配对" },
    { "This iPhone may ask you to tap Trust again.", "此 iPhone 可能需要重新确认信任" },
    { "All Bluetooth phones", "所有蓝牙设备" },
    { "CarPlay trusted phones", "CarPlay 信任设备" },
    { "Wired iPhone  /  %.8s", "有线 iPhone / %.8s" },
    { "Saved phone record", "已保存设备" },
    { "Saved devices: %u%s | Error %d", "设备：%u%s | 错误 %d" },
    { "Update service busy.", "更新服务忙" },
    { "Update changed or busy. Check again.", "更新已变化或服务忙，请重试" },
    { "Install %s?\n\nCurrent: %s (%.8s)\n\nDisconnect CarPlay. Keep power connected.\nRestart after installation.",
      "安装 %s？\n\n当前：%s (%.8s)\n\n请断开 CarPlay 并保持供电\n安装后将重启" },
    { "Firmware %s (%.8s) | %s", "固件 %s (%.8s) | %s" },
    { "Up to date", "已是最新" },
    { "Update available", "有可用更新" },
    { "Downloading", "下载中" },
    { "Installing", "安装中" },
    { "Restarting", "重启中" },
    { "Update failed", "更新失败" },
    { "Not checked", "未检查" },
    { "%s\n\nIPv4       %s\nMask      %s\nGateway %s\nDNS       %s\n\nHTTP 204: %s | %s\nCarPlay sharing: %s",
      "%s\n\nIPv4   %s\n掩码   %s\n网关   %s\nDNS   %s\n\nHTTP 204：%s | %s\nCarPlay 网络共享：%s" },
    { "Connection: %s / %s     Error: %d (last %d)\n"
      "USB service: %s    Attached: %s    USB error: %d\n"
      "Wireless: %s    Bluetooth: %s    MFi: %s    Radio error: %d\n"
      "Network: %s    Wi-Fi: %s\n"
      "Transport: %u   Authenticated: %u   Session: %u\n"
      "Video endpoint: %u   Audio endpoint: %u   Input: %u\n"
      "Decoded frames: %u   Drops: %u   Decode errors: %u\n"
      "Display handoff: %s    Screen RX: %u    HID sent/failed: %u/%u\n"
      "Trust cache: %d    Pairing write: %d    Bonds: %u/%u\n"
      "BT HCI/security: %02x/%u    Invite/ACK/retry: %u/%u/%u\n"
      "Recovery cycles: %u    Current retry wait: %u s",
      "连接：%s / %s    错误：%d（上次 %d）\n"
      "USB 服务：%s    设备：%s    错误：%d\n"
      "无线：%s    蓝牙：%s    MFi：%s    错误：%d\n"
      "网络：%s    Wi-Fi：%s\n"
      "传输：%u    认证：%u    会话：%u\n"
      "视频：%u    音频：%u    输入：%u\n"
      "解码帧：%u    丢帧：%u    解码错误：%u\n"
      "显示：%s    接收：%u    HID 发送/失败：%u/%u\n"
      "信任缓存：%d    配对保存：%d    绑定：%u/%u\n"
      "蓝牙 HCI/安全：%02x/%u    邀请/确认/重试：%u/%u/%u\n"
      "恢复次数：%u    等待重试：%u s" },
};

/* Labels, rather than buttons, own translation bindings. Dynamic file names
 * and technical values remain unchanged. Delete events retire bindings. */
static struct binding
{
    lv_obj_t *label;
    const char *key;
    const lv_font_t *font;
} labels[256];

static const char *lookup(const char *english)
{
    for (unsigned i = 0; i < sizeof(words) / sizeof(words[0]); ++i)
        if (!strcmp(english, words[i].en))
            return chinese ? words[i].zh : words[i].en;
    return english;
}
bool carplay_ui_is_chinese(void)
{
    return chinese;
}
const char *carplay_ui_translate(const char *english)
{
    return lookup(english);
}

static void deleted(lv_event_t *event)
{
    struct binding *b = lv_event_get_user_data(event);
    memset(b, 0, sizeof(*b));
}
static struct binding *bind_label(lv_obj_t *label)
{
    struct binding *free_slot = NULL;
    for (unsigned i = 0; i < sizeof(labels) / sizeof(labels[0]); ++i)
    {
        if (labels[i].label == label)
            return &labels[i];
        if (!labels[i].label && !free_slot)
            free_slot = &labels[i];
    }
    if (free_slot)
    {
        free_slot->label = label;
        free_slot->font = lv_obj_get_style_text_font(label, 0);
        lv_obj_add_event_cb(label, deleted, LV_EVENT_DELETE, free_slot);
    }
    return free_slot;
}
static void apply_font(struct binding *b)
{
    if (b)
        lv_obj_set_style_text_font(b->label, chinese ? (b->font->line_height >= 24 ? &carplay_cjk_24 : &carplay_cjk_16) : b->font, 0);
}
void carplay_ui_set_text(lv_obj_t *label, const char *english)
{
    struct binding *b = bind_label(label);
    if (b)
    {
        b->key = NULL;
        for (unsigned i = 0; i < sizeof(words) / sizeof(words[0]); ++i)
            if (!strcmp(english, words[i].en))
            {
                b->key = words[i].en;
                break;
            }
        if (!chinese)
            b->font = lv_obj_get_style_text_font(label, 0);
        if (!strcmp(english, "中文"))
            b->font = &carplay_cjk_16;
        apply_font(b);
    }
    lv_label_set_text(label, lookup(english));
}
void carplay_ui_set_text_fmt(lv_obj_t *label, const char *format, ...)
{
    struct binding *b = bind_label(label);
    if (b)
    {
        b->key = NULL;
        if (!chinese)
            b->font = lv_obj_get_style_text_font(label, 0);
        apply_font(b);
    }
    va_list args, copy;
    va_start(args, format);
    va_copy(copy, args);
    const char *translated = lookup(format);
    int length = vsnprintf(NULL, 0, translated, copy);
    va_end(copy);
    if (length >= 0)
    {
        char *buffer = lv_malloc((size_t)length + 1U);
        if (buffer)
        {
            vsnprintf(buffer, (size_t)length + 1U, translated, args);
            lv_label_set_text(label, buffer);
            lv_free(buffer);
        }
    }
    va_end(args);
}
void carplay_ui_set_chinese(bool value)
{
    if (!chinese)
    {
        for (unsigned i = 0; i < sizeof(labels) / sizeof(labels[0]); ++i)
            if (labels[i].label)
                labels[i].font = lv_obj_get_style_text_font(labels[i].label, 0);
    }
    chinese = value;
    for (unsigned i = 0; i < sizeof(labels) / sizeof(labels[0]); ++i)
        if (labels[i].label)
        {
            apply_font(&labels[i]);
            if (labels[i].key)
                lv_label_set_text(labels[i].label, lookup(labels[i].key));
        }
}
