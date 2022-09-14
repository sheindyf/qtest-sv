/*
 * QTest testcase for virtio
 *
 * Copyright (c) 2018 Red Hat, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "libqos-sv/pci.h"
#include "libqos-sv/pci-pc.h"
#include "libqos-sv/libqos-pc.h"
#include "libqos-sv/libqos.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qlist.h"
#include "qapi/qmp/qjson.h"
#include "libqos-sv/malloc-pc.h"
#include "libqos-sv/virtio-pci.h"
#include "hw/pci/pci.h"

#include "libqos-sv/libqos-pc.h"
#include "libqos-sv/libqos-spapr.h"
#include "libqos-sv/virtio.h"
#include "libqtest-single.h"
#include "libqos-sv/virtio-pci.h"
#include "libqos-sv/virtio-mmio.h"
#include "qemu/bswap.h"
#include "standard-headers/linux/virtio_ids.h"
#include "standard-headers/linux/virtio_config.h"
#include "standard-headers/linux/virtio_ring.h"
#include "standard-headers/linux/virtio_blk.h"
#include "standard-headers/linux/virtio_pci.h"



//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
/*


#define VIRTIO_VENDOR_ID 0x1AF4
#define VIRTIO_BLK_DEVID 0x1042
#define VIRTIO_NET_DEVID 0x1041

QVirtioPCIDevice *Xvirtio_blk_pci_init(QOSState *qs, uint32_t devfn);
QVirtioPCIDevice *Xvirtio_blk_device_init(uint32_t devfn, QVirtQueue** virt_queues, uint16_t num_queues);
QVirtioPCIDevice *Xvirtio_blk_device_init_g(QOSState *qs, uint32_t devfn, QVirtQueue** virt_queues, uint16_t num_queues);


QVirtioPCIDevice *Xvirtio_blk_pci_init(QOSState *qs, uint32_t devfn)
{
    QVirtioPCIDevice *dev;
    QPCIAddress pci_addr;

    pci_addr.devfn = devfn;
    pci_addr.vendor_id = VIRTIO_VENDOR_ID;
    pci_addr.device_id = VIRTIO_BLK_DEVID;

    uint64_t features;


    dev = virtio_pci_new(qs->pcibus, &pci_addr);
    if (!dev) {
        printf("virtio PCI device is NULL\n");
        return NULL;
    }

    g_assert_cmphex(dev->vdev.device_type, ==, VIRTIO_ID_BLOCK);

    qvirtio_pci_device_enable(dev);
    qvirtio_reset(&dev->vdev);
    qvirtio_set_acknowledge(&dev->vdev);
    qvirtio_set_driver(&dev->vdev);
    qpci_msix_enable(dev->pdev);
    qvirtio_pci_set_msix_configuration_vector(dev, &qs->alloc, 0); //TODO: dynamic entry

    features = qvirtio_get_features(&dev->vdev);
    qvirtio_set_features(&dev->vdev, features);

    return dev;
}

QVirtioPCIDevice *Xvirtio_blk_device_init_g(QOSState *qs, uint32_t devfn, QVirtQueue** virt_queues, uint16_t num_queues)
{
    QVirtioPCIDevice *dev = Xvirtio_blk_pci_init(qs, devfn);

    for(int q = 0; q < num_queues; q++)
    {
        virt_queues[q] = qvirtqueue_setup(&dev->vdev, &(qs->alloc), q);
        qvirtqueue_pci_msix_setup(dev, (QVirtQueuePCI *)virt_queues[q], &qs->alloc, 1);
    }
    
    // finalize virtio device initialization
    qvirtio_driver_ok(dev);
    return dev;
}

QVirtioPCIDevice *Xvirtio_blk_device_init(uint32_t devfn, QVirtQueue** virt_queues, uint16_t num_queues)
{
    return Xvirtio_blk_device_init_g(global_qs, devfn, virt_queues);
}
*/


//------------------------------------------------------------------------

typedef struct QPCIHostFunction {
    char host_bdf[30];
    int guest_devfn;
} QPCIHostFunction;

#define ERROR -1

static void qpci_host_get_bdfs(QPCIHostFunction **funcs, uint32_t devid)
{
    FILE *fp;
    char cmd[40];
    char path[30];
    uint16_t func = 0;
    sprintf(cmd, "lspci | grep %x | awk \'{print $1}\'", devid);

    fp = popen(cmd, "r");
    if (fp == NULL) {
        printf("Failed to get host functions BDF\n" );
        exit(1);
    }

    while (fgets(path, sizeof(path), fp) != NULL) {
        path[strlen(path) - 1] = '\0';
        strcpy(funcs[func]->host_bdf, path);
        func++;
    }
    /* close */
    pclose(fp);
}

// create qtest instance with requested pfs

static void qtest_guest_boot(QPCIHostFunction **funcs, uint32_t k)
{
    // TODO: look for free pci slots - TBD
    int addr = 0x4; //alloc_pci_slot();

    char cmd[100] = "";
    char cmdline[500] = "";
    // char *command;
    for(int i = 0; i < k; i++) {
        sprintf(cmd, "-device vfio-pci,host=%s,addr=0x%d.0x0 ",
                funcs[i]->host_bdf, addr);
        strcat(cmdline, cmd);
        // assign devfn per function
        funcs[i]->guest_devfn = PCI_DEVFN(addr, 0);
        printf("func: %d, host bdf %s, guess devfn %x\n", i, funcs[i]->host_bdf, funcs[i]->guest_devfn);
        addr++;
    }

    printf("cmdline: %s\n", cmdline);
    // initialise global qs
    global_qs = qtest_pc_boot(cmd);
}

//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@


static void virtio_blk_test(void)
{

//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
/*

    //---------------virtio configuration------------------
    QVirtQueue** virtqs;
    int virtdev_slot = funcs[x]->guest_devfn;
    dev = virtio_blk_device_init(virtdev_slot, virtqs, num_queues);
    {
        
        // init virtio pci device
        dev = virtio_blk_pci_init(virtdev_slot);

        // setup virtqueues
        // alloc virtqs - TBD
        for(int i = 0; i < num_queues; i++)
            virtqs[i] = qvirtq_setup(dev, i, msix, num_vectors)

        
        // finalize virtio initialization
        qvirtio_driver_ok(dev)
    }

*/

//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@


    QPCIHostFunction *funcs[8];
    for(int i = 0; i < 8; i++){
        funcs[i] = malloc(sizeof(QPCIHostFunction));
        memset(funcs[i]->host_bdf, '\0', 30);
    }
    qpci_host_get_bdfs(funcs, 0x145a);
    qtest_guest_boot(funcs, 8);
    goto end;


end:
    printf("end\n");
  //  qtest_pc_shutdown(qs);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("virtio-poc-test/virtio_blk_test", virtio_blk_test);

    return g_test_run();
}
