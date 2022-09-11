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

//------------------------------------------------------------------------

#define ERROR -1

void qpci_host_get_bdfs(QPCIHostFunction **funcs, uint32_t devid)
{
    char cmd[25];
    sprintf(cmd, "sudo lspci | grep %x", devid);
    int status = system(cmd);
    if(status == ERROR)
    {
        //TBD
    }

    int x = lspci();
}
*/

//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@



#define TEST_IMAGE_SIZE (64 * 1024 * 1024)
#define QVIRTIO_BLK_TIMEOUT_US (30 * 1000 * 1000)
#define PCI_SLOT_CUSTOM 0x04
#define PCI_FN 0x00


typedef struct QVirtioBlkReq
{
    uint32_t type;
    uint32_t ioprio;
    uint64_t sector;
    char *data;
    uint8_t status;
} QVirtioBlkReq;

static uint64_t virtio_blk_request(QGuestAllocator *alloc, QVirtioDevice *d,
                                   QVirtioBlkReq *req, uint64_t data_size, QTestState *qts)
{
    uint64_t addr;
    uint8_t status = 0xFF;
    g_assert_cmpuint(data_size % 4096, ==, 0);
    addr = guest_alloc(alloc, sizeof(*req) + data_size);
    if (!addr)
    {
        printf("Guest addr is NULL (in virtio_blk_request)\n");
        return 0;
    }

    qtest_memwrite(qts, addr, req, 16);
    qtest_memwrite(qts, addr + 16, req->data, data_size);
    qtest_memwrite(qts, addr + 16 + data_size, &status, sizeof(status));

    return addr;
}

static void virtio_blk_send_command(QGuestAllocator *alloc, QTestState *qts, QVirtioDevice *dev, QVirtQueue *vq)
{
    uint32_t free_head;
    uint64_t req_addr = 0;

    QVirtioBlkReq req;

    int size = 4096;
    char *data;

    printf("---== [Write request]==---\n");
    req.type = VIRTIO_BLK_T_OUT;
    req.ioprio = 1;
    req.sector = 0;
    req.data = g_malloc(size);

    if (!req.data)
    {
        printf("req.data is NULL (in virtio_blk_send_request)\n");
        return;
    }

    strcpy(req.data, "TEST");
    printf("\tVirtio request data = %s\n", req.data);

    req_addr = virtio_blk_request(alloc, dev, &req, size, qts);
    printf("\tRequest address = 0x%lx\n", req_addr);
    g_free(req.data);

    free_head = qvirtqueue_add(qts, vq, req_addr, 16, false, true);
    qvirtqueue_add(qts, vq, req_addr + 16, size, false, true);
    qvirtqueue_add(qts, vq, req_addr + size + 16, 1, true, false);

    printf("\tFree head = %d, ringing doorbell...\n", free_head);
    qvirtqueue_kick(qts, dev, vq, free_head);
    qvirtio_wait_used_elem(qts, dev, vq, free_head, NULL,
                           QVIRTIO_BLK_TIMEOUT_US);

    // uint8_t status = 0;
    // qtest_memwrite(qts, req_addr + size + 16, &status, 1);
    // qtest_memread(qts, req_addr + size + 16, &status, 1);
    // g_assert_cmpint(status, ==, 0);

    guest_free(alloc, req_addr);

    getchar();

    printf("\n---== [Read request]==---\n");
    req.type = VIRTIO_BLK_T_IN;
    req.ioprio = 1;
    req.sector = 0;
    req.data = g_malloc0(size);

    req_addr = virtio_blk_request(alloc, dev, &req, size, qts);
    printf("\tRequest address = 0x%lx\n", req_addr);

    g_free(req.data);

    free_head = qvirtqueue_add(qts, vq, req_addr, 16, false, true);
    qvirtqueue_add(qts, vq, req_addr + 16, size, true, true);
    qvirtqueue_add(qts, vq, req_addr + size+16, 1, true, false);
    printf("\tFree head = %d, ringing doorbell...\n", free_head);
    qvirtqueue_kick(qts, dev, vq, free_head);

    qvirtio_wait_used_elem(qts, dev, vq, free_head, NULL,
                           QVIRTIO_BLK_TIMEOUT_US);


    // qtest_memwrite(qts, req_addr + size + 16, &status, 1);
    // qtest_memread(qts, req_addr + size + 16, &status, 1);
    // g_assert_cmpint(status, ==, 0);

    data = g_malloc0(size);
    printf("\tReading data...\n");
    qtest_memread(qts, req_addr + 16, data, size);
    printf("\tData=%s\n", data);
    g_assert_cmpstr(data, ==, "TEST");

    guest_free(alloc, req_addr);
    g_free(data);
}

static QVirtioPCIDevice *virtio_blk_pci_init(QPCIBus *bus, int slot, QOSState *qs)
{
    QVirtioPCIDevice *dev;
    QPCIAddress PciAddr;

    PciAddr.devfn = slot;
    PciAddr.vendor_id = 0x1af4;
    PciAddr.device_id = 0x1042;

    uint64_t features;

    /* Device initialization flow:
        1. Reset the device
        2. Set acknowledge
        3. Set driver status bit
        4. Get features
        5. Set features
    */

    dev = virtio_pci_new(bus, &PciAddr);

    if (!dev)
    {
        printf("DEV is null (in virtio_blk_pci_init)\n");
        return NULL;
    }

    g_assert_cmphex(dev->vdev.device_type, ==, VIRTIO_ID_BLOCK);

    qvirtio_pci_device_enable(dev);
    printf("$ Starting device initialization:\n");
    printf("\t1. Resetting virtio device...\n");
    qvirtio_reset(&dev->vdev);

    printf("\t2. Setting device acknowlege...\n");
    qvirtio_set_acknowledge(&dev->vdev);

    printf("\t3. Seeting driver status...\n");
    qvirtio_set_driver(&dev->vdev);

    printf("\t4. Configuring MSI-X...\n");
    qpci_msix_enable(dev->pdev);
    qvirtio_pci_set_msix_configuration_vector(dev, &qs->alloc, 0);
    getchar();

    printf("\t5. Getting virtio features...\n");
    features = qvirtio_get_features(&dev->vdev);

    printf("\t6. Setting virtio features...\n");
    qvirtio_set_features(&dev->vdev, features);

    return dev;
}
static void virtio_blk_pci_destroy(QVirtioPCIDevice *dev)
{
    qvirtio_pci_device_disable(dev);
    g_free(dev);
}

static void virtio_blk_test(void)
{
    printf("\n---------------- [virtio_poc_test] ----------------\n");




//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
/*
    typedef struct QPCIHostFunction {
        char host_bdf[100];
        int guest_devfn;
    } QPCIHostFunction;

    // configurable params: devid, num pfs, num queues

    //----------------pre requirements--------------------
    // get requested bdfs by device id
    int devid = 0x145a;
    QPCIHostFunction* funcs[8];
    qpci_host_get_bdfs(funcs, devid);
    

    // create qtest instance with requested pfs
    qtest_guest_boot(funcs, k);
    {
        // look for free pci slots - TBD
        int addr = 0x4;

        // create command line
        const char cmd[500];
        for(int i = 0; i < k; i++) {
            cmd = g_strdup_printf("%s
                                -device vfio-pci,"
                                "host=%s,"
                                "addr=0x%d.0x0 ",
                                cmd, funcs[i]->host_bdf, addr);
            // assign devfn per function
            funcs[i]->guest_devfn = PCI_DEVFN(addr, 0);
        }


        // initialise global qs
        global_qs = qtest_pc_boot(cmd);
    }

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

    QOSState *qs;
    QVirtioPCIDevice *dev;
    QVirtQueue *vq;


    const char *cmd = "-device vfio-pci,"
                        "host=86:01.0,"
                        "addr=0x4.0x0 "
                        "-device vfio-pci,"
                        "host=86:01.1,"
                        "addr=0x5.0x0 ";

    qs = qtest_pc_boot(cmd);

    // sleep(2);
    // getchar();

    // QPCIDevice *pdev;
    // for(int i = 0; i < 0xff; i++){
    //     pdev = qpci_device_find(qs->pcibus, i);
    //     if(pdev != NULL)
    //     {
    //         printf("dev 0x%x is not NULL\n", i);
    //         printf("cfg ofst 0: 0x%x\n", qpci_config_readl(pdev, 0x0));
    //         getchar();
    //     }
    //     else 
    //     {
    //         printf("dev 0x%x is NULL\n", i);
    //     }
    // }
   // goto end;
    dev = virtio_blk_pci_init(qs->pcibus, PCI_DEVFN(4, 0), qs);
    if (!dev)
    {
        printf("DEV is null (in virtio_blk_test)\n");
        goto end;
    }

    printf("\t7. Setting up virtQueue...\n");
    vq = qvirtqueue_setup(&dev->vdev, &(qs->alloc), 0);
    if (!vq)
    {
        printf("VirtQ is null (in virtio_blk_test)\n");
        goto end;
    }
    qvirtqueue_pci_msix_setup(dev, (QVirtQueuePCI *)vq, &qs->alloc, 1);

    printf("\t8. Setting driver status OK...\n");
    qvirtio_set_driver_ok(&dev->vdev);
    getchar();
    // printf("configure isr\n");
    // getchar();
    // qvirtio_wait_config_isr(&dev->vdev, QVIRTIO_BLK_TIMEOUT_US);
    // getchar();

    printf("\n$ Sending virtio command...\n");
    virtio_blk_send_command(&qs->alloc, qs->qts, &dev->vdev, vq);

    printf("Press any key to end test: ");
    getchar();

    /* End test */
    qpci_msix_disable(dev->pdev);
    qvirtqueue_cleanup(dev->vdev.bus, vq, &qs->alloc);
    virtio_blk_pci_destroy(dev);

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
