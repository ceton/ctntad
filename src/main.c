#include "config.h"

#define MOCUR_DEVICE_TYPE "urn:schemas-cetoncorp-com:device:SecureContainer:1"
#define OCTA_DEVICE_TYPE "urn:schemas-cetoncorp-com:device:SecureContainer:1"
#define OCTA_SERVICE_TYPE "urn:schemas-microsoft-com:service:OCTAMessage:1"

#define G_USB_API_IS_SUBJECT_TO_CHANGE
#include <gusb.h>
#include <libgupnp/gupnp.h>
#include <stdlib.h>
#include <string.h>

#include "octa_client.h"

#define CISCO_TA_VENDOR_ID 0x05a6
#define CISCO_TA_PRODUCT_ID 0x0008
#define MOT_TA_VENDOR_ID 0x07b2
#define MOT_TA_PRODUCT_ID 0x6002

#define TA_EP_READ 0x81
#define TA_EP_WRITE 0x02
#define TA_TIMEOUT 10000 //ms
#define TA_BUFFER_SIZE 16*1024
#define TA_RECV_BUFFERS 5

static guint16 g_bus = 0xFFFF;
static guint16 g_addr = 0xFFFF;

typedef struct _Pair Pair;

typedef struct {
    Pair* p;
    GCancellable* cancellable;
    guchar buffer[TA_BUFFER_SIZE];
} TABuffer;

struct _Pair {
    GUPnPDeviceProxy* mocur;
    GUPnPServiceProxy* octa;
    GUsbDevice* ta;
    TABuffer ta_buffers[TA_RECV_BUFFERS];
    gint refs;
};

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
ta_message_ready(
        GObject* source,
        GAsyncResult* res,
        gpointer user_data);

static gboolean
enable_octa(
        gpointer userdata);

static gboolean
toggle_octa(
        gpointer userdata);

static Pair*
pair_new()
{
    Pair* p = g_slice_new0(Pair);
    p->refs = 1;
    return p;
}

static void
pair_ref(Pair* p)
{
    if(!p) { 
        g_print("!pair on ref\n");        
        return;
    }

    g_atomic_int_inc( &p->refs );
}

static void
pair_unref(Pair* p)
{
    if(!p) { 
        g_print("!pair on unref\n");        
        return;
    }

    if( g_atomic_int_dec_and_test( &p->refs ) ) {
        g_free( p );
    }
}

static void
submit_ta_buffers(
        Pair* p)
{
    int i;
    for( i=0; i<TA_RECV_BUFFERS; i++ ) {
        TABuffer* tab = &p->ta_buffers[i];
        if( !tab->cancellable ) {
            tab->cancellable = g_cancellable_new();
        } else {
            g_cancellable_reset( tab->cancellable );
        }

        pair_ref(p);

        tab->p = p;
        g_usb_device_bulk_transfer_async(
                p->ta,
                TA_EP_READ,
                tab->buffer,
                TA_BUFFER_SIZE,
                0,
                tab->cancellable,
                ta_message_ready,
                tab);
    }
}

static void usb_reset_complete_finished(
        GUPnPServiceProxy *proxy,
        GError *error,
        gpointer userdata)
{
    g_print("usb reset complete finished\n");
}

static gboolean
reset_ta_step2(
        Pair* p)
{
    GError* error = NULL;
    
    if( !g_usb_device_reset( p->ta, &error ) ) {
        g_printerr("ta reset failed %s\n", error->message);
        g_error_free( error );
        error = NULL;
    }

    if( !g_usb_device_claim_interface( p->ta, 0x00,
            G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER,
            &error) ) {
        g_printerr("failed to claim if %s\n", error->message);
        g_error_free(error);
    }

    g_print("reset done\n");
    usb_reset_complete_async( p->octa, usb_reset_complete_finished, p );

    submit_ta_buffers(p);
    pair_unref(p);
    return FALSE;
}

static gboolean
reset_ta(
        Pair* p)
{
    g_print("reset ta\n");
    //TODO move this to async call
    GError* error = NULL;
    int i;

    //cancel outstanding transfers
    for( i=0; i<TA_RECV_BUFFERS; i++ ) {
        g_cancellable_cancel(p->ta_buffers[i].cancellable);
    }

    g_usb_device_release_interface(
            p->ta,
            0,
            G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER,
            &error);

    if( error ) {
        g_printerr("failed to release device %s\n", error->message);
        g_error_free( error );
        error = NULL;
    }

    g_idle_add( (GSourceFunc)reset_ta_step2, p );
    return FALSE;
}

static void
schedule_ta_reset(Pair* p)
{
    pair_ref(p);
    g_idle_add((GSourceFunc)reset_ta, p);
}

static void
ta_communication_error_changed(
        GUPnPServiceProxy *proxy,
        gboolean ta_communication_error,
        gpointer userdata)
{
    Pair* p = userdata;
    g_print("ta comm error %d\n", ta_communication_error);
    if( ta_communication_error ) {
        schedule_ta_reset(p);
    }
}

typedef struct {
    Pair* p;
    guchar* buffer; 
} SendContext;

static void
udcp_message_sent(
        GObject* source,
        GAsyncResult* res,
        gpointer user_data)
{
    SendContext* sc = user_data;
    GError* error = NULL;
    Pair* p = sc->p;
    g_usb_device_bulk_transfer_finish( p->ta, res, &error );

    g_free( sc->buffer );
    g_slice_free1( sizeof(SendContext), sc );
    pair_unref(p);

    if( error ) {
        g_printerr("ta write failed %s\n", error->message);
        g_error_free( error );
        error = NULL;
    }
}

static void
udcp_message_changed(
        GUPnPServiceProxy* proxy,
        const gchar *udcp_message,
        gpointer userdata)
{
    Pair* p = userdata;
    gsize len = 0;
    guchar* message = g_base64_decode( udcp_message, &len );
    g_print("mocur -> ta: %d bytes\n", len);

    if( strlen( message ) ) {
        SendContext* sc = g_slice_new(SendContext);
        pair_ref(p);
        sc->p = p;
        sc->buffer = message;

        g_usb_device_bulk_transfer_async( p->ta,
                TA_EP_WRITE,
                message,
                len,
                TA_TIMEOUT,
                NULL,
                udcp_message_sent,
                sc);
    }
}


static void message_to_udcp_sent(
        GUPnPServiceProxy *proxy,
        GError *error,
        gpointer userdata)
{
}


static void
ta_message_ready(
        GObject* source,
        GAsyncResult* res,
        gpointer user_data)
{
    GError* error = NULL;
    TABuffer* tab = user_data;
    Pair* p = tab->p;
    gssize len = g_usb_device_bulk_transfer_finish( p->ta, res, &error );

    if( error ) {
        gint code = error->code;
        if( code == G_USB_DEVICE_ERROR_CANCELLED ||
                code == G_USB_DEVICE_ERROR_NO_DEVICE ) {
            g_error_free( error );
            pair_unref(p);
            return;
        }
        g_printerr("ta read failed %s\n", error->message);
        g_error_free( error );
        goto resubmit;
    }

    gchar* encoded = g_base64_encode( tab->buffer, len );

    g_print("ta -> mocur: %d bytes\n", len);

    send_message_to_udcp_async(
            p->octa,
            encoded,
            message_to_udcp_sent,
            p);

resubmit:
    g_usb_device_bulk_transfer_async(
            p->ta,
            TA_EP_READ,
            tab->buffer,
            TA_BUFFER_SIZE,
            0,
            tab->cancellable,
            ta_message_ready,
            tab);
}

static void
octa_init_complete(
        GUPnPServiceProxy *proxy,
        GError *error,
        gpointer userdata)
{
    if( error ) {
        g_printerr("octa init failed %s\n", error->message);
        g_error_free(error);
        error = NULL;
        g_timeout_add( 1, enable_octa, userdata );
        return;
    }
    g_print("octa init complete\n");
}

static gboolean
enable_octa(
        gpointer userdata)
{
    GUPnPServiceProxy* octa = userdata;
    octa_init_async(octa, TRUE, octa_init_complete, userdata);
    return FALSE;
}

static void
octa_init_complete_disable(
        GUPnPServiceProxy *proxy,
        GError *error,
        gpointer userdata)
{
    if( error ) {
        g_printerr("octa init failed %s\n", error->message);
        g_error_free(error);
        error = NULL;
        return;
    }

    g_print("disable octa complete\n");
    g_object_unref( proxy );
}

static gboolean
disable_octa(
        GUPnPServiceProxy* octa)
{
    octa_init_async(octa, FALSE, octa_init_complete_disable, NULL);
    return FALSE;
}


static void
octa_init_complete_toggle(
        GUPnPServiceProxy *proxy,
        GError *error,
        gpointer userdata)
{
    if( error ) {
        g_printerr("octa init failed %s\n", error->message);
        g_error_free(error);
        error = NULL;
        //retry
        g_timeout_add( 1, toggle_octa, userdata );
        return;
    }

    g_print("disable octa complete, scheduling re-enable\n");
    g_timeout_add_seconds(1, enable_octa, userdata);
}


static gboolean
toggle_octa(
        gpointer userdata)
{
    GUPnPServiceProxy* octa = userdata;
    octa_init_async(octa, FALSE, octa_init_complete_toggle, userdata);
    return FALSE;
}

static void
octa_get_enable_octa_complete(
        GUPnPServiceProxy* proxy,
        gboolean octa_enable,
        GError* error,
        gpointer userdata)
{
    if( error ) {
        g_printerr("failed to get octa_enable %s\n", error->message);
        g_error_free( error );
        error = NULL;
        return;
    }

    g_print("octa_enable was %d\n", octa_enable);

    if( octa_enable ) {
        g_timeout_add( 1, toggle_octa, proxy );
    } else {
        g_timeout_add( 1, enable_octa, proxy );
    }
}

static void
pair(CtnTa* ct)
{
    while( ct->mocurs->len && ct->tas->len ) {
        GUPnPDeviceProxy* mocur = g_ptr_array_index( ct->mocurs, 0 );
        GUsbDevice* ta = g_ptr_array_index( ct->tas, 0 );

        Pair* p = pair_new();
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
        guint16 bus = g_usb_device_get_bus( p->ta );
        guint16 address = g_usb_device_get_address( p->ta );

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

        submit_ta_buffers(p);
        octa_get_enable_octa_async(p->octa, octa_get_enable_octa_complete, NULL);
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
            pair_unref( p );
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
    if( ( vid == MOT_TA_VENDOR_ID && pid == MOT_TA_PRODUCT_ID ) ||
            ( vid == CISCO_TA_VENDOR_ID && pid == CISCO_TA_PRODUCT_ID ) ) {

        guint16 bus = g_usb_device_get_bus(device);
        guint16 addr = g_usb_device_get_address(device);

        if( (g_bus == 0xFFFF || g_bus == bus) && (g_addr == 0xFFFF || g_addr == addr) ) {
            g_print("found ta on bus %d addr %d\n", bus, addr);

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
    guint16 bus_remove = g_usb_device_get_bus( device );
    guint16 address_remove = g_usb_device_get_address( device );

    int i,j;
    GError* error = NULL;
    //check pairings for this usb device first
    for( i=0; i<ct->pairs->len; i++ ) {
        Pair* p = g_ptr_array_index( ct->pairs, i );
        guint16 bus = g_usb_device_get_bus( p->ta );
        guint16 address = g_usb_device_get_address( p->ta );
        if( ( bus == bus_remove ) && ( address == address_remove ) ) {

            g_print("ta %x.%x gone\n", bus, address);

            g_object_ref( p->octa );
            disable_octa( p->octa );

            g_ptr_array_remove_index_fast( ct->pairs, i );

            g_ptr_array_add( ct->mocurs, p->mocur );
            g_object_unref( p->octa );

            //cancel outstanding transfers
            for( j=0; j<TA_RECV_BUFFERS; j++ ) {
                g_cancellable_cancel(p->ta_buffers[i].cancellable);
            }

            g_usb_device_release_interface(
                    p->ta,
                    0,
                    G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER,
                    &error);

            if( error ) {
                g_printerr("failed to release device %s\n", error->message);
                g_error_free( error );
                error = NULL;
            }

            g_object_unref( p->ta );
            pair_unref( p );
            break;
        }
    }

    for( i=0; i<ct->tas->len; i++ ) {
        GUsbDevice* d = g_ptr_array_index( ct->tas, i );
        guint16 bus = g_usb_device_get_bus( d );
        guint16 address = g_usb_device_get_address( d );
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

static gboolean
stdin_cb(
        GIOChannel* iochannel, GIOCondition condition, gpointer data)
{
    CtnTa* ct = data;

    if( condition & G_IO_ERR ) {
        g_print("stdin error\n");
        goto error;
    }

    if( condition & G_IO_IN ) {
        GIOStatus status;
        GError* error = NULL;
        gchar buffer[1024] = {};
        gsize bytes_read;

        status = g_io_channel_read_chars(iochannel, buffer, sizeof(buffer), &bytes_read, &error);

        if( status != G_IO_STATUS_NORMAL ) {
            g_print("failed to read stdin\n");
            goto error;
        }

        if( bytes_read == 0 ) {
            goto error;
        }

        if( strncmp( buffer, "reset", strlen("reset") ) == 0 ) {
            if( ct->pairs->len ) {
                Pair* p = g_ptr_array_index( ct->pairs, 0 );
                schedule_ta_reset(p);
            } else {
                g_print("No pair found\n");
            }
        } else {
            g_print("Commands available:\n");
            g_print("\treset\n");
        }
    }

    return TRUE;
error:
    return FALSE;
}

static void
list_usb(CtnTa* ct)
{
    GPtrArray* devices;
    GUsbDevice* device;
    int i;
    int found = 0;

    ct->usb_list = g_usb_device_list_new( ct->usb_context );
    g_usb_device_list_coldplug( ct->usb_list );

    devices = g_usb_device_list_get_devices( ct->usb_list );
    for( i=0; i<devices->len; i++ ) {
        device = g_ptr_array_index( devices, i );

        guint16 vid = g_usb_device_get_vid( device );
        guint16 pid = g_usb_device_get_pid( device );
        if( ( vid == MOT_TA_VENDOR_ID && pid == MOT_TA_PRODUCT_ID ) ||
                ( vid == CISCO_TA_VENDOR_ID && pid == CISCO_TA_PRODUCT_ID ) ) {
            guint16 bus = g_usb_device_get_bus(device);
            guint16 addr = g_usb_device_get_address(device);

            if(vid == MOT_TA_VENDOR_ID) {
                g_print("Found Motorola Tuning Adapter on bus %d address %d\n", bus, addr);
            } else if(vid == CISCO_TA_VENDOR_ID) {
                g_print("Found Cisco Tuning Adapter on bus %d address %d\n", bus, addr);
            } else {
                g_print("Found Unknown Tuning Adapter on bus %d address %d\n", bus, addr);
            }
            found = 1;
        }
    }

    if( !found ) {
        g_print("No Tuning Adapters found\n");
    }
    
    g_ptr_array_unref( devices );
}

static gchar* interface = NULL;
static gboolean list_tas = FALSE;
static gint i_bus = -1;
static gint i_addr = -1;

static GOptionEntry options[] = {
    { "interface", 'i', 0, G_OPTION_ARG_STRING, &interface, "IP interface to bind to", "I" },
    { "bus", 'b', 0, G_OPTION_ARG_INT, &i_bus, "bus of the TA you want to use", NULL },
    { "address", 'a', 0, G_OPTION_ARG_INT, &i_addr, "address of the TA you want to use", NULL },
    { "list-tas", 'l', 0, G_OPTION_ARG_NONE, &list_tas, "List the TAs found", NULL },
    { NULL }
};

int main(int argc, char** argv)
{
    GError* error = NULL;
    GOptionContext* option_ctx = NULL;

    option_ctx = g_option_context_new( " - Tuning Adapter service for the Ceton InfiniTV" );
    g_option_context_add_main_entries( option_ctx, options, NULL );

    if( !g_option_context_parse( option_ctx, &argc, &argv, &error ) ) {
        g_print("Option parsing failed: %s\n", error->message);
        return EXIT_FAILURE;
    }

    g_print("Starting %s\n", PACKAGE_STRING);

    CtnTa* ct = g_slice_new0( CtnTa );

    ct->mocurs = g_ptr_array_new();
    ct->tas = g_ptr_array_new();
    ct->pairs = g_ptr_array_new();
    ct->context = gupnp_context_new( interface, 0, &error );

    if(i_bus != -1) {
        g_bus = (guint16)i_bus;
    }

    if(i_addr != -1) {
        g_addr = (guint16)i_addr;
    }

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

    if(list_tas) {
        list_usb(ct);
    } else {

        GIOChannel* in = g_io_channel_unix_new(fileno(stdin));
        g_io_add_watch(in, G_IO_IN | G_IO_ERR | G_IO_HUP | G_IO_NVAL,
                stdin_cb, ct);

        ct->main_loop = g_main_loop_new( NULL, FALSE );

        setup_upnp(ct);
        setup_usb(ct);

        g_main_loop_run(ct->main_loop);

        g_main_loop_unref( ct->main_loop );
    }

    if( ct->cp ) {
        g_object_unref( ct->cp );
    }

    g_object_unref( ct->context );
    g_object_unref( ct->usb_list );
    g_object_unref( ct->usb_context );
    g_ptr_array_unref( ct->mocurs );
    g_ptr_array_unref( ct->tas );
    g_ptr_array_unref( ct->pairs );

    g_slice_free( CtnTa, ct );

    return EXIT_SUCCESS;
}
