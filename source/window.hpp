#ifndef CHEEMSIT_GUI_VK_WINDOW_HPP
#define CHEEMSIT_GUI_VK_WINDOW_HPP

#include <xcb/xcb.h>
#include <utility>
#include <memory>

#define LOG_X11_MSG(stream, msg) fmt::print(stream, "x11: {}\n", msg)
#define LOG_X11_MSG_VARGS(stream, msg, ...) fmt::print(stream, "x11: {}\n", fmt::format(msg, __VA_ARGS__))
#define LOGX11(msg, ...) LOG_X11_MSG##__VA_OPT__(_VARGS)(stdout, msg __VA_OPT__(,) __VA_ARGS__)
#define LOGX11_ERR(msg, ...) LOG_X11_MSG##__VA_OPT__(_VARGS)(stderr, msg __VA_OPT__(,) __VA_ARGS__)

class x11_window
{
public:
    using resize_callback_t = void(*)(uint16_t, uint16_t, void*);

    x11_window();
    x11_window(std::string window_name, uint16_t width, uint16_t height, bool is_resizable);
    ~x11_window();

    void open();
    void open_checked();
    void close();
    void flush();
    void poll_events();

    void set_size(uint32_t width, uint32_t height);
    void set_position(uint32_t x, uint32_t y);

    void connect_root();

    bool is_open() const
    {
        return window != 0;
    }

    bool is_connected() const
    {
        return connection != nullptr;
    }

    ///@return width, height
    std::tuple<uint16_t, uint16_t> get_window_size_px() const
    {
        return {window_width, window_height};
    }

    std::string name;
    std::string display;

    xcb_connection_t* connection;
    const xcb_setup_t* setup;
    xcb_screen_t* screen;
    xcb_window_t window;

    xcb_atom_t wm_protocols_property;
    xcb_atom_t wm_delete_window_property;

    uint16_t window_width;
    uint16_t window_height;
    bool resizable;

    resize_callback_t resize_callback;
    void* resize_callback_data;

    xcb_get_geometry_cookie_t get_geometry_cookie;
    xcb_get_geometry_reply_t get_geometry_reply;

private:

    void handle_event_error(xcb_generic_event_t* event);
    void handle_expose_event(xcb_generic_event_t* event);
    void handle_client_message(xcb_generic_event_t* event);
    void handle_configure_event(xcb_generic_event_t* event);
    void handle_key_press(xcb_generic_event_t* event);
    void handle_key_release(xcb_generic_event_t* event);
    void handle_button_press(xcb_generic_event_t* event);
    void handle_button_release(xcb_generic_event_t* event);
    void handle_mouse_motion(xcb_generic_event_t* event);
    void handle_ignored_event(xcb_generic_event_t* event);
};

#endif //CHEEMSIT_GUI_VK_WINDOW_HPP
