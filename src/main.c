
#include <libgupnp/gupnp.h>
#include <stdlib.h>

static GMainLoop* main_loop;

static void
service_proxy_available_cb(GUPnPControlPoint* cp, GUPnPServiceProxy* proxy)
{
    GError* error = NULL;
    g_print("service found");
}

int main(int argc, char** argv)
{
    GError* error = NULL;
    GUPnPContext* context;
    GUPnPControlPoint* cp;

    g_thread_init(NULL);
    g_type_init();

    context = gupnp_context_new(NULL, NULL, 0, &error);

    if( error ) {
        g_printerr("Error creating the GUPnP context: %s\n",
                error->message);
        g_error_free(error);
        return EXIT_FAILURE;
    }

    cp = gupnp_control_point_new(context, "urn:schemas-cablelabs-org:service:OCTA:1");

    g_signal_connect( cp, "service-proxy-available",
            G_CALLBACK(service_proxy_available_cb),
            NULL);

    gssdp_resource_browser_set_active( GSSDP_RESOURCE_BROWSER(cp), TRUE );

    main_loop = g_main_loop_new( NULL, FALSE );
    g_main_loop_run(main_loop);

    g_main_loop_unref(main_loop);
    g_object_unref(cp);
    g_object_unref(context);

    return EXIT_SUCCESS;
}
