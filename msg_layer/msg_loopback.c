#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

#include <popcorn/pcn_kmsg.h>
#include <popcorn/bundle.h>

static int loopback_kmsg_send_long(unsigned int nid, struct pcn_kmsg_long_message *lmsg, unsigned int payload_size)
{
	void *msg;
	size_t size = payload_size + sizeof(struct pcn_kmsg_hdr);
	struct pcn_kmsg_hdr *hdr;
	pcn_kmsg_cbftn fn;

	msg = vmalloc(size);
	BUG_ON(!msg);
	memcpy(msg, lmsg, size);

	hdr = (struct pcn_kmsg_hdr *)msg;
	hdr->from_cpu = get_nid();
	hdr->size = size;

	fn = callbacks[hdr->type];
	if (!fn) {
		printk(KERN_ERR"%s: NULL FN", __func__);
		vfree(msg);
		return -ENOENT;
	}
	// printk(KERN_ERR"%s: CALL %d %d\n", __func__, hdr->type, hdr->size);

	fn(msg);
	return 0;
}

static int __init loopback_load(void)
{
	printk(KERN_INFO"Popcorn message layer loopback loaded\n");
	send_callback = (send_cbftn)loopback_kmsg_send_long;

	return 0;
}

static void loopback_unload(void)
{
	send_callback = NULL;
	printk(KERN_INFO"Popcorn message layer loopback unloaded\n");
}

module_init(loopback_load);
module_exit(loopback_unload);
MODULE_LICENSE("GPL");
