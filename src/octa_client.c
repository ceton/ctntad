#include "octa_client.h"

typedef struct {GCallback cb; gpointer userdata; } GUPnPAsyncData;

/* action SendMessageToUDCP */

gboolean
send_message_to_udcp (GUPnPServiceProxy *proxy,
        const gchar *in_octa_message,
        GError **error)
{
    return gupnp_service_proxy_send_action
        (proxy, "SendMessageToUDCP", error,
         "OCTAMessage", GUPNP_TYPE_BIN_BASE64, in_octa_message,
         NULL,
         NULL);
}

static void _send_message_to_udcp_async_callback (GUPnPServiceProxy *proxy,
        GUPnPServiceProxyAction *action,
        gpointer user_data)
{
    GUPnPAsyncData *cbdata;
    GError *error = NULL;

    cbdata = (GUPnPAsyncData *) user_data;
    gupnp_service_proxy_end_action
        (proxy, action, &error,
         NULL);
    ((send_message_to_udcp_reply)cbdata->cb)
        (proxy,
         error, cbdata->userdata);

    g_slice_free1 (sizeof (*cbdata), cbdata);
}

GUPnPServiceProxyAction *
send_message_to_udcp_async (GUPnPServiceProxy *proxy,
        const gchar *in_octa_message,
        send_message_to_udcp_reply callback,
        gpointer userdata)
{
    GUPnPServiceProxyAction* action;
    GUPnPAsyncData *cbdata;

    cbdata = (GUPnPAsyncData *) g_slice_alloc (sizeof (*cbdata));
    cbdata->cb = G_CALLBACK (callback);
    cbdata->userdata = userdata;
    action = gupnp_service_proxy_begin_action
        (proxy, "SendMessageToUDCP",
         _send_message_to_udcp_async_callback, cbdata,
         "OCTAMessage", GUPNP_TYPE_BIN_BASE64, in_octa_message,
         NULL);

    return action;
}

static void _octa_get_enable_octa_async_callback(GUPnPServiceProxy* proxy,
        GUPnPServiceProxyAction* action,
        gpointer userdata)
{
    GUPnPAsyncData* cbdata = userdata;
    GError* error = NULL;
    gboolean octa_enable = FALSE;

    gupnp_service_proxy_end_action(
            proxy, action, &error,
            "return", G_TYPE_BOOLEAN, &octa_enable,
            NULL);

    ((get_octa_enable_reply)cbdata->cb)(
        proxy, octa_enable, error, cbdata->userdata);

    g_slice_free1(sizeof(*cbdata), cbdata);
}

GUPnPServiceProxyAction*
octa_get_enable_octa_async(GUPnPServiceProxy* proxy,
        get_octa_enable_reply callback,
        gpointer userdata)
{
    GUPnPServiceProxyAction* action;
    GUPnPAsyncData* cbdata;
    
    cbdata = g_slice_alloc(sizeof(*cbdata));
    cbdata->cb = G_CALLBACK(callback);
    cbdata->userdata = userdata;
    action = gupnp_service_proxy_begin_action(
            proxy, "QueryStateVariable",
            _octa_get_enable_octa_async_callback, cbdata,
            "varName", G_TYPE_STRING, "A_ARG_TYPE_OCTA_ENABLE",
            NULL);

    return action;
}

/* action OCTAInit */

gboolean
octa_init (GUPnPServiceProxy *proxy,
        const gboolean in_enable_octa,
        GError **error)
{
    return gupnp_service_proxy_send_action
        (proxy, "OCTAInit", error,
         "EnableOCTA", G_TYPE_BOOLEAN, in_enable_octa,
         NULL,
         NULL);
}

static void _octa_init_async_callback (GUPnPServiceProxy *proxy,
        GUPnPServiceProxyAction *action,
        gpointer user_data)
{
    GUPnPAsyncData *cbdata;
    GError *error = NULL;

    cbdata = (GUPnPAsyncData *) user_data;
    gupnp_service_proxy_end_action
        (proxy, action, &error,
         NULL);
    ((octa_init_reply)cbdata->cb)
        (proxy,
         error, cbdata->userdata);

    g_slice_free1 (sizeof (*cbdata), cbdata);
}

GUPnPServiceProxyAction *
octa_init_async (GUPnPServiceProxy *proxy,
        const gboolean in_enable_octa,
        octa_init_reply callback,
        gpointer userdata)
{
    GUPnPServiceProxyAction* action;
    GUPnPAsyncData *cbdata;

    cbdata = (GUPnPAsyncData *) g_slice_alloc (sizeof (*cbdata));
    cbdata->cb = G_CALLBACK (callback);
    cbdata->userdata = userdata;
    g_print("OCTAInit %d\n", in_enable_octa);
    action = gupnp_service_proxy_begin_action
        (proxy, "OCTAInit",
         _octa_init_async_callback, cbdata,
         "EnableOCTA", G_TYPE_BOOLEAN, in_enable_octa,
         NULL);

    return action;
}

/* action USBResetComplete */

gboolean
usb_reset_complete (GUPnPServiceProxy *proxy,
        GError **error)
{
    return gupnp_service_proxy_send_action
        (proxy, "USBResetComplete", error,
         NULL,
         NULL);
}


static void _usb_reset_complete_async_callback (GUPnPServiceProxy *proxy,
        GUPnPServiceProxyAction *action,
        gpointer user_data)
{
    GUPnPAsyncData *cbdata;
    GError *error = NULL;

    cbdata = (GUPnPAsyncData *) user_data;
    gupnp_service_proxy_end_action
        (proxy, action, &error,
         NULL);
    ((usb_reset_complete_reply)cbdata->cb)
        (proxy,
         error, cbdata->userdata);

    g_slice_free1 (sizeof (*cbdata), cbdata);
}

GUPnPServiceProxyAction *
usb_reset_complete_async (GUPnPServiceProxy *proxy,
        usb_reset_complete_reply callback,
        gpointer userdata)
{
    GUPnPServiceProxyAction* action;
    GUPnPAsyncData *cbdata;

    cbdata = (GUPnPAsyncData *) g_slice_alloc (sizeof (*cbdata));
    cbdata->cb = G_CALLBACK (callback);
    cbdata->userdata = userdata;
    action = gupnp_service_proxy_begin_action
        (proxy, "USBResetComplete",
         _usb_reset_complete_async_callback, cbdata,
         NULL);

    return action;
}

/* state variable UDCPMessage */
const gchar*
g_value_get_binbase64 (const GValue *value)
{
    g_return_val_if_fail (
            G_TYPE_CHECK_VALUE_TYPE ((value), gupnp_bin_base64_get_type ()),
            NULL);

    return value->data[0].v_pointer;
}

static void
_udcp_message_changed_callback (GUPnPServiceProxy *proxy,
        const gchar *variable,
        GValue *value,
        gpointer userdata)
{
    GUPnPAsyncData *cbdata;
    const gchar *udcp_message;

    cbdata = (GUPnPAsyncData *) userdata;
    udcp_message = g_value_get_binbase64 (value);
    ((udcp_message_changed_callback)cbdata->cb)
        (proxy,
         udcp_message,
         cbdata->userdata);
}

gboolean
udcp_message_add_notify (GUPnPServiceProxy *proxy,
        udcp_message_changed_callback callback,
        gpointer userdata)
{
    GUPnPAsyncData *cbdata;

    cbdata = (GUPnPAsyncData *) g_slice_alloc (sizeof (*cbdata));
    cbdata->cb = G_CALLBACK (callback);
    cbdata->userdata = userdata;

    return gupnp_service_proxy_add_notify
        (proxy,
         "UDCPMessage",
         GUPNP_TYPE_BIN_BASE64,
         _udcp_message_changed_callback,
         cbdata);
}

/* state variable TACommunicationError */

static void
_ta_communication_error_changed_callback (GUPnPServiceProxy *proxy,
        const gchar *variable,
        GValue *value,
        gpointer userdata)
{
    GUPnPAsyncData *cbdata;
    gboolean ta_communication_error;

    cbdata = (GUPnPAsyncData *) userdata;
    ta_communication_error = g_value_get_boolean (value);
    ((ta_communication_error_changed_callback)cbdata->cb)
        (proxy,
         ta_communication_error,
         cbdata->userdata);
}

gboolean
ta_communication_error_add_notify (GUPnPServiceProxy *proxy,
        ta_communication_error_changed_callback callback,
        gpointer userdata)
{
    GUPnPAsyncData *cbdata;

    cbdata = (GUPnPAsyncData *) g_slice_alloc (sizeof (*cbdata));
    cbdata->cb = G_CALLBACK (callback);
    cbdata->userdata = userdata;

    return gupnp_service_proxy_add_notify
        (proxy,
         "TACommunicationError",
         G_TYPE_BOOLEAN,
         _ta_communication_error_changed_callback,
         cbdata);
}
