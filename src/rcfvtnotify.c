/* simple code to send a notification using libnotify */
#include <stdbool.h>
#include <libnotify/notify.h>

bool rcfvtnotify_init(void)
{
	return notify_init("rcfvt");
}

bool rcfvtnotify_termbell(const char *title)
{
	NotifyNotification *notification;
	bool ret;

	notification = notify_notification_new(title, "bell received", NULL);
	ret = notify_notification_show(notification, NULL);
	g_object_unref(G_OBJECT(notification));
	return ret;
}

