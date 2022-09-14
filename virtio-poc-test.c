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
    QOSState *qs;
    QVirtioPCIDevice *dev;
    QVirtQueue *vq;


    const char *cmd = "-device vfio-pci,"
                        "host=86:01.0,"
                        "addr=0x4.0x0 ";

    qs = qtest_pc_boot(cmd);

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
    qtest_pc_shutdown(qs);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("virtio-poc-test/virtio_blk_test", virtio_blk_test);

    return g_test_run();
}
