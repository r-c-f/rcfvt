/* simple code to ring the terminal bell using canberra*/
#include <canberra.h>
#include <stdbool.h>
#include <glib.h>

static ca_context *ca_con = NULL;
bool rcfvtca_init(void)
{
	int ret;
	if ((ret = ca_context_create(&ca_con))) {
		g_warning("Could not create canberra context: %s", ca_strerror(ret));
		return false;
	}
	if (ca_con && (ret = ca_context_open(ca_con))) {
		g_warning("Could not open canberra context: %s", ca_strerror(ret));
		ca_context_destroy(ca_con);
		return false;
	}
	return true;
}
bool rcfvtca_termbell(void)
{
	if (ca_con) {
		ca_context_play(ca_con, 0, CA_PROP_EVENT_ID, "bell-terminal", NULL);
	}
	return !!ca_con;
}
