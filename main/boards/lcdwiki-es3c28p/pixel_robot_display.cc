#include "pixel_robot_display.h"
#include "pixel_touch_ui.h"
#include "display/lvgl_display/lvgl_theme.h"

#include <material_symbols.h>

#if CONFIG_LCDWIKI_ES3C28P_LANDSCAPE && CONFIG_USE_WECHAT_MESSAGE_STYLE

#include <cstring>
#include "application.h"
#if CONFIG_LCDWIKI_ES3C28P_RESOURCE_DIAGNOSTICS

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

    // V1.1 mascot: 96px orbit, 78px circular face, soft eyes and smile.
    // Keep one reusable robot object across chat and idle pages.
    lv_obj_t* robot = lv_obj_create(robot_area_);
    lv_obj_remove_style_all(robot);
    lv_obj_set_size(robot, 104, 124);
    lv_obj_clear_flag(robot, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(robot);

    auto* orbit = lv_obj_create(robot);
    lv_obj_remove_style_all(orbit);
    lv_obj_set_pos(orbit, 4, 0);
    lv_obj_set_size(orbit, 96, 96);
    lv_obj_set_style_radius(orbit, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(orbit, 1, 0);
    lv_obj_set_style_border_color(orbit, lv_color_hex(0x418b89), 0);
    lv_obj_clear_flag(orbit, LV_OBJ_FLAG_CLICKABLE);

    auto* face = PixelRect(robot, 13, 9, 78, 78, 0x183b59);
    lv_obj_set_style_radius(face, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_grad_color(face, lv_color_hex(0x356d8f), 0);
    lv_obj_set_style_bg_grad_dir(face, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(face, 1, 0);
    lv_obj_set_style_border_color(face, lv_color_hex(0x467c95), 0);

    for (int i = 0; i < 2; ++i) {
        auto* cheek = PixelRect(robot, i == 0 ? 25 : 70, 58, 9, 4, 0xd77e91);
        lv_obj_set_style_radius(cheek, LV_RADIUS_CIRCLE, 0);
        eyes_[i] = PixelRect(robot, i == 0 ? 34 : 61, 36, 9, 13, 0xaffbf4);
        lv_obj_set_style_radius(eyes_[i], LV_RADIUS_CIRCLE, 0);
        // Preserve existing blink lifecycle without a second eye timer.
        pupils_[i] = PixelRect(robot, i == 0 ? 37 : 64, 40, 1, 1, 0xaffbf4);
        lv_obj_add_flag(pupils_[i], LV_OBJ_FLAG_HIDDEN);
    }
    // Low-memory native polyline approximates the original SVG smile.
    static const lv_point_precise_t smile_points[] = {
        {0, 0}, {8, 9}, {16, 12}, {24, 9}, {32, 0},
    };
    mouth_ = lv_line_create(robot);
    lv_line_set_points(mouth_, smile_points, 5);
    lv_obj_set_pos(mouth_, 36, 53);
    lv_obj_set_style_line_color(mouth_, lv_color_hex(0xb0f8f5), 0);
    lv_obj_set_style_line_width(mouth_, 3, 0);
    lv_obj_set_style_line_rounded(mouth_, true, 0);
    lv_obj_clear_flag(mouth_, LV_OBJ_FLAG_CLICKABLE);

    auto* caption = lv_label_create(robot);
    lv_label_set_text(caption, "点我说话");
    lv_obj_set_style_text_color(caption, lv_color_hex(0xa5f2e9), 0);
    lv_obj_set_width(caption, 104);
    lv_obj_set_style_text_align(caption, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(caption, 0, 104);

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
        lv_obj_set_height(eyes_[i], closed ? 2 : 13);
        lv_obj_set_y(eyes_[i], closed ? 41 : 36);
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
    lv_obj_set_style_opa(mouth_, subdued ? LV_OPA_60 : LV_OPA_COVER, 0);
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
    const auto state = Application::GetInstance().GetDeviceState();
    if (state == kDeviceStateConnecting || state == kDeviceStateListening ||
        state == kDeviceStateSpeaking) {
        // Replace the old chat-only surface with the V1.1 voice state screen.
        // Never interrupt a deliberately opened touch player during playback.
        if (!touch_ui_->IsPlayerVisible()) {
            touch_ui_->ShowVoice(robot_area_, container_);
        }
        // Keep the existing media entry accessible during audio streaming.
        if (chat_player_button_ != nullptr) {
            lv_obj_clear_flag(chat_player_button_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_to_index(chat_player_button_,
                                 lv_obj_get_child_count(lv_screen_active()) - 1);
        }
        return;
    }
    touch_ui_->Hide();
    if (chat_player_button_ != nullptr) {
        lv_obj_clear_flag(chat_player_button_, LV_OBJ_FLAG_HIDDEN);
    }
}
#endif

#endif
