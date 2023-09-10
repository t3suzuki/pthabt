#include "spdk/nvme.h"
//#include "spdk/env.h"
//#include "spdk/string.h"
//#include "spdk/log.h"
#include "common.h"


int is_comp[ULT_N_TH];
char *rbuf_tmp[ULT_N_TH];

struct spdk_nvme_ns	*g_ns;
struct spdk_nvme_qpair	*g_qpair;
static struct spdk_nvme_transport_id g_trid = {};


#define LBA_POS(x)   (x / 512)
#define LBA_COUNT(x) ((x + 511) / 512)

static void
myspdk_read_comp_cb(void *arg, const struct spdk_nvme_cpl *completion)
{
  int id = (int)arg;
  //printf("%s %d id %d \n", __func__, __LINE__, id);

  is_comp[id] = 1;
  if (spdk_nvme_cpl_is_error(completion)) {
    exit(1);
  }
  //printf("%s %d id %d \n", __func__, __LINE__, id);
}


int myspdk_read_req(int id, int fd, long count, long pos) {
  is_comp[id] = 0;
  //printf("%s %d pos %d sz %d\n", __func__, __LINE__, LBA_POS(pos), LBA_COUNT(count));
  int rc = spdk_nvme_ns_cmd_read(g_ns, g_qpair, rbuf_tmp[id],
				 LBA_POS(pos),
				 LBA_COUNT(count),
				 myspdk_read_comp_cb, (void *)id, 0);
  return rc;
}

int myspdk_read_comp(int id, char *buf, long count) {
  spdk_nvme_qpair_process_completions(g_qpair, 0);
  if (is_comp[id]) {
    memcpy(buf, rbuf_tmp[id], count);
    return 1;
  } else {
    return 0;
  }
}


static void
attach_cb(void *cb_ctx, const struct spdk_nvme_transport_id *trid,
	  struct spdk_nvme_ctrlr *ctrlr, const struct spdk_nvme_ctrlr_opts *opts)
{
  int nsid;
  struct spdk_nvme_ns *ns;

  printf("Attached to %s\n", trid->traddr);
  for (nsid = spdk_nvme_ctrlr_get_first_active_ns(ctrlr); nsid != 0;
       nsid = spdk_nvme_ctrlr_get_next_active_ns(ctrlr, nsid)) {
    ns = spdk_nvme_ctrlr_get_ns(ctrlr, nsid);
    if (ns == NULL) {
      continue;
    }
    g_ns = ns;
    g_qpair = spdk_nvme_ctrlr_alloc_io_qpair(ctrlr, NULL, 0);
  }
}

int
main(int argc, char **argv)
{
  int rc;
  struct spdk_env_opts opts;

  spdk_env_opts_init(&opts);
  spdk_nvme_trid_populate_transport(&g_trid, SPDK_NVME_TRANSPORT_PCIE);
  snprintf(g_trid.subnqn, sizeof(g_trid.subnqn), "%s", SPDK_NVMF_DISCOVERY_NQN);
  if (spdk_env_init(&opts) < 0) {
    fprintf(stderr, "Unable to initialize SPDK env\n");
    return 1;
  }

  for (int i=0; i<ULT_N_TH; i++) {
    rbuf_tmp[i] = spdk_zmalloc(0x1000, 0x1000, NULL, SPDK_ENV_SOCKET_ID_ANY, SPDK_MALLOC_DMA);
  }
  
  rc = spdk_nvme_probe(&g_trid, NULL, NULL, attach_cb, NULL);
  if (rc != 0) {
    fprintf(stderr, "spdk_nvme_probe() failed\n");
    rc = 1;
  }
  
  printf("Initialization complete.\n");

  char buf[128];
  myspdk_read_req(0, 0, 128, 0);
  while (1) {
    sched_yield();
    if (myspdk_read_comp(0, buf, 128))
      break;
  }
}
