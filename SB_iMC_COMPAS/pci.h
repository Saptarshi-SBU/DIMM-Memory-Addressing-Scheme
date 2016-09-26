// This file contains the data structures for holding the address mapping 
// tables

#include "sb_imc.h"

//
//  pci device descriptor
//
struct pci_id_descr {
        int                     dev_id;
        int                     dev;
        int                     func;
        int                     optional;
};

#define PCI_DESCR(device_id, device, function)  \
        .dev_id   = (device_id),                \
        .dev      = (device),                   \
        .func     = (function)


// 
// Group registers as PCI Devices related to iMC
//

struct pci_id_descr sbridge_pci_id_descr_tbl[] = {

				/*GROUP REGISTER 1 : NODE ID DECODE RULE*/
	{ PCI_DESCR(PCI_DEVICE_INTEL_SANDYBRIDGE_SAD0, PCI_SUBDEV_INTEL_SANDYBRIDGE_SAD0, PCI_DEVFN_INTEL_SANDYBRIDGE_SAD0)},

				/*GROUP REGISTER 2 : TYPE OF MEMORY(LOW,HIGH) */
	{ PCI_DESCR(PCI_DEVICE_INTEL_SANDYBRIDGE_SAD1, PCI_SUBDEV_INTEL_SANDYBRIDGE_SAD1, PCI_DEVFN_INTEL_SANDYBRIDGE_SAD1)},

				/*GROUP REGISTER 3 : DRAM RULE & DRAM CONTROLLER MEM LIMITS */
	{ PCI_DESCR(PCI_DEVICE_INTEL_SANDYBRIDGE_BR, PCI_SUBDEV_INTEL_SANDYBRIDGE_BR, PCI_DEVFN_INTEL_SANDYBRIDGE_BR)}, 

				/*GROUP REGISTER 4 : SOCKET ID & NODE ID : Home Agent :13*/
	{ PCI_DESCR(PCI_DEVICE_INTEL_SANDYBRIDGE_IMC_HA0, PCI_SUBDEV_INTEL_SANDYBRIDGE_IMC_HA0, PCI_DEVFN_INTEL_SANDYBRIDGE_IMC_HA0)}, 

				/*GROUP REGISTER 5 : TAD RULE :14*/
	{ PCI_DESCR(PCI_DEVICE_INTEL_SANDYBRIDGE_IMC_TA, PCI_SUBDEV_INTEL_SANDYBRIDGE_IMC_TA, PCI_DEVFN_INTEL_SANDYBRIDGE_IMC_TA)},
	
				/*GROUP REGISTER 6 : CH & RANK DECODE :15*/
	{ PCI_DESCR(PCI_DEVICE_INTEL_SANDYBRIDGE_IMC_CH0_TAD, PCI_SUBDEV_INTEL_SANDYBRIDGE_IMC_CH0_TAD,PCI_DEVFN_INTEL_SANDYBRIDGE_IMC_CH0_TAD)}, 

	{ PCI_DESCR(PCI_DEVICE_INTEL_SANDYBRIDGE_IMC_CH1_TAD,PCI_SUBDEV_INTEL_SANDYBRIDGE_IMC_CH1_TAD, PCI_DEVFN_INTEL_SANDYBRIDGE_IMC_CH1_TAD)},

	{ PCI_DESCR(PCI_DEVICE_INTEL_SANDYBRIDGE_IMC_CH2_TAD,PCI_SUBDEV_INTEL_SANDYBRIDGE_IMC_CH2_TAD,PCI_DEVFN_INTEL_SANDYBRIDGE_IMC_CH2_TAD)},

	{ PCI_DESCR(PCI_DEVICE_INTEL_SANDYBRIDGE_IMC_CH3_TAD, PCI_SUBDEV_INTEL_SANDYBRIDGE_IMC_CH3_TAD, PCI_DEVFN_INTEL_SANDYBRIDGE_IMC_CH3_TAD)}, 
				/*GROUP REGISTER 7 :*/
	{ PCI_DESCR(PCI_DEVICE_INTEL_SANDYBRIDGE_IMC_DDRIO,PCI_SUBDEV_INTEL_SANDYBRIDGE_IMC_DDRIO,PCI_DEVFN_INTEL_SANDYBRIDGE_IMC_DDRIO)},
	{ PCI_DESCR(PCI_DEVICE_INTEL_SANDYBRIDGE_IMC_CH0_TC,PCI_SUBDEV_INTEL_SANDYBRIDGE_IMC_TC,PCI_DEVFN_INTEL_SANDYBRIDGE_IMC_CH0_TC)},
	{ PCI_DESCR(PCI_DEVICE_INTEL_SANDYBRIDGE_IMC_CH1_TC,PCI_SUBDEV_INTEL_SANDYBRIDGE_IMC_TC,PCI_DEVFN_INTEL_SANDYBRIDGE_IMC_CH1_TC)},
	{ PCI_DESCR(PCI_DEVICE_INTEL_SANDYBRIDGE_IMC_CH2_TC,PCI_SUBDEV_INTEL_SANDYBRIDGE_IMC_TC,PCI_DEVFN_INTEL_SANDYBRIDGE_IMC_CH2_TC)},
	{ PCI_DESCR(PCI_DEVICE_INTEL_SANDYBRIDGE_IMC_CH3_TC,PCI_SUBDEV_INTEL_SANDYBRIDGE_IMC_TC,PCI_DEVFN_INTEL_SANDYBRIDGE_IMC_CH3_TC)}
};

//
//pci devices descriptor table
//

struct pci_id_table {
	struct pci_id_descr     *descr;
	int			 ndevs;
};

#define PCI_ID_TABLE_ENTRY(A) { .descr=A, .ndevs = ARRAY_SIZE(A) }

static const struct pci_id_table sbridge_pci_id_table[] = {
        PCI_ID_TABLE_ENTRY(sbridge_pci_id_descr_tbl),
	{0,}
};

//
// SBridge Device
//

struct sbridge_dev {
        struct list_head        list;
        u8                      bus;
        struct pci_dev          **pdev;
        int                     n_devs;
};

struct sbridge_channel {
       uint32_t ranks;
       uint32_t dimm;
} ;

//
//  SBridge Memory Controller Info
//

#define NUM_CHANNELS 4
struct sbridge_mc_info {
        struct pci_dev          *pci_ta, *pci_ddrio, *pci_ras;
        struct pci_dev          *pci_sad0, *pci_sad1, *pci_ha0;
        struct pci_dev          *pci_br;
        struct pci_dev          *pci_tad[NUM_CHANNELS];
        struct sbridge_dev      *sb_dev;
        struct sbridge_channel  channel[NUM_CHANNELS];
        bool                    is_mirrored, is_lockstep, is_close_pg;
        uint64_t                tolm, tohm;
};
