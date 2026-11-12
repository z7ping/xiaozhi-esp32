#pragma once

#include <lvgl.h>
#include <cstdint>
#include <string>
#include <vector>

// Lightweight touch pages for the 320x240 LCDWiki landscape variant.
// All calls take place under the existing display/LVGL lock.
class PixelTouchUI {
public:
    explicit PixelTouchUI(lv_obj_t* screen);
    ~PixelTouchUI();

    void Show(lv_obj_t* robot_area, lv_obj_t* chat_container);
    void ShowPlayer(lv_obj_t* robot_area, lv_obj_t* chat_container);
    void Hide();
    void Refresh();
    void SetMediaState(const char* state, const char* title, int64_t position_ms,
                       int64_t duration_ms, int current_index, int playlist_length,
                       const char* error);
    void SetMediaPlaylist(int offset, const std::vector<std::string>& titles);

private:
    enum class Page { Home, Calendar, Player, Settings };

    void Navigate(Page page);
    void QueueNavigate(Page page);
    static void RunQueuedNavigation(void* data);
    void AttachRobot();
    void DetachRobot();
    void BuildHome();
    void BuildCalendar();
    void BuildPlayer();
    void SyncMediaAvailability();
    void UpdatePlayer();
    void UpdatePlaylist();
    void BuildSettings();
    void BuildHeader(const char* title, bool back = false);
    void BuildBackButton();
    void RefreshHome();
    void RefreshCalendar();
    void UpdateCalendarSelection();
    void ResetCalendarToday();
    void NavigateCalendarMonth(int delta);
    bool IsSelectedToday() const;
    void ApplyPalette();
    void UpdateSliderValues();

    static void OnSettings(lv_event_t* event);
    static void OnHome(lv_event_t* event);
    static void OnCalendar(lv_event_t* event);
    static void OnPlayer(lv_event_t* event);
    static void OnTalk(lv_event_t* event);
    static void OnSliderChange(lv_event_t* event);
    static void OnSliderRelease(lv_event_t* event);
    static void OnMediaPrev(lv_event_t* event);
    static void OnMediaToggle(lv_event_t* event);
    static void OnMediaNext(lv_event_t* event);
    static void OnMediaStop(lv_event_t* event);
    static void OnMediaSeek(lv_event_t* event);
    static void OnPlaylistOpen(lv_event_t* event);
    static void OnPlaylistClose(lv_event_t* event);
    static void OnPlaylistPrevious(lv_event_t* event);
    static void OnPlaylistNext(lv_event_t* event);
    static void OnPlaylistSelect(lv_event_t* event);
    static void OnCalendarPrevious(lv_event_t* event);
    static void OnCalendarNext(lv_event_t* event);
    static void OnCalendarToday(lv_event_t* event);
    static void OnCalendarDay(lv_event_t* event);
    static void OnCalendarDetail(lv_event_t* event);
    static void OnCalendarClose(lv_event_t* event);
    static void OnChildModeChanged(lv_event_t* event);
    static void OnTwilightChanged(lv_event_t* event);

    Page page_ = Page::Home;
    Page queued_page_ = Page::Home;
    bool media_available_ = false;
    uint32_t media_generation_ = 0;
    std::string media_status_;
    std::string media_title_;
    std::string media_error_;
    int64_t media_position_ms_ = 0;
    int64_t media_duration_ms_ = 0;
    int media_current_index_ = -1;
    int media_playlist_length_ = 0;
    int playlist_offset_ = 0;
    std::string playlist_titles_[6];
    int playlist_count_ = 0;
    uint8_t refresh_ticks_ = 0;
    int calendar_year_ = 0;
    int calendar_month_ = 0;
    lv_obj_t* calendar_cells_[32] = {};
    int selected_year_ = 0;
    int selected_month_ = 0;
    int selected_day_ = 0;
    bool calendar_lookup_unavailable_ = false;
    bool child_mode_ = true;
    bool twilight_ = false;
    lv_obj_t* root_ = nullptr;
    lv_obj_t* body_ = nullptr;
    lv_obj_t* robot_area_ = nullptr;
    lv_obj_t* chat_container_ = nullptr;
    lv_obj_t* robot_slot_ = nullptr;
    lv_obj_t* home_day_label_ = nullptr;
    lv_obj_t* time_label_ = nullptr;
    lv_obj_t* date_label_ = nullptr;
    lv_obj_t* weather_label_ = nullptr;
    lv_obj_t* lunar_label_ = nullptr;
    lv_obj_t* calendar_date_label_ = nullptr;
    lv_obj_t* calendar_detail_label_ = nullptr;
    lv_obj_t* calendar_month_label_ = nullptr;
    lv_obj_t* calendar_selected_label_ = nullptr;
    lv_obj_t* calendar_details_popup_ = nullptr;
    lv_obj_t* child_mode_switch_ = nullptr;
    lv_obj_t* twilight_switch_ = nullptr;
    lv_obj_t* brightness_slider_ = nullptr;
    lv_obj_t* volume_slider_ = nullptr;
    lv_obj_t* brightness_value_ = nullptr;
    lv_obj_t* volume_value_ = nullptr;
    lv_obj_t* player_title_ = nullptr;
    lv_obj_t* player_status_ = nullptr;
    lv_obj_t* player_position_ = nullptr;
    lv_obj_t* player_seek_slider_ = nullptr;
    lv_obj_t* player_toggle_ = nullptr;
    lv_obj_t* player_prev_button_ = nullptr;
    lv_obj_t* player_next_button_ = nullptr;
    lv_obj_t* player_stop_button_ = nullptr;
    lv_obj_t* player_list_button_ = nullptr;
    lv_obj_t* playlist_panel_ = nullptr;
    lv_obj_t* playlist_buttons_[6] = {};
    lv_obj_t* playlist_page_label_ = nullptr;
    lv_timer_t* refresh_timer_ = nullptr;
};
