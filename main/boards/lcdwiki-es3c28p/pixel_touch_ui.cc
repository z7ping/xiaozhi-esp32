#include "pixel_touch_ui.h"

#include "application.h"
#include "audio_codec.h"
#include "board.h"
#include "display/lvgl_display/lvgl_theme.h"
#include "settings.h"

#include <material_symbols.h>

#if CONFIG_WEATHER_DASHBOARD
#include "almanac_service.h"
#include "weather_service.h"
LV_FONT_DECLARE(lv_font_impact_64);
#endif

#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <algorithm>
#include <cstdint>

namespace {
constexpr uint32_t kBg = 0x071326;
constexpr uint32_t kPanel = 0x10263B;
constexpr uint32_t kBorder = 0x345C74;
constexpr uint32_t kText = 0xEBF9FE;
constexpr uint32_t kMuted = 0xACC8D6;
constexpr uint32_t kAccent = 0x80E9DD;
constexpr uint32_t kTwilightBg = 0x181724;
constexpr uint32_t kTwilightPanel = 0x302846;
constexpr uint32_t kTwilightBorder = 0x635273;

int DaysInMonth(int year, int month) {
    static constexpr int lengths[12] = {31, 28, 31, 30, 31, 30,
                                         31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) return 0;
    if (month == 2 && year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) return 29;
    return lengths[month - 1];
}
int WeekdayOfFirst(int year, int month) {
    std::tm value{};
    value.tm_year = year - 1900;
    value.tm_mon = month - 1;
    value.tm_mday = 1;
    value.tm_hour = 12;
    value.tm_isdst = -1;
    std::mktime(&value);
    return value.tm_wday;
}

void StylePanel(lv_obj_t* obj, uint32_t bg, uint32_t border) {
    lv_obj_set_style_bg_color(obj, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(border), 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

lv_obj_t* Label(lv_obj_t* parent, const char* text, int x, int y, int w, int h,
                uint32_t color = kText) {
    lv_obj_t* label = lv_label_create(parent);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_size(label, w, h);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_label_set_text(label, text);
    return label;
}

lv_obj_t* Button(lv_obj_t* parent, const char* text, int x, int y, int w, int h,
                 lv_event_cb_t callback, void* user_data, const char* icon = nullptr) {
    lv_obj_t* button = lv_obj_create(parent);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_size(button, w, h);
    StylePanel(button, kPanel, kBorder);
    lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, user_data);
    lv_obj_t* label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(kText), 0);
    if (icon != nullptr) {
        auto* glyph = lv_label_create(button);
        lv_label_set_text(glyph, icon);
        auto* theme = static_cast<LvglTheme*>(Board::GetInstance().GetDisplay()->GetTheme());
        lv_obj_set_style_text_font(glyph, theme->icon_font()->font(), 0);
        lv_obj_set_style_text_color(glyph, lv_color_hex(kAccent), 0);
        lv_obj_align(glyph, LV_ALIGN_LEFT_MID, 7, 0);
        if (text[0] != '\0') {
            lv_obj_align(label, LV_ALIGN_LEFT_MID, 30, 0);
        } else {
            lv_obj_center(glyph);
        }
    } else {
        lv_obj_center(label);
    }
    return button;
}

void SetIfChanged(lv_obj_t* label, const char* value) {
    if (label != nullptr && strcmp(lv_label_get_text(label), value) != 0) {
        lv_label_set_text(label, value);
    }
}
}  // namespace

PixelTouchUI::PixelTouchUI(lv_obj_t* screen) {
    Settings settings("pixel_ui");
    child_mode_ = settings.GetBool("child_mode", true);
    twilight_ = false;  // Approved V1.1 is single-palette; retain legacy NVS key.
    root_ = lv_obj_create(screen);
    lv_obj_set_pos(root_, 0, 0);
    lv_obj_set_size(root_, 320, 240);
    StylePanel(root_, kBg, kBg);
    lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);

    body_ = lv_obj_create(root_);
    lv_obj_remove_style_all(body_);
    lv_obj_set_pos(body_, 0, 0);
    lv_obj_set_size(body_, 320, 240);
    lv_obj_clear_flag(body_, LV_OBJ_FLAG_SCROLLABLE);

    refresh_timer_ = lv_timer_create([](lv_timer_t* timer) {
        auto* self = static_cast<PixelTouchUI*>(lv_timer_get_user_data(timer));
        self->Refresh();
    }, 5000, this);
    lv_timer_pause(refresh_timer_);
}

PixelTouchUI::~PixelTouchUI() {
    // LVGL queues only the raw this pointer. Cancel callbacks before deletion
    // to avoid use-after-free during screen teardown or reboot.
    lv_async_call_cancel(RunQueuedNavigation, this);
    // The robot is owned by the existing chat display, not by these pages.
    DetachRobot();
    if (refresh_timer_ != nullptr) {
        lv_timer_delete(refresh_timer_);
        refresh_timer_ = nullptr;
    }
    if (root_ != nullptr) {
        lv_obj_delete(root_);
        root_ = nullptr;
    }
}

void PixelTouchUI::Show(lv_obj_t* robot_area, lv_obj_t* chat_container) {
    if (robot_area == nullptr || chat_container == nullptr) {
        return;
    }
    robot_area_ = robot_area;
    chat_container_ = chat_container;
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_to_index(root_, lv_obj_get_child_count(lv_obj_get_parent(root_)) - 1);
    Navigate(Page::Home);
    lv_timer_resume(refresh_timer_);
}

void PixelTouchUI::ShowPlayer(lv_obj_t* robot_area, lv_obj_t* chat_container) {
    if (robot_area == nullptr || chat_container == nullptr) return;
    robot_area_ = robot_area;
    chat_container_ = chat_container;
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_to_index(root_, lv_obj_get_child_count(lv_obj_get_parent(root_)) - 1);
    Navigate(Page::Player);
    lv_timer_resume(refresh_timer_);
}

void PixelTouchUI::ShowVoice(lv_obj_t* robot_area, lv_obj_t* chat_container) {
    if (robot_area == nullptr || chat_container == nullptr) return;
    robot_area_ = robot_area;
    chat_container_ = chat_container;
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_to_index(root_, lv_obj_get_child_count(lv_obj_get_parent(root_)) - 1);
    if (page_ != Page::Voice) Navigate(Page::Voice);
    RefreshVoice();
    lv_timer_resume(refresh_timer_);
}

bool PixelTouchUI::IsPlayerVisible() const {
    return page_ == Page::Player && root_ != nullptr &&
           !lv_obj_has_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

void PixelTouchUI::Hide() {
    DetachRobot();
    lv_timer_pause(refresh_timer_);
    lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

void PixelTouchUI::AttachRobot() {
    if (robot_area_ == nullptr || robot_slot_ == nullptr) {
        return;
    }
    lv_obj_set_parent(robot_area_, robot_slot_);
    // Robot content is 104x124 on both pages; offset it under the 98px voice viewport.
    lv_obj_set_size(robot_area_, 104, 124);
    lv_obj_set_pos(robot_area_, page_ == Page::Voice ? -3 : 0, 0);
}

void PixelTouchUI::DetachRobot() {
    if (robot_area_ == nullptr || chat_container_ == nullptr) {
        return;
    }
    if (lv_obj_get_parent(robot_area_) != chat_container_) {
        lv_obj_set_parent(robot_area_, chat_container_);
        lv_obj_set_size(robot_area_, 320, 100);
        lv_obj_move_to_index(robot_area_, 1);
    }
    robot_slot_ = nullptr;
}

void PixelTouchUI::Navigate(Page page) {
    DetachRobot();
    lv_obj_clean(body_);
    page_ = page;
    voice_title_ = nullptr;
    voice_caption_ = nullptr;
    voice_primary_ = nullptr;
    voice_stop_ = nullptr;
    home_day_label_ = nullptr;
    time_label_ = nullptr;
    date_label_ = nullptr;
    weather_label_ = nullptr;
    lunar_label_ = nullptr;
    calendar_date_label_ = nullptr;
    calendar_detail_label_ = nullptr;
    brightness_slider_ = nullptr;
    volume_slider_ = nullptr;
    brightness_value_ = nullptr;
    volume_value_ = nullptr;
    player_title_ = nullptr;
    player_header_ = nullptr;
    player_duration_ = nullptr;
    player_volume_popup_ = nullptr;
    player_volume_slider_ = nullptr;
    player_volume_value_ = nullptr;
    player_status_ = nullptr;
    player_position_ = nullptr;
    player_seek_slider_ = nullptr;
    player_toggle_ = nullptr;
    player_prev_button_ = nullptr;
    player_next_button_ = nullptr;
    player_stop_button_ = nullptr;
    player_list_button_ = nullptr;
    playlist_panel_ = nullptr;
    playlist_page_label_ = nullptr;
    for (auto& button : playlist_buttons_) button = nullptr;
    calendar_month_label_ = nullptr;
    calendar_selected_label_ = nullptr;
    calendar_details_popup_ = nullptr;
    for (auto& cell : calendar_cells_) cell = nullptr;
    child_mode_switch_ = nullptr;
    twilight_switch_ = nullptr;
    refresh_ticks_ = 0;

    switch (page) {
        case Page::Home:
            BuildHome();
            AttachRobot();
            break;
        case Page::Voice:
            BuildVoice();
            AttachRobot();
            break;
        case Page::Calendar:
            BuildCalendar();
            break;
        case Page::Player:
            BuildPlayer();
            break;
        case Page::Settings:
            BuildSettings();
            break;
    }
    if (page == Page::Home) RefreshHome();
    if (page == Page::Voice) RefreshVoice();
    if (page == Page::Calendar) RefreshCalendar();
    if (page == Page::Player) {
        UpdatePlayer();
        Application::GetInstance().SendMediaCommand("get_state");
    }
    ApplyPalette();
}

void PixelTouchUI::RunQueuedNavigation(void* data) {
    auto* self = static_cast<PixelTouchUI*>(data);
    if (self->root_ != nullptr &&
        !lv_obj_has_flag(self->root_, LV_OBJ_FLAG_HIDDEN)) {
        self->Navigate(self->queued_page_);
    }
}

void PixelTouchUI::QueueNavigate(Page page) {
    queued_page_ = page;
    // Only one deferred transition is needed after an LVGL click callback.
    // Cancel previous requests so fast taps cannot rebuild the page repeatedly.
    lv_async_call_cancel(RunQueuedNavigation, this);
    lv_async_call(RunQueuedNavigation, this);
}

void PixelTouchUI::BuildHeader(const char* title, bool back) {
    // Approved V1.1 header: 320x35, transparent bar and compact back arrow.
    auto* bar = lv_obj_create(body_);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_size(bar, 320, 35);
    lv_obj_remove_style_all(bar);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(bar, lv_color_hex(0x293b4d), 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    if (back) {
        auto* button = Button(bar, "", 12, 2, 28, 30, OnHome, this,
                              MATERIAL_SYMBOLS_ARROW_BACK);
        lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(button, 0, 0);
    }
    auto* label = Label(bar, title, back ? 49 : 12, 8, back ? 220 : 290, 20);
    lv_obj_set_style_text_color(label, lv_color_hex(kText), 0);
}

void PixelTouchUI::BuildBackButton() {
    Button(body_, "返回", 8, 194, 92, 38, OnHome, this,
           MATERIAL_SYMBOLS_ARROW_BACK);
}

void PixelTouchUI::BuildHome() {
    // V1.1 browser baseline: no legacy 32px banner; compact date/status row.
    home_day_label_ = Label(body_, "--月--日 · 星期--", 14, 10, 172, 21, kMuted);
    auto* settings = Button(body_, "", 279, 6, 28, 28, OnSettings, this,
                            MATERIAL_SYMBOLS_SETTINGS);
    lv_obj_set_style_bg_opa(settings, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(settings, 0, 0);

    // The previously validated robot object is reused, never duplicated.
    // Its sprite is still awaiting V1.1 round-face visual replacement.
    robot_slot_ = lv_obj_create(body_);
    lv_obj_remove_style_all(robot_slot_);
    lv_obj_set_pos(robot_slot_, 200, 51);
    lv_obj_set_size(robot_slot_, 104, 124);
    lv_obj_add_flag(robot_slot_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(robot_slot_, OnTalk, LV_EVENT_CLICKED, this);

    time_label_ = Label(body_, "--:--", 17, 52, 192, 62);
#if CONFIG_WEATHER_DASHBOARD
    lv_obj_set_style_text_font(time_label_, &lv_font_impact_64, 0);
#endif
    weather_label_ = Label(body_, "天气暂无数据", 17, 115, 192, 22, kAccent);
    lunar_label_ = Label(body_, "农历暂无数据", 17, 146, 192, 18, kMuted);

    auto* indicator = lv_obj_create(body_);
    lv_obj_remove_style_all(indicator);
    lv_obj_set_pos(indicator, 18, 165);
    lv_obj_set_size(indicator, 7, 7);
    lv_obj_set_style_radius(indicator, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(indicator, lv_color_hex(0x70dfcb), 0);
    lv_obj_set_style_bg_opa(indicator, LV_OPA_COVER, 0);
    lv_obj_clear_flag(indicator, LV_OBJ_FLAG_CLICKABLE);
    Label(body_, "你好，我是天天", 31, 160, 145, 18, kMuted);

    auto* story = Button(body_, "听故事", 13, 185, 93, 43, OnStory, this,
                         MATERIAL_SYMBOLS_HEADPHONES);
    auto* song = Button(body_, "听儿歌", 114, 185, 93, 43, OnSong, this,
                        MATERIAL_SYMBOLS_MUSIC_NOTE);
    auto* calendar = Button(body_, "万年历", 214, 185, 93, 43, OnCalendar, this,
                            MATERIAL_SYMBOLS_CALENDAR_MONTH);
    for (auto* action : {story, song, calendar}) {
        lv_obj_set_style_radius(action, 11, 0);
        lv_obj_set_style_bg_color(action, lv_color_hex(0x20354e), 0);
        lv_obj_set_style_border_color(action, lv_color_hex(0x2d4660), 0);
    }
}

void PixelTouchUI::BuildVoice() {
    BuildHeader("和天天聊天", true);
    // The same circular mascot is used on Home and Voice. No duplicate face.
    robot_slot_ = lv_obj_create(body_);
    lv_obj_remove_style_all(robot_slot_);
    lv_obj_set_pos(robot_slot_, 111, 47);
    lv_obj_set_size(robot_slot_, 98, 98);
    lv_obj_add_flag(robot_slot_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(robot_slot_, OnVoicePrimary, LV_EVENT_CLICKED, this);

    voice_title_ = Label(body_, "正在聆听…", 8, 151, 304, 24);
    lv_obj_set_style_text_align(voice_title_, LV_TEXT_ALIGN_CENTER, 0);
    voice_caption_ = Label(body_, "我在听，你想聊什么？", 20, 177, 280, 16, kMuted);
    lv_obj_set_style_text_align(voice_caption_, LV_TEXT_ALIGN_CENTER, 0);

    voice_stop_ = Button(body_, "结束对话", 12, 197, 128, 33, OnVoiceStop, this);
    voice_primary_ = Button(body_, "说话中", 148, 197, 160, 33, OnVoicePrimary, this);
    lv_obj_set_style_radius(voice_stop_, 9, 0);
    lv_obj_set_style_radius(voice_primary_, 9, 0);
    lv_obj_set_style_bg_color(voice_primary_, lv_color_hex(0x80e4d7), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(voice_primary_, 0), lv_color_hex(0x092b34), 0);
    RefreshVoice();
}

void PixelTouchUI::RefreshVoice() {
    if (voice_title_ == nullptr || voice_primary_ == nullptr) return;
    const auto state = Application::GetInstance().GetDeviceState();
    const char* title = "等待对话";
    const char* caption = "点击开始说话";
    const char* primary = "开始说话";
    bool enabled = true;
    if (state == kDeviceStateListening) {
        title = "正在聆听…";
        caption = "我在听，你想聊什么？";
        primary = "正在聆听";
        enabled = false;
    } else if (state == kDeviceStateSpeaking) {
        title = "天天正在回答";
        caption = "点击停止播报";
        primary = "停止播报";
    } else if (state == kDeviceStateConnecting) {
        title = "正在连接…";
        caption = "正在连接语音服务";
        primary = "连接中";
        enabled = false;
    }
    SetIfChanged(voice_title_, title);
    SetIfChanged(voice_caption_, caption);
    SetIfChanged(lv_obj_get_child(voice_primary_, 0), primary);
    SetIfChanged(lv_obj_get_child(voice_stop_, 0),
                 state == kDeviceStateSpeaking ? "停止播报" : "结束对话");
    if (enabled) lv_obj_remove_state(voice_primary_, LV_STATE_DISABLED);
    else lv_obj_add_state(voice_primary_, LV_STATE_DISABLED);
}

void PixelTouchUI::BuildCalendar() {
    BuildHeader("万年历", true);
    auto* today_button = Button(body_, "今天", 270, 6, 38, 23,
                                OnCalendarToday, this);
    lv_obj_set_style_radius(today_button, 6, 0);
    lv_obj_set_style_bg_color(today_button, lv_color_hex(0x263f51), 0);
    auto* previous = Button(body_, "", 13, 38, 35, 26,
                            OnCalendarPrevious, this,
                            MATERIAL_SYMBOLS_KEYBOARD_ARROW_LEFT);
    auto* next = Button(body_, "", 272, 38, 35, 26,
                        OnCalendarNext, this,
                        MATERIAL_SYMBOLS_KEYBOARD_ARROW_RIGHT);
    for (auto* button : {previous, next}) {
        lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(button, 0, 0);
    }
    calendar_month_label_ = Label(body_, "", 89, 40, 142, 21);
    lv_obj_set_style_text_align(calendar_month_label_, LV_TEXT_ALIGN_CENTER, 0);

    static constexpr const char* week[] = {"日", "一", "二", "三", "四", "五", "六"};
    for (int col = 0; col < 7; ++col) {
        auto* label = Label(body_, week[col], 13 + col * 42, 70, 42, 15, 0x718da6);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    }
    std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);
    if (calendar_year_ == 0 && local.tm_year >= 125) ResetCalendarToday();
    if (calendar_year_ == 0) {
        Label(body_, "设备时间未同步", 20, 104, 278, 35, kMuted);
        return;
    }

    char month_text[32];
    std::snprintf(month_text, sizeof(month_text), "%d年%d月",
                  calendar_year_, calendar_month_);
    SetIfChanged(calendar_month_label_, month_text);

    const int first = WeekdayOfFirst(calendar_year_, calendar_month_);
    const int days = DaysInMonth(calendar_year_, calendar_month_);
    const bool six_rows = (first + days) > 35;
    const int row_height = six_rows ? 17 : 20;
    for (int day = 1; day <= days; ++day) {
        const int index = first + day - 1;
        const int col = index % 7;
        const int row = index / 7;
        auto* cell = lv_label_create(body_);
        calendar_cells_[day] = cell;
        lv_obj_set_pos(cell, 18 + col * 42, 87 + row * row_height);
        lv_obj_set_size(cell, 32, six_rows ? 17 : 19);
        lv_obj_set_style_radius(cell, 8, 0);
        lv_obj_set_style_text_align(cell, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(cell, lv_color_hex(kText), 0);
        lv_obj_add_flag(cell, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_user_data(cell, reinterpret_cast<void*>(static_cast<intptr_t>(day)));
        lv_obj_add_event_cb(cell, OnCalendarDay, LV_EVENT_CLICKED, this);
        char day_text[4];
        std::snprintf(day_text, sizeof(day_text), "%d", day);
        lv_label_set_text(cell, day_text);
        const bool selected = selected_year_ == calendar_year_ &&
            selected_month_ == calendar_month_ && selected_day_ == day;
        const bool today = local.tm_year + 1900 == calendar_year_ &&
            local.tm_mon + 1 == calendar_month_ && local.tm_mday == day;
        if (selected) {
            lv_obj_set_style_bg_color(cell, lv_color_hex(0x7ddfd6), 0);
            lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
            lv_obj_set_style_text_color(cell, lv_color_hex(0x0a2533), 0);
        } else if (today) {
            lv_obj_set_style_border_color(cell, lv_color_hex(0x4c8f91), 0);
            lv_obj_set_style_border_width(cell, 1, 0);
        }
    }

    auto* footer = lv_obj_create(body_);
    lv_obj_set_pos(footer, 12, 198);
    lv_obj_set_size(footer, 296, 34);
    StylePanel(footer, 0x1a2d44, 0x2b4660);
    lv_obj_set_style_radius(footer, 9, 0);
    lv_obj_add_flag(footer, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(footer, OnCalendarDetail, LV_EVENT_CLICKED, this);
    calendar_selected_label_ = Label(footer, "", 11, 2, 180, 16, kText);
    Label(footer, "查看黄历详情 ›", 195, 8, 92, 19, 0x80dfd2);

    calendar_details_popup_ = lv_obj_create(body_);
    lv_obj_set_pos(calendar_details_popup_, 9, 60);
    lv_obj_set_size(calendar_details_popup_, 302, 173);
    StylePanel(calendar_details_popup_, 0x1b314b, 0x42647a);
    lv_obj_set_style_radius(calendar_details_popup_, 12, 0);
    calendar_date_label_ = Label(calendar_details_popup_, "", 16, 14, 238, 25, kText);
    Button(calendar_details_popup_, "", 258, 12, 26, 25, OnCalendarClose,
           this, MATERIAL_SYMBOLS_CLOSE);
    calendar_detail_label_ = Label(calendar_details_popup_,
                                    "点击查询选中日期", 16, 47, 267, 105, kMuted);
    lv_label_set_long_mode(calendar_detail_label_, LV_LABEL_LONG_WRAP);
    lv_obj_add_flag(calendar_details_popup_, LV_OBJ_FLAG_HIDDEN);
    RefreshCalendar();
}

void PixelTouchUI::BuildPlayer() {
    BuildHeader(player_song_mode_ ? "儿歌播放" : "故事播放", true);
    auto* volume_button = Button(body_, "", 280, 2, 28, 30,
                                 OnVolumePopup, this, MATERIAL_SYMBOLS_VOLUME_UP);
    lv_obj_set_style_border_width(volume_button, 0, 0);
    lv_obj_set_style_bg_opa(volume_button, LV_OPA_TRANSP, 0);

    // V1.1 player cover 77x77 at (21,48).
    auto* cover = lv_obj_create(body_);
    lv_obj_set_pos(cover, 21, 48);
    lv_obj_set_size(cover, 77, 77);
    StylePanel(cover, player_song_mode_ ? 0x765d9b : 0x355f65,
               player_song_mode_ ? 0x765d9b : 0x355f65);
    lv_obj_set_style_radius(cover, 14, 0);
    lv_obj_add_flag(cover, LV_OBJ_FLAG_CLICKABLE);
    // Retained legacy queue opens by long-press on the cover.
    lv_obj_add_event_cb(cover, OnPlaylistOpen, LV_EVENT_LONG_PRESSED, this);
    auto* cover_icon = lv_label_create(cover);
    lv_label_set_text(cover_icon, player_song_mode_ ?
                      MATERIAL_SYMBOLS_MUSIC_NOTE : MATERIAL_SYMBOLS_BOOK);
    auto* theme = static_cast<LvglTheme*>(Board::GetInstance().GetDisplay()->GetTheme());
    lv_obj_set_style_text_font(cover_icon, theme->icon_font()->font(), 0);
    lv_obj_set_style_text_color(cover_icon, lv_color_hex(0xe2f2d6), 0);
    lv_obj_center(cover_icon);

    player_status_ = Label(body_, "等待播放", 127, 51, 168, 16, 0x83cfc3);
    player_title_ = Label(body_, "暂无播放内容", 115, 73, 186, 29);
    Label(body_, "Jellyfin", 115, 108, 164, 16, 0x89a1b8);

    player_seek_slider_ = lv_slider_create(body_);
    lv_obj_set_pos(player_seek_slider_, 22, 139);
    lv_obj_set_size(player_seek_slider_, 276, 22);
    lv_slider_set_range(player_seek_slider_, 0, 1000);
    lv_obj_set_style_bg_color(player_seek_slider_, lv_color_hex(0x2e5066), LV_PART_MAIN);
    lv_obj_set_style_bg_color(player_seek_slider_, lv_color_hex(0x81dfd5), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(player_seek_slider_, lv_color_hex(0xa4f9eb), LV_PART_KNOB);
    lv_obj_set_style_radius(player_seek_slider_, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(player_seek_slider_, 2, LV_PART_KNOB);
    lv_obj_set_style_pad_all(player_seek_slider_, 4, LV_PART_KNOB);
    lv_obj_add_state(player_seek_slider_, LV_STATE_DISABLED);
    lv_obj_add_event_cb(player_seek_slider_, OnMediaSeek, LV_EVENT_RELEASED, this);

    player_position_ = Label(body_, "--:--", 22, 164, 65, 15, 0x8fa7bd);
    player_duration_ = Label(body_, "--:--", 235, 164, 63, 15, 0x8fa7bd);
    lv_obj_set_style_text_align(player_duration_, LV_TEXT_ALIGN_RIGHT, 0);

    player_prev_button_ = Button(body_, "", 65, 181, 41, 41,
                                 OnMediaPrev, this, MATERIAL_SYMBOLS_SKIP_PREVIOUS);
    player_toggle_ = Button(body_, "", 136, 177, 49, 49,
                            OnMediaToggle, this, MATERIAL_SYMBOLS_PLAY_ARROW);
    player_next_button_ = Button(body_, "", 214, 181, 41, 41,
                                 OnMediaNext, this, MATERIAL_SYMBOLS_SKIP_NEXT);
    for (auto* button : {player_prev_button_, player_toggle_, player_next_button_}) {
        lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(button, 0, 0);
        lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, 0);
    }
    lv_obj_set_style_bg_color(player_toggle_, lv_color_hex(0x81e4d8), 0);
    lv_obj_set_style_bg_opa(player_toggle_, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(player_toggle_, 1),
                                lv_color_hex(0x0a3041), 0);
    if (!child_mode_) {
        // The old stop capability remains available without adding
        // an extra visible button that violates V1.1.
        lv_obj_add_event_cb(player_toggle_, OnMediaStop, LV_EVENT_LONG_PRESSED, this);
    }

    player_volume_popup_ = lv_obj_create(body_);
    lv_obj_set_pos(player_volume_popup_, 91, 37);
    lv_obj_set_size(player_volume_popup_, 220, 47);
    StylePanel(player_volume_popup_, 0x20364d, 0x35556b);
    lv_obj_set_style_radius(player_volume_popup_, 10, 0);
    Label(player_volume_popup_, "音量", 10, 13, 35, 18, kText);
    player_volume_slider_ = lv_slider_create(player_volume_popup_);
    lv_obj_set_pos(player_volume_slider_, 41, 13);
    lv_obj_set_size(player_volume_slider_, 110, 22);
    lv_slider_set_range(player_volume_slider_, 0, 100);
    lv_obj_set_style_bg_color(player_volume_slider_, lv_color_hex(0x2e5066), LV_PART_MAIN);
    lv_obj_set_style_bg_color(player_volume_slider_, lv_color_hex(0x7be7d7), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(player_volume_slider_, lv_color_hex(0xa4f9eb), LV_PART_KNOB);
    lv_obj_add_event_cb(player_volume_slider_, OnSliderChange, LV_EVENT_VALUE_CHANGED, this);
    lv_obj_add_event_cb(player_volume_slider_, OnSliderRelease, LV_EVENT_RELEASED, this);
    int initial_volume = 50;
    if (auto* codec = Board::GetInstance().GetAudioCodec()) {
        initial_volume = codec->output_volume();
    }
    lv_slider_set_value(player_volume_slider_, initial_volume, LV_ANIM_OFF);
    player_volume_value_ = Label(player_volume_popup_, "", 161, 13, 48, 19, kText);
    char percent[8];
    std::snprintf(percent, sizeof(percent), "%d%%", initial_volume);
    SetIfChanged(player_volume_value_, percent);
    lv_obj_add_flag(player_volume_popup_, LV_OBJ_FLAG_HIDDEN);

    // Preserve real queue/selection control from the tested Go server.
    playlist_panel_ = lv_obj_create(body_);
    lv_obj_set_pos(playlist_panel_, 10, 35);
    lv_obj_set_size(playlist_panel_, 300, 201);
    StylePanel(playlist_panel_, 0x1b314b, 0x42647a);
    lv_obj_set_style_radius(playlist_panel_, 12, 0);
    Label(playlist_panel_, "播放列表", 10, 5, 230, 22, kAccent);
    Button(playlist_panel_, "", 251, 1, 44, 29, OnPlaylistClose, this,
           MATERIAL_SYMBOLS_CLOSE);
    for (int i = 0; i < 6; ++i) {
        playlist_buttons_[i] = Button(playlist_panel_, "", 8, 29 + i * 23,
                                      284, 23, OnPlaylistSelect, this);
        lv_obj_add_flag(playlist_buttons_[i], LV_OBJ_FLAG_HIDDEN);
    }
    Button(playlist_panel_, "上页", 9, 170, 81, 27, OnPlaylistPrevious, this);
    playlist_page_label_ = Label(playlist_panel_, "", 108, 173, 91, 24, kMuted);
    Button(playlist_panel_, "下页", 210, 170, 81, 27, OnPlaylistNext, this);
    lv_obj_add_flag(playlist_panel_, LV_OBJ_FLAG_HIDDEN);
}

void PixelTouchUI::SetMediaState(const char* state, const char* title,
                                  int64_t position_ms, int64_t duration_ms,
                                  int current_index, int playlist_length, const char* error) {
    // Clear old-session data before storing the first state of a new session.
    SyncMediaAvailability();
    if (!media_available_)
        return;
    media_status_ = state != nullptr ? state : "";
    media_title_ = title != nullptr ? title : "";
    media_error_ = error != nullptr ? error : "";
    media_position_ms_ = position_ms >= 0 ? position_ms : 0;
    media_duration_ms_ = duration_ms > 0 ? duration_ms : 0;
    media_current_index_ = current_index;
    media_playlist_length_ = playlist_length;
    if (page_ == Page::Player && !lv_obj_has_flag(root_, LV_OBJ_FLAG_HIDDEN)) {
        UpdatePlayer();
        UpdatePlaylist();
    }
}

void PixelTouchUI::SetMediaPlaylist(int offset, const std::vector<std::string>& titles) {
    SyncMediaAvailability();
    // Ignore stale responses after a rapid page change or closing the overlay.
    if (page_ != Page::Player || playlist_panel_ == nullptr ||
        lv_obj_has_flag(playlist_panel_, LV_OBJ_FLAG_HIDDEN) ||
        offset != playlist_offset_) return;
    playlist_count_ = std::min(static_cast<int>(titles.size()), 6);
    for (int i = 0; i < 6; ++i) {
        playlist_titles_[i] = i < playlist_count_ ? titles[i] : "";
    }
    UpdatePlaylist();
}

void PixelTouchUI::UpdatePlaylist() {
    if (playlist_panel_ == nullptr) return;
    char label[56];
    std::snprintf(label, sizeof(label), "%d-%d / %d",
                  playlist_count_ == 0 ? 0 : playlist_offset_ + 1,
                  playlist_offset_ + playlist_count_,
                  media_playlist_length_);
    SetIfChanged(playlist_page_label_, label);
    for (int i = 0; i < 6; ++i) {
        auto* button = playlist_buttons_[i];
        if (button == nullptr) continue;
        if (i >= playlist_count_) {
            lv_obj_add_flag(button, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_clear_flag(button, LV_OBJ_FLAG_HIDDEN);
        // Let the LVGL label clip visually instead of slicing UTF-8 bytes,
        // which can corrupt a Chinese character mid-sequence.
        std::string caption = std::to_string(playlist_offset_ + i + 1) +
                              ". " + playlist_titles_[i];
        SetIfChanged(lv_obj_get_child(button, 0), caption.c_str());
        lv_obj_set_user_data(button,
            reinterpret_cast<void*>(static_cast<intptr_t>(playlist_offset_ + i + 1)));
        const bool selected = media_current_index_ == playlist_offset_ + i;
        lv_obj_set_style_border_color(button,
            lv_color_hex(selected ? kAccent : kBorder), 0);
    }
}

void PixelTouchUI::SyncMediaAvailability() {
    auto& app = Application::GetInstance();
    const bool supported = app.IsMediaControlAvailable();
    const uint32_t generation = app.GetMediaControlGeneration();
    if (supported != media_available_ || (supported && generation != media_generation_)) {
        // Do not reuse playback/queue data across a reconnect or downgrade.
        media_available_ = supported;
        media_status_.clear();
        media_title_.clear();
        media_error_.clear();
        media_position_ms_ = 0;
        media_duration_ms_ = 0;
        media_current_index_ = -1;
        media_playlist_length_ = 0;
        playlist_count_ = 0;
        playlist_offset_ = 0;
        if (playlist_panel_ != nullptr) {
            lv_obj_add_flag(playlist_panel_, LV_OBJ_FLAG_HIDDEN);
        }
    }
    media_generation_ = generation;
}

void PixelTouchUI::UpdatePlayer() {
    if (player_title_ == nullptr) return;
    SyncMediaAvailability();
    const auto connection = Application::GetInstance().GetMediaControlState();
    const bool supported = connection == MediaControlState::kAvailable;
    for (auto* control : {player_prev_button_, player_toggle_, player_next_button_}) {
        if (control == nullptr) continue;
        if (supported) lv_obj_remove_state(control, LV_STATE_DISABLED);
        else lv_obj_add_state(control, LV_STATE_DISABLED);
    }
    if (!supported) {
        SetIfChanged(player_title_, "请通过语音播放故事或儿歌");
        SetIfChanged(player_status_, connection == MediaControlState::kDisconnected
                                       ? "未连接对话服务"
                                       : "服务端未声明触摸播放控制");
        SetIfChanged(player_position_, "--:--");
        SetIfChanged(player_duration_, "--:--");
        if (player_seek_slider_ != nullptr) {
            lv_obj_add_state(player_seek_slider_, LV_STATE_DISABLED);
            lv_slider_set_value(player_seek_slider_, 0, LV_ANIM_OFF);
        }
        return;
    }
    SetIfChanged(player_title_, media_title_.empty() ? "暂无播放内容" : media_title_.c_str());
    const char* status = "等待播放";
    if (media_status_ == "playing") status = "正在播放";
    else if (media_status_ == "paused") status = "已暂停";
    else if (media_status_ == "stopped") status = "已停止";
    else if (media_status_ == "error") status = "播放发生错误";
    SetIfChanged(player_status_, media_error_.empty() ? status : media_error_.c_str());
    const int64_t seconds = media_position_ms_ / 1000;
    char current[32];
    std::snprintf(current, sizeof(current), "%02lld:%02lld",
                  static_cast<long long>(seconds / 60),
                  static_cast<long long>(seconds % 60));
    SetIfChanged(player_position_, current);
    if (media_duration_ms_ > 0) {
        const int64_t total = media_duration_ms_ / 1000;
        char duration[32];
        std::snprintf(duration, sizeof(duration), "%02lld:%02lld",
                      static_cast<long long>(total / 60),
                      static_cast<long long>(total % 60));
        SetIfChanged(player_duration_, duration);
        if (player_seek_slider_ != nullptr) {
            if (media_duration_ms_ <= 2LL * 60 * 60 * 1000)
                lv_obj_remove_state(player_seek_slider_, LV_STATE_DISABLED);
            else
                lv_obj_add_state(player_seek_slider_, LV_STATE_DISABLED);
            if (!lv_obj_has_state(player_seek_slider_, LV_STATE_PRESSED)) {
                const int ratio = static_cast<int>(std::min<int64_t>(
                    1000, media_position_ms_ * 1000 / media_duration_ms_));
                lv_slider_set_value(player_seek_slider_, ratio, LV_ANIM_OFF);
            }
        }
    } else {
        SetIfChanged(player_duration_, "--:--");
        if (player_seek_slider_ != nullptr) {
            lv_obj_add_state(player_seek_slider_, LV_STATE_DISABLED);
            lv_slider_set_value(player_seek_slider_, 0, LV_ANIM_OFF);
        }
    }
    if (player_toggle_ != nullptr) {
        // Button child 0 is text, child 1 is the approved icon glyph.
        SetIfChanged(lv_obj_get_child(player_toggle_, 1),
                     media_status_ == "playing" ?
                         MATERIAL_SYMBOLS_PAUSE : MATERIAL_SYMBOLS_PLAY_ARROW);
    }
}

void PixelTouchUI::BuildSettings() {
    BuildHeader("家长设置", true);
    Label(body_, "本机", 273, 9, 32, 16, 0x829fb7);

    // V1.1 rows: x=14..306, top=42, each 44px high; no enclosing card.
    for (int i = 1; i <= 4; ++i) {
        auto* divider = lv_obj_create(body_);
        lv_obj_remove_style_all(divider);
        lv_obj_set_pos(divider, 14, 42 + i * 44 - 1);
        lv_obj_set_size(divider, 292, 1);
        lv_obj_set_style_bg_color(divider, lv_color_hex(0x22384e), 0);
        lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);
        lv_obj_clear_flag(divider, LV_OBJ_FLAG_CLICKABLE);
    }
    Label(body_, "儿童模式", 14, 51, 160, 18);
    Label(body_, "减少误触和复杂操作", 14, 68, 190, 14, 0x829eb5);
    child_mode_switch_ = lv_switch_create(body_);
    lv_obj_set_pos(child_mode_switch_, 271, 54);
    lv_obj_set_size(child_mode_switch_, 32, 19);
    lv_obj_set_style_radius(child_mode_switch_, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(child_mode_switch_, lv_color_hex(0x34445a), LV_PART_MAIN);
    lv_obj_set_style_bg_color(child_mode_switch_, lv_color_hex(0x4caea9), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(child_mode_switch_, lv_color_hex(0xffffff), LV_PART_KNOB);
    if (child_mode_) lv_obj_add_state(child_mode_switch_, LV_STATE_CHECKED);
    lv_obj_add_event_cb(child_mode_switch_, OnChildModeChanged, LV_EVENT_VALUE_CHANGED, this);

    Label(body_, "屏幕亮度", 14, 96, 150, 20);
    Label(body_, "默认音量", 14, 140, 150, 20);
    brightness_slider_ = lv_slider_create(body_);
    lv_obj_set_pos(brightness_slider_, 184, 96);
    lv_obj_set_size(brightness_slider_, 120, 22);
    // LCD PWM cannot achieve the HTML demo's 110% CSS brightness.
    lv_slider_set_range(brightness_slider_, 10, 100);
    if (auto* backlight = Board::GetInstance().GetBacklight()) {
        lv_slider_set_value(brightness_slider_, backlight->brightness(), LV_ANIM_OFF);
    }
    volume_slider_ = lv_slider_create(body_);
    lv_obj_set_pos(volume_slider_, 184, 140);
    lv_obj_set_size(volume_slider_, 120, 22);
    lv_slider_set_range(volume_slider_, 0, 100);
    if (auto* codec = Board::GetInstance().GetAudioCodec()) {
        lv_slider_set_value(volume_slider_, codec->output_volume(), LV_ANIM_OFF);
    }
    for (auto* slider : {brightness_slider_, volume_slider_}) {
        lv_obj_set_style_bg_color(slider, lv_color_hex(0x2e5066), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_bg_color(slider, lv_color_hex(0x7be7d7), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(slider, lv_color_hex(0xa4f9eb), LV_PART_KNOB);
        lv_obj_set_style_border_color(slider, lv_color_hex(0x83a7b9), LV_PART_MAIN);
        lv_obj_set_style_border_width(slider, 1, LV_PART_MAIN);
        lv_obj_set_style_radius(slider, 1, LV_PART_MAIN);
        lv_obj_set_style_radius(slider, 2, LV_PART_KNOB);
        lv_obj_set_style_pad_all(slider, 5, LV_PART_KNOB);
        lv_obj_add_event_cb(slider, OnSliderChange, LV_EVENT_VALUE_CHANGED, this);
        lv_obj_add_event_cb(slider, OnSliderRelease, LV_EVENT_RELEASED, this);
    }
    Label(body_, "当前布局", 14, 184, 120, 18);
    Label(body_, "320 × 240 · 横屏", 14, 200, 164, 14, 0x829eb5);
    Label(body_, "V1.1", 271, 189, 36, 18, 0x8bd7c7);
    Label(body_, child_mode_ ? "儿童模式已开启" : "儿童模式已关闭",
          15, 212, 220, 16, 0x829eb5);
}

void PixelTouchUI::RefreshHome() {
    std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);
    char output[64];
    if (local.tm_year >= 125) {
        static const char* weekday[] = {"星期日", "星期一", "星期二", "星期三",
                                        "星期四", "星期五", "星期六"};
        std::snprintf(output, sizeof(output), "%d月%d日 · %s",
                      local.tm_mon + 1, local.tm_mday, weekday[local.tm_wday]);
        SetIfChanged(home_day_label_, output);
        std::snprintf(output, sizeof(output), "%02d:%02d", local.tm_hour, local.tm_min);
        SetIfChanged(time_label_, output);
    }
#if CONFIG_WEATHER_DASHBOARD
    const auto weather = WeatherService::GetInstance().GetSnapshot();
    if (weather.valid) {
        std::snprintf(output, sizeof(output), "%d°C %s %s", weather.temperature,
                      weather.city.c_str(), weather.weather_text.c_str());
        SetIfChanged(weather_label_, output);
    } else {
        SetIfChanged(weather_label_, weather.no_key ? "天气未配置" : "天气暂无数据");
    }
    const auto almanac = AlmanacService::GetInstance().GetSnapshot();
    if (almanac.valid) {
        std::string lunar = "农历 " + almanac.lunar_date;
        if (!almanac.solar_term.empty()) lunar += " · " + almanac.solar_term;
        SetIfChanged(lunar_label_, lunar.c_str());
    } else {
        SetIfChanged(lunar_label_, "农历暂无数据");
    }
#endif
}

void PixelTouchUI::RefreshCalendar() {
    if (calendar_year_ == 0) return;
    char date[64];
    std::snprintf(date, sizeof(date), "%d年%d月%d日",
                  selected_year_, selected_month_, selected_day_);
    SetIfChanged(calendar_date_label_, date);
    std::snprintf(date, sizeof(date), "%d月%d日", selected_month_, selected_day_);
    SetIfChanged(calendar_selected_label_, date);
#if CONFIG_WEATHER_DASHBOARD
    AlmanacSnapshot info;
    if (IsSelectedToday()) {
        info = AlmanacService::GetInstance().GetSnapshot();
    } else {
        if (calendar_lookup_unavailable_) {
            SetIfChanged(calendar_detail_label_, "黄历服务不可用或所选日期不支持");
            return;
        }
        const auto lookup = AlmanacService::GetInstance().GetDateResult();
        if (lookup.year != selected_year_ || lookup.month != selected_month_ ||
            lookup.day != selected_day_) {
            SetIfChanged(calendar_detail_label_, "点击详情查询选中日期");
            return;
        }
        if (lookup.loading) {
            SetIfChanged(calendar_detail_label_, "正在查询选中日期...");
            return;
        }
        if (!lookup.ready) {
            SetIfChanged(calendar_detail_label_,
                lookup.quota_exceeded ? "日历 API 今日配额已用完" :
                lookup.credentials_invalid ? "万年历密钥无效" :
                "查询失败，请检查网络和密钥");
            return;
        }
        info = lookup.snapshot;
    }
    if (!info.valid) {
        SetIfChanged(calendar_detail_label_,
                     info.no_credentials ? "请先配置万年历密钥" : "今日黄历暂无数据");
        return;
    }
    std::string message = "农历 " + info.lunar_date;
    if (!info.solar_term.empty()) message += "\n节气 " + info.solar_term;
    if (!info.yi.empty()) message += "\n宜 " + info.yi.front();
    if (!info.ji.empty()) message += "\n忌 " + info.ji.front();
    SetIfChanged(calendar_detail_label_, message.c_str());
#else
    SetIfChanged(calendar_detail_label_, "当前固件未启用黄历服务");
#endif
}

void PixelTouchUI::UpdateCalendarSelection() {
    // Day selection updates two label styles instead of deleting and
    // rebuilding 31 LVGL objects (avoids heap churn on repeated touches).
    for (int day = 1; day <= 31; ++day) {
        auto* cell = calendar_cells_[day];
        if (cell == nullptr) continue;
        const bool picked = selected_year_ == calendar_year_ &&
                            selected_month_ == calendar_month_ &&
                            selected_day_ == day;
        lv_obj_set_style_bg_opa(cell, picked ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_text_color(cell, lv_color_hex(picked ? kBg : kText), 0);
        if (picked) {
            lv_obj_set_style_bg_color(cell, lv_color_hex(kAccent), 0);
        }
    }
}

void PixelTouchUI::ResetCalendarToday() {
    std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);
    if (local.tm_year < 125) return;
    calendar_year_ = selected_year_ = local.tm_year + 1900;
    calendar_month_ = selected_month_ = local.tm_mon + 1;
    selected_day_ = local.tm_mday;
    calendar_lookup_unavailable_ = false;
}

bool PixelTouchUI::IsSelectedToday() const {
    std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);
    return selected_year_ == local.tm_year + 1900 &&
           selected_month_ == local.tm_mon + 1 &&
           selected_day_ == local.tm_mday;
}

void PixelTouchUI::NavigateCalendarMonth(int delta) {
    int year = calendar_year_;
    int month = calendar_month_ + delta;
    if (month < 1) { month = 12; --year; }
    if (month > 12) { month = 1; ++year; }
    if (year < 2025 || year > 2099) return;
    calendar_year_ = selected_year_ = year;
    calendar_month_ = selected_month_ = month;
    selected_day_ = std::min(selected_day_, DaysInMonth(year, month));
    calendar_lookup_unavailable_ = false;
    QueueNavigate(Page::Calendar);
}

void PixelTouchUI::Refresh() {
    if (lv_obj_has_flag(root_, LV_OBJ_FLAG_HIDDEN)) return;
    // If the user opened calendar before SNTP, rebuild the month once
    // the clock is ready. A failed first read must not leave an empty page.
    if (page_ == Page::Calendar && calendar_year_ == 0) {
        ResetCalendarToday();
        if (calendar_year_ != 0) {
            Navigate(Page::Calendar);
        }
        return;
    }
    if (page_ == Page::Player) {
        UpdatePlayer();  // Recheck support on reconnect or server downgrade.
        // Only the new Go server explicitly negotiates this command.
        if (Application::GetInstance().IsMediaControlAvailable()) {
            Application::GetInstance().SendMediaCommand("get_state");
        }
    } else if (page_ == Page::Calendar && calendar_details_popup_ != nullptr &&
               !lv_obj_has_flag(calendar_details_popup_, LV_OBJ_FLAG_HIDDEN)) {
        RefreshCalendar(); // Read the one-slot asynchronous date lookup result.
    } else if (++refresh_ticks_ >= 6) {
        refresh_ticks_ = 0;
        if (page_ == Page::Home) RefreshHome();
        if (page_ == Page::Voice) RefreshVoice();
        if (page_ == Page::Calendar) RefreshCalendar();
    }
}

void PixelTouchUI::UpdateSliderValues() {
    char text[8];
    if (brightness_slider_ != nullptr) {
        std::snprintf(text, sizeof(text), "%d%%", static_cast<int>(lv_slider_get_value(brightness_slider_)));
        SetIfChanged(brightness_value_, text);
    }
    if (volume_slider_ != nullptr) {
        std::snprintf(text, sizeof(text), "%d%%", static_cast<int>(lv_slider_get_value(volume_slider_)));
        SetIfChanged(volume_value_, text);
    }
    if (player_volume_slider_ != nullptr) {
        std::snprintf(text, sizeof(text), "%d%%",
                      static_cast<int>(lv_slider_get_value(player_volume_slider_)));
        SetIfChanged(player_volume_value_, text);
    }
}

void PixelTouchUI::OnSettings(lv_event_t* event) {
    static_cast<PixelTouchUI*>(lv_event_get_user_data(event))->QueueNavigate(Page::Settings);
}
void PixelTouchUI::OnHome(lv_event_t* event) {
    static_cast<PixelTouchUI*>(lv_event_get_user_data(event))->QueueNavigate(Page::Home);
}
void PixelTouchUI::OnCalendar(lv_event_t* event) {
    static_cast<PixelTouchUI*>(lv_event_get_user_data(event))->QueueNavigate(Page::Calendar);
}
void PixelTouchUI::OnPlayer(lv_event_t* event) {
    static_cast<PixelTouchUI*>(lv_event_get_user_data(event))->QueueNavigate(Page::Player);
}
void PixelTouchUI::OnStory(lv_event_t* event) {
    auto* self = static_cast<PixelTouchUI*>(lv_event_get_user_data(event));
    self->player_song_mode_ = false;
    self->QueueNavigate(Page::Player);
}
void PixelTouchUI::OnSong(lv_event_t* event) {
    auto* self = static_cast<PixelTouchUI*>(lv_event_get_user_data(event));
    self->player_song_mode_ = true;
    self->QueueNavigate(Page::Player);
}
void PixelTouchUI::OnVolumePopup(lv_event_t* event) {
    auto* self = static_cast<PixelTouchUI*>(lv_event_get_user_data(event));
    if (self->player_volume_popup_ == nullptr) return;
    if (lv_obj_has_flag(self->player_volume_popup_, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_clear_flag(self->player_volume_popup_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_to_index(self->player_volume_popup_,
                             lv_obj_get_child_count(self->body_) - 1);
    } else {
        lv_obj_add_flag(self->player_volume_popup_, LV_OBJ_FLAG_HIDDEN);
    }
}
void PixelTouchUI::OnTalk(lv_event_t*) {
    Application::GetInstance().ToggleChatState();
}
void PixelTouchUI::OnVoiceStop(lv_event_t* event) {
    auto* self = static_cast<PixelTouchUI*>(lv_event_get_user_data(event));
    const auto state = Application::GetInstance().GetDeviceState();
    if (state == kDeviceStateListening || state == kDeviceStateSpeaking) {
        Application::GetInstance().ToggleChatState();
    } else {
        self->QueueNavigate(Page::Home);
    }
}
void PixelTouchUI::OnVoicePrimary(lv_event_t*) {
    const auto state = Application::GetInstance().GetDeviceState();
    if (state == kDeviceStateIdle || state == kDeviceStateSpeaking) {
        Application::GetInstance().ToggleChatState();
    }
}
void PixelTouchUI::OnSliderChange(lv_event_t* event) {
    static_cast<PixelTouchUI*>(lv_event_get_user_data(event))->UpdateSliderValues();
}
void PixelTouchUI::OnSliderRelease(lv_event_t* event) {
    auto* self = static_cast<PixelTouchUI*>(lv_event_get_user_data(event));
    auto* target = lv_event_get_target_obj(event);
    if (target == self->brightness_slider_) {
        if (auto* backlight = Board::GetInstance().GetBacklight()) {
            backlight->SetBrightness(lv_slider_get_value(self->brightness_slider_), true);
        }
    } else if (target == self->volume_slider_ ||
               target == self->player_volume_slider_) {
        if (auto* codec = Board::GetInstance().GetAudioCodec()) {
            codec->SetOutputVolume(lv_slider_get_value(target));
        }
    }
}

void PixelTouchUI::OnMediaPrev(lv_event_t*) {
    Application::GetInstance().SendMediaCommand("prev");
}
void PixelTouchUI::OnMediaNext(lv_event_t*) {
    Application::GetInstance().SendMediaCommand("next");
}
void PixelTouchUI::OnMediaStop(lv_event_t*) {
    Application::GetInstance().SendMediaCommand("stop");
}
void PixelTouchUI::OnMediaToggle(lv_event_t* event) {
    auto* self = static_cast<PixelTouchUI*>(lv_event_get_user_data(event));
    Application::GetInstance().SendMediaCommand(
        self->media_status_ == "playing" ? "pause" : "resume");
}

void PixelTouchUI::OnCalendarPrevious(lv_event_t* event) {
    static_cast<PixelTouchUI*>(lv_event_get_user_data(event))->NavigateCalendarMonth(-1);
}
void PixelTouchUI::OnCalendarNext(lv_event_t* event) {
    static_cast<PixelTouchUI*>(lv_event_get_user_data(event))->NavigateCalendarMonth(1);
}
void PixelTouchUI::OnCalendarToday(lv_event_t* event) {
    auto* self = static_cast<PixelTouchUI*>(lv_event_get_user_data(event));
    self->ResetCalendarToday();
    self->QueueNavigate(Page::Calendar);
}
void PixelTouchUI::OnCalendarDay(lv_event_t* event) {
    auto* self = static_cast<PixelTouchUI*>(lv_event_get_user_data(event));
    auto* obj = lv_event_get_target_obj(event);
    int day = static_cast<int>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(obj)));
    if (day < 1 || day > DaysInMonth(self->calendar_year_, self->calendar_month_)) return;
    self->selected_year_ = self->calendar_year_;
    self->selected_month_ = self->calendar_month_;
    self->selected_day_ = day;
    self->calendar_lookup_unavailable_ = false;
    self->UpdateCalendarSelection();
    self->RefreshCalendar();
}
void PixelTouchUI::OnCalendarDetail(lv_event_t* event) {
    auto* self = static_cast<PixelTouchUI*>(lv_event_get_user_data(event));
#if CONFIG_WEATHER_DASHBOARD
    if (!self->IsSelectedToday()) {
        self->calendar_lookup_unavailable_ =
            !AlmanacService::GetInstance().RequestDate(
                self->selected_year_, self->selected_month_, self->selected_day_);
        self->RefreshCalendar();
    } else {
        self->RefreshCalendar();
    }
#else
    self->RefreshCalendar();
#endif
    if (self->calendar_details_popup_ != nullptr) {
        lv_obj_clear_flag(self->calendar_details_popup_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_to_index(self->calendar_details_popup_,
            lv_obj_get_child_count(self->body_) - 1);
    }
}
void PixelTouchUI::OnCalendarClose(lv_event_t* event) {
    auto* self = static_cast<PixelTouchUI*>(lv_event_get_user_data(event));
    if (self->calendar_details_popup_ != nullptr) {
        lv_obj_add_flag(self->calendar_details_popup_, LV_OBJ_FLAG_HIDDEN);
    }
}

void PixelTouchUI::ApplyPalette() {
    // Recolor only explicitly styled panels. Labels, calendar selection
    // highlights and the existing robot sprite retain their own colors.
    auto recolor = [this](auto&& walk, lv_obj_t* object) -> void {
        if (object == robot_area_) return;
        const uint32_t current = lv_color_to_u32(lv_obj_get_style_bg_color(object, LV_PART_MAIN));
        // LVGL may quantize colors to RGB565; compare through the same
        // converter instead of comparing against unquantized 24-bit constants.
        if (current == lv_color_to_u32(lv_color_hex(kBg)) ||
            current == lv_color_to_u32(lv_color_hex(kTwilightBg))) {
            lv_obj_set_style_bg_color(object, lv_color_hex(twilight_ ? kTwilightBg : kBg), 0);
        } else if (current == lv_color_to_u32(lv_color_hex(kPanel)) ||
                   current == lv_color_to_u32(lv_color_hex(kTwilightPanel))) {
            lv_obj_set_style_bg_color(object, lv_color_hex(twilight_ ? kTwilightPanel : kPanel), 0);
            lv_obj_set_style_border_color(object,
                lv_color_hex(twilight_ ? kTwilightBorder : kBorder), 0);
        }
        for (uint32_t index = 0; index < lv_obj_get_child_count(object); ++index) {
            walk(walk, lv_obj_get_child(object, index));
        }
    };
    recolor(recolor, root_);
}

void PixelTouchUI::OnChildModeChanged(lv_event_t* event) {
    auto* self = static_cast<PixelTouchUI*>(lv_event_get_user_data(event));
    self->child_mode_ = lv_obj_has_state(self->child_mode_switch_, LV_STATE_CHECKED);
    Settings settings("pixel_ui", true);
    settings.SetBool("child_mode", self->child_mode_);
}

void PixelTouchUI::OnTwilightChanged(lv_event_t* event) {
    auto* self = static_cast<PixelTouchUI*>(lv_event_get_user_data(event));
    self->twilight_ = lv_obj_has_state(self->twilight_switch_, LV_STATE_CHECKED);
    Settings settings("pixel_ui", true);
    settings.SetBool("twilight", self->twilight_);
    self->ApplyPalette();
}

void PixelTouchUI::OnPlaylistOpen(lv_event_t* event) {
    auto* self = static_cast<PixelTouchUI*>(lv_event_get_user_data(event));
    if (self->playlist_panel_ == nullptr) return;
    self->playlist_offset_ = 0;
    self->playlist_count_ = 0;
    self->UpdatePlaylist();
    lv_obj_clear_flag(self->playlist_panel_, LV_OBJ_FLAG_HIDDEN);
    Application::GetInstance().SendMediaCommand("get_playlist", 0);
}
void PixelTouchUI::OnPlaylistClose(lv_event_t* event) {
    auto* self = static_cast<PixelTouchUI*>(lv_event_get_user_data(event));
    if (self->playlist_panel_ != nullptr) {
        lv_obj_add_flag(self->playlist_panel_, LV_OBJ_FLAG_HIDDEN);
    }
}
void PixelTouchUI::OnPlaylistPrevious(lv_event_t* event) {
    auto* self = static_cast<PixelTouchUI*>(lv_event_get_user_data(event));
    if (self->playlist_offset_ < 6) return;
    self->playlist_offset_ -= 6;
    self->playlist_count_ = 0;
    self->UpdatePlaylist();
    Application::GetInstance().SendMediaCommand("get_playlist", self->playlist_offset_);
}
void PixelTouchUI::OnPlaylistNext(lv_event_t* event) {
    auto* self = static_cast<PixelTouchUI*>(lv_event_get_user_data(event));
    if (self->playlist_offset_ + 6 >= self->media_playlist_length_) return;
    self->playlist_offset_ += 6;
    self->playlist_count_ = 0;
    self->UpdatePlaylist();
    Application::GetInstance().SendMediaCommand("get_playlist", self->playlist_offset_);
}
void PixelTouchUI::OnPlaylistSelect(lv_event_t* event) {
    auto* self = static_cast<PixelTouchUI*>(lv_event_get_user_data(event));
    auto* button = lv_event_get_target_obj(event);
    int index = static_cast<int>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(button))) - 1;
    if (index < 0 || index >= self->media_playlist_length_) return;
    Application::GetInstance().SendMediaCommand("select", index);
    if (self->playlist_panel_ != nullptr) {
        lv_obj_add_flag(self->playlist_panel_, LV_OBJ_FLAG_HIDDEN);
    }
}

void PixelTouchUI::OnMediaSeek(lv_event_t* event) {
    auto* self = static_cast<PixelTouchUI*>(lv_event_get_user_data(event));
    if (self->player_seek_slider_ == nullptr || self->media_duration_ms_ <= 0) return;
    int ratio = lv_slider_get_value(self->player_seek_slider_);
    int64_t targetMs = self->media_duration_ms_ * ratio / 1000;
    Application::GetInstance().SendMediaCommand("seek", -1, targetMs);
}
