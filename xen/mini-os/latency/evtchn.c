#include <mini-os/os.h>
#include <mini-os/events.h>
#include <mini-os/sched.h>

#define NR_ITERATIONS 1000000

static void
server_handler(evtchn_port_t port, struct pt_regs *regs, void *ignore)
{
	notify_remote_via_evtchn(port);
}

static void
client_handler(evtchn_port_t port, struct pt_regs *regs, void *_cntr)
{
	int *cntr = _cntr;
	if (*cntr == -1)
		return;
	if (*cntr < NR_ITERATIONS)
		notify_remote_via_evtchn(port);
	(*cntr)++;
}

static int
run_server(void)
{
	int client_dom;
	int r;
	static evtchn_port_t port;

	printk("I am the server\n");
	do {
		client_dom = xenbus_read_integer("client_dom");
	} while (client_dom == -1);

	printk("Client domain is %d\n", client_dom);
	r = evtchn_alloc_unbound(client_dom, server_handler,
				 &port, &port);
	if (r) {
		printk("Failed to allocate unbound port; %d\n", r);
		return 1;
	}
	printk("Allocated port %d\n", port);

	unmask_evtchn(port);

	return 0;
}

static int
run_client(void)
{
	int server_dom;
	int server_port;
	int cntr;
	evtchn_port_t local_port;
	int r;
	int64_t start;
	int64_t end;

	do {
		server_dom = xenbus_read_integer("server_dom");
		server_port = xenbus_read_integer("server_port");
	} while (server_dom == -1 || server_port == -1);

	printk("Server domain:port is %d:%d\n", server_dom, server_port);

	cntr = -1;
	r = evtchn_bind_interdomain(server_dom, server_port,
				    client_handler, &cntr,
				    &local_port);
	if (r != 0) {
		printk("Error %d binding event channel\n", r);
		return 1;
	}
	printk("Bound to local port %d\n", local_port);

	unmask_evtchn(local_port);

	/* Give it a second to make sure everything is nice and
	 * stable. */
	/* For some reason msleep(1000) waits forever, but msleep(900)
	   does the right thing.  Meh. */
	msleep(900);

	if (cntr != -1)
		printk("Huh? cntr %d before we even started\n", cntr);
	cntr = 0;

	start = NOW();
	r = notify_remote_via_evtchn(local_port);
	if (r != 0)
		printk("Failed to notify remote on port %d!\n");

	while (cntr < NR_ITERATIONS)
		;

	end = NOW();
	printk("All done; took %ld nanoseconds for %d(%d) iterations\n", end - start, NR_ITERATIONS, cntr);

	return 0;
}

static void
real_app_main(void *_si)
{
	start_info_t *si = _si;
	printk("Real app main started\n");
	if (!strcmp((char *)si->cmd_line, "server")) {
		run_server();
	} else if (!strcmp((char *)si->cmd_line, "client")) {
		run_client();
	} else {
		printk("Command line should say either server or client\n");
	}
}

int
app_main(start_info_t *si)
{
	printk("app_main started %s\n", si->cmd_line);

	create_thread("worker", real_app_main, si);

	return 0;
}