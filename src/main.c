#include "config.h"

#define MOCUR_DEVICE_TYPE "urn:schemas-cetoncorp-com:device:SecureContainer:1"
#define OCTA_DEVICE_TYPE "urn:schemas-cetoncorp-com:device:SecureContainer:1"
#define OCTA_SERVICE_TYPE "urn:schemas-microsoft-com:service:OCTAMessage:1"

#define G_USB_API_IS_SUBJECT_TO_CHANGE
#include <gusb.h>
#include <libgupnp/gupnp.h>
#include <stdlib.h>

#include "octa_client.h"

typedef struct {
    GUPnPDeviceProxy* mocur;
    GUPnPServiceProxy* octa;
    GUsbDevice* ta;
} Pair;

typedef struct {
    GPtrArray* tas;
    GPtrArray* mocurs;
    GMainLoop* main_loop;
    GUPnPContext* context;
    GUPnPControlPoint* cp;
    GUsbContext* usb_context;
    GUsbDeviceList* usb_list;
    GPtrArray* pairs;
} CtnTa;

static void
ta_communication_error_changed(
        GUPnPServiceProxy *proxy,
        gboolean ta_communication_error,
        gpointer userdata)
{
    g_print("ta comm error %d\n", ta_communication_error);
    if( ta_communication_error ) {
        //TODO do usb reset
    }
}

static void
udcp_message_changed(
        GUPnPServiceProxy* proxy,
        const gchar *udcp_message,
        gpointer userdata)
{
    g_print("udcp message %s\n", udcp_message);
}


static void
pair(CtnTa* ct)
{
    while( ct->mocurs->len && ct->tas->len ) {
        GUPnPDeviceProxy* mocur = g_ptr_array_index( ct->mocurs, 0 );
        GUsbDevice* ta = g_ptr_array_index( ct->tas, 0 );

        Pair* p = g_slice_new0( Pair );
        p->mocur = mocur;
        p->ta = ta;

        //get first octa instance
        GList* devices = gupnp_device_info_list_devices( GUPNP_DEVICE_INFO(p->mocur) );
        GList* i = devices;
        for( ; i; i = g_list_next(i) ) {
            GUPnPDeviceProxy* sub_device = GUPNP_DEVICE_PROXY(i->data);
            const char* type = gupnp_device_info_get_device_type( GUPNP_DEVICE_INFO(sub_device) );
            if( strcmp( type, OCTA_DEVICE_TYPE ) == 0 ) {
                p->octa = GUPNP_SERVICE_PROXY(gupnp_device_info_get_service( GUPNP_DEVICE_INFO(sub_device), OCTA_SERVICE_TYPE ));
                break;
            }
        }
        g_list_free(devices);

        
        const char* udn = gupnp_device_info_get_udn( GUPNP_DEVICE_INFO(p->mocur) );
        guint8 bus = g_usb_device_get_bus( p->ta );
        guint8 address = g_usb_device_get_address( p->ta );

        g_print("paired '%s' and %x:%x\n", udn, bus, address);
     
        g_ptr_array_remove_index( ct->mocurs, 0 );
        g_ptr_array_remove_index( ct->tas, 0 );
        g_ptr_array_add( ct->pairs, p );

        gupnp_service_proxy_set_subscribed( p->octa, TRUE );

        ta_communication_error_add_notify(p->octa,
                ta_communication_error_changed,
                p);

        udcp_message_add_notify(p->octa,
                udcp_message_changed,
                p);
    }
}

static void
device_proxy_available_cb(GUPnPControlPoint* cp, GUPnPDeviceProxy* proxy, gpointer user_data)
{
    CtnTa* ct = user_data;
    GError* error = NULL;
    const char* device_type = gupnp_device_info_get_device_type( GUPNP_DEVICE_INFO(proxy) );
    g_print("root device found type: '%s'\n", device_type);
    if( strcmp( device_type, "urn:schemas-cetoncorp-com:device:SecureContainer:1" ) == 0 ) {
        g_print("mocur found\n");
        g_ptr_array_add( ct->mocurs, proxy );
        pair( ct );
    }
}

static void
remove_mocur(
        CtnTa* ct,
        GUPnPDeviceProxy* mocur)
{
    int i;
    const char* udn_remove = gupnp_device_info_get_udn( GUPNP_DEVICE_INFO(mocur) );

    //first check pairings
    for( i=0; i<ct->pairs->len; i++ ) {
        Pair* p = g_ptr_array_index( ct->pairs, i );
        const char* udn = gupnp_device_info_get_udn( GUPNP_DEVICE_INFO(p->mocur) );
        if( strcmp( udn, udn_remove ) == 0 ) {
            g_ptr_array_remove_index_fast( ct->pairs, i );

            g_ptr_array_add( ct->tas, p->ta );
            g_object_unref( p->octa );
            g_object_unref( p->mocur );
            g_free( p );
            break;
        }
    }

    for( i=0; i<ct->mocurs->len; i++ ) {
        GUPnPDeviceProxy* m = g_ptr_array_index( ct->mocurs, i );
        const char* udn = gupnp_device_info_get_udn( GUPNP_DEVICE_INFO(m) );
        if( strcmp( udn, udn_remove ) == 0 ) {
            g_ptr_array_remove_index_fast( ct->mocurs, i );
            break;
        }
    }
}

static void
device_proxy_unavailable_cb(GUPnPControlPoint* cp, GUPnPDeviceProxy* proxy, gpointer user_data)
{
    CtnTa* ct = user_data;
    remove_mocur( ct, proxy );
}

static void
setup_upnp(CtnTa* ct)
{
    ct->cp = gupnp_control_point_new( ct->context, "upnp:rootdevice" );

    g_signal_connect( ct->cp, "device-proxy-available",
            G_CALLBACK(device_proxy_available_cb),
            ct );

    g_signal_connect( ct->cp, "device-proxy-unavailable",
            G_CALLBACK(device_proxy_unavailable_cb),
            ct );

    gssdp_resource_browser_set_active( GSSDP_RESOURCE_BROWSER(ct->cp), TRUE );
}

static void
check_for_ta(
        CtnTa* ct,
        GUsbDevice* device)
{
    GError* error = NULL;
    guint16 vid = g_usb_device_get_vid( device );
    guint16 pid = g_usb_device_get_pid( device );
    if( ( vid == 0x07b2 && pid == 0x6002 ) ||
            ( vid == 0x05a6 && pid == 0x0008 ) ) {
        g_print("found ta\n");

        gboolean ret = g_usb_device_open( device, &error );
        if( !ret ) {
            g_printerr("failed to open device %s\n", error->message);
            g_error_free( error );
            return;
        }

        ret = g_usb_device_set_configuration( device, 0x01, &error );
        if( !ret ) {
            g_printerr("failed to set config %s\n", error->message);
            g_error_free(error);
            return;
        }

        ret = g_usb_device_claim_interface( device, 0x00,
                G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER,
                &error);
        if( !ret ) {
            g_printerr("failed to claim if %s\n", error->message);
            g_error_free(error);
            return;
        }


        g_ptr_array_add( ct->tas, device );
        pair( ct ); 
    }
}

static void
usb_device_list_added_cb(
        GUsbDeviceList* list,
        GUsbDevice* device,
        gpointer user_data)
{
    CtnTa* ct = user_data;
    g_print("device %s added %x:%x\n",
            g_usb_device_get_platform_id( device ),
            g_usb_device_get_bus( device ),
            g_usb_device_get_address( device ));
    check_for_ta( ct, device );
}

static void
check_for_removed_ta(
        CtnTa* ct,
        GUsbDevice* device)
{
    guint8 bus_remove = g_usb_device_get_bus( device );
    guint8 address_remove = g_usb_device_get_address( device );

    int i;
    //check pairings for this usb device first
    for( i=0; i<ct->pairs->len; i++ ) {
        Pair* p = g_ptr_array_index( ct->pairs, i );
        guint8 bus = g_usb_device_get_bus( p->ta );
        guint8 address = g_usb_device_get_address( p->ta );
        if( ( bus == bus_remove ) && ( address == address_remove ) ) {

            g_ptr_array_remove_index_fast( ct->pairs, i );

            g_ptr_array_add( ct->mocurs, p->mocur );
            g_object_unref( p->octa );
            g_object_unref( p->ta );
            g_free( p );
            break;
        }
    }

    for( i=0; i<ct->tas->len; i++ ) {
        GUsbDevice* d = g_ptr_array_index( ct->tas, i );
        guint8 bus = g_usb_device_get_bus( d );
        guint8 address = g_usb_device_get_address( d );
        if( ( bus == bus_remove ) && ( address == address_remove ) ) {
            g_ptr_array_remove_index_fast( ct->tas, i );
        }
    }
}

static void
usb_device_list_removed_cb(
        GUsbDeviceList* list,
        GUsbDevice* device,
        gpointer user_data)
{
    CtnTa* ct = user_data;
    g_print("device %s removed %x:%x\n",
            g_usb_device_get_platform_id( device ),
            g_usb_device_get_bus( device ),
            g_usb_device_get_address( device ));
    check_for_removed_ta( ct, device );
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
        check_for_ta( ct, device );
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

    ct->mocurs = g_ptr_array_new();
    ct->tas = g_ptr_array_new();
    ct->pairs = g_ptr_array_new();
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
    g_ptr_array_unref( ct->mocurs );
    g_ptr_array_unref( ct->tas );
    g_ptr_array_unref( ct->pairs );

    g_slice_free( CtnTa, ct );

    return EXIT_SUCCESS;
}
