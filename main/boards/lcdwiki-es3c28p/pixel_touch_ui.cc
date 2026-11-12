#include "pixel_touch_ui.h"

#include "application.h"
#include "audio_codec.h"
#include "board.h"
#include "display/lvgl_display/lvgl_theme.h"

#include <material_symbols.h>

#if CONFIG_WEATHER_DASHBOARD
#include "almanac_service.h"
#include "weather_service.h"
#endif

#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>

namespace {
constexpr uint32_t kBg = 0x071326;
constexpr uint32_t kPanel = 0x10263B;
constexpr uint32_t kBorder = 0x345C74;
constexpr uint32_t kText = 0xEBF9FE;
constexpr uint32_t kMuted = 0xACC8D6;
constexpr uint32_t kAccent = 0x80E9DD;

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
    }, 30000, this);
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
    lv_obj_move_foreground(root_);
    Navigate(Page::Home);
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
    Refresh();
}

void PixelTouchUI::QueueNavigate(Page page) {
    queued_page_ = page;
    // Do not delete the clicked LVGL object while its event is being dispatched.
    lv_async_call([](void* data) {
        auto* self = static_cast<PixelTouchUI*>(data);
        self->Navigate(self->queued_page_);
    }, this);
}

void PixelTouchUI::BuildHeader(const char* title) {
    auto* bar = lv_obj_create(body_);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_size(bar, 320, 32);
    StylePanel(bar, kPanel, kBorder);
    Label(bar, title, 12, 3, 260, 26);
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
    BuildHeader("今日万年历");
    calendar_date_label_ = Label(body_, "读取日期...", 14, 48, 290, 30, kAccent);
    calendar_detail_label_ = Label(body_, "正在读取黄历", 14, 91, 292, 96, kText);
    BuildBackButton();
}

void PixelTouchUI::BuildPlayer() {
    BuildHeader("媒体播放器");
    Label(body_, "播放状态尚未同步", 14, 62, 292, 34, kAccent);
    Label(body_, "等待 Go 服务端的播放状态接口", 14, 106, 292, 54, kMuted);
    // No fake controls: existing server-side playback remains unchanged.
    BuildBackButton();
}

void PixelTouchUI::BuildSettings() {
    BuildHeader("家长设置");

    Label(body_, "显示亮度", 14, 61, 131, 26);
    Label(body_, "默认音量", 14, 119, 131, 26);

    brightness_slider_ = lv_slider_create(body_);
    lv_obj_set_pos(brightness_slider_, 153, 62);
    lv_obj_set_size(brightness_slider_, 105, 15);
    lv_slider_set_range(brightness_slider_, 10, 100);
    lv_slider_set_value(brightness_slider_, Board::GetInstance().GetBacklight()->brightness(),
                        LV_ANIM_OFF);

    volume_slider_ = lv_slider_create(body_);
    lv_obj_set_pos(volume_slider_, 153, 120);
    lv_obj_set_size(volume_slider_, 105, 15);
    lv_slider_set_range(volume_slider_, 0, 100);
    lv_slider_set_value(volume_slider_, Board::GetInstance().GetAudioCodec()->output_volume(),
                        LV_ANIM_OFF);

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
    brightness_value_ = Label(body_, "", 266, 57, 49, 29, kAccent);
    volume_value_ = Label(body_, "", 266, 115, 49, 29, kAccent);
    Label(body_, "松开滑块后保存设置", 14, 167, 292, 25, kMuted);
    UpdateSliderValues();
    BuildBackButton();
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
#if CONFIG_WEATHER_DASHBOARD
    std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);
    char date[48];
    if (local.tm_year >= 125) {
        std::snprintf(date, sizeof(date), "%d年%d月%d日",
                      local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
        SetIfChanged(calendar_date_label_, date);
    }
    const auto info = AlmanacService::GetInstance().GetSnapshot();
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
#endif
}

void PixelTouchUI::Refresh() {
    if (lv_obj_has_flag(root_, LV_OBJ_FLAG_HIDDEN)) return;
    if (page_ == Page::Home) RefreshHome();
    if (page_ == Page::Calendar) RefreshCalendar();
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
