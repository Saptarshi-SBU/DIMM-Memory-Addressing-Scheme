/* Program : kload.c
 * Description : This program generates memory traffic for 
 * address mapping scheme verification
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/mmzone.h>
#include <linux/mm.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/kthread.h>
#include <linux/pagemap.h>
#include <linux/delay.h>
#include <linux/time.h>
#include "kload.h"

#define MAX_BYTES (1UL << 28 )
#define MAX_ALLOCATION 40

//#define MAX_KTHREAD 7
#define MAX_KTHREAD 1

#define MAX_SAD_RANGES 8
#define MAX_TAD_RANGES 12
#define MAX_CHANNELS   4
#define MAX_RIR_RANGES 6
#define MAX_RANKS      8
#define MAX_SOCKETS    8

#define GET_BITFIELD(v, lo, hi) \
       (((v) & ((1ULL << ((hi) - (lo) + 1)) - 1) << (lo)) >> (lo))
#define TAD_OFFSET(reg)             (GET_BITFIELD(reg, 6,25) << 26)

extern inline int isSocket(long long unsigned int phy_addr);
extern inline int isChannel(long long unsigned int , int , long long unsigned int *);
extern inline int isRank(long long unsigned int, int , int,long long unsigned int *);

extern struct list_head vaddr_list1;
extern struct list_head vaddr_list2;

extern uint64_t current_alloc_bytes_list1;
extern uint64_t current_alloc_bytes_list2;

/* contain tad offsets per tad range per channel per socket */
extern uint32_t    tad_offset[MAX_SOCKETS][MAX_CHANNELS][MAX_TAD_RANGES];

DEFINE_SPINLOCK(pages_lock);
DECLARE_WAIT_QUEUE_HEAD(myevent);

struct kva {
   long long unsigned int *va;
   struct list_head       node;
};

typedef struct kva kva;
struct   task_struct *kthread_task[MAX_KTHREAD];

static int kgenerate_traffic(int start_mb, int end_mb, int id) { 

  int i=0;
  int k=0;
  int ch_no = -1;
  int rank_no = -1;
  int lrank_no = -1;
  int sck = 1; 	

  // tad_offset is hard_coded
  const long long unsigned int tad_off = TAD_OFFSET(tad_offset[0][0][0]);
  
  long long unsigned int *va_addr   =0; 	
  long long unsigned int phy_addr   =0; 	
  long long unsigned int chn_addr   =0; 	
  long long unsigned int rank_addr  =0; 	
  long long unsigned int p          =0; 	
  long long unsigned int seq        =0;
  long long unsigned int mem_total  =0;	

  long long unsigned int min	    =~(0x0ULL); 	
  long long unsigned int max	    =0; 	
  u64  bits               	    =0;
  kva                      *pdesc,*tmp;

  if ((current_alloc_bytes_list1 >> 20) > MAX_ALLOCATION )	
  {
       list_for_each_entry_safe(pdesc, tmp, &vaddr_list1, node){

         if(!kthread_should_stop()) {

	  if(min > phy_addr) 
             min = phy_addr;

          if(max < phy_addr)
             max = phy_addr;  

	   for( i=0 ; i < 30; i++) 
	    {	  
		  va_addr  =  (pdesc->va+i*8); 
		  phy_addr =  virt_to_phys(va_addr);
/*
		  sck = isSocket(phy_addr);
                  ch_no    =  isChannel(phy_addr,sck,&chn_addr);
		  if(ch_no!=1)
		     continue;	
*/
		  chn_addr  =     phy_addr-tad_off;

		  bits      =     chn_addr & 0x3f;
                  chn_addr  =     chn_addr >> 6;
                  chn_addr  =     chn_addr >> 1;
		  rank_addr =     chn_addr << 6;
		  rank_addr =     rank_addr | bits;		  

		  rank_addr =     rank_addr >> 13;
                  lrank_no  =     rank_addr % 4;
                  switch(lrank_no) {
			 case 0 :
				 rank_no = 0;
				 break;
			 case 1 :
				 rank_no = 4;
				 break;
			 case 2 :
				 rank_no = 1;
				 break;
			 case 3 :
				 rank_no = 5;
				 break;
			 default :
				 break;
		  }

		 // rank_no  = isRank(chn_addr,sck,ch_no,&rank_addr);
		  if((rank_no == 0 ) || (rank_no == 4))	
		  //if((rank_no == 1 ) || (rank_no == 5))	
		  //if((rank_no == 1 ))	
	 	   *(long long unsigned int *)va_addr = jiffies;
                 //yield();
		  
	 }
	}
       }
    }
       printk ( KERN_INFO "Writing Pages %llu %llu \n", min, max);

  return 0;
}

static int kwork(void *arg) {
    printk(KERN_INFO "Starting kthread :\n");
    while(!kthread_should_stop()) {
           yield();
	   if((current_alloc_bytes_list1 >> 20) > (MAX_ALLOCATION/MAX_KTHREAD) )
		kgenerate_traffic(0,0,0);
    }
    printk(KERN_CRIT "Kernel thread traffic generator Stopped\n");
    return 0;
} 

int start_module() {

    int i = 0;
    for(i=0;i< MAX_KTHREAD;i++)
        kthread_task[i] = kthread_create(kwork,NULL,"compas_worker_thread2");

    if (IS_ERR(kthread_task))
        printk( KERN_CRIT "Error Starting Kernel Thread\n");
    else  {
        for(i=0;i< MAX_KTHREAD;i++) {
	    msleep(5);
            wake_up_process(kthread_task[i]);
	}
    }
    return 0;
}
    
void stop_module() {
    int i = 0;
    for(i=0;i< MAX_KTHREAD;i++)
        kthread_stop(kthread_task[i]);
    printk(KERN_INFO "Removing Module\n");
 } 

module_init(start_module);
module_exit(stop_module);
MODULE_LICENSE("GPL");
