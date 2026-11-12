#include "pixel_robot_display.h"
#include "pixel_touch_ui.h"
#include "display/lvgl_display/lvgl_theme.h"

#include <material_symbols.h>

#if CONFIG_LCDWIKI_ES3C28P_LANDSCAPE && CONFIG_USE_WECHAT_MESSAGE_STYLE

#include <cstring>
#if CONFIG_LCDWIKI_ES3C28P_RESOURCE_DIAGNOSTICS
#include "application.h"

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>
#endif

namespace {
constexpr uint32_t kBackground = 0x071326;
constexpr uint32_t kRobot = 0x319da1;
constexpr uint32_t kText = 0xd4e5e6;
constexpr uint32_t kUserBubble = 0x28616d;
constexpr uint32_t kAssistantBubble = 0x1a2c3c;

lv_obj_t* PixelRect(lv_obj_t* parent, int x, int y, int w, int h, uint32_t color) {
    lv_obj_t* rect = lv_obj_create(parent);
    lv_obj_remove_style_all(rect);
    lv_obj_set_pos(rect, x, y);
    lv_obj_set_size(rect, w, h);
    lv_obj_set_style_bg_color(rect, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(rect, LV_OPA_COVER, 0);
    lv_obj_clear_flag(rect, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(rect, LV_OBJ_FLAG_CLICKABLE);
    return rect;
}
}  // namespace

PixelRobotDisplay::~PixelRobotDisplay() {
    if (chat_player_button_ != nullptr) {
        DisplayLockGuard lock(this);
        lv_obj_delete(chat_player_button_);
        chat_player_button_ = nullptr;
    }
    if (touch_ui_) {
        DisplayLockGuard lock(this);
        touch_ui_.reset();
    }
    if (blink_timer_ != nullptr) {
        DisplayLockGuard lock(this);
        lv_timer_delete(blink_timer_);
        blink_timer_ = nullptr;
    }
#if CONFIG_LCDWIKI_ES3C28P_RESOURCE_DIAGNOSTICS
    if (resource_timer_ != nullptr) {
        DisplayLockGuard lock(this);
        lv_timer_delete(resource_timer_);
        resource_timer_ = nullptr;
    }
#endif
}

void PixelRobotDisplay::SetupUI() {
    if (setup_ui_called_)
        return;
    SpiLcdDisplay::SetupUI();
    DisplayLockGuard lock(this);

    // Insert a fixed-height portrait between the existing status bar and chat.
    robot_area_ = lv_obj_create(container_);
    lv_obj_remove_style_all(robot_area_);
    lv_obj_set_size(robot_area_, 320, 100);
    lv_obj_clear_flag(robot_area_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(robot_area_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_move_to_index(robot_area_, 1);

    lv_obj_t* robot = lv_obj_create(robot_area_);
    lv_obj_remove_style_all(robot);
    lv_obj_set_size(robot, 104, 96);
    lv_obj_clear_flag(robot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(robot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(robot);

    // Pixel-grid silhouette follows the approved antenna/face/feet design.
    PixelRect(robot, 22, 2, 8, 8, kRobot);
    PixelRect(robot, 26, 10, 4, 8, kRobot);
    PixelRect(robot, 74, 2, 8, 8, kRobot);
    PixelRect(robot, 74, 10, 4, 8, kRobot);
    PixelRect(robot, 28, 18, 48, 5, kRobot);
    PixelRect(robot, 23, 23, 58, 5, kRobot);
    PixelRect(robot, 18, 28, 68, 40, kRobot);
    PixelRect(robot, 23, 68, 58, 5, kRobot);
    PixelRect(robot, 28, 73, 48, 5, kRobot);
    PixelRect(robot, 10, 31, 8, 30, kRobot);
    PixelRect(robot, 86, 31, 8, 30, kRobot);
    PixelRect(robot, 28, 29, 48, 35, kBackground);
    PixelRect(robot, 33, 78, 38, 5, kRobot);
    PixelRect(robot, 29, 83, 8, 5, kRobot);
    PixelRect(robot, 67, 83, 8, 5, kRobot);
    PixelRect(robot, 25, 88, 8, 5, kRobot);
    PixelRect(robot, 71, 88, 8, 5, kRobot);

    eyes_[0] = PixelRect(robot, 34, 37, 9, 9, kRobot);
    eyes_[1] = PixelRect(robot, 61, 37, 9, 9, kRobot);
    pupils_[0] = PixelRect(robot, 37, 40, 3, 3, kBackground);
    pupils_[1] = PixelRect(robot, 64, 40, 3, 3, kBackground);
    PixelRect(robot, 43, 53, 4, 3, kRobot);
    mouth_ = PixelRect(robot, 47, 56, 10, 3, kRobot);
    PixelRect(robot, 57, 53, 4, 3, kRobot);

    HideDefaultEmotion();
    ApplyChatColors();
    blink_timer_ = lv_timer_create(
        [](lv_timer_t* timer) {
            auto* self = static_cast<PixelRobotDisplay*>(lv_timer_get_user_data(timer));
            self->blinking_ = !self->blinking_;
            self->SetEyes(self->blinking_);
            lv_timer_set_period(timer, self->blinking_ ? 130 : 3500);
        },
        3500, this);
    touch_ui_ = std::make_unique<PixelTouchUI>(lv_screen_active());

    // During streamed media the app is in 'speaking', not 'idle'.
    // Keep an entry point on the existing chat surface so the player
    // remains accessible while the Go server is streaming audio.
    chat_player_button_ = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(chat_player_button_);
    lv_obj_set_pos(chat_player_button_, 273, 32);
    lv_obj_set_size(chat_player_button_, 42, 34);
    lv_obj_set_style_bg_color(chat_player_button_, lv_color_hex(0x10263B), 0);
    lv_obj_set_style_bg_opa(chat_player_button_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(chat_player_button_, lv_color_hex(0x80E9DD), 0);
    lv_obj_set_style_border_width(chat_player_button_, 1, 0);
    lv_obj_add_flag(chat_player_button_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(chat_player_button_, LV_OBJ_FLAG_SCROLLABLE);
    auto* music_icon = lv_label_create(chat_player_button_);
    lv_label_set_text(music_icon, MATERIAL_SYMBOLS_MUSIC_NOTE);
    auto* theme = static_cast<LvglTheme*>(GetTheme());
    lv_obj_set_style_text_font(music_icon, theme->icon_font()->font(), 0);
    lv_obj_set_style_text_color(music_icon, lv_color_hex(0x80E9DD), 0);
    lv_obj_center(music_icon);
    lv_obj_add_event_cb(chat_player_button_, [](lv_event_t* event) {
        auto* self = static_cast<PixelRobotDisplay*>(lv_event_get_user_data(event));
        if (self->touch_ui_ != nullptr) {
            self->touch_ui_->ShowPlayer(self->robot_area_, self->container_);
        }
    }, LV_EVENT_CLICKED, this);
    lv_obj_add_flag(chat_player_button_, LV_OBJ_FLAG_HIDDEN);
#if CONFIG_LCDWIKI_ES3C28P_RESOURCE_DIAGNOSTICS
    // Diagnostic-only: sample in the existing LVGL task; no extra task or
    // full-screen refresh. Heap minimums are historical low-water marks.
    auto sample_resources = [](lv_timer_t*) {
        const auto state = Application::GetInstance().GetDeviceState();
        constexpr uint32_t internal = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
        constexpr uint32_t psram = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
        ESP_LOGI("ES3C28P_RES",
                 "uptime_ms=%llu state=%s internal_free=%u internal_min=%u "
                 "internal_largest=%u psram_free=%u psram_min=%u psram_largest=%u",
                 static_cast<unsigned long long>(esp_timer_get_time() / 1000),
                 DeviceStateMachine::GetStateName(state),
                 static_cast<unsigned>(heap_caps_get_free_size(internal)),
                 static_cast<unsigned>(heap_caps_get_minimum_free_size(internal)),
                 static_cast<unsigned>(heap_caps_get_largest_free_block(internal)),
                 static_cast<unsigned>(heap_caps_get_free_size(psram)),
                 static_cast<unsigned>(heap_caps_get_minimum_free_size(psram)),
                 static_cast<unsigned>(heap_caps_get_largest_free_block(psram)));
    };
    sample_resources(nullptr);
    resource_timer_ = lv_timer_create(sample_resources, 3000, nullptr);
#endif
}

void PixelRobotDisplay::SetEyes(bool closed) {
    for (int i = 0; i < 2; ++i) {
        lv_obj_set_height(eyes_[i], closed ? 3 : 9);
        lv_obj_set_y(eyes_[i], closed ? 40 : 37);
        if (closed)
            lv_obj_add_flag(pupils_[i], LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_clear_flag(pupils_[i], LV_OBJ_FLAG_HIDDEN);
    }
}

void PixelRobotDisplay::HideDefaultEmotion() {
    if (emoji_label_ != nullptr)
        lv_obj_add_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);
    if (emoji_image_ != nullptr)
        lv_obj_add_flag(emoji_image_, LV_OBJ_FLAG_HIDDEN);
}

void PixelRobotDisplay::ApplyChatColors() {
    if (container_ == nullptr)
        return;
    lv_obj_set_style_bg_image_src(container_, nullptr, 0);
    lv_obj_set_style_bg_color(container_, lv_color_hex(kBackground), 0);
    lv_obj_set_style_bg_color(top_bar_, lv_color_hex(kBackground), 0);
    lv_obj_set_style_bg_color(content_, lv_color_hex(kBackground), 0);
    lv_obj_set_style_bg_opa(content_, LV_OPA_COVER, 0);
    for (auto* label :
         {network_label_, status_label_, notification_label_, mute_label_, battery_label_}) {
        if (label != nullptr)
            lv_obj_set_style_text_color(label, lv_color_hex(kText), 0);
    }
    StyleMessages();
}

void PixelRobotDisplay::StyleMessages() {
    if (content_ == nullptr)
        return;
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(content_); ++i) {
        lv_obj_t* bubble = lv_obj_get_child(content_, i);
        const char* role = static_cast<const char*>(lv_obj_get_user_data(bubble));
        if (role == nullptr && lv_obj_get_child_cnt(bubble) > 0) {
            bubble = lv_obj_get_child(bubble, 0);
            role = static_cast<const char*>(lv_obj_get_user_data(bubble));
        }
        if (role == nullptr || (strcmp(role, "user") != 0 && strcmp(role, "assistant") != 0 &&
                                strcmp(role, "system") != 0))
            continue;
        lv_obj_set_style_bg_color(
            bubble, lv_color_hex(strcmp(role, "user") == 0 ? kUserBubble : kAssistantBubble), 0);
        lv_obj_set_style_bg_opa(bubble, LV_OPA_COVER, 0);
        if (lv_obj_get_child_cnt(bubble) > 0) {
            lv_obj_set_style_text_color(lv_obj_get_child(bubble, 0), lv_color_hex(kText), 0);
        }
    }
}

void PixelRobotDisplay::SetTheme(Theme* theme) {
    SpiLcdDisplay::SetTheme(theme);
    DisplayLockGuard lock(this);
    if (robot_area_ != nullptr) {
        ApplyChatColors();
        HideDefaultEmotion();
    }
}

void PixelRobotDisplay::SetChatMessage(const char* role, const char* content) {
    SpiLcdDisplay::SetChatMessage(role, content);
    DisplayLockGuard lock(this);
    StyleMessages();
    HideDefaultEmotion();
}

void PixelRobotDisplay::RemoveChatMessage(const char* role, const char* content) {
    SpiLcdDisplay::RemoveChatMessage(role, content);
    DisplayLockGuard lock(this);
    HideDefaultEmotion();
}

void PixelRobotDisplay::ClearChatMessages() {
    SpiLcdDisplay::ClearChatMessages();
    DisplayLockGuard lock(this);
    HideDefaultEmotion();
}

bool PixelRobotDisplay::SetEmotion(const char* emotion) {
    if (robot_area_ == nullptr || emotion == nullptr)
        return false;
    DisplayLockGuard lock(this);
    // Keep the same character on screen; use a compact mouth for thinking/sadness.
    bool subdued = strcmp(emotion, "thinking") == 0 || strcmp(emotion, "sad") == 0;
    bool supported = subdued || strcmp(emotion, "neutral") == 0 ||
                     strcmp(emotion, "robot_2") == 0 || strcmp(emotion, "happy") == 0;
    if (!supported)
        return false;
    lv_obj_set_width(mouth_, subdued ? 6 : 10);
    HideDefaultEmotion();
    return true;
}

void PixelRobotDisplay::SetMediaState(const char* state, const char* title,
                                      int64_t position_ms, int64_t duration_ms,
                                      int current_index, int playlist_length,
                                      const char* error) {
    DisplayLockGuard lock(this);
    if (touch_ui_ != nullptr) {
        touch_ui_->SetMediaState(state, title, position_ms, duration_ms,
                                 current_index, playlist_length, error);
    }
}

void PixelRobotDisplay::SetMediaPlaylist(int offset, const std::vector<std::string>& titles) {
    DisplayLockGuard lock(this);
    if (touch_ui_) touch_ui_->SetMediaPlaylist(offset, titles);
}

#if CONFIG_WEATHER_DASHBOARD
void PixelRobotDisplay::ShowDashboard() {
    if (touch_ui_ == nullptr || robot_area_ == nullptr) {
        SpiLcdDisplay::ShowDashboard();
        return;
    }
    DisplayLockGuard lock(this);
    // Preserve existing weather/almanac services; replace only the idle view.
    touch_ui_->Show(robot_area_, container_);
    if (chat_player_button_ != nullptr) {
        lv_obj_add_flag(chat_player_button_, LV_OBJ_FLAG_HIDDEN);
    }
    // Keep the existing low-battery warning above the idle touch surface.
    if (low_battery_popup_ != nullptr) {
        auto* screen = lv_obj_get_parent(low_battery_popup_);
        lv_obj_move_to_index(low_battery_popup_, lv_obj_get_child_count(screen) - 1);
    }
}

void PixelRobotDisplay::HideDashboard() {
    if (touch_ui_ == nullptr) {
        SpiLcdDisplay::HideDashboard();
        return;
    }
    DisplayLockGuard lock(this);
    touch_ui_->Hide();
    if (chat_player_button_ != nullptr) {
        lv_obj_clear_flag(chat_player_button_, LV_OBJ_FLAG_HIDDEN);
    }
}
#endif

#endif
