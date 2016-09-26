// Program : imc.c
// Description : This program is a kernel module which probes 
// the PCI bus for uncore registers and updates the address mapping 
// tables 

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
#include <linux/list.h>

#include "pci.h"

#define MAX_SAD_RANGES 8
#define MAX_TAD_RANGES 12
#define MAX_CHANNELS   4
#define MAX_RIR_RANGES 6
#define MAX_RANKS      8
#define MAX_SOCKETS    8

#define E_OK 0
#define E_ERR 1

#define GET_BITFIELD(v, lo, hi) \
       (((v) & ((1ULL << ((hi) - (lo) + 1)) - 1) << (lo)) >> (lo))


#define SAD_LIMIT(x)		     GET_BITFIELD((x),6,25)   
#define SOCKET_INTERLEAVING_MODE(x)  GET_BITFIELD((x),1,1)
#define MID_HASH_INTERLEAVE(x) 	    (GET_BITFIELD((x),6,8) ^ GET_BITFIELD((x),16,18))
#define LOW_INTERLEAVE(x) 	     GET_BITFIELD((x),6,8)
#define RULE_ENABLE(x)               GET_BITFIELD((x),0,0)
#define SAD_TARGET(x,n)       	    ((((x)%2)) ? (((x) >> ((n/2)*8)) & 0x7UL) : ((x) >> ((((n/2)*8) + 3) & 0x7UL)))

#define TAD_LIMIT(reg)              ((GET_BITFIELD(reg, 12, 31) << 26) | 0x3ffffff)
#define TAD_OFFSET(reg)             (GET_BITFIELD(reg, 6,25) << 26)
#define SOCK_WAYS(x)  		    GET_BITFIELD((x),10,11)
#define CHN_WAYS(x)                 GET_BITFIELD((x),8,9)
#define CHN_ID0(x)                  GET_BITFIELD((x),0,1)
#define CHN_ID1(x)                  GET_BITFIELD((x),2,3)
#define CHN_ID2(x)                  GET_BITFIELD((x),4,5)
#define CHN_ID3(x)                  GET_BITFIELD((x),6,7)

#define RIR_LIMIT(x)                GET_BITFIELD((x),1,10)
#define RIR_WAYS(x)                 GET_BITFIELD((x),28,29)	
#define RIR_VAL(x)                  GET_BITFIELD((x),31,31)
#define RANK_TGT(x)                 GET_BITFIELD((x),16,19)
#define CLOSED_PAGE(x)              GET_BITFIELD((x),0,0)
#define RIR_OFFSET(reg)             GET_BITFIELD(reg,  2, 14)

#define DIMM_POP(x)  		    GET_BITFIELD((x),14,14)
#define NUM_RANKS(x) 		    GET_BITFIELD((x),12,13)
#define RANK_DISABLE(x)             GET_BITFIELD((x),16,19) 

static LIST_HEAD(sb_list);
static DEFINE_MUTEX(sb_lock);

/* contain sad ranges for the board */
uint32_t    sad_table[MAX_SOCKETS][MAX_SAD_RANGES];
/* contain sad interleaving list */
uint32_t    sad_interleave_table[MAX_SOCKETS][MAX_SAD_RANGES];
/* contain tad ranges per socket */
uint32_t    tad_table[MAX_SOCKETS][MAX_TAD_RANGES];
/* contain tad offsets per tad range per channel per socket */
uint32_t    tad_offset[MAX_SOCKETS][MAX_CHANNELS][MAX_TAD_RANGES];
//EXPORT_SYMBOL_GPL(tad_offset);
/* contain rank limits per channel per socket */
uint32_t    rnk_table[MAX_SOCKETS][MAX_CHANNELS][MAX_RIR_RANGES];
/* contain rank interleaving list per range per channel per socket */
uint32_t    rnk_rir[MAX_SOCKETS][MAX_CHANNELS][MAX_RIR_RANGES][MAX_RANKS];
/* contain dimm table per dimm per channel */
uint32_t    dimm_table[MAX_SOCKETS][MAX_CHANNELS][MAX_DIMMS];
/* contain memory type per DIMM */
uint32_t    memory_technology[MAX_SOCKETS];
uint32_t    dimm_technology[MAX_SOCKETS][MAX_CHANNELS][MAX_DIMMS];


struct Mobj {
		int sad_rules[MAX_SOCKETS];
		int tad_rules[MAX_SOCKETS];
		int rir_rules[MAX_SOCKETS][MAX_CHANNELS];
}mobj;

/* 12/06 */
int pci_read_sad(struct pci_dev *pdev,int bus_count) {
         int         i   =0;
         int         ret = -1;
         uint32_t    var = 0;
	 uint32_t    prev = 0;
	 for( i= 0; i < MAX_SAD_RANGES ; i++) { 
              ret  = pci_read_config_dword(pdev,0x80+i*(0x8),&var);
              if(ret < 0) 
		 return E_ERR;
	      if(prev == SAD_LIMIT(var))
		 break;
	      prev= SAD_LIMIT(var);
              sad_table[bus_count][i] = var;
	      mobj.sad_rules[bus_count]++;	
         }	
	 return E_OK;
}

/* 12/06 */

int pci_read_sad_interleave(struct pci_dev *pdev, int bus_count) {
         int 	    i   = 0;
         int        ret = 0;
         uint32_t   var = 0;

	 for( i= 0; i < MAX_SAD_RANGES ; i++) { 
              ret  = pci_read_config_dword(pdev,0x84+i*(0x8),&var);
              if(ret < 0) 
	         return E_ERR;
              sad_interleave_table[bus_count][i] = var;/*
	      var|=0x1;	
	      ret = pci_write_config_dword(pdev,0x84+i*(0x8),var);
              if(ret < 0) {
		 printk (KERN_INFO "Pci Write Error \n");
	         return E_ERR;
	      }	
              ret  = pci_read_config_dword(pdev,0x84+i*(0x8),&var);
              if(ret < 0) 
	         return E_ERR;
              sad_interleave_table[bus_count][i] = var;
		*/	
	 }
         return E_OK;
}

/* 13/06 */

int pci_read_sad_target(struct pci_dev *pdev, int bus_count) {
         int ret      = -1;
         uint32_t var = 0;

         ret  = pci_read_config_dword(pdev,0xf0,&var);
         if(ret < 0)
            return E_ERR;
         ret  = pci_read_config_dword(pdev,0xf4,&var);
	 if (ret < 0)
             return E_ERR;
         return E_OK; 
}


/* 14/00 */
int pci_read_tad_table(struct pci_dev *pdev, int bus_count) {

        int 	 i   =0;
        int      ret =0;
        uint32_t var =0;
	uint32_t prev = 0;

	for( i=0; i < MAX_TAD_RANGES; i++) {
	     ret = pci_read_config_dword(pdev,0x80 + i*0x4, &var);
	     if(ret < 0) 
		return E_ERR; 
	     printk (KERN_INFO " Tad Table Bus count %d: Tad  %d Val %x\n",bus_count,i,var);	
	     if (prev == TAD_LIMIT(var))
		  break;
	     prev = TAD_LIMIT(var);
	     tad_table[bus_count][i] = var;
	     mobj.tad_rules[bus_count]++;	
	}

	return E_OK;
}

/* 15/00 */
int pci_read_memory_technology(struct pci_dev *pdev, int bus_count) {

	int ret = 0;
	uint32_t var = 0;

  	ret = pci_read_config_dword(pdev,0x7c,&var);
	if (ret < 0) 
	    return E_ERR;
	memory_technology[bus_count] = var;	
	return E_OK;
}

/* 15/02/03/04/05 */
int pci_read_tadoffset(struct pci_dev *pdev, int bus_count, int chn_no) {

	int i=0;	
	int ret = 0;
	uint32_t var = 0;

	for( i=0; i < MAX_TAD_RANGES ; i++ ) {
	     ret = pci_read_config_dword(pdev,0x90 + i*0x4, &var);
	     if(ret < 0)  
                return E_ERR;
	     tad_offset[bus_count][chn_no][i] = var;
	     printk (KERN_INFO "Tad Table Offset Bus count %d: Channel %d Tad limit %d Val %x\n",bus_count,chn_no,i,var);	
	}
        return E_OK;
}

/* 15/02/03/04/05 */
int pci_read_rirlimit(struct pci_dev *pdev, int bus_count, int chn_no) {

	int i=0;	
	int ret = 0;
	uint32_t var = 0;
	uint32_t prev = 0;
	
	for( i = 0; i < MAX_RIR_RANGES ; i++ ) {
	     ret = pci_read_config_dword(pdev,0x108 + i*0x4, &var);
	     if(ret < 0) 
	        return E_ERR;
	     if (prev == RIR_LIMIT(var))
		 break;
	     prev  = rnk_table[bus_count][chn_no][i];   
	     rnk_table[bus_count][chn_no][i] = var;
	     mobj.rir_rules[bus_count][chn_no]++; 	
         }
	 return E_OK;
}

/* 15/02/03/04/05 */
int pci_read_ririnterleave(struct pci_dev *pdev, int bus_count, int chn_no) {
	
	int i=0;	
	int k=0;	
	int ret = 0;
	uint32_t var = 0;

	for( k = 0; k < MAX_RIR_RANGES ; k++ ) {
	  for( i = 0; i < MAX_RANKS ; i++ ) {
	     ret = pci_read_config_dword(pdev,0x120 + i*0x4, &var);
	     if(ret  < 0) 
	        return E_ERR;
	    // printk (KERN_INFO "Bus count %d: Channel No %d: Rule No %d: Rank No %d\n",bus_count,chn_no,k,i);	
	     rnk_rir[bus_count][chn_no][k][i] = var;
	  }
	}
	return E_OK;
}  

/* 15/02/03/04/05 */
int pci_read_dimm_table(struct pci_dev *pdev, int bus_count, int chn_no) {
	
	int i=0;	
	int ret = 0;
	uint32_t var = 0;

	for( i = 0; i < MAX_DIMMS ; i++ ) {
           ret = pci_read_config_dword(pdev,0x80 + i*0x4, &var);
           if(ret < 0) 
              return E_ERR;
	   dimm_table[bus_count][chn_no][i] = var;
        }
	return E_OK;
}

/*15/02/03/04/05 */
int pci_read_write_dimm_technology_register(struct pci_dev *pdev, int bus_count, int chn_no) {

        int i = 0;
        int ret = 0;
        uint32_t var = 0;
	uint32_t to_write = 0xf << 15;
        
        for(i=0;i < MAX_DIMMS ;i++) { 
  	ret = pci_read_config_dword(pdev,0x80+i*0x4,&var);
	if (ret < 0) 
	    return E_ERR;
	dimm_technology[bus_count][chn_no][i] = var;
	//var|=to_write;
  	ret = pci_write_config_dword(pdev,0x80+i*0x4,&var);
        }	
	return E_OK;
}

/*16/0*/
int pci_read_pm_sref(struct pci_dev *pdev, int bus_count) {
         int        ret = 0;
         uint32_t   var = 0;
/*
	 printk(KERN_INFO "Reading PM_SREF\n");

         ret  = pci_read_config_dword(pdev,0x1d8,&var);
         if(ret < 0)
            return E_ERR;
         printk(KERN_INFO " Old VAR :%x\n",var);
       
         var&=~(1 << 20 ); 
         ret = pci_write_config_dword(pdev,0x1d8,var);
         if(ret < 0) {
            printk (KERN_INFO "Pci Write Error \n");
            return E_ERR;
         }
	 var = 0; 
         ret  = pci_read_config_dword(pdev,0x1d8,&var);
         if(ret < 0) 
            return E_ERR;
         printk(KERN_INFO " New VAR :%x\n",var);       
 */
         return E_OK;
}

/* Decode Socket */
#define SAD_TARGET_PKG1(x) GET_BITFIELD((x),0,2)
#define SAD_TARGET_PKG2(x) GET_BITFIELD((x),3,5)

#define SAD_TARGET_PKG3(x) GET_BITFIELD((x),8,10)
#define SAD_TARGET_PKG4(x) GET_BITFIELD((x),11,13)

#define SAD_TARGET_PKG5(x) GET_BITFIELD((x),16,18)
#define SAD_TARGET_PKG6(x) GET_BITFIELD((x),19,21)

#define SAD_TARGET_PKG7(x) GET_BITFIELD((x),24,26)
#define SAD_TARGET_PKG8(x) GET_BITFIELD((x),27,29)

int isSocket(long long unsigned int phy_addr) {

       int i=0;
       int k=0;	
       int socket_no    = -1;
       int sockilv_indx = -1;
       long long unsigned int prev_limit = 0;	
       long long unsigned int curr_limit = 0;	

       for (k = 0 ; k < 2 ; k++) {

        for (i = 0 ; i < MAX_SAD_RANGES ; i++) {

	    if (!RULE_ENABLE(sad_table[k][i]))
                 break;

	    prev_limit = curr_limit;	
	    curr_limit =  SAD_LIMIT(sad_table[k][i]) << 26;
/*	
	    printk(KERN_INFO " prev_limit :%llu",prev_limit);	
	    printk(KERN_INFO " curr_limit :%llu",curr_limit);	
	    printk(KERN_INFO "********************\n");	
*/	
	    if( (prev_limit < phy_addr ) && ( curr_limit > phy_addr )) {

                if(SOCKET_INTERLEAVING_MODE(sad_table[k][i]))
                   sockilv_indx = MID_HASH_INTERLEAVE(phy_addr);
                else
                   sockilv_indx = LOW_INTERLEAVE(phy_addr);

	//	printk(KERN_INFO "Node Interleaving Index :%d\n",sockilv_indx);
	//	printk(KERN_INFO "Node Interleaving Register Value :%d\n",sad_interleave_table[k][i]);


		switch(sockilv_indx) {
		   case 0 :  socket_no    =  SAD_TARGET_PKG1(sad_interleave_table[k][i]);
		             break; 
		   case 1 :  socket_no    =  SAD_TARGET_PKG2(sad_interleave_table[k][i]);
			     break;
		   case 2 :  socket_no    =  SAD_TARGET_PKG3(sad_interleave_table[k][i]);
			     break;
		   case 3 :  socket_no    =  SAD_TARGET_PKG4(sad_interleave_table[k][i]);
			     break;
		   case 4 :  socket_no    =  SAD_TARGET_PKG5(sad_interleave_table[k][i]);
			     break;
		   case 5 :  socket_no    =  SAD_TARGET_PKG6(sad_interleave_table[k][i]);
			     break;
		   case 6 :  socket_no    =  SAD_TARGET_PKG7(sad_interleave_table[k][i]);
			     break;
		   case 7 :  socket_no    =  SAD_TARGET_PKG8(sad_interleave_table[k][i]);
			     break;
		   default :
			     break;
		}	
                goto ok;
            }
        }
       }
       ok:
       return socket_no;
}
EXPORT_SYMBOL(isSocket);

/* Decode Channel */
inline int isChannel(long long unsigned int phy_addr, int socket_no,long long unsigned int *channel_addr ) {

	 int i          =  0;
         int channel_no = -1;
         int sck_ways   = -1;
         int chn_ways   = -1;
	 long long unsigned int chn_addr = 0;
         long long unsigned int prev_limit = 0;	
         long long unsigned int curr_limit = 0;	

         for (i = 0 ; i < MAX_TAD_RANGES ; i++) {

	    prev_limit = curr_limit;	
	    curr_limit =  TAD_LIMIT(tad_table[socket_no][i]);

	    //printk(KERN_INFO " curr %llu : phy %llu \n", curr_limit, phy_addr);

	    if (prev_limit == curr_limit)
		break;
	    
	    if( (prev_limit < phy_addr ) && ( curr_limit > phy_addr )) {

	#if 0
	    printk(KERN_INFO "SOCK_WAYS :%d\n",SOCK_WAYS(tad_table[socket_no][i]));
	    printk(KERN_INFO "CHN_WAYS  :%d\n",CHN_WAYS(tad_table[socket_no][i]));

	#endif
            sck_ways  = SOCK_WAYS(tad_table[socket_no][i]);
            chn_ways  = CHN_WAYS(tad_table[socket_no][i]);

		/*correction added */
	    phy_addr -= TAD_OFFSET(tad_offset[socket_no][0][i]);	
	    //printk(KERN_INFO "TADOFFSET INDEX :%d\n",i);

		/* decode channels */ 
                   //  interleaving covers all the channels ok
            if(chn_ways==2)
               chn_addr = phy_addr >> 6;
		  //  if number of channels is lessa than required, repeat entries
            else
               chn_addr = phy_addr >> (6 + sck_ways);
            channel_no = chn_addr % (1 << chn_ways) ;

	    switch(channel_no) {

		case 0 : 
			channel_no = CHN_ID0(tad_table[socket_no][i]);
			break;
		case 1 :
			channel_no = CHN_ID1(tad_table[socket_no][i]);
			break;
		case 2 :
			channel_no = CHN_ID2(tad_table[socket_no][i]);
			break;
		case 3 :
			channel_no = CHN_ID3(tad_table[socket_no][i]);
			break;
		default :
            		break;
             }
	  }
         }
	 //printk(KERN_INFO "Socket No :%d Channel No :%d %llx %llx\n",socket_no, channel_no,phy_addr,chn_addr);
	*channel_addr = chn_addr;
	 return channel_no;
}
EXPORT_SYMBOL(isChannel);

/* Rank */
inline int isRank(long long unsigned int chn_addr, int socket_no, int channel_no, long long unsigned int *rank_local_addr) {

	  int i=0;
          int rank_no = -1;
	  int phy_rank_no = -1;
	  int rnk_ways = 0;
	  long long unsigned int curr_rank_limit = 0;
	  long long unsigned int prev_rank_limit = 0;
	  long long unsigned int rank_addr = 0;
	  static long long unsigned int ser = 0;
	
          if(CLOSED_PAGE(memory_technology[socket_no]))
             rank_addr = chn_addr >> 6;
          else 
             rank_addr = chn_addr >> 13;

          for (i = 0; i < MAX_RIR_RANGES ; i++) {
	       if(!RIR_VAL(rnk_table[socket_no][channel_no][i])) 	
		   break;
	       prev_rank_limit = curr_rank_limit;	
	       curr_rank_limit = (RIR_LIMIT(rnk_table[socket_no][channel_no][i]) << 29);	
		
               if((rank_addr >= prev_rank_limit) && (rank_addr < curr_rank_limit)) {
                  rnk_ways    =  RIR_WAYS(rnk_table[socket_no][channel_no][i]);
                  rank_no     =  rank_addr % (1 << rnk_ways);
                  phy_rank_no =  RANK_TGT(rnk_rir[socket_no][channel_no][i][rank_no]);
		  break;
                }
           }
           *rank_local_addr = rank_addr;
	   //printk(KERN_INFO "Socket No :%d Channel No :%d Rank No :%d Channel Address :%llx->%llx Ser No :%llu\n",socket_no,channel_no,phy_rank_no,chn_addr,rank_addr,ser++);
 	   return phy_rank_no;
}

EXPORT_SYMBOL(isRank);

/*IsDimm*/
int isDimm(long long unsigned int phy_addr, int socket_no, int channel_no, int phy_rank_no) {
	  int i=0;
	  int num_ranks;
	  int dimm_no=-1;

	  for( i = 0 ; i < MAX_DIMMS ;i++) {
               if (DIMM_POP(dimm_table[socket_no][channel_no][i])) {
                   num_ranks = NUM_RANKS(dimm_table[socket_no][channel_no][i]);
                   if((phy_rank_no < (i+1)*num_ranks) && (phy_rank_no > i*num_ranks)) {
		       dimm_no = i;
                       break;
		   }
	   }
          }
	  return dimm_no;
}
EXPORT_SYMBOL(isDimm);

/*
 *  Tables
*/

int print_tables(void) {

	int i=0;
	int k=0;
	int j=0;
	int m=0;

	uint64_t prev=0;
	uint64_t curr=0;
	
        /* print sad table */


	printk(KERN_INFO "\n\n*********************** ADDRESS_MAPPING TABLES FOR INTEL_SANDYBRIDGE ************************\n");

	for( k = 0; k < 2 ; k++) {

	 printk(KERN_INFO "Socket No :%d\n",k); 

	 for( i= 0; i < MAX_SAD_RANGES ; i++) {
	       curr  = SAD_LIMIT(sad_table[k][i]);	
	       if((prev!=curr) && (curr!=0)){
			prev = SAD_LIMIT(sad_table[k][i]);
	   	 	printk(KERN_INFO "SAD RANGE #%d %llu[MB] INTERLEAVE %x\n",i,(SAD_LIMIT(sad_table[k][i]) << 26)/(1024*1024) ,sad_interleave_table[k][i]);
	   		printk(KERN_INFO "sad_table[%d][%d] %x %x\n",k,i,sad_table[k][i],sad_interleave_table[k][i]);
	       }
	 }

	   printk(KERN_INFO "******************************************************************\n");
	 prev=curr=0;
	 for( i= 0; i < MAX_TAD_RANGES ; i++) {
	       curr = TAD_LIMIT(tad_table[k][i]);	
	       if(prev!=curr) {
		        prev = TAD_LIMIT(tad_table[k][i]);		
	       		printk (KERN_INFO " TAD LIMIT %d %llu[MB], SOCK WAYS :%lu CHN_WAYS %lu TAD_OFFSET : %llu\n",i,TAD_LIMIT(tad_table[k][i])/(1024*1024),  SOCK_WAYS(tad_table[k][i]), CHN_WAYS(tad_table[k][i]),TAD_OFFSET(tad_offset[k][CHN_ID0(tad_table[k][i])][i])/(1024*1024));	
	       	       // printk( KERN_INFO "ID :%llu Offset :%llu \n",CHN_ID0(tad_table[k][i]), TAD_OFFSET(tad_offset[k][CHN_ID0(tad_table[k][i])][i])/(1024*1024));
	               // printk( KERN_INFO "ID :%llu Offset :%llu \n",CHN_ID1(tad_table[k][i]), TAD_OFFSET(tad_offset[k][CHN_ID0(tad_table[k][i])][i])/(1024*1024));
	               // printk( KERN_INFO "ID :%llu Offset :%llu \n",CHN_ID2(tad_table[k][i]), TAD_OFFSET(tad_offset[k][CHN_ID0(tad_table[k][i])][i])/(1024*1024));
	               // printk( KERN_INFO "ID :%llu Offset :%llu \n",CHN_ID3(tad_table[k][i]), TAD_OFFSET(tad_offset[k][CHN_ID0(tad_table[k][i])][i])/(1024*1024));
	               // printk(KERN_INFO "tad_table[%d][%d] %x tad_offset[%d][%d][%d] [MB] %llu\n",k,i,tad_table[k][i],k,CHN_ID0(tad_table[k][i]),i,TAD_OFFSET(tad_offset[k][CHN_ID0(tad_table[k][i])][i])/(1024*1024));
		}
	 } 
/*
	for( k=0; k < MAX_SOCKETS; k++) {
          printk(KERN_INFO " SOCKET NO :%d\n", i);
	  for( i=0; i < MAX_CHANNELS; i++) {
             printk(KERN_INFO " CHANNEL NO :%d\n", i);
             for ( j=0; j < MAX_DIMMS ; i++) 
                   printk(KERN_INFO "DIMM NO %d POP :%d RANK_DISABLE ",j,DIMM_POP(dimm_technology[k][i][j]),RANK_DISABLE(dimm_technology[k][i][j]),RANK_CNT(dimm_technology[k][i][j]));   	
	  }
	}
*/

	   printk(KERN_INFO "******************************************************************\n");
	/* print rir table */ 
	 prev=curr=0;
	 for( i= 0; i < MAX_CHANNELS ; i++) {
	      for( j= 0; j < MAX_RIR_RANGES ; j++) {
		curr = RIR_LIMIT(rnk_table[k][i][j]);
		if((prev!=curr) && (curr!=0)){
	          printk(KERN_INFO " CHANNEL NO :%d\n",i);
		  prev = RIR_LIMIT(rnk_table[k][i][j]);
	          printk(KERN_INFO " RIR LIMIT %d :%llu[MB]  RIR_WAYS :%llu\n",j,(RIR_LIMIT(rnk_table[k][i][j]) << 29)/(1024*1024), RIR_WAYS(rnk_table[k][i][j]));
	          //printk(KERN_INFO "rnk_table[%d][%d][%d] %x \n",k,i,j,rnk_table[k][i][j]);
		  for ( m=0; m < (1 << RIR_WAYS(rnk_table[k][i][j])) ; m++) {
		        printk (KERN_INFO " PHY RIR NO :%llu : RIR_OFFSET[MB] %llu\n", RANK_TGT(rnk_rir[k][i][j][m]), (RIR_OFFSET(rnk_rir[k][i][j][m]) << 6)); 
		    //    printk (KERN_INFO " rnk_rir[%d][%d][%d][%d]  :%x\n", k,i,j,m,rnk_rir[k][i][j][m]); 
		  }
		}
	      }
	   prev=curr=0;
	   }
	   printk(KERN_INFO "******************************************************************\n");
	/* print dimm table */
	   for( i= 0; i < MAX_CHANNELS ; i++) {
	      for(j= 0; j < MAX_DIMMS ; j++)  {
		  if(DIMM_POP(dimm_table[k][i][j]))
		  printk(KERN_INFO " CHANNEL_NO :%d DIMM No :%d Present %llu\n",i,j, DIMM_POP(dimm_table[k][i][j])); 
		  //printk(KERN_INFO " dimm_table[%d][%d][%d] : %x POP %d  RANK_ENABLE :%x\n",k,i,j,dimm_table[k][i][j],DIMM_POP(dimm_table[k][i][j]),RANK_DISABLE(dimm_table[k][i][j])); 
		}
	  }

          if(CLOSED_PAGE(memory_technology[k]))
	     printk(KERN_INFO "Memory Technology: CLOSED\n");
	  else
	     printk(KERN_INFO "Memory Technology: OPEN\n");
	   printk(KERN_INFO "memory_technology[%d] :%x\n",k,memory_technology[k]);
	 }
         return 0;
}

//
// Create SBDevice Instance
//

struct sbridge_dev *alloc_sbridge_dev(uint8_t bus, const struct pci_id_table *table){
	
       struct sbridge_dev *sb_dev;
       sb_dev = kzalloc(sizeof(struct sbridge_dev),GFP_KERNEL);
       if(!sb_dev)
	   return NULL;
       sb_dev->pdev = kzalloc(sizeof(*sb_dev->pdev) * table->ndevs, GFP_KERNEL);			
       if(!sb_dev->pdev) {
	   kfree(sb_dev);
	   return NULL;	
	}
       sb_dev->bus = bus;
       sb_dev->n_devs = table->ndevs;
       list_add_tail(&sb_dev->list,&sb_list);
       return sb_dev;	  	 
}

//
// Release SB Device Instance : Ensure all the pci devices have been put to
//
void free_sbridge_dev(struct sbridge_dev* sb_dev) {
     list_del(&sb_dev->list);
     kfree(sb_dev->pdev);
     kfree(sb_dev);
}

//
// Look for Existing Instances
// 

struct sbridge_dev *get_sbridge_dev(uint8_t bus) {
       struct sbridge_dev *sb_dev;
       list_for_each_entry(sb_dev,&sb_list,list) {
          if(sb_dev->bus == bus)
	     return sb_dev;
	}
       return NULL;	
}

//
//  Create Device Instance
//

int sbridge_get_one_dev(struct sbridge_dev *sb_dev,
	        	const struct pci_id_table *table,
		        const unsigned devno,
		        struct pci_dev **prev) {

      int ret=-1;
      static  uint8_t bus = 0;
      struct pci_dev *pdev;	

      pdev = pci_get_device(PCI_VENDOR_ID_INTEL,table->descr[devno].dev_id,*prev);

      if (!pdev) {
            if (*prev) {		// i do not know the condition
		  *prev = pdev;
		   return 0;
            }
	    if (devno == 0)
                 return -ENODEV;
	    printk(KERN_ALERT "Device Not Found %d : %d PCI_ID :%d : %d\n",PCI_VENDOR_ID_INTEL, table->descr[devno].dev_id, 
									   PCI_SLOT(pdev->devfn), PCI_FUNC(pdev->devfn));
	    return -ENODEV;
       }	


      printk(KERN_INFO " PCI_SLOTS NO :%d\n",PCI_SLOT(pdev->devfn));

      if(pdev->bus->number) 
	    bus = pdev->bus->number;	
	

      if(!sb_dev)	
          sb_dev = get_sbridge_dev(pdev->bus->number);
      else
	  return 0;	

      if(!sb_dev)  {
             sb_dev = alloc_sbridge_dev(pdev->bus->number,table);
	     if(!sb_dev) {
		 return -ENOMEM;
	     }	
	}

      if(sb_dev->pdev[devno]) {
             printk(KERN_ERR "Duplicated Initialization %d : %d PCI_ID :%d : %d\n",PCI_VENDOR_ID_INTEL,table->descr[devno].dev_id,
										   PCI_SLOT(sb_dev->pdev[devno]->devfn), 
										   PCI_FUNC(sb_dev->pdev[devno]->devfn));
	     pci_dev_put(sb_dev->pdev[devno]);
	     return -ENODEV;

	}
       
      if ((PCI_SLOT(pdev->devfn) != table->descr[devno].dev) || (PCI_FUNC(pdev->devfn) != table->descr[devno].func)) {
	     printk(KERN_ERR "Device PCI ID %d:%d has dev %d:%d instead of %d:%d\n",PCI_VENDOR_ID_INTEL, 
										       table->descr[devno].dev_id,
										       PCI_SLOT(pdev->devfn),
										       PCI_FUNC(pdev->devfn),
										       table->descr[devno].dev,
										       table->descr[devno].func);
	     return -ENODEV;
	}	

        if (pci_enable_device(pdev) < 0) {
	    printk(KERN_ERR " Could not enable device %d:%d PCI ID %d:%d\n", PCI_VENDOR_ID_INTEL, 
									     table->descr[devno].dev_id,
									     PCI_SLOT(pdev->devfn),PCI_FUNC(pdev->devfn));		           return -ENODEV;
	}
        printk(KERN_INFO "Bus Number :%d\n",bus);
	printk(KERN_INFO "Device Detected :%x:%x PCI ID %x:%x\n", PCI_VENDOR_ID_INTEL, 
								  table->descr[devno].dev_id, 
								  PCI_SLOT(pdev->devfn), 
								  PCI_FUNC(pdev->devfn));
	pci_dev_get(pdev); // increase the reference count
       *prev = pdev;

	bus=(bus+1)/64;
	if((PCI_SLOT(pdev->devfn) == 12) && (PCI_FUNC(pdev->devfn) == 6)) {
	   ret = pci_read_sad(pdev,bus-1);
	   ret = pci_read_sad_interleave(pdev,bus-1);
	}

	if((PCI_SLOT(pdev->devfn) == 13) && (PCI_FUNC(pdev->devfn) == 6))
	   ret = pci_read_sad_target(pdev,bus-1);
/*
	if((PCI_SLOT(pdev->devfn) == 14) && (PCI_FUNC(pdev->devfn) == 0))
	   ret = pci_read_tad_table(pdev,bus-1);
*/
	if((PCI_SLOT(pdev->devfn) == 15) && (PCI_FUNC(pdev->devfn) == 0)) {
	   ret = pci_read_memory_technology(pdev,bus-1);
	   ret = pci_read_tad_table(pdev,bus-1);
	}

        if((PCI_SLOT(pdev->devfn) == 15) 
                  && 
     ((PCI_FUNC(pdev->devfn)==2) || (PCI_FUNC(pdev->devfn) == 3) || (PCI_FUNC(pdev->devfn)==4) || (PCI_FUNC(pdev->devfn) == 5))){ 
	   ret = pci_read_tadoffset(pdev,bus-1,PCI_FUNC(pdev->devfn)-2);
	   ret = pci_read_rirlimit(pdev,bus-1,PCI_FUNC(pdev->devfn)-2);
	   ret = pci_read_ririnterleave(pdev,bus-1,PCI_FUNC(pdev->devfn)-2);
	   ret = pci_read_dimm_table(pdev,bus-1,PCI_FUNC(pdev->devfn)-2);
	   ret = pci_read_write_dimm_technology_register(pdev,bus-1,PCI_FUNC(pdev->devfn)-2); 
	}
 
        if((PCI_SLOT(pdev->devfn) == 16) && (((PCI_FUNC(pdev->devfn)==0) || (PCI_FUNC(pdev->devfn) == 1) || (PCI_FUNC(pdev->devfn)==4) || (PCI_FUNC(pdev->devfn) == 5))))
           ret = pci_read_pm_sref(pdev,bus-1); 
	
      return 0;
  }	


//
//  Release Device Instance
//

int sbridge_put_devices(struct sbridge_dev* sb_dev) {

	int i=0;
	const struct pci_id_table *table = sbridge_pci_id_table;
        for(i = 0 ; i < sb_dev->n_devs; i++ ) {
	   if(sb_dev->pdev[i]) {
	      printk(KERN_INFO "Removing Device %d:%d\n",PCI_VENDOR_ID_INTEL,table->descr[i].dev_id);
	      pci_dev_put(sb_dev->pdev[i]);	
	   }
	}	
	return 0;	
}

//
//  Release Device Instances
//

int sbridge_put_all_devices(void) {
	struct sbridge_dev *sb_dev, *tmp;
	list_for_each_entry_safe(sb_dev,tmp,&sb_list,list) {
	     sbridge_put_devices(sb_dev);
	     free_sbridge_dev(sb_dev);
	}
	return 0;
}

//
//  Create Device Instances
//

int sbridge_get_all_devices(void) {

	int ret=0;
	int i = 0;
	struct sbridge_dev *sb_dev = NULL;
	const struct pci_id_table *table  = sbridge_pci_id_table;
	struct pci_dev            *pdev   = NULL;

	while (table && table->descr) {	
	for (i = 0 ; i < table->ndevs; i++ ) {
	      do {	
	      ret = sbridge_get_one_dev(sb_dev, table, i, &pdev);
	      if ( ret < 0 ) {			// id sb_dev instantiated
		   if (sb_dev)
		       sbridge_put_all_devices();
		   return -ENODEV;
	       }
	      } while (pdev);	
	}
	table++;
	}
	return 1;
}
//
// SBridge Meminfo 
//

int bind_compas_devices(struct sbridge_dev *, struct sbridge_mc_info *);

int sbridge_register_devices(struct sbridge_dev *sb_dev, struct sbridge_mc_info *pvt) {

    int ret = 0; 	
    pvt->sb_dev  = sb_dev;
    ret = bind_compas_devices(sb_dev,pvt);
    if (unlikely(ret < 0))
         goto fail0;
    return 1;

    fail0 :
    return -1;
}

int bind_compas_devices(struct sbridge_dev *sb_dev, struct sbridge_mc_info *pvt) {

    int i = 0;
    int slot;
    int func;

    struct pci_dev *pdev;

    if(!sb_dev)
	return -1;
	
    for (i = 0; i < sb_dev->n_devs; i++) {

         pdev = sb_dev->pdev[i];
         if (!pdev)
              continue;

         slot = PCI_SLOT(pdev->devfn);
         func = PCI_FUNC(pdev->devfn);
         
	 switch (slot) {
            case 12:
                switch (func) {
                        case 6:
                                pvt->pci_sad0 = pdev;
                                break;
                        case 7:
                                pvt->pci_sad1 = pdev;
                                break;
                        default:
                                goto error;
                        }
                        break;

	    case 13:
                 switch (func) {
                        case 6:
                                pvt->pci_br = pdev;
                                break;
                        default:
                                goto error;
                        }
                        break;
	    case 14:
                 switch (func) {
                        case 0:
                                pvt->pci_ha0 = pdev;
                                break;
                        default:
                                goto error;
                        }
                        break;
            case 15:
                 switch (func) {
                        case 0:
                                pvt->pci_ta = pdev;
                                break;
                        case 1:
                                pvt->pci_ras = pdev;
                                break;
                        case 2:
                        case 3:
                        case 4:
                        case 5:
                                pvt->pci_tad[func - 2] = pdev;
                                break;
                        default:
                                goto error;
                        }
                        break;
	     case 17:
                 switch (func) {
                        case 0:
                                pvt->pci_ddrio = pdev;
                                break;
                        default:
                                goto error;
                        }
                        break;
                default:
                        goto error;
                }
	 }

	       /* Check if everything were registered */
        if (!pvt->pci_sad0 || !pvt->pci_sad1 || !pvt->pci_ha0 ||
            !pvt-> pci_tad || !pvt->pci_ras  || !pvt->pci_ta ||
            !pvt->pci_ddrio)
                goto enodev;

        for (i = 0; i < NUM_CHANNELS; i++) {
                if (!pvt->pci_tad[i])
                        goto enodev;
        }
        return 0;

	enodev:
        printk(KERN_ERR "Some needed devices are missing\n");
        return -ENODEV;

	error:
        printk(KERN_ERR "Device %d, function %d is out of the expected range\n",slot, func);
        return -EINVAL;
}

//
// Sandy Bridge Probe
// 

static int sbridge_probe(struct pci_dev *pdev, const struct pci_device_id *id) {
       int ret=0; 
       int probed = 0;	
       mutex_lock(&sb_lock);
       if( probed >=1 ) {	
          mutex_unlock(&sb_lock); 
	  return -ENODEV;
       }	
       probed++;
       ret = sbridge_get_all_devices();
       if(ret < 0)
         printk(KERN_ERR "SBridge_MC_driver_probe_fail"); 
       mutex_unlock(&sb_lock); 
       print_tables();	

       return ret;
}

static void sbridge_remove(struct pci_dev *pdev) {
      
       int probed = 0;	
       mutex_lock(&sb_lock);
       if(probed)
          sbridge_put_all_devices();
       else {
          mutex_unlock(&sb_lock); 
	  return;
       }	
       probed--;
       mutex_unlock(&sb_lock); 
}


/*
 *      pci_device_id   table for which devices we are looking for
 *
 */
static DEFINE_PCI_DEVICE_TABLE(sbridge_pci_tbl) = {
        {PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_INTEL_SANDYBRIDGE_IMC_TA)},
        {0,}                    /* 0 terminated list. */
};

MODULE_DEVICE_TABLE(pci, sbridge_pci_tbl);

static struct pci_driver sbridge_driver = {
	.name  = "SBridge_MC_driver",
        .probe = sbridge_probe,
	.remove= sbridge_remove,
	.id_table = sbridge_pci_tbl,
};


static int __init sbridge_init(void) {

   int ret = 0;
   //opstate_init();
   ret = pci_register_driver(&sbridge_driver);
   if (ret < 0)
      printk(KERN_ERR "Address Mapping Module registration fail with error %d", ret);
   else
      printk(KERN_INFO "Address Mapping Module driver registration OK\n");		
   return ret;
}
  
static void __exit sbridge_exit(void)
{
   pci_unregister_driver(&sbridge_driver);
}

module_init(sbridge_init);
module_exit(sbridge_exit);

MODULE_LICENSE("GPL");
