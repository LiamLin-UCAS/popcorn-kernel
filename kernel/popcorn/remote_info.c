/**
 * @file remote_info.c
 *
 * Popcorn Linux remote meminfo implementation
 * This work is a rework of Akshay Giridhar's implementation
 * to provide the proc/meminfo for remote nodes.
 *
 * @author Jingoo Han, SSRG Virginia Tech 2017
 */

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/swap.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

#include <popcorn/bundle.h>
#include <popcorn/pcn_kmsg.h>
#include <popcorn/remote_meminfo.h>
#include <popcorn/cpuinfo.h>

#include "wait_station.h"

//#define REMOTE_INFO_VERBOSE
#ifdef REMOTE_INFO_VERBOSE
#define RIPRINTK(...) printk(__VA_ARGS__)
#else
#define RIPRINTK(...)
#endif

int fill_meminfo_response(remote_mem_info_response_t *res)
{
	struct sysinfo i;
	unsigned long committed;
	long cached;
	long available;
	unsigned long pagecache;
	unsigned long wmark_low = 0;
	unsigned long pages[NR_LRU_LISTS];
	struct zone *zone;
	int lru;

/*
 * display in kilobytes.
 */
#define K(x) ((x) << (PAGE_SHIFT - 10))
	si_meminfo(&i);
	si_swapinfo(&i);
	committed = percpu_counter_read_positive(&vm_committed_as);

	cached = global_page_state(NR_FILE_PAGES) -
			total_swapcache_pages() - i.bufferram;
	if (cached < 0)
		cached = 0;

	for (lru = LRU_BASE; lru < NR_LRU_LISTS; lru++)
		pages[lru] = global_page_state(NR_LRU_BASE + lru);

	for_each_zone(zone)
		wmark_low += zone->watermark[WMARK_LOW];

	/*
	 * Estimate the amount of memory available for userspace allocations,
	 * without causing swapping.
	 *
	 * Free memory cannot be taken below the low watermark, before the
	 * system starts swapping.
	 */
	available = i.freeram - wmark_low;

	/*
	 * Not all the page cache can be freed, otherwise the system will
	 * start swapping. Assume at least half of the page cache, or the
	 * low watermark worth of cache, needs to stay.
	 */
	pagecache = pages[LRU_ACTIVE_FILE] + pages[LRU_INACTIVE_FILE];
	pagecache -= min(pagecache / 2, wmark_low);
	available += pagecache;

	/*
	 * Part of the reclaimable slab consists of items that are in use,
	 * and cannot be freed. Cap this estimate at the low watermark.
	 */
	available += global_page_state(NR_SLAB_RECLAIMABLE) -
		     min(global_page_state(NR_SLAB_RECLAIMABLE) / 2, wmark_low);

	if (available < 0)
		available = 0;

	/* Fill the mem information for response */
	res->MemTotal = K(i.totalram);
	res->MemFree = K(i.freeram);
	res->MemAvailable = K(available);
	res->Buffers = K(i.bufferram);
	res->Cached = K(cached);
	res->SwapCached = K(total_swapcache_pages());
	res->Active = K(pages[LRU_ACTIVE_ANON]   + pages[LRU_ACTIVE_FILE]);
	res->Inactive = K(pages[LRU_INACTIVE_ANON] + pages[LRU_INACTIVE_FILE]);
	res->Active_anon = K(pages[LRU_ACTIVE_ANON]);
	res->Inactive_anon = K(pages[LRU_INACTIVE_ANON]);
	res->Active_file = K(pages[LRU_ACTIVE_FILE]);
	res->Inactive_file = K(pages[LRU_INACTIVE_FILE]);
	res->Unevictable = K(pages[LRU_UNEVICTABLE]);
	res->Mlocked = K(global_page_state(NR_MLOCK));
#ifdef CONFIG_HIGHMEM
	res->HighTotal = K(i.totalhigh);
	res->HighFree = K(i.freehigh);
	res->LowTotal = K(i.totalram-i.totalhigh);
	res->LowFree = K(i.freeram-i.freehigh);
#endif
#ifndef CONFIG_MMU
	res->rem_mem.MmapCopy = K((unsigned long) atomic_long_read(&mmap_pages_allocated));
#endif
	res->SwapTotal = K(i.totalswap);
	res->SwapFree = K(i.freeswap);
	res->Dirty = K(global_page_state(NR_FILE_DIRTY));
	res->Writeback = K(global_page_state(NR_WRITEBACK));
	res->AnonPages = K(global_page_state(NR_ANON_PAGES));
	res->Mapped = K(global_page_state(NR_FILE_MAPPED));
	res->Shmem = K(i.sharedram);
	res->Slab = K(global_page_state(NR_SLAB_RECLAIMABLE) +
				global_page_state(NR_SLAB_UNRECLAIMABLE));
	res->SReclaimable = K(global_page_state(NR_SLAB_RECLAIMABLE));
	res->SUnreclaim = K(global_page_state(NR_SLAB_UNRECLAIMABLE));
	res->KernelStack = global_page_state(NR_KERNEL_STACK) * THREAD_SIZE / 1024;
	res->PageTables = K(global_page_state(NR_PAGETABLE));
#ifdef CONFIG_QUICKLIST
	res->Quicklists = K(quicklist_total_size());
#endif
	res->NFS_Unstable = K(global_page_state(NR_UNSTABLE_NFS));
	res->Bounce = K(global_page_state(NR_BOUNCE));
	res->WritebackTmp = K(global_page_state(NR_WRITEBACK_TEMP));
	res->CommitLimit = K(vm_commit_limit());
	res->Committed_AS = K(committed);
	res->VmallocTotal = (unsigned long)VMALLOC_TOTAL >> 10;
	res->VmallocUsed = 0ul;
	res->VmallocChunk = 0ul;
#ifdef CONFIG_MEMORY_FAILURE
	res->HardwareCorrupted = atomic_long_read(&num_poisoned_pages) << (PAGE_SHIFT - 10);
#endif
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	res->AnonHugePages = K(global_page_state(NR_ANON_TRANSPARENT_HUGEPAGES) *
		   HPAGE_PMD_NR);
#endif
#ifdef CONFIG_CMA
	res->CmaTotal = K(totalcma_pages);
	res->CmaFree = K(global_page_state(NR_FREE_CMA_PAGES));
#endif

	return 0;
}

static int handle_remote_mem_info_request(struct pcn_kmsg_message *inc_msg)
{
	remote_mem_info_request_t *request;
	remote_mem_info_response_t *response;
	int ret;

	RIPRINTK("%s: Entered\n", __func__);

	request = (remote_mem_info_request_t *)inc_msg;
	if (request == NULL) {
		RIPRINTK("%s: NULL pointer\n", __func__);
		return -EINVAL;
	}

	response = kzalloc(sizeof(*response), GFP_KERNEL);

	/* 1. Construct response data to send it into remote node */

	/* 1-1. Fill the header information */
	response->header.type = PCN_KMSG_TYPE_REMOTE_PROC_MEMINFO_RESPONSE;
	response->header.prio = PCN_KMSG_PRIO_NORMAL;
	response->nid = my_nid;
	response->origin_ws = request->origin_ws;

	/* 1-2. Fill the machine-dependent MEMORY information */
	ret = fill_meminfo_response(response);
	if (ret < 0) {
		RIPRINTK("%s: failed to fill memory info\n", __func__);
		goto out;
	}

	/* 1-3. Send response into remote node */
	ret = pcn_kmsg_send(request->nid, response, sizeof(*response));
	if (ret < 0) {
		RIPRINTK("%s: failed to send response message\n", __func__);
		goto out;
	}

	/* 2. Remove request message received from remote node */
	pcn_kmsg_free_msg(request);

	RIPRINTK("%s: done\n", __func__);
out:
	kfree(response);
	return 0;
}

static int handle_remote_mem_info_response(struct pcn_kmsg_message *inc_msg)
{
	remote_mem_info_response_t *response;
	struct wait_station *ws;

	RIPRINTK("%s: Entered\n", __func__);

	response = (remote_mem_info_response_t *)inc_msg;
	if (response == NULL) {
		RIPRINTK("%s: NULL pointer\n", __func__);
		return -EINVAL;
	}

	ws = wait_station(response->origin_ws);
	ws->private = response;

	smp_mb();

	if (atomic_dec_and_test(&ws->pendings_count))
		complete(&ws->pendings);

	RIPRINTK("%s: done\n", __func__);
	return 0;
}

remote_mem_info_response_t *send_remote_mem_info_request(unsigned int nid)
{
	remote_mem_info_request_t request = {
		.header = {
			.type = PCN_KMSG_TYPE_REMOTE_PROC_MEMINFO_REQUEST,
			.prio = PCN_KMSG_PRIO_NORMAL,
		},
		.nid = my_nid,
	};
	remote_mem_info_response_t *response;
	struct wait_station *ws = get_wait_station(current);

	RIPRINTK("%s: Entered, nid: %d\n", __func__, nid);

	/* 1. Construct request data to send it into remote node */

	/* 1-1. Fill the header information */
	request.origin_ws = ws->id;

	/* 1-2. Send request into remote node */
	pcn_kmsg_send(nid, &request, sizeof(request));

	response = wait_at_station(ws);
	put_wait_station(ws);

	RIPRINTK("%s: done\n", __func__);
	return response;
}

int remote_proc_mem_info(remote_mem_info_response_t *total)
{
	int i;

	memset(total, 0, sizeof(*total));

	for (i = 0; i < MAX_POPCORN_NODES; i++) {
		remote_mem_info_response_t *res;
		if (i == my_nid)
			continue;

		if (!get_popcorn_node_online(i))
			continue;

		res = send_remote_mem_info_request(i);
		if (res == NULL)
			return -EINVAL;

		total->MemTotal += res->MemTotal;
		total->MemFree += res->MemFree;
		total->MemAvailable += res->MemAvailable;
		total->Buffers += res->Buffers;
		total->Cached += res->Cached;
		total->SwapCached += res->SwapCached;
		total->Active += res->Active;
		total->Inactive += res->Inactive;
		total->Active_anon += res->Active_anon;
		total->Inactive_anon += res->Inactive_anon;
		total->Active_file += res->Active_file;
		total->Inactive_file += res->Inactive_file;
		total->Unevictable += res->Unevictable;
		total->Mlocked += res->Mlocked;

#ifdef CONFIG_HIGHMEM
		total->HighTotal += res->HighTotal;
		total->HighFre += res->HighFree;
		total->LowTotal += res->LowTotal;
		total->LowFree += res->LowFree;
#endif

#ifndef CONFIG_MMU
		total->MmapCopy += res->MmapCopy;
#endif

		total->SwapTotal += res->SwapTotal;
		total->SwapFree += res->SwapFree;
		total->Dirty += res->Dirty;
		total->Writeback += res->Writeback;
		total->AnonPages += res->AnonPages;
		total->Mapped += res->Mapped;
		total->Shmem += res->Shmem;
		total->Slab += res->Slab;
		total->SReclaimable += res->SReclaimable;
		total->SUnreclaim += res->SUnreclaim;
		total->KernelStack += res->KernelStack;
		total->PageTables += res->PageTables;
#ifdef CONFIG_QUICKLIST
		total->Quicklists += res->Quicklists;
#endif

		total->NFS_Unstable += res->NFS_Unstable;
		total->Bounce += res->Bounce;
		total->WritebackTmp += res->WritebackTmp;
		total->CommitLimit += res->CommitLimit;
		total->Committed_AS += res->Committed_AS;
		total->VmallocTotal += res->VmallocTotal;
		total->VmallocUsed += 0ul;
		total->VmallocChunk += 0ul;
#ifdef CONFIG_MEMORY_FAILURE
		total->HardwareCorrupted += res->HardwareCorrupted;
#endif
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
		total->AnonHugePages += res->AnonHugePages;
#endif
#ifdef CONFIG_CMA
		total->CmaTotal += res->CmaTotal;
		total->CmaFree += res->CmaFree;
#endif

		pcn_kmsg_free_msg(res);
	}

	return 0;
}


/***********************************CPU INFO***********************************/

#define REMOTE_CPUINFO_MESSAGE_FIELDS \
	struct remote_cpu_info cpu_info_data; \
	int nid; \
	int origin_ws;
DEFINE_PCN_KMSG(remote_cpu_info_data_t, REMOTE_CPUINFO_MESSAGE_FIELDS);

static struct remote_cpu_info *saved_cpu_info[MAX_POPCORN_NODES];

void send_remote_cpu_info_request(unsigned int nid)
{
	remote_cpu_info_data_t *request;
	remote_cpu_info_data_t *response;
	struct wait_station *ws = get_wait_station(current);

	RIPRINTK("%s: Entered, nid: %d\n", __func__, nid);

	request = kzalloc(sizeof(*request), GFP_KERNEL);

	/* 1. Construct request data to send it into remote node */

	/* 1-1. Fill the header information */
	request->header.type = PCN_KMSG_TYPE_REMOTE_PROC_CPUINFO_REQUEST;
	request->header.prio = PCN_KMSG_PRIO_NORMAL;
	request->nid = my_nid;
	request->origin_ws = ws->id;

	/* 1-2. Fill the machine-dependent CPU infomation */
	fill_cpu_info(&request->cpu_info_data);

	/* 1-3. Send request into remote node */
	pcn_kmsg_send(nid, request, sizeof(*request));

	/* 2. Request message should wait until response message is done. */
	response = wait_at_station(ws);
	put_wait_station(ws);

	memcpy(saved_cpu_info[nid], &response->cpu_info_data,
	       sizeof(response->cpu_info_data));

	kfree(request);
	pcn_kmsg_free_msg(response);

	RIPRINTK("%s: done\n", __func__);
}

unsigned int get_number_cpus_from_remote_node(unsigned int nid)
{
	unsigned int num_cpus = 0;

	switch (saved_cpu_info[nid]->arch_type) {
	case POPCORN_ARCH_X86:
		num_cpus = saved_cpu_info[nid]->arch.x86.num_cpus;
		break;
	case POPCORN_ARCH_ARM:
		num_cpus = saved_cpu_info[nid]->arch.arm64.num_cpus;
		break;
	default:
		RIPRINTK("%s: Unknown CPU\n", __func__);
		num_cpus = 0;
		break;
	}

	return num_cpus;
}

static int handle_remote_cpu_info_request(struct pcn_kmsg_message *inc_msg)
{
	remote_cpu_info_data_t *request;
	remote_cpu_info_data_t *response;
	int ret;

	RIPRINTK("%s: Entered\n", __func__);

	request = (remote_cpu_info_data_t *)inc_msg;

	response = kzalloc(sizeof(*response), GFP_KERNEL);
	if (!response) return -ENOMEM;

	/* 1. Save remote cpu info from remote node */
	memcpy(saved_cpu_info[request->nid],
	       &request->cpu_info_data, sizeof(request->cpu_info_data));

	/* 2. Construct response data to send it into remote node */

	/* 2-1. Fill the header information */
	response->header.type = PCN_KMSG_TYPE_REMOTE_PROC_CPUINFO_RESPONSE;
	response->header.prio = PCN_KMSG_PRIO_NORMAL;
	response->nid = my_nid;
	response->origin_ws = request->origin_ws;

	/* 2-2. Fill the machine-dependent CPU infomation */
	ret = fill_cpu_info(&response->cpu_info_data);
	if (ret < 0) {
		RIPRINTK("%s: failed to fill cpu info\n", __func__);
		goto out;
	}

	/* 2-3. Send response into remote node */
	ret = pcn_kmsg_send(request->nid, response, sizeof(*response));
	if (ret < 0) {
		RIPRINTK("%s: failed to send response message\n", __func__);
		goto out;
	}

	/* 3. Remove request message received from remote node */
	pcn_kmsg_free_msg(request);

	RIPRINTK("%s: done\n", __func__);
out:
	kfree(response);
	return 0;
}

static int handle_remote_cpu_info_response(struct pcn_kmsg_message *inc_msg)
{
	remote_cpu_info_data_t *response;
	struct wait_station *ws;

	RIPRINTK("%s: Entered\n", __func__);

	response = (remote_cpu_info_data_t *)inc_msg;

	ws = wait_station(response->origin_ws);
	ws->private = response;

	smp_mb();

	if (atomic_dec_and_test(&ws->pendings_count))
		complete(&ws->pendings);

	RIPRINTK("%s: done\n", __func__);
	return 0;
}

static void print_x86_cpuinfo(struct seq_file *m,
		       struct remote_cpu_info *data,
		       int count)
{
	seq_printf(m, "processor\t: %u\n", data->arch.x86.cpu[count].processor);
	seq_printf(m, "vendor_id\t: %s\n", data->arch.x86.cpu[count].vendor_id);
	seq_printf(m, "cpu_family\t: %d\n", data->arch.x86.cpu[count].cpu_family);
	seq_printf(m, "model\t\t: %u\n", data->arch.x86.cpu[count].model);
	seq_printf(m, "model name\t: %s\n", data->arch.x86.cpu[count].model_name);

	if (data->arch.x86.cpu[count].stepping != -1)
		seq_printf(m, "stepping\t: %d\n", data->arch.x86.cpu[count].stepping);
	else
		seq_puts(m, "stepping\t: unknown\n");

	seq_printf(m, "microcode\t: 0x%lx\n", data->arch.x86.cpu[count].microcode);
	seq_printf(m, "cpu MHz\t\t: %u\n", data->arch.x86.cpu[count].cpu_freq);
	seq_printf(m, "cache size\t: %d kB\n", data->arch.x86.cpu[count].cache_size);
	seq_puts(m, "flags\t\t:");
	seq_printf(m, " %s", data->arch.x86.cpu[count].flags);
	seq_printf(m, "\nbogomips\t: %lu\n", data->arch.x86.cpu[count].nbogomips);
	seq_printf(m, "TLB size\t: %d 4K pages\n", data->arch.x86.cpu[count].TLB_size);
	seq_printf(m, "clflush size\t: %u\n", data->arch.x86.cpu[count].clflush_size);
	seq_printf(m, "cache_alignment\t: %d\n", data->arch.x86.cpu[count].cache_alignment);
	seq_printf(m, "address sizes\t: %u bits physical, %u bits virtual\n",
		   data->arch.x86.cpu[count].bits_physical,
		   data->arch.x86.cpu[count].bits_virtual);
}

static void print_arm_cpuinfo(struct seq_file *m,
		       struct remote_cpu_info *data,
		       int count)
{
	seq_printf(m, "processor\t: %u\n", data->arch.arm64.percore[count].processor_id);

	if (data->arch.arm64.percore[count].compat)
		 seq_printf(m, "model name\t: %s %d (%s)\n",
			    data->arch.arm64.percore[count].model_name,
			    data->arch.arm64.percore[count].model_rev,
			    data->arch.arm64.percore[count].model_elf);
	else
		 seq_printf(m, "model name\t: %s\n",
			    data->arch.arm64.percore[count].model_name);

	seq_printf(m, "BogoMIPS\t: %lu.%02lu\n",
		   data->arch.arm64.percore[count].bogo_mips,
		   data->arch.arm64.percore[count].bogo_mips_fraction);
	seq_puts(m, "Features\t:");
	seq_printf(m, " %s", data->arch.arm64.percore[count].flags);
	seq_puts(m, "\n");

	seq_printf(m, "CPU implementer\t: 0x%02x\n", data->arch.arm64.percore[count].cpu_implementer);
	seq_printf(m, "CPU architecture: %d\n", data->arch.arm64.percore[count].cpu_archtecture);
	seq_printf(m, "CPU variant\t: 0x%x\n", data->arch.arm64.percore[count].cpu_variant);
	seq_printf(m, "CPU part\t: 0x%03x\n", data->arch.arm64.percore[count].cpu_part);
	seq_printf(m, "CPU revision\t: %d\n", data->arch.arm64.percore[count].cpu_revision);

	return;
}

static void print_unknown_cpuinfo(struct seq_file *m)
{
	seq_puts(m, "processor\t: Unknown\n");
	seq_puts(m, "vendor_id\t: Unknown\n");
	seq_puts(m, "cpu_family\t: Unknown\n");
	seq_puts(m, "model\t\t: Unknown\n");
	seq_puts(m, "model name\t: Unknown\n");
}

int remote_proc_cpu_info(struct seq_file *m, unsigned int nid, unsigned int vpos)
{
	seq_printf(m, "****    Remote CPU at %d   ****\n", nid);

	switch (saved_cpu_info[nid]->arch_type) {
	case POPCORN_ARCH_X86:
		print_x86_cpuinfo(m, saved_cpu_info[nid], vpos);
		break;
	case POPCORN_ARCH_ARM:
		print_arm_cpuinfo(m, saved_cpu_info[nid], vpos);
		break;
	default:
		print_unknown_cpuinfo(m);
		break;
	}

	seq_puts(m, "\n");
	return 0;
}

int remote_info_init(void)
{
	int i = 0;

	/* Allocate the buffer for saving remote CPU info */
	for (i = 0; i < MAX_POPCORN_NODES; i++)
		saved_cpu_info[i] = kzalloc(sizeof(struct remote_cpu_info),
					    GFP_KERNEL);

	pcn_kmsg_register_callback(PCN_KMSG_TYPE_REMOTE_PROC_CPUINFO_REQUEST,
				   handle_remote_cpu_info_request);
	pcn_kmsg_register_callback(PCN_KMSG_TYPE_REMOTE_PROC_CPUINFO_RESPONSE,
				   handle_remote_cpu_info_response);

	pcn_kmsg_register_callback(PCN_KMSG_TYPE_REMOTE_PROC_MEMINFO_REQUEST,
				   handle_remote_mem_info_request);
	pcn_kmsg_register_callback(PCN_KMSG_TYPE_REMOTE_PROC_MEMINFO_RESPONSE,
				   handle_remote_mem_info_response);
	return 0;
}
