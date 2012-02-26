#ifndef OCTA_CLIENT_H
#define OCTA_CLIENT_H

#include <libgupnp/gupnp.h>

G_BEGIN_DECLS

gboolean
send_message_to_udcp (GUPnPServiceProxy *proxy,
        const gchar *in_octa_message,
        GError **error);

typedef void (*send_message_to_udcp_reply) (GUPnPServiceProxy *proxy,
        GError *error,
        gpointer userdata);

GUPnPServiceProxyAction *
send_message_to_udcp_async (GUPnPServiceProxy *proxy,
        const gchar *in_octa_message,
        send_message_to_udcp_reply callback,
        gpointer userdata);

gboolean
octa_init (GUPnPServiceProxy *proxy,
        const gboolean in_enable_octa,
        GError **error);

typedef void (*octa_init_reply) (GUPnPServiceProxy *proxy,
        GError *error,
        gpointer userdata);

GUPnPServiceProxyAction *
octa_init_async (GUPnPServiceProxy *proxy,
        const gboolean in_enable_octa,
        octa_init_reply callback,
        gpointer userdata);


typedef void (*get_octa_enable_reply) (GUPnPServiceProxy *proxy,
        gboolean octa_enable,
        GError *error,
        gpointer userdata);

gboolean
usb_reset_complete (GUPnPServiceProxy *proxy,
        GError **error);

typedef void (*usb_reset_complete_reply) (GUPnPServiceProxy *proxy,
        GError *error,
        gpointer userdata);

GUPnPServiceProxyAction *
usb_reset_complete_async (GUPnPServiceProxy *proxy,
        usb_reset_complete_reply callback,
        gpointer userdata);

typedef void
(*udcp_message_changed_callback) (GUPnPServiceProxy *proxy,
        const gchar *udcp_message,
        gpointer userdata);

gboolean
udcp_message_add_notify (GUPnPServiceProxy *proxy,
        udcp_message_changed_callback callback,
        gpointer userdata);

typedef void
(*ta_communication_error_changed_callback) (GUPnPServiceProxy *proxy,
        gboolean ta_communication_error,
        gpointer userdata);

gboolean
ta_communication_error_add_notify (GUPnPServiceProxy *proxy,
        ta_communication_error_changed_callback callback,
        gpointer userdata);

G_END_DECLS

#endif
