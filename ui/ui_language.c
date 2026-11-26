/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "ui_language.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

LV_FONT_DECLARE(carplay_cjk_16);
LV_FONT_DECLARE(carplay_cjk_18);
LV_FONT_DECLARE(carplay_cjk_24);
LV_FONT_DECLARE(carplay_cjk_30);

static bool chinese;
typedef struct
{
    const char *en, *zh;
} translation_t;

static const translation_t words[] = {
    { "Dashboard", "仪表" },
    { "Connectivity", "车机互联" },
    { "Settings", "系统设置" },
    { "System update", "系统更新" },
    { "Display", "显示" },
    { "Sound", "声音" },
    { "Connections", "连接" },
    { "General", "通用" },
    { "System", "系统" },
    { "Developer options", "开发者选项" },
    { "About", "关于" },
    { "Build hash", "构建哈希" },
    { "Build time (UTC)", "构建时间 (UTC)" },
    { "Speed", "当前速度" },
    { "Battery", "剩余电量" },
    { "Connected", "已连接" },
    { "Not connected", "未连接" },
    { "Wi-Fi", "Wi-Fi" },
    { "Ethernet", "有线网络" },
    { "Connection status", "连接状态" },
    { "Network unavailable", "网络不可用" },
    { "Cable disconnected", "网线未连接" },
    { "Waiting for IPv4", "等待 IPv4 地址" },
    { "IPv4 address", "IPv4 地址" },
    { "Subnet mask", "子网掩码" },
    { "Default gateway", "默认网关" },
    { "System DNS", "系统 DNS" },
    { "Demo values", "演示数据" },
    { "Waiting for phone", "等待连接" },
    { "Apple CarPlay", "Apple CarPlay" },
    { "Wired / wireless", "有线 / 无线" },
    { "Connect phone", "连接手机" },
    { "Resume CarPlay", "重新显示 CarPlay" },
    { "Connecting", "正在连接" },
    { "Connection failed", "连接失败" },
    { "Disconnecting", "正在断开" },
    { "iPhone · Wireless", "iPhone · 无线" },
    { "iPhone · USB", "iPhone · USB" },
    { "Available over USB and Wi-Fi", "支持 USB 和 Wi-Fi 连接" },
    { "Available over USB", "支持 USB 连接" },
    { "Other protocols", "其他协议" },
    { "Android Auto  /  CarLife+", "Android Auto  /  CarLife+" },
    { "Planned", "后续支持" },
    { "Media volume", "媒体音量" },
    { "Mute microphone", "静音麦克风" },
    { "Unmute microphone", "开启麦克风" },
    { "Display brightness", "屏幕亮度" },
    { "Screen", "屏幕" },
    { "Appearance", "外观" },
    { "Phone projection", "手机投屏" },
    { "Phone appearance", "手机界面外观" },
    { "Phone appearance requested", "已请求更改手机外观" },
    { "Auto uses daylight on this board", "本机自动模式使用日间外观" },
    { "Phone controls", "手机控制" },
    { "Now Playing", "正在播放" },
    { "Route guidance", "路线引导" },
    { "Phone status", "手机状态" },
    { "Upcoming turn", "即将转向" },
    { "Not charging", "未充电" },
    { "Charging", "充电中" },
    { "Charged", "已充满" },
    { "External power", "外接电源" },
    { "Battery unavailable", "暂无电量" },
    { "Cellular", "蜂窝网络" },
    { "Airplane mode", "飞行模式" },
    { "Signal", "信号" },
    { "Cellular not supported", "不支持蜂窝网络" },
    { "Cellular unavailable", "暂无网络状态" },
    { "No active call", "无通话" },
    { "Dialing", "拨号中" },
    { "Ringing", "来电响铃" },
    { "Connecting call", "通话连接中" },
    { "Call active", "通话中" },
    { "Call on hold", "通话保持" },
    { "Ending call", "正在结束通话" },
    { "Call status unavailable", "暂无通话状态" },
    { "Manual clock", "手动时钟" },
    { "iPhone time received", "已收到 iPhone 时间" },
    { "Waiting for iPhone time", "等待 iPhone 时间" },
    { "Clock unavailable", "时钟不可用" },
    { "GNSS simulation", "模拟定位" },
    { "Date & time", "日期与时间" },
    { "Local time", "本地时间" },
    { "Sync with iPhone", "与 iPhone 同步" },
    { "Sync via network", "联网校时" },
    { "Time server", "校时服务器" },
    { "Enter a host name or IPv4 address.", "请输入主机名或 IPv4 地址。" },
    { "Invalid server address.", "服务器地址无效。" },
    { "Could not save the time server.", "无法保存校时服务器。" },
    { "Restore default", "恢复默认" },
    { "Save", "保存" },
    { "Synchronizing network time...", "正在联网校时…" },
    { "Network time synchronized", "联网校时成功" },
    { "Connect Ethernet or Wi-Fi and try again.", "请连接有线网络或 Wi-Fi 后重试。" },
    { "Time server address could not be resolved.", "无法解析时间服务器地址。" },
    { "Time server did not respond.", "时间服务器无响应。" },
    { "Network time unavailable", "联网校时不可用" },
    { "Enter an HTTP or HTTPS URL, ending in / for a base URL.", "输入 HTTP 或 HTTPS 地址。目录地址以 / 结尾。" },
    { "Invalid update URL.", "更新地址无效。" },
    { "Manual", "手动" },
    { "Date", "日期" },
    { "Time", "时间" },
    { "UTC offset", "UTC 偏移" },
    { "Invalid date, time or UTC offset", "日期、时间或 UTC 偏移无效" },
    { "Unable to set clock", "无法设置时钟" },
    { "Clock updated", "时钟已更新" },
    { "Fixed point", "固定点" },
    { "A to B", "A 点到 B 点" },
    { "Latitude", "纬度" },
    { "Longitude", "经度" },
    { "Speed (km/h)", "速度 (km/h)" },
    { "Off", "关闭" },
    { "On", "开启" },
    { "Apply", "应用" },
    { "GNSS settings busy", "定位设置忙，请重试" },
    { "Invalid coordinates", "坐标格式或范围有误" },
    { "Speed must be 1 to 300 km/h", "速度须为 1 至 300 km/h" },
    { "Choose a different destination", "请选择其他终点" },
    { "Simulation off", "模拟定位已关闭" },
    { "Connect wirelessly to test", "请连接无线 CarPlay" },
    { "Reconnect wireless CarPlay", "请重新连接无线 CarPlay" },
    { "Waiting for GNSS request", "等待手机请求定位" },
    { "Phone requested simulated GNSS", "手机已请求模拟定位" },
    { "Moving from A to B", "正在从 A 点驶向 B 点" },
    { "Arrived at B", "已到达 B 点" },
    { "Failed", "失败" },
    { "Playing", "播放中" },
    { "Not playing", "未播放" },
    { "Light", "浅色" },
    { "Dark", "深色" },
    { "Auto", "自动" },
    { "Language", "语言" },
    { "简体中文", "简体中文" },
    { "English", "English" },
    { "CarPlay display", "CarPlay 显示" },
    { "CarPlay frame rate", "CarPlay 帧率" },
    { "Requested maximum", "请求的上限" },
    { "Bluetooth", "蓝牙" },
    { "Saved. Restart the receiver to apply.", "已保存，重启接收端后生效" },
    { "Bluetooth name", "蓝牙名称" },
    { "1-28 printable ASCII characters", "支持 1 至 28 个可打印 ASCII 字符" },
    { "Saved. Restart the device to apply.", "已保存，重启设备后生效" },
    { "Invalid Bluetooth name", "蓝牙名称无效" },
    { "Fullscreen", "全屏" },
    { "Fusion", "融合" },
    { "Back", "返回" },
    { "Adjust local audio output", "调节本机音频输出" },
    { "Adjust panel backlight", "调节本机屏幕背光" },
    { "Changes apply immediately and survive reboot", "设置立即生效，重启后保留" },
    { "Not supported on this device", "此设备不支持" },
    { "Not supported", "不支持" },
    { "Settings saved.", "设置已保存" },
    { "Settings not saved.", "设置保存失败" },
    { "Reconnect required to apply the new layout", "重新连接后应用新布局" },
    { "Installed version", "当前版本" },
    { "Firmware URL", "固件地址" },
    { "Enter firmware URL", "输入固件地址" },
    { "Enter an HTTP or HTTPS URL to an OTA .cpio file.", "请输入 OTA .cpio 文件的 HTTP 或 HTTPS 地址。" },
    { "Invalid firmware URL.", "固件地址无效。" },
    { "Update worker is busy.", "系统更新正在进行。" },
    { "Update changed. Retry.", "更新内容已变更，请重试。" },
    { "Download update", "下载更新包" },
    { "Cancel download", "取消下载" },
    { "Install update", "安装更新" },
    { "Downloading", "下载中" },
    { "Installing", "安装中" },
    { "Restarting", "重启中" },
    { "Update failed", "更新失败" },
    { "Ready to download", "可下载镜像" },
    { "Ready to install", "可安装更新" },
    { "Verifying update", "正在校验镜像" },
    { "Version %s", "版本 %s" },
    { "Slot %s", "分区 %s" },
    { "Update backend unavailable", "更新服务不可用" },
    { "Update backend available", "更新服务可用" },
    { "No network connection.", "无网络连接" },
    { "Disconnect CarPlay and keep power connected before installing.", "安装前请断开 CarPlay 并保持供电" },
    { "Install this update and restart?", "安装此更新并重启？" },
    { "Install and restart", "安装并重启" },
    { "Cancel", "取消" },
    { "Use the existing firmware updater", "使用现有固件升级服务" },
    { "Firmware %s (%.8s) | %s", "固件 %s (%.8s) | %s" },
    { "progress %d%%", "进度 %d%%" },
    { "Allow Bluetooth pairing?", "允许蓝牙配对？" },
    { "Confirm this number on your iPhone.\n\n%06u", "请在 iPhone 上确认此配对码。\n\n%06u" },
    { "Enter this number on your iPhone.\n\n%06u", "请在 iPhone 上输入此配对码。\n\n%06u" },
    { "Numbers match / Allow", "号码一致 / 允许" },
    { "Cancel pairing", "取消配对" },
    { "unknown", "未知" },
};

/* Labels, rather than buttons, own translation bindings. Delete events retire
 * bindings, so rebuilding a page cannot leak a stale pointer. */
typedef struct binding
{
    lv_obj_t *label;
    const translation_t *word;
    const lv_font_t *font; /* Latin font chosen by the page owner */
    struct binding *prev, *next;
} binding_t;

static binding_t *labels;

static const translation_t *lookup(const char *english)
{
    for (unsigned i = 0; i < sizeof(words) / sizeof(words[0]); ++i)
        if (!strcmp(english, words[i].en))
            return &words[i];
    return NULL;
}

bool carplay_ui_is_chinese(void)
{
    return chinese;
}

const char *carplay_ui_translate(const char *english)
{
    const translation_t *word = lookup(english);
    return word && chinese ? word->zh : english;
}

/* Only labels whose rendered text actually contains non-ASCII characters get
 * a CJK face. Numerals, units and version strings keep their Latin design
 * font, so a 220 px speed reading is never replaced by a 30 px CJK face. */
static bool needs_cjk(const char *text)
{
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p)
        if (*p > 0x7f)
            return true;
    return false;
}

static const lv_font_t *cjk_for(const lv_font_t *latin)
{
    int height = latin ? latin->line_height : 0;
    if (height >= 30)
        return &carplay_cjk_30;
    if (height >= 22)
        return &carplay_cjk_24;
    if (height >= 18)
        return &carplay_cjk_18;
    return &carplay_cjk_16;
}

static void apply_font(binding_t *binding)
{
    if (!binding)
        return;
    const char *text = lv_label_get_text(binding->label);
    const lv_font_t *font = needs_cjk(text) ? cjk_for(binding->font) : binding->font;
    if (lv_obj_get_style_text_font(binding->label, 0) != font)
        lv_obj_set_style_text_font(binding->label, font, 0);
}

static void deleted(lv_event_t *event)
{
    binding_t *binding = lv_event_get_user_data(event);
    if (binding->prev)
        binding->prev->next = binding->next;
    else
        labels = binding->next;
    if (binding->next)
        binding->next->prev = binding->prev;
    lv_free(binding);
}

static binding_t *bind_label(lv_obj_t *label, bool needed)
{
    uint32_t count = lv_obj_get_event_count(label);
    for (uint32_t index = 0; index < count; ++index)
    {
        lv_event_dsc_t *event = lv_obj_get_event_dsc(label, index);
        if (lv_event_dsc_get_cb(event) == deleted)
            return lv_event_dsc_get_user_data(event);
    }
    if (!needed)
        return NULL;
    binding_t *binding = lv_malloc(sizeof(*binding));
    if (!binding)
        return NULL;
    *binding = (binding_t){ .label = label,
                            .font = lv_obj_get_style_text_font(label, 0),
                            .next = labels };
    if (!lv_obj_add_event_cb(label, deleted, LV_EVENT_DELETE, binding))
    {
        lv_free(binding);
        return NULL;
    }
    if (labels)
        labels->prev = binding;
    labels = binding;
    return binding;
}

static void set_text(lv_obj_t *label, const char *text, const translation_t *word)
{
    binding_t *binding = bind_label(label, word || needs_cjk(text));
    if (binding)
        binding->word = word;
    if (strcmp(lv_label_get_text(label), text) != 0)
        lv_label_set_text(label, text);
    apply_font(binding);
}

void carplay_ui_set_text(lv_obj_t *label, const char *english)
{
    const translation_t *word = lookup(english);
    set_text(label, word && chinese ? word->zh : english, word);
}

void carplay_ui_set_text_fmt(lv_obj_t *label, const char *format, ...)
{
    va_list args, copy;
    va_start(args, format);
    va_copy(copy, args);
    const char *translated = carplay_ui_translate(format);
    int length = vsnprintf(NULL, 0, translated, copy);
    va_end(copy);
    if (length >= 0)
    {
        char *buffer = lv_malloc((size_t)length + 1U);
        if (buffer)
        {
            vsnprintf(buffer, (size_t)length + 1U, translated, args);
            set_text(label, buffer, NULL);
            lv_free(buffer);
        }
    }
    va_end(args);
}

void carplay_ui_set_chinese(bool value)
{
    if (value == chinese)
        return;
    chinese = value;
    for (binding_t *binding = labels; binding; binding = binding->next)
    {
        if (binding->word)
        {
            const char *text = chinese ? binding->word->zh : binding->word->en;
            if (strcmp(lv_label_get_text(binding->label), text) != 0)
                lv_label_set_text(binding->label, text);
            apply_font(binding);
        }
    }
}
