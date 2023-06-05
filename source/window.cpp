#include "window.hpp"
#include <xcb/xcb.h>
#include <xcb/xc_misc.h>
#include <xcb/xcb_util.h>
#include <xcb/xcb_icccm.h>
#include <exception>
#include <string>
#include <fmt/format.h>
#include <array>

constexpr std::string_view wm_protocols_name = "WM_PROTOCOLS";
constexpr std::string_view wm_window_delete_protocol_name = "WM_DELETE_WINDOW";
constexpr std::string_view net_wm_desktop_name = "_NET_WM_DESKTOP";

namespace x11
{
    struct exception : public std::exception
    {
        std::string message;

        template<typename... Ts>
        constexpr exception(fmt::format_string<Ts...> msg, Ts&&... fmtargs)
        {
            message = fmt::format(msg, std::forward<Ts>(fmtargs)...);
        }

        virtual const char* what() const noexcept override
        {
            return message.c_str();
        }
    };
}

static void check_cookie(xcb_connection_t* connection, xcb_void_cookie_t cookie) noexcept(false)
{
    xcb_generic_error_t* error = xcb_request_check(connection, cookie);

    if(error != nullptr)
    {
        int error_code = error->error_code;
        free(error);

        throw x11::exception("request check failed {}", error_code);
    }
}

xcb_screen_t* screen_of_display(xcb_connection_t *c, int screen)
{
    xcb_screen_iterator_t iter = xcb_setup_roots_iterator(xcb_get_setup(c));

    for(; iter.rem; --screen, xcb_screen_next (&iter))
    {
        if(screen == 0)
        {
            return iter.data;
        }
    }

    return nullptr;
}

x11_window::x11_window()
{
    connection = nullptr;
    setup = nullptr;
    screen = nullptr;
    window = 0;
    wm_protocols_property = 0;
    wm_delete_window_property = 0;
    window_width = 0;
    window_height = 0;
    resizable = false;
    resize_callback = nullptr;
    resize_callback_data = nullptr;
}

x11_window::x11_window(std::string window_name, uint16_t width, uint16_t height, bool is_resizable)
{
    name = std::move(window_name);
    connection = nullptr;
    setup = nullptr;
    screen = nullptr;
    window = 0;
    wm_protocols_property = 0;
    wm_delete_window_property = 0;
    window_width = width;
    window_height = height;
    resizable = is_resizable;
    resize_callback = nullptr;
    resize_callback_data = nullptr;
}

x11_window::~x11_window()
{
    close();
}

void x11_window::open()
{
    LOGX11("opening {}", name);

    int screen_number;
    connection = xcb_connect(nullptr, &screen_number);

    if(int error = xcb_connection_has_error(connection))
    {
        throw x11::exception("{} connection error {}", name, error);
    }

    xcb_intern_atom_cookie_t protocols_request = xcb_intern_atom_unchecked(connection, true, wm_protocols_name.size(), wm_protocols_name.data());
    xcb_intern_atom_cookie_t delete_protocol_request = xcb_intern_atom_unchecked(connection, true, wm_window_delete_protocol_name.size(), wm_window_delete_protocol_name.data());

    setup = xcb_get_setup(connection);
    screen = xcb_aux_get_screen(connection, screen_number);
    window = xcb_generate_id(connection);

    uint32_t value_mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;

    uint8_t red = 255; uint8_t green = 0; uint8_t blue = 255; uint8_t alpha = 255; //alpha doesnt matter
    xcb_create_window_value_list_t value_list{};
    value_list.background_pixel = (alpha << 24) | (red << 16) | (green << 8) | (blue);
    value_list.event_mask = XCB_EVENT_MASK_EXPOSURE
                            | XCB_EVENT_MASK_STRUCTURE_NOTIFY
                            | XCB_EVENT_MASK_BUTTON_PRESS
                            | XCB_EVENT_MASK_BUTTON_RELEASE
                            | XCB_EVENT_MASK_KEY_PRESS
                            | XCB_EVENT_MASK_KEY_RELEASE
                            | XCB_EVENT_MASK_POINTER_MOTION
                            | XCB_EVENT_MASK_BUTTON_MOTION;

    xcb_create_window_aux(
            connection,
            screen->root_depth,
            window,
            screen->root,
            0, 0,
            window_width, window_height,
            0,
            XCB_WINDOW_CLASS_INPUT_OUTPUT,
            screen->root_visual,
            value_mask,
            &value_list);

    if(!resizable)
    {
        xcb_size_hints_t size_hints{};

        xcb_icccm_size_hints_set_min_size(&size_hints, window_width, window_height);
        xcb_icccm_size_hints_set_max_size(&size_hints, window_width, window_height);
        xcb_icccm_set_wm_size_hints(connection, window, XCB_ATOM_WM_NORMAL_HINTS, &size_hints);
    }

    xcb_change_property(
            connection,
            XCB_PROP_MODE_REPLACE,
            window,
            XCB_ATOM_WM_NAME,
            XCB_ATOM_STRING,
            8,
            name.length(),
            name.c_str());

    xcb_intern_atom_reply_t* protocols_reply = xcb_intern_atom_reply(connection, protocols_request, nullptr);
    wm_protocols_property = protocols_reply->atom;
    free(protocols_reply);

    xcb_intern_atom_reply_t* delete_protocol_reply = xcb_intern_atom_reply(connection, delete_protocol_request, nullptr);
    wm_delete_window_property = delete_protocol_reply->atom;
    free(delete_protocol_reply);

    xcb_change_property(
            connection,
            XCB_PROP_MODE_REPLACE,
            window,
            wm_protocols_property,
            XCB_ATOM_ATOM,
            32,
            1,
            &wm_delete_window_property
            );

    xcb_map_window(connection, window);

    flush();
}

void x11_window::connect_root() //todo
{
    LOGX11("connecting {} to root", name);

    int screen_number;
    connection = xcb_connect(nullptr, &screen_number);

    if(int error = xcb_connection_has_error(connection))
    {
        abort();
        throw x11::exception("{} connection error {}", name, error);
    }

    setup = xcb_get_setup(connection);

    xcb_screen_iterator_t iterator = xcb_setup_roots_iterator(setup);
    LOGX11("rem {}", iterator.rem);

    while(iterator.rem != 0)
    {
        xcb_screen_next(&iterator);
    }

    setup = xcb_get_setup(connection);

    screen = iterator.data;
    window = iterator.data->root;
}

void x11_window::close()
{
    if(window)
    {
        LOGX11("closing {}", name);

        xcb_destroy_window(connection, window);
        window = 0;
    }

    if(connection)
    {
        LOGX11("disconnecting {}", name);

        xcb_disconnect(connection);
        connection = nullptr;
    }
}

void x11_window::flush()
{
    if(xcb_flush(connection) == 0)
    {
        throw x11::exception("{} connection failed to flush", name);
    }
}

void x11_window::poll_events()
{
    xcb_generic_event_t* event;
    while(is_connected() && (event = xcb_poll_for_event(connection)) != nullptr)
    {
        switch(XCB_EVENT_RESPONSE_TYPE(event))
        {
            case 0:
                handle_event_error(event);
                break;
            case XCB_EXPOSE:
                handle_expose_event(event);
                break;
            case XCB_CLIENT_MESSAGE:
                handle_client_message(event);
                break;
            case XCB_CONFIGURE_NOTIFY:
                handle_configure_event(event);
                break;
            case XCB_KEY_PRESS:
                handle_key_press(event);
                break;
            case XCB_KEY_RELEASE:
                handle_key_release(event);
                break;
            case XCB_BUTTON_PRESS:
                handle_button_press(event);
                break;
            case XCB_BUTTON_RELEASE:
                handle_button_release(event);
                break;
            case XCB_MOTION_NOTIFY:
                handle_mouse_motion(event);
                break;
            default:
                handle_ignored_event(event);
                break;
        }

        free(event);
    }
}

void x11_window::set_size(uint32_t width, uint32_t height)
{
    window_width = width;
    window_height = height;

    uint32_t values[]{width, height};
    uint32_t mask = XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT;

    xcb_configure_window(connection, window, mask, values);
}

void x11_window::set_position(uint32_t x, uint32_t y)
{
    uint32_t values[]{x, y};
    uint32_t mask = XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y;

    xcb_configure_window(connection, window, mask, values);
}

void x11_window::handle_event_error(xcb_generic_event_t* event)
{
    auto error = reinterpret_cast<xcb_generic_error_t*>(event);
    throw x11::exception("{} event error {}", name, error->error_code);
}

void x11_window::handle_expose_event(xcb_generic_event_t* event)
{

}

void x11_window::handle_client_message(xcb_generic_event_t* event)
{
    auto message_event = reinterpret_cast<xcb_client_message_event_t*>(event);
    xcb_client_message_data_t& message_data = message_event->data;

    if(message_event->window == window && message_data.data32[0] == wm_delete_window_property) //window was deleted
    {
        close();
    }
}

void x11_window::handle_configure_event(xcb_generic_event_t* event)
{
    auto configure_event = reinterpret_cast<xcb_configure_notify_event_t*>(event);

    uint16_t old_width = window_width;
    uint16_t old_height = window_height;

    window_width = configure_event->width;
    window_height = configure_event->height;

    if(old_width != window_width || old_height != window_height)
    {
        if(resize_callback)
        {
            resize_callback(window_width, window_height, resize_callback_data);
        }
    }
}

void x11_window::handle_key_press(xcb_generic_event_t* event)
{

}

void x11_window::handle_key_release(xcb_generic_event_t* event)
{

}

void x11_window::handle_button_press(xcb_generic_event_t* event)
{

}

void x11_window::handle_button_release(xcb_generic_event_t* event)
{

}

void x11_window::handle_mouse_motion(xcb_generic_event_t* event)
{

}

void x11_window::handle_ignored_event(xcb_generic_event_t* event)
{
    LOGX11("{}: event {} ignored", name, event->response_type);
}

void x11_window::open_checked()
{
    LOGX11("opening window checked");

    int screen_number;
    connection = xcb_connect(nullptr, &screen_number);

    if(int error = xcb_connection_has_error(connection))
    {
        throw x11::exception("xcb connection error {}", error);
    }

    setup = xcb_get_setup(connection);

    xcb_intern_atom_cookie_t protocols_request = xcb_intern_atom_unchecked(connection, true, wm_protocols_name.size(), wm_protocols_name.data());
    xcb_intern_atom_cookie_t delete_protocol_request = xcb_intern_atom_unchecked(connection, true, wm_window_delete_protocol_name.size(), wm_window_delete_protocol_name.data());

    screen = xcb_aux_get_screen(connection, screen_number);
    window = xcb_generate_id(connection);

    uint32_t value_mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;

    uint8_t red = 255; uint8_t green = 0; uint8_t blue = 255; uint8_t alpha = 255; //alpha doesnt matter
    xcb_create_window_value_list_t value_list{};
    value_list.background_pixel = (alpha << 24) | (red << 16) | (green << 8) | (blue);
    value_list.event_mask = XCB_EVENT_MASK_EXPOSURE
                            | XCB_EVENT_MASK_STRUCTURE_NOTIFY
                            | XCB_EVENT_MASK_BUTTON_PRESS
                            | XCB_EVENT_MASK_BUTTON_RELEASE
                            | XCB_EVENT_MASK_KEY_PRESS
                            | XCB_EVENT_MASK_KEY_RELEASE
                            | XCB_EVENT_MASK_POINTER_MOTION
                            | XCB_EVENT_MASK_BUTTON_MOTION;

    xcb_void_cookie_t cookie = xcb_create_window_aux_checked(
            connection,
            screen->root_depth,
            window,
            screen->root,
            0, 0,
            window_width, window_height,
            0,
            XCB_WINDOW_CLASS_INPUT_OUTPUT,
            screen->root_visual,
            value_mask,
            &value_list);
    check_cookie(connection, cookie);

    if(!resizable)
    {
        xcb_size_hints_t size_hints{};

        xcb_icccm_size_hints_set_min_size(&size_hints, window_width, window_height);
        xcb_icccm_size_hints_set_max_size(&size_hints, window_width, window_height);
        cookie = xcb_icccm_set_wm_size_hints_checked(connection, window, XCB_ATOM_WM_NORMAL_HINTS, &size_hints);

        check_cookie(connection, cookie);
    }

    const char* window_name = "cheemsit-x11-vk";

    cookie = xcb_change_property_checked(
            connection,
            XCB_PROP_MODE_REPLACE,
            window,
            XCB_ATOM_WM_NAME,
            XCB_ATOM_STRING,
            8,
            std::strlen(window_name),
            window_name);

    check_cookie(connection, cookie);

    xcb_intern_atom_reply_t* protocols_reply = xcb_intern_atom_reply(connection, protocols_request, nullptr);
    wm_protocols_property = protocols_reply->atom;
    free(protocols_reply);

    xcb_intern_atom_reply_t* delete_protocol_reply = xcb_intern_atom_reply(connection, delete_protocol_request, nullptr);
    wm_delete_window_property = delete_protocol_reply->atom;
    free(delete_protocol_reply);

    cookie = xcb_icccm_set_wm_protocols(connection, window, wm_protocols_property, 1, &wm_delete_window_property);
    check_cookie(connection, cookie);

    cookie = xcb_map_window_checked(connection, window);
    check_cookie(connection, cookie);

    flush();
}