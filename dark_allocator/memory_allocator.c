
// Program : memory_allocator.c
// Description : This program is a kernel module which is a memory 
// allcoator which runs on top of the kernel's zoned buddy allocator 

// Copyright (C) 2013 S.Sen
//
// This program is free software: you can redistribute it and/or modify
//it under the terms of the GNU General Public License as published by
//the Free Software Foundation, either version 3 of the License, or
//(at your option) any later version.
//This program is distributed in the hope that it will be useful,
//but WITHOUT ANY WARRANTY; without even the implied warranty of
//MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//GNU General Public License for more details.

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
#include "alloc.h"

#define MAX_BYTES (1UL << 28 )

/* Dark Memory Allocator Size : 40 MB */ 
#define MAX_ALLOCATION 40
extern inline int isSocket(long long unsigned int phy_addr);

DEFINE_SPINLOCK(pages_lock);
DECLARE_WAIT_QUEUE_HEAD(myevent);

struct kva {
   long long unsigned int *va;
   struct list_head       node;
};

typedef struct kva kva;

struct list_head vaddr_list1;
EXPORT_SYMBOL(vaddr_list1);

struct list_head vaddr_list2;
EXPORT_SYMBOL(vaddr_list2);

struct   task_struct *kthread_task;
int      sleep_flag = 0;

uint64_t current_alloc_bytes_list1;
EXPORT_SYMBOL(current_alloc_bytes_list1);

uint64_t current_alloc_bytes_list2;
EXPORT_SYMBOL(current_alloc_bytes_list2);

static int kfill() {

    unsigned long flag;
    kva *pdesc;

    pdesc  = kmalloc(sizeof(kva),GFP_KERNEL);
    if(!pdesc)  {
        msleep(200);
        return -ENOMEM;	
    }

    pdesc->va   = kmalloc(PAGE_SIZE,GFP_KERNEL);
    if(!pdesc->va) {
	kfree(pdesc);
        msleep(200);
        return -ENOMEM;
    }


    if(kstore(virt_to_phys(pdesc->va)))
     {
	     list_add_tail(&pdesc->node,&vaddr_list1);
    	     current_alloc_bytes_list1+=PAGE_SIZE;	
     }	
     else
     {
	     list_add_tail(&pdesc->node,&vaddr_list2);
    	     current_alloc_bytes_list2+=PAGE_SIZE;	
     }	
    return 0;
 } 

#define SOCKET_NO1     0
#define CHANNEL_NO1    0

#define SOCKET_NO2     1
#define CHANNEL_NO2    2

static int kstore(long long unsigned int phy_addr) {

   int sck     = isSocket(phy_addr);
    // put allocator condition 
   if(( sck == SOCKET_NO1 )) {
        printk(KERN_INFO "Current Alloc Bytes List1 (KB):%llu\n",(current_alloc_bytes_list1 >> 10));
	return 1;
   }
   else 
   if(( sck == SOCKET_NO2 )) {
        printk(KERN_INFO "Current Alloc Bytes List2 (KB):%llu\n",(current_alloc_bytes_list2 >> 10));
        return 0;
   }
}

static int kleak() {

  unsigned long flag;
  kva          *pdesc,*tmp;
  list_for_each_entry_safe(pdesc, tmp, &vaddr_list1, node) {
	  list_del(&pdesc->node);
	  kfree(pdesc->va);
	  kfree(pdesc);
	  current_alloc_bytes_list1-=PAGE_SIZE;
  }
  printk(KERN_INFO "Current De-Alloc Bytes to (MB):%llu\n",(current_alloc_bytes_list1 >> 20));

  list_for_each_entry_safe(pdesc, tmp, &vaddr_list2, node) {
	  list_del(&pdesc->node);
	  kfree(pdesc->va);
	  kfree(pdesc);
	  current_alloc_bytes_list2-=PAGE_SIZE;
  }
  printk(KERN_INFO "Current De-Alloc Bytes to (MB):%llu\n",(current_alloc_bytes_list2 >> 20));
  return 0;
}

static int kwork(void *arg) {
    printk(KERN_CRIT "Kernel thread Spawned\n");
    while(!kthread_should_stop()) {
           msleep(30);
	   if(((current_alloc_bytes_list1 >> 20) > MAX_ALLOCATION ) || ((current_alloc_bytes_list2 >> 20) > MAX_ALLOCATION))
 		//msleep(500);
 		yield();
	   else
	        kfill();
    }
    printk(KERN_CRIT "Kernel thread Stopped\n");
    printk(KERN_INFO "Current Alloc Bytes (MB):%llu\n",((current_alloc_bytes_list1 + current_alloc_bytes_list2) >> 20));
    return 0;
} 

int start_module() {

    INIT_LIST_HEAD(&vaddr_list1);	
    INIT_LIST_HEAD(&vaddr_list2);	
    kthread_task = kthread_create(kwork,NULL,"compas_worker_thread");
    if (IS_ERR(kthread_task))
        printk( KERN_CRIT "Error Starting Kernel Thread\n");
    else 
        wake_up_process(kthread_task);
    printk(KERN_INFO "Starting Module :%p\n",kthread_task);
    return 0;
}
    
void stop_module() {
  kthread_stop(kthread_task);
  msleep(5000);
  kleak();
  printk(KERN_INFO "Removing Module\n");
 } 

module_init(start_module);
module_exit(stop_module);
MODULE_LICENSE("GPL");
