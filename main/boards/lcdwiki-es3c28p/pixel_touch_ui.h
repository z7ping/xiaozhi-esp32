#pragma once

#include <lvgl.h>

// Lightweight touch pages for the 320x240 LCDWiki landscape variant.
// All calls take place under the existing display/LVGL lock.
class PixelTouchUI {
public:
    explicit PixelTouchUI(lv_obj_t* screen);
    ~PixelTouchUI();

    void Show(lv_obj_t* robot_area, lv_obj_t* chat_container);
    void Hide();
    void Refresh();

private:
    enum class Page { Home, Calendar, Player, Settings };

    void Navigate(Page page);
    void QueueNavigate(Page page);
    void AttachRobot();
    void DetachRobot();
    void BuildHome();
    void BuildCalendar();
    void BuildPlayer();
    void BuildSettings();
    void BuildHeader(const char* title);
    void BuildBackButton();
    void RefreshHome();
    void RefreshCalendar();
    void UpdateSliderValues();

    static void OnSettings(lv_event_t* event);
    static void OnHome(lv_event_t* event);
    static void OnCalendar(lv_event_t* event);
    static void OnPlayer(lv_event_t* event);
    static void OnTalk(lv_event_t* event);
    static void OnSliderChange(lv_event_t* event);
    static void OnSliderRelease(lv_event_t* event);

    Page page_ = Page::Home;
    Page queued_page_ = Page::Home;
    lv_obj_t* root_ = nullptr;
    lv_obj_t* body_ = nullptr;
    lv_obj_t* robot_area_ = nullptr;
    lv_obj_t* chat_container_ = nullptr;
    lv_obj_t* robot_slot_ = nullptr;
    lv_obj_t* time_label_ = nullptr;
    lv_obj_t* date_label_ = nullptr;
    lv_obj_t* weather_label_ = nullptr;
    lv_obj_t* lunar_label_ = nullptr;
    lv_obj_t* calendar_date_label_ = nullptr;
    lv_obj_t* calendar_detail_label_ = nullptr;
    lv_obj_t* brightness_slider_ = nullptr;
    lv_obj_t* volume_slider_ = nullptr;
    lv_obj_t* brightness_value_ = nullptr;
    lv_obj_t* volume_value_ = nullptr;
    lv_timer_t* refresh_timer_ = nullptr;
};
