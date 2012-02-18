#include "config.h"

#define G_USB_API_IS_SUBJECT_TO_CHANGE
#include <gusb.h>
#include <libgupnp/gupnp.h>
#include <stdlib.h>

typedef struct {
    GPtrArray* tas;
    GPtrArray* mocurs;
    GMainLoop* main_loop;
    GUPnPContext* context;
    GUPnPControlPoint* cp;
    GUsbContext* usb_context;
    GUsbDeviceList* usb_list;
} CtnTa;

static void
device_proxy_available_cb(GUPnPControlPoint* cp, GUPnPDeviceProxy* proxy)
{
    GError* error = NULL;
    g_print("device found");
}

static void
setup_upnp(CtnTa* ct)
{
    ct->cp = gupnp_control_point_new( ct->context, "urn:schemas-cetoncorp-com:device:SecureContainer:1" );

    g_signal_connect( ct->cp, "device-proxy-available",
            G_CALLBACK(device_proxy_available_cb),
            ct );

    gssdp_resource_browser_set_active( GSSDP_RESOURCE_BROWSER(ct->cp), TRUE );
}

static void
usb_device_list_added_cb(
        GUsbDeviceList* list,
        GUsbDevice* device,
        gpointer user_data)
{
    g_print("device %s added %x:%x\n",
            g_usb_device_get_platform_id( device ),
            g_usb_device_get_bus( device ),
            g_usb_device_get_address( device ));
}

static void
usb_device_list_removed_cb(
        GUsbDeviceList* list,
        GUsbDevice* device,
        gpointer user_data)
{
    g_print("device %s removed %x:%x\n",
            g_usb_device_get_platform_id( device ),
            g_usb_device_get_bus( device ),
            g_usb_device_get_address( device ));
}

static void
setup_usb(CtnTa* ct)
{
    GPtrArray* devices;
    GUsbDevice* device;
    int i;

    ct->usb_list = g_usb_device_list_new( ct->usb_context );
    g_usb_device_list_coldplug( ct->usb_list );

    devices = g_usb_device_list_get_devices( ct->usb_list );
    for( i=0; i<devices->len; i++ ) {
        device = g_ptr_array_index( devices, i );
        g_print("usb device %x:%x\n",
                g_usb_device_get_bus(device),
                g_usb_device_get_address(device));
    }

    g_signal_connect( ct->usb_list, "device-added",
            G_CALLBACK( usb_device_list_added_cb ),
            ct);

    g_signal_connect( ct->usb_list, "device-removed",
            G_CALLBACK( usb_device_list_removed_cb ),
            ct);

    g_ptr_array_unref( devices );
}

int main(int argc, char** argv)
{
    GError* error = NULL;

    g_thread_init(NULL);
    g_type_init();

    CtnTa* ct = g_slice_new0( CtnTa );

    ct->context = gupnp_context_new( NULL, NULL, 0, &error );

    if( error ) {
        g_printerr("Error creating the GUPnP context: %s\n",
                error->message);
        g_error_free(error);
        return EXIT_FAILURE;
    }

    ct->usb_context = g_usb_context_new( &error );
    
    if( error ) {
        g_printerr("Error creating GUsb context: %s\n",
                error->message);
        g_error_free(error);
        return EXIT_FAILURE;
    }
 
    ct->main_loop = g_main_loop_new( NULL, FALSE );

    setup_upnp(ct);
    setup_usb(ct);

    g_main_loop_run(ct->main_loop);

    g_main_loop_unref( ct->main_loop );
    g_object_unref( ct->cp );
    g_object_unref( ct->context );
    g_object_unref( ct->usb_list );
    g_object_unref( ct->usb_context );

    g_slice_free( CtnTa, ct );

    return EXIT_SUCCESS;
}
