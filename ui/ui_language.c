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
static const struct
{
    const char *en, *zh;
} words[] = {
    { "Dashboard", "仪表" },
    { "Connectivity", "车机互联" },
    { "Settings", "系统设置" },
    { "System update", "系统更新" },
    { "Display", "显示" },
    { "Sound", "声音" },
    { "About", "关于" },
    { "Build hash", "构建哈希" },
    { "Build time (UTC)", "构建时间 (UTC)" },
    { "Speed", "当前速度" },
    { "Battery", "剩余电量" },
    { "Connected", "已连接" },
    { "Not connected", "未连接" },
    { "Demo values", "演示数据" },
    { "Waiting for phone", "等待连接" },
    { "Apple CarPlay", "Apple CarPlay" },
    { "Wired / wireless", "有线 / 无线" },
    { "Connect phone", "连接手机" },
    { "Resume CarPlay", "重新显示 CarPlay" },
    { "Connecting", "正在连接" },
    { "Other protocols", "其他协议" },
    { "Android Auto  /  CarLife+", "Android Auto  /  CarLife+" },
    { "Planned", "后续支持" },
    { "Media volume", "媒体音量" },
    { "Display brightness", "屏幕亮度" },
    { "Appearance", "外观" },
    { "Phone appearance", "手机界面外观" },
    { "Phone appearance requested", "已请求更改手机外观" },
    { "Auto uses daylight on this board", "本机自动模式使用日间外观" },
    { "Phone controls", "手机控制" },
    { "Now Playing", "正在播放" },
    { "Route guidance", "路线引导" },
    { "GNSS simulation", "模拟定位" },
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
    { "Check for updates", "检查更新" },
    { "Downloading", "下载中" },
    { "Installing", "安装中" },
    { "Restarting", "重启中" },
    { "Up to date", "已是最新" },
    { "Update available", "有可用更新" },
    { "Update failed", "更新失败" },
    { "Not checked", "尚未检查更新" },
    { "Checking for updates...", "正在检查更新…" },
    { "Install update", "安装更新" },
    { "Update service busy.", "更新服务忙" },
    { "Update changed. Retry.", "更新已变化，请重试" },
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
static struct binding
{
    lv_obj_t *label;
    const char *key;
    const lv_font_t *font; /* Latin font chosen by the page owner */
} labels[384];

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

static void apply_font(struct binding *b)
{
    if (!b || !b->label)
        return;
    const char *text = lv_label_get_text(b->label);
    lv_obj_set_style_text_font(b->label, needs_cjk(text) ? cjk_for(b->font) : b->font, 0);
}

static void deleted(lv_event_t *event)
{
    memset(lv_event_get_user_data(event), 0, sizeof(struct binding));
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
    }
    lv_label_set_text(label, lookup(english));
    apply_font(b);
}

void carplay_ui_set_text_fmt(lv_obj_t *label, const char *format, ...)
{
    struct binding *b = bind_label(label);
    if (b)
        b->key = NULL;
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
    apply_font(b);
}

void carplay_ui_set_chinese(bool value)
{
    chinese = value;
    for (unsigned i = 0; i < sizeof(labels) / sizeof(labels[0]); ++i)
    {
        if (!labels[i].label)
            continue;
        if (labels[i].key)
            lv_label_set_text(labels[i].label, lookup(labels[i].key));
        apply_font(&labels[i]);
    }
}
