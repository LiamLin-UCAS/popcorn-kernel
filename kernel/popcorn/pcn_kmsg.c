/*
 * pcn_kmesg.c - Kernel Module for Popcorn Messaging Layer over Socket
 */

#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>

#include <popcorn/pcn_kmsg.h>
#include <popcorn/debug.h>
#include <popcorn/stat.h>

char *msg_layer = NULL;
EXPORT_SYMBOL(msg_layer);

pcn_kmsg_cbftn callbacks[PCN_KMSG_TYPE_MAX];
EXPORT_SYMBOL(callbacks);

send_cbftn send_callback;
EXPORT_SYMBOL(send_callback);

send_rdma_cbftn send_rdma_callback;
EXPORT_SYMBOL(send_rdma_callback);

handle_rdma_request_ftn handle_rdma_callback;
EXPORT_SYMBOL(handle_rdma_callback);

kmsg_free_ftn kmsg_free_callback = NULL;
EXPORT_SYMBOL(kmsg_free_callback);

/* Initialize callback table to null, set up control and data channels */
int __init pcn_kmsg_init(void)
{
	send_callback = NULL;
	MSGPRINTK("%s: done\n", __func__);
	return 0;
}

int pcn_kmsg_register_callback(enum pcn_kmsg_type type, pcn_kmsg_cbftn callback)
{
	if (type >= PCN_KMSG_TYPE_MAX)
		return -ENODEV; /* invalid type */

	MSGPRINTK("%s: %d \n", __func__, type);
	callbacks[type] = callback;
	return 0;
}

int pcn_kmsg_unregister_callback(enum pcn_kmsg_type type)
{
	if (type >= PCN_KMSG_TYPE_MAX)
		return -ENODEV;

	MSGPRINTK("%s: %d\n", __func__, type);
	callbacks[type] = NULL;
	return 0;
}

int pcn_kmsg_send(unsigned int to, void *msg, unsigned int size)
{
	struct pcn_kmsg_hdr *hdr = msg;
	if (send_callback == NULL) {
		printk(KERN_ERR"%s: No send fn. from=%u, type=%d, size=%u\n",
					__func__, hdr->from_nid, hdr->type, size);
		// msleep(100);
		//printk("Waiting for call back function to be registered\n");
		return -ENOENT;
	}

#ifdef CONFIG_POPCORN_STAT
	account_pcn_message_sent(msg);
#endif

	return send_callback(to, (struct pcn_kmsg_message *)msg, size);
}

void *pcn_kmsg_alloc_msg(size_t size)
{
	return kmalloc(size, GFP_KERNEL);
}

void pcn_kmsg_free_msg(void *msg)
{
	if (!memcmp(msg_layer,"IB", 2)) {
#ifdef CONFIG_POPCORN_KMSG_IB_RDMA
		struct pcn_kmsg_message *m = (struct pcn_kmsg_message *)msg;
		if (m->header.is_rdma) {
			if (((pcn_kmsg_rdma_t *)m)->rdma_header.rdma_ack &&
				((pcn_kmsg_rdma_t *)m)->rdma_header.is_write) {
					kfree(((pcn_kmsg_rdma_t *)m)->poll_head_addr);
			} else if (!((pcn_kmsg_rdma_t *)m)->rdma_header.rdma_ack) {
				kmsg_free_callback(msg);
			} else {
				kfree(msg);
			}
		}
		else
#endif
		{
			kmsg_free_callback(msg);
		}
	} else {
		kfree(msg);
	}
}

void pcn_rdma_kmsg_free_msg(void *msg)
{
	
	kfree(msg);
}

/*
 * Your request must be allocated by kmalloc().
 * rw_size: Max size you expect remote to perform a R/W
 */
void *pcn_kmsg_send_rdma(unsigned int to, void *lmsg,
						unsigned int msg_size, unsigned int rw_size)
{
    if (send_rdma_callback == NULL) {
		struct pcn_kmsg_hdr *hdr = lmsg;
		printk(KERN_ERR"%s: No send fn. from=%u, type=%d, "
				"msg_size=%u rw_size=%u\n", __func__,
				hdr->from_nid, hdr->type, msg_size, rw_size);
        return NULL;
    }

    return send_rdma_callback(to, lmsg, msg_size, rw_size);
}

void pcn_kmsg_handle_remote_rdma_request(
				void *inc_lmsg, void *paddr, u32 rw_size)
{
	if (!memcmp(msg_layer,"IB", 2))
		handle_rdma_callback(inc_lmsg, paddr, rw_size);
	else
		printk(KERN_ERR "%s: current msg_layer (%s) "
				"is not \"IB\"\n", __func__, msg_layer);
}


EXPORT_SYMBOL(pcn_kmsg_alloc_msg);
EXPORT_SYMBOL(pcn_kmsg_free_msg);
EXPORT_SYMBOL(pcn_rdma_kmsg_free_msg);
EXPORT_SYMBOL(pcn_kmsg_send_rdma);
EXPORT_SYMBOL(pcn_kmsg_send);
EXPORT_SYMBOL(pcn_kmsg_unregister_callback);
EXPORT_SYMBOL(pcn_kmsg_register_callback);
EXPORT_SYMBOL(pcn_kmsg_handle_remote_rdma_request);
