#pragma once

#include "display/lcd_display.h"

#if CONFIG_LCDWIKI_ES3C28P_LANDSCAPE && CONFIG_USE_WECHAT_MESSAGE_STYLE

class PixelTouchUI;

class PixelRobotDisplay : public SpiLcdDisplay {
public:
    using SpiLcdDisplay::SpiLcdDisplay;
    ~PixelRobotDisplay() override;

    void SetupUI() override;
    void SetTheme(Theme* theme) override;
    void SetChatMessage(const char* role, const char* content) override;
    void RemoveChatMessage(const char* role, const char* content) override;
    void ClearChatMessages() override;
    bool SetEmotion(const char* emotion) override;
    void SetMediaState(const char* state, const char* title, int64_t position_ms,
                       int current_index, int playlist_length, const char* error) override;
#if CONFIG_WEATHER_DASHBOARD
    void ShowDashboard() override;
    void HideDashboard() override;
    bool UseLegacyDashboard() const override { return false; }
#endif

private:
    void ApplyChatColors();
    void StyleMessages();
    void HideDefaultEmotion();
    void SetEyes(bool closed);

    std::unique_ptr<PixelTouchUI> touch_ui_;
    lv_obj_t* robot_area_ = nullptr;
    lv_obj_t* chat_player_button_ = nullptr;
    lv_obj_t* eyes_[2] = {nullptr, nullptr};
    lv_obj_t* pupils_[2] = {nullptr, nullptr};
    lv_obj_t* mouth_ = nullptr;
    lv_timer_t* blink_timer_ = nullptr;
#if CONFIG_LCDWIKI_ES3C28P_RESOURCE_DIAGNOSTICS
    lv_timer_t* resource_timer_ = nullptr;
#endif
    bool blinking_ = false;
};

#endif
