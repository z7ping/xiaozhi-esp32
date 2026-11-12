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
    twilight_ = settings.GetBool("twilight", false);
    root_ = lv_obj_create(screen);
    lv_obj_set_pos(root_, 0, 0);
    lv_obj_set_size(root_, 320, 240);
    StylePanel(root_, kBg, kBg);
    lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);

    body_ = lv_obj_create(root_);
    lv_obj_set_pos(body_, 0, 0);
    lv_obj_set_size(body_, 320, 240);
    lv_obj_remove_style_all(body_);
    lv_obj_clear_flag(body_, LV_OBJ_FLAG_SCROLLABLE);

    refresh_timer_ = lv_timer_create([](lv_timer_t* timer) {
        auto* self = static_cast<PixelTouchUI*>(lv_timer_get_user_data(timer));
        self->Refresh();
    }, 5000, this);
    lv_timer_pause(refresh_timer_);
}

PixelTouchUI::~PixelTouchUI() {
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
    lv_obj_set_size(robot_area_, 116, 100);
    lv_obj_set_pos(robot_area_, 0, 0);
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
    player_status_ = nullptr;
    player_position_ = nullptr;
    player_toggle_ = nullptr;
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
    if (page == Page::Calendar) RefreshCalendar();
    if (page == Page::Player) {
        UpdatePlayer();
        Application::GetInstance().SendMediaCommand("get_state");
    }
    ApplyPalette();
}

void PixelTouchUI::QueueNavigate(Page page) {
    queued_page_ = page;
    // Do not delete the clicked LVGL object while its event is being dispatched.
    lv_async_call([](void* data) {
        auto* self = static_cast<PixelTouchUI*>(data);
        if (!lv_obj_has_flag(self->root_, LV_OBJ_FLAG_HIDDEN)) {
            self->Navigate(self->queued_page_);
        }
    }, this);
}

void PixelTouchUI::BuildHeader(const char* title, bool back) {
    auto* bar = lv_obj_create(body_);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_size(bar, 320, 32);
    StylePanel(bar, kPanel, kBorder);
    if (back) {
        Button(bar, "", 1, 1, 43, 29, OnHome, this, MATERIAL_SYMBOLS_ARROW_BACK);
    }
    Label(bar, title, back ? 52 : 12, 3, back ? 215 : 260, 26);
}

void PixelTouchUI::BuildBackButton() {
    Button(body_, "返回", 8, 194, 92, 38, OnHome, this,
           MATERIAL_SYMBOLS_ARROW_BACK);
}

void PixelTouchUI::BuildHome() {
    BuildHeader("TIAN TIAN / OS");
    Button(body_, "", 270, 1, 48, 30, OnSettings, this, MATERIAL_SYMBOLS_SETTINGS);

    Label(body_, "HELLO, TIAN TIAN", 12, 43, 165, 24, kMuted);
    time_label_ = Label(body_, "--:--", 12, 72, 170, 42);
    date_label_ = Label(body_, "", 12, 119, 166, 26, kMuted);
    weather_label_ = Label(body_, "天气未就绪", 12, 149, 172, 24, kAccent);
    lunar_label_ = Label(body_, "", 12, 178, 175, 20, kMuted);

    robot_slot_ = Button(body_, "", 194, 40, 116, 104, OnTalk, this);
    Label(body_, "点我说话", 205, 153, 102, 27, kAccent);

    Button(body_, "听故事", 8, 199, 98, 38, OnPlayer, this,
           MATERIAL_SYMBOLS_HEADPHONES);
    Button(body_, "听儿歌", 111, 199, 98, 38, OnPlayer, this,
           MATERIAL_SYMBOLS_MUSIC_NOTE);
    Button(body_, "万年历", 214, 199, 98, 38, OnCalendar, this,
           MATERIAL_SYMBOLS_CALENDAR_MONTH);
}

void PixelTouchUI::BuildCalendar() {
    BuildHeader("万年历", true);
    Button(body_, "今天", 261, 35, 53, 28, OnCalendarToday, this);
    Button(body_, "", 10, 35, 43, 29, OnCalendarPrevious, this,
           MATERIAL_SYMBOLS_KEYBOARD_ARROW_LEFT);
    calendar_month_label_ = Label(body_, "", 89, 39, 151, 25, kAccent);
    Button(body_, "", 210, 35, 43, 29, OnCalendarNext, this,
           MATERIAL_SYMBOLS_KEYBOARD_ARROW_RIGHT);

    static constexpr const char* week[] = {"日", "一", "二", "三", "四", "五", "六"};
    for (int col = 0; col < 7; ++col) {
        Label(body_, week[col], 23 + col * 43, 68, 28, 19, kMuted);
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
    const int days_in_month = DaysInMonth(calendar_year_, calendar_month_);
    for (int day = 1; day <= days_in_month; ++day) {
        const int offset = first + day - 1;
        const int col = offset % 7;
        const int row = offset / 7;
        auto* cell = lv_label_create(body_);
        calendar_cells_[day] = cell;
        lv_obj_set_pos(cell, 9 + col * 43, 89 + row * 18);
        lv_obj_set_size(cell, 40, 18);
        lv_obj_set_style_text_align(cell, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(cell, lv_color_hex(kText), 0);
        lv_obj_add_flag(cell, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_user_data(cell, reinterpret_cast<void*>(static_cast<intptr_t>(day)));
        lv_obj_add_event_cb(cell, OnCalendarDay, LV_EVENT_CLICKED, this);
        char day_text[4];
        std::snprintf(day_text, sizeof(day_text), "%d", day);
        lv_label_set_text(cell, day_text);
        const bool picked = selected_year_ == calendar_year_ &&
            selected_month_ == calendar_month_ && selected_day_ == day;
        const bool today = local.tm_year + 1900 == calendar_year_ &&
            local.tm_mon + 1 == calendar_month_ && local.tm_mday == day;
        if (picked) {
            lv_obj_set_style_bg_color(cell, lv_color_hex(kAccent), 0);
            lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
            lv_obj_set_style_text_color(cell, lv_color_hex(kBg), 0);
        } else if (today) {
            lv_obj_set_style_border_color(cell, lv_color_hex(kAccent), 0);
            lv_obj_set_style_border_width(cell, 1, 0);
        }
    }
    calendar_selected_label_ = Label(body_, "", 9, 207, 207, 27, kAccent);
    Button(body_, "黄历详情", 216, 204, 99, 32, OnCalendarDetail, this);
    calendar_details_popup_ = lv_obj_create(body_);
    lv_obj_set_pos(calendar_details_popup_, 10, 71);
    lv_obj_set_size(calendar_details_popup_, 300, 135);
    StylePanel(calendar_details_popup_, kPanel, kAccent);
    calendar_date_label_ = Label(calendar_details_popup_, "", 12, 10, 231, 25, kAccent);
    calendar_detail_label_ = Label(calendar_details_popup_, "", 12, 43, 274, 76, kText);
    Button(calendar_details_popup_, "", 253, 5, 39, 30, OnCalendarClose, this,
           MATERIAL_SYMBOLS_CLOSE);
    lv_obj_add_flag(calendar_details_popup_, LV_OBJ_FLAG_HIDDEN);
    RefreshCalendar();
}

void PixelTouchUI::BuildPlayer() {
    BuildHeader("媒体播放器");
    player_title_ = Label(body_, "正在同步播放状态", 14, 45, 290, 30);
    player_status_ = Label(body_, "连接服务端中", 14, 80, 290, 28, kAccent);
    player_position_ = Label(body_, "00:00", 14, 112, 290, 26, kMuted);

    Button(body_, "上一首", 8, 148, 98, 38, OnMediaPrev, this,
           MATERIAL_SYMBOLS_SKIP_PREVIOUS);
    player_toggle_ = Button(body_, "播放", 111, 148, 98, 38, OnMediaToggle, this,
                            MATERIAL_SYMBOLS_PLAY_ARROW);
    Button(body_, "下一首", 214, 148, 98, 38, OnMediaNext, this,
           MATERIAL_SYMBOLS_SKIP_NEXT);
    BuildBackButton();
    if (!child_mode_) {
        Button(body_, "停止", 111, 194, 98, 38, OnMediaStop, this, MATERIAL_SYMBOLS_STOP);
    }
}

void PixelTouchUI::SetMediaState(const char* state, const char* title,
                                  int64_t position_ms, int current_index,
                                  int playlist_length, const char* error) {
    media_status_ = state != nullptr ? state : "";
    media_title_ = title != nullptr ? title : "";
    media_error_ = error != nullptr ? error : "";
    media_position_ms_ = position_ms >= 0 ? position_ms : 0;
    media_current_index_ = current_index;
    media_playlist_length_ = playlist_length;
    if (page_ == Page::Player && !lv_obj_has_flag(root_, LV_OBJ_FLAG_HIDDEN)) {
        UpdatePlayer();
    }
}

void PixelTouchUI::UpdatePlayer() {
    if (player_title_ == nullptr) return;
    SetIfChanged(player_title_, media_title_.empty() ? "暂无播放内容"
                                                     : media_title_.c_str());
    const char* status = "尚无播放数据";
    if (media_status_ == "playing") status = "正在播放";
    else if (media_status_ == "paused") status = "已暂停";
    else if (media_status_ == "stopped") status = "已停止";
    else if (media_status_ == "idle") status = "等待播放";
    else if (media_status_ == "error") status = "播放发生错误";
    SetIfChanged(player_status_, media_error_.empty() ? status : media_error_.c_str());
    const int64_t seconds = media_position_ms_ / 1000;
    char position[48];
    std::snprintf(position, sizeof(position), "进度 %02lld:%02lld",
                  static_cast<long long>(seconds / 60),
                  static_cast<long long>(seconds % 60));
    SetIfChanged(player_position_, position);
    if (player_toggle_ != nullptr) {
        auto* label = lv_obj_get_child(player_toggle_, 0);
        SetIfChanged(label, media_status_ == "playing" ? "暂停" : "播放");
    }
}

void PixelTouchUI::BuildSettings() {
    BuildHeader("家长设置", true);
    Label(body_, "儿童模式", 14, 40, 140, 25);
    Label(body_, "减少误触操作", 14, 61, 140, 18, kMuted);
    child_mode_switch_ = lv_switch_create(body_);
    lv_obj_set_pos(child_mode_switch_, 258, 47);
    lv_obj_set_size(child_mode_switch_, 47, 25);
    if (child_mode_) lv_obj_add_state(child_mode_switch_, LV_STATE_CHECKED);
    lv_obj_add_event_cb(child_mode_switch_, OnChildModeChanged, LV_EVENT_VALUE_CHANGED, this);

    Label(body_, "显示亮度", 14, 92, 134, 24);
    Label(body_, "默认音量", 14, 140, 134, 24);
    brightness_slider_ = lv_slider_create(body_);
    lv_obj_set_pos(brightness_slider_, 151, 97);
    lv_obj_set_size(brightness_slider_, 107, 15);
    lv_slider_set_range(brightness_slider_, 10, 100);
    if (auto* backlight = Board::GetInstance().GetBacklight()) {
        lv_slider_set_value(brightness_slider_, backlight->brightness(), LV_ANIM_OFF);
    }

    volume_slider_ = lv_slider_create(body_);
    lv_obj_set_pos(volume_slider_, 151, 145);
    lv_obj_set_size(volume_slider_, 107, 15);
    lv_slider_set_range(volume_slider_, 0, 100);
    if (auto* codec = Board::GetInstance().GetAudioCodec()) {
        lv_slider_set_value(volume_slider_, codec->output_volume(), LV_ANIM_OFF);
    }

    for (auto* slider : {brightness_slider_, volume_slider_}) {
        lv_obj_set_style_bg_color(slider, lv_color_hex(kBorder), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_bg_color(slider, lv_color_hex(kAccent), LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(slider, lv_color_hex(kAccent), LV_PART_KNOB);
        lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_KNOB);
        lv_obj_set_style_radius(slider, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(slider, 0, LV_PART_INDICATOR);
        lv_obj_set_style_radius(slider, 0, LV_PART_KNOB);
        lv_obj_set_style_pad_all(slider, 4, LV_PART_KNOB);
        lv_obj_add_event_cb(slider, OnSliderChange, LV_EVENT_VALUE_CHANGED, this);
        lv_obj_add_event_cb(slider, OnSliderRelease, LV_EVENT_RELEASED, this);
    }
    brightness_value_ = Label(body_, "", 266, 89, 50, 27, kAccent);
    volume_value_ = Label(body_, "", 266, 137, 50, 27, kAccent);
    Label(body_, "暮光配色", 14, 188, 140, 25);
    Label(body_, "紫色科技主题", 14, 209, 175, 20, kMuted);
    twilight_switch_ = lv_switch_create(body_);
    lv_obj_set_pos(twilight_switch_, 258, 192);
    lv_obj_set_size(twilight_switch_, 47, 25);
    if (twilight_) lv_obj_add_state(twilight_switch_, LV_STATE_CHECKED);
    lv_obj_add_event_cb(twilight_switch_, OnTwilightChanged, LV_EVENT_VALUE_CHANGED, this);
    UpdateSliderValues();
}

void PixelTouchUI::RefreshHome() {
    std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);
    char output[64];
    if (local.tm_year >= 125) {
        std::snprintf(output, sizeof(output), "%02d:%02d", local.tm_hour, local.tm_min);
        SetIfChanged(time_label_, output);
        std::snprintf(output, sizeof(output), "%d月%d日", local.tm_mon + 1, local.tm_mday);
        SetIfChanged(date_label_, output);
    }
#if CONFIG_WEATHER_DASHBOARD
    const auto weather = WeatherService::GetInstance().GetSnapshot();
    if (weather.valid) {
        std::snprintf(output, sizeof(output), "%d°C %s", weather.temperature,
                      weather.weather_text.c_str());
        SetIfChanged(weather_label_, output);
    } else {
        SetIfChanged(weather_label_, weather.no_key ? "天气未配置" : "天气暂无数据");
    }
    const auto almanac = AlmanacService::GetInstance().GetSnapshot();
    SetIfChanged(lunar_label_,
                 almanac.valid ? almanac.lunar_date.c_str() : "农历暂无数据");
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
    if (year < 2000 || year > 2099) return;
    calendar_year_ = selected_year_ = year;
    calendar_month_ = selected_month_ = month;
    selected_day_ = std::min(selected_day_, DaysInMonth(year, month));
    QueueNavigate(Page::Calendar);
}

void PixelTouchUI::Refresh() {
    if (lv_obj_has_flag(root_, LV_OBJ_FLAG_HIDDEN)) return;
    if (page_ == Page::Player) {
        // Five-second state requests are tiny JSON messages over the existing
        // control transport. Playback/audio buffers remain on the Go server.
        Application::GetInstance().SendMediaCommand("get_state");
    } else if (page_ == Page::Calendar && calendar_details_popup_ != nullptr &&
               !lv_obj_has_flag(calendar_details_popup_, LV_OBJ_FLAG_HIDDEN)) {
        RefreshCalendar(); // Read the one-slot asynchronous date lookup result.
    } else if (++refresh_ticks_ >= 6) {
        refresh_ticks_ = 0;
        if (page_ == Page::Home) RefreshHome();
        if (page_ == Page::Calendar) RefreshCalendar();
    }
}

void PixelTouchUI::UpdateSliderValues() {
    char text[8];
    if (brightness_slider_ != nullptr) {
        std::snprintf(text, sizeof(text), "%d%%", lv_slider_get_value(brightness_slider_));
        SetIfChanged(brightness_value_, text);
    }
    if (volume_slider_ != nullptr) {
        std::snprintf(text, sizeof(text), "%d%%", lv_slider_get_value(volume_slider_));
        SetIfChanged(volume_value_, text);
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
void PixelTouchUI::OnTalk(lv_event_t*) {
    Application::GetInstance().ToggleChatState();
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
    } else if (target == self->volume_slider_) {
        if (auto* codec = Board::GetInstance().GetAudioCodec()) {
            codec->SetOutputVolume(lv_slider_get_value(self->volume_slider_));
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
    self->UpdateCalendarSelection();
    self->RefreshCalendar();
}
void PixelTouchUI::OnCalendarDetail(lv_event_t* event) {
    auto* self = static_cast<PixelTouchUI*>(lv_event_get_user_data(event));
#if CONFIG_WEATHER_DASHBOARD
    if (!self->IsSelectedToday()) {
        AlmanacService::GetInstance().RequestDate(
            self->selected_year_, self->selected_month_, self->selected_day_);
    }
#endif
    self->RefreshCalendar();
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
