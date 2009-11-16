/*
 * property.c - property handlers
 *
 * Copyright © 2008-2009 Julien Danjou <julien@danjou.info>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#include <xcb/xcb_atom.h>

#include "awesome.h"
#include "screen.h"
#include "property.h"
#include "objects/client.h"
#include "ewmh.h"
#include "objects/wibox.h"
#include "xwindow.h"
#include "luaa.h"
#include "common/atoms.h"
#include "common/xutil.h"

#define HANDLE_TEXT_PROPERTY(funcname, atom, setfunc) \
    xcb_get_property_cookie_t \
    property_get_##funcname(client_t *c) \
    { \
        return xcb_get_property(_G_connection, \
                                false, \
                                c->window, \
                                atom, \
                                XCB_GET_PROPERTY_TYPE_ANY, \
                                0, \
                                UINT_MAX); \
    } \
    void \
    property_update_##funcname(client_t *c, xcb_get_property_cookie_t cookie) \
    { \
        xcb_get_property_reply_t * reply = \
                    xcb_get_property_reply(_G_connection, cookie, NULL); \
        luaA_object_push(globalconf.L, c); \
        setfunc(globalconf.L, -1, xutil_get_text_property_from_reply(reply)); \
        lua_pop(globalconf.L, 1); \
        p_delete(&reply); \
    } \
    static int \
    property_handle_##funcname(uint8_t state, \
                               xcb_window_t window) \
    { \
        client_t *c = client_getbywin(window); \
        if(c) \
            property_update_##funcname(c, property_get_##funcname(c));\
        return 0; \
    }


HANDLE_TEXT_PROPERTY(wm_name, XCB_ATOM_WM_NAME, client_set_alt_name)
HANDLE_TEXT_PROPERTY(net_wm_name, _NET_WM_NAME, client_set_name)
HANDLE_TEXT_PROPERTY(wm_icon_name, XCB_ATOM_WM_ICON_NAME, client_set_alt_icon_name)
HANDLE_TEXT_PROPERTY(net_wm_icon_name, _NET_WM_ICON_NAME, client_set_icon_name)
HANDLE_TEXT_PROPERTY(wm_client_machine, XCB_ATOM_WM_CLIENT_MACHINE, client_set_machine)
HANDLE_TEXT_PROPERTY(wm_window_role, WM_WINDOW_ROLE, client_set_role)

#undef HANDLE_TEXT_PROPERTY

#define HANDLE_PROPERTY(name) \
    static int \
    property_handle_##name(uint8_t state, \
                           xcb_window_t window) \
    { \
        client_t *c = client_getbywin(window); \
        if(c) \
            property_update_##name(c, property_get_##name(c));\
        return 0; \
    }

HANDLE_PROPERTY(wm_protocols)
HANDLE_PROPERTY(wm_transient_for)
HANDLE_PROPERTY(wm_client_leader)
HANDLE_PROPERTY(wm_normal_hints)
HANDLE_PROPERTY(wm_hints)
HANDLE_PROPERTY(wm_class)
HANDLE_PROPERTY(net_wm_icon)
HANDLE_PROPERTY(net_wm_pid)

#undef HANDLE_PROPERTY

xcb_get_property_cookie_t
property_get_wm_transient_for(client_t *c)
{
    return xcb_get_wm_transient_for_unchecked(_G_connection, c->window);
}

void
property_update_wm_transient_for(client_t *c, xcb_get_property_cookie_t cookie)
{
    xcb_window_t trans;

    if(!xcb_get_wm_transient_for_reply(_G_connection,
                                       cookie,
                                       &trans, NULL))
            return;

    luaA_object_push(globalconf.L, c);
    ewindow_set_type(globalconf.L, -1, EWINDOW_TYPE_DIALOG);
    ewindow_set_above(globalconf.L, -1, false);
    luaA_object_push(globalconf.L, client_getbywin(trans));
    client_set_transient_for(globalconf.L, -2, -1);
    lua_pop(globalconf.L, 2);
}

xcb_get_property_cookie_t
property_get_wm_client_leader(client_t *c)
{
    return xcb_get_property_unchecked(_G_connection, false, c->window,
                                      WM_CLIENT_LEADER, XCB_ATOM_WINDOW, 1, 32);
}

/** Update leader hint of a client.
 * \param c The client.
 * \param cookie Cookie returned by property_get_wm_client_leader.
 */
void
property_update_wm_client_leader(client_t *c, xcb_get_property_cookie_t cookie)
{
    xcb_get_property_reply_t *reply;
    void *data;

    reply = xcb_get_property_reply(_G_connection, cookie, NULL);

    if(reply && reply->value_len && (data = xcb_get_property_value(reply)))
        c->leader_window = *(xcb_window_t *) data;

    p_delete(&reply);
}

xcb_get_property_cookie_t
property_get_wm_normal_hints(client_t *c)
{
    return xcb_get_wm_normal_hints_unchecked(_G_connection, c->window);
}

/** Update the size hints of a client.
 * \param c The client.
 * \param cookie Cookie returned by property_get_wm_normal_hints.
 */
void
property_update_wm_normal_hints(client_t *c, xcb_get_property_cookie_t cookie)
{
    if(!xcb_get_wm_normal_hints_reply(_G_connection,
                                      cookie,
                                      &c->size_hints, NULL))
        return;

    c->resizable = !(c->size_hints.flags & XCB_SIZE_HINT_P_MAX_SIZE
                     && c->size_hints.flags & XCB_SIZE_HINT_P_MIN_SIZE
                     && c->size_hints.max_width == c->size_hints.min_width
                     && c->size_hints.max_height == c->size_hints.min_height
                     && c->size_hints.max_width
                     && c->size_hints.max_height);
}

xcb_get_property_cookie_t
property_get_wm_hints(client_t *c)
{
    return xcb_get_wm_hints_unchecked(_G_connection, c->window);
}

/** Update the WM hints of a client.
 * \param c The client.
 * \param cookie Cookie returned by property_get_wm_hints.
 */
void
property_update_wm_hints(client_t *c, xcb_get_property_cookie_t cookie)
{
    xcb_wm_hints_t wmh;

    if(!xcb_get_wm_hints_reply(_G_connection,
                               cookie,
                               &wmh, NULL))
        return;

    luaA_object_push(globalconf.L, c);
    client_set_urgent(globalconf.L, -1, xcb_wm_hints_get_urgency(&wmh));

    if(wmh.flags & XCB_WM_HINT_INPUT)
        c->focusable = wmh.input;

    if(wmh.flags & XCB_WM_HINT_WINDOW_GROUP)
        client_set_group_window(globalconf.L, -1, wmh.window_group);

    lua_pop(globalconf.L, 1);
}

xcb_get_property_cookie_t
property_get_wm_class(client_t *c)
{
    return xcb_get_wm_class_unchecked(_G_connection, c->window);
}

/** Update WM_CLASS of a client.
 * \param c The client.
 * \param cookie Cookie returned by property_get_wm_class.
 */
void
property_update_wm_class(client_t *c, xcb_get_property_cookie_t cookie)
{
    xcb_get_wm_class_reply_t hint;

    if(!xcb_get_wm_class_reply(_G_connection,
                               cookie,
                               &hint, NULL))
        return;

    luaA_object_push(globalconf.L, c);
    client_set_class_instance(globalconf.L, -1, hint.class_name, hint.instance_name);
    lua_pop(globalconf.L, 1);

    xcb_get_wm_class_reply_wipe(&hint);
}

static int
property_handle_net_wm_strut_partial(uint8_t state,
                                     xcb_window_t window)
{
    client_t *c = client_getbywin(window);

    if(c)
        ewmh_process_client_strut(c);

    return 0;
}

xcb_get_property_cookie_t
property_get_net_wm_icon(client_t *c)
{
    return ewmh_window_icon_get_unchecked(c->window);
}

void
property_update_net_wm_icon(client_t *c, xcb_get_property_cookie_t cookie)
{
    luaA_object_push(globalconf.L, c);

    if(ewmh_window_icon_get_reply(cookie))
        client_set_icon(globalconf.L, -2, -1);

    /* remove client */
    lua_pop(globalconf.L, 1);
}

xcb_get_property_cookie_t
property_get_net_wm_pid(client_t *c)
{
    return xcb_get_property_unchecked(_G_connection, false, c->window, _NET_WM_PID, XCB_ATOM_CARDINAL, 0L, 1L);
}

void
property_update_net_wm_pid(client_t *c, xcb_get_property_cookie_t cookie)
{
    xcb_get_property_reply_t *reply;

    reply = xcb_get_property_reply(_G_connection, cookie, NULL);

    if(reply && reply->value_len)
    {
        uint32_t *rdata = xcb_get_property_value(reply);
        if(rdata)
        {
            luaA_object_push(globalconf.L, c);
            client_set_pid(globalconf.L, -1, *rdata);
            lua_pop(globalconf.L, 1);
        }
    }

    p_delete(&reply);
}

xcb_get_property_cookie_t
property_get_wm_protocols(client_t *c)
{
    return xcb_get_wm_protocols_unchecked(_G_connection, c->window, WM_PROTOCOLS);
}

/** Update the list of supported protocols for a client.
 * \param c The client.
 * \param cookie Cookie from property_get_wm_protocols.
 */
void
property_update_wm_protocols(client_t *c, xcb_get_property_cookie_t cookie)
{
    xcb_get_wm_protocols_reply_t protocols;

    /* If this fails for any reason, we still got the old value */
    if(!xcb_get_wm_protocols_reply(_G_connection,
                                   cookie,
                                   &protocols, NULL))
        return;

    xcb_get_wm_protocols_reply_wipe(&c->protocols);
    memcpy(&c->protocols, &protocols, sizeof(protocols));
}

/** The property notify event handler.
 * \param state currently unused
 * \param window The window to obtain update the property with.
 * \param name The protocol atom, currently unused.
 * \param reply (Optional) An existing reply.
 */
static int
property_handle_xembed_info(uint8_t state,
                            xcb_window_t window)
{
    xembed_window_t *emwin = xembed_getbywin(&globalconf.embedded, window);

    if(emwin)
    {
        xcb_get_property_cookie_t cookie =
            xcb_get_property(_G_connection, 0, window, _XEMBED_INFO,
                             XCB_GET_PROPERTY_TYPE_ANY, 0, 3);
        xcb_get_property_reply_t *propr =
            xcb_get_property_reply(_G_connection, cookie, 0);
        xembed_property_update(_G_connection, emwin, propr);
        p_delete(&propr);
    }

    return 0;
}

static int
property_handle_xrootpmap_id(uint8_t state,
                             xcb_window_t window)
{
    foreach(w, globalconf.wiboxes)
       (*w)->need_update = true;

    return 0;
}

static int
property_handle_net_wm_opacity(uint8_t state,
                               xcb_window_t window)
{
    wibox_t *wibox = wibox_getbywin(window);

    if(wibox)
    {
        luaA_object_push(globalconf.L, wibox);
        ewindow_set_opacity(globalconf.L, -1, xwindow_get_opacity(wibox->window));
        lua_pop(globalconf.L, -1);
    }
    else
    {
        client_t *c = client_getbywin(window);
        if(c)
        {
            luaA_object_push(globalconf.L, c);
            ewindow_set_opacity(globalconf.L, -1, xwindow_get_opacity(c->window));
            lua_pop(globalconf.L, 1);
        }
    }

    return 0;
}

/** The property notify event handler.
 * \param data Unused data.
 * \param connection The connection to the X server.
 * \param ev The event.
 * \return Status code, 0 if everything's fine.
 */
void
property_handle_propertynotify(xcb_property_notify_event_t *ev)
{
    int (*handler)(uint8_t state,
                   xcb_window_t window) = NULL;

    globalconf.timestamp = ev->time;

    /* Find the correct event handler */
#define HANDLE(atom_, cb) \
    if (ev->atom == atom_) \
    { \
        handler = cb; \
    } else
#define END return

    /* Xembed stuff */
    HANDLE(_XEMBED_INFO, property_handle_xembed_info)

    /* ICCCM stuff */
    HANDLE(XCB_ATOM_WM_TRANSIENT_FOR, property_handle_wm_transient_for)
    HANDLE(WM_CLIENT_LEADER, property_handle_wm_client_leader)
    HANDLE(XCB_ATOM_WM_NORMAL_HINTS, property_handle_wm_normal_hints)
    HANDLE(XCB_ATOM_WM_HINTS, property_handle_wm_hints)
    HANDLE(XCB_ATOM_WM_NAME, property_handle_wm_name)
    HANDLE(XCB_ATOM_WM_ICON_NAME, property_handle_wm_icon_name)
    HANDLE(XCB_ATOM_WM_CLASS, property_handle_wm_class)
    HANDLE(WM_PROTOCOLS, property_handle_wm_protocols)
    HANDLE(XCB_ATOM_WM_CLIENT_MACHINE, property_handle_wm_client_machine)
    HANDLE(WM_WINDOW_ROLE, property_handle_wm_window_role)

    /* EWMH stuff */
    HANDLE(_NET_WM_NAME, property_handle_net_wm_name)
    HANDLE(_NET_WM_ICON_NAME, property_handle_net_wm_icon_name)
    HANDLE(_NET_WM_STRUT_PARTIAL, property_handle_net_wm_strut_partial)
    HANDLE(_NET_WM_ICON, property_handle_net_wm_icon)
    HANDLE(_NET_WM_PID, property_handle_net_wm_pid)
    HANDLE(_NET_WM_WINDOW_OPACITY, property_handle_net_wm_opacity)

    /* background change */
    HANDLE(_XROOTPMAP_ID, property_handle_xrootpmap_id)

    /* If nothing was found, return */
    END;

#undef HANDLE
#undef END

    (*handler)(ev->state, ev->window);
}

// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:encoding=utf-8:textwidth=80
