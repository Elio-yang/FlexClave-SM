//******************************************************************************
// Copyright (c) 2018, The Regents of the University of California (Regents).
// All Rights Reserved. See LICENSE for license details.
//------------------------------------------------------------------------------
#include "ipi.h"
#include "sm.h"
#include "pmp.h"
#include <crypto.h>
#include "enclave.h"
#include "platform-hook.h"
#include "sm-sbi-opensbi.h"
#include <sbi/sbi_string.h>
#include <sbi/riscv_locks.h>
#include <sbi/riscv_barrier.h>
#include <sbi/sbi_console.h>
#include <sbi/sbi_hart.h>

static int sm_init_done = 0;
static int sm_region_id = 0, os_region_id = 0;

#ifndef TARGET_PLATFORM_HEADER
#error "SM requires a defined platform to build"
#endif

// Special target platform header, set by configure script
#include TARGET_PLATFORM_HEADER

byte sm_hash[MDSIZE] = { 0, };
byte sm_signature[SIGNATURE_SIZE] = { 0, };
byte sm_public_key[PUBLIC_KEY_SIZE] = { 0, };
byte sm_private_key[PRIVATE_KEY_SIZE] = { 0, };
byte dev_public_key[PUBLIC_KEY_SIZE] = { 0, };

int osm_pmp_set(uint8_t perm)
{
  /* in case of OSM, PMP cfg is exactly the opposite.*/
  return pmp_set_keystone(os_region_id, perm);
}

static int smm_init(void)
{
  int region = -1;
  int ret = pmp_region_init_atomic(SMM_BASE, SMM_SIZE, PMP_PRI_TOP, &region, 0);
  if(ret)
    return -1;

  return region;
}

static int osm_init(void)
{
  int region = -1;
  // specify all memory region
  // init from 0x0 - 0xffff...f
  int ret = pmp_region_init_atomic(0, -1UL, PMP_PRI_BOTTOM, &region, 1);
  if(ret) 
    return -1;

  return region;
}

void sm_sign(void* signature, const void* data, size_t len)
{
  sign(signature, data, len, sm_public_key, sm_private_key);
}

int sm_derive_sealing_key(unsigned char *key, const unsigned char *key_ident,
                          size_t key_ident_size,
                          const unsigned char *enclave_hash)
{
  unsigned char info[MDSIZE + key_ident_size];

  sbi_memcpy(info, enclave_hash, MDSIZE);
  sbi_memcpy(info + MDSIZE, key_ident, key_ident_size);

  /*
   * The key is derived without a salt because we have no entropy source
   * available to generate the salt.
   */
  return kdf(NULL, 0,
             (const unsigned char *)sm_private_key, PRIVATE_KEY_SIZE,
             info, MDSIZE + key_ident_size, key, SEALING_KEY_SIZE);
}

static void sm_print_hash(void)
{
  for (int i=0; i<MDSIZE; i++)
  {
    sbi_printf("%02x", (char) sm_hash[i]);
  }
  sbi_printf("\n");
}

/*
void sm_print_cert()
{
	int i;

	printm("Booting from Security Monitor\n");
	printm("Size: %d\n", sanctum_sm_size[0]);

	printm("============ PUBKEY =============\n");
	for(i=0; i<8; i+=1)
	{
		printm("%x",*((int*)sanctum_dev_public_key+i));
		if(i%4==3) printm("\n");
	}
	printm("=================================\n");

	printm("=========== SIGNATURE ===========\n");
	for(i=0; i<16; i+=1)
	{
		printm("%x",*((int*)sanctum_sm_signature+i));
		if(i%4==3) printm("\n");
	}
	printm("=================================\n");
}
*/

// entry point from sbi_init <bootloader> jumps here as next stage
void sm_init(bool cold_boot)
{


	// initialize SMM
  if (cold_boot) {
    /* only the cold-booting hart will execute these */
    sbi_printf("[SM] Initializing ... hart [%lx]\n", csr_read(mhartid));
    sbi_ecall_register_extension(&ecall_keystone_enclave);

    sm_region_id = smm_init();
    if(sm_region_id < 0) {
      sbi_printf("[SM] intolerable error - failed to initialize SM memory");
      sbi_hart_hang();
    }

    os_region_id = osm_init();
    if(os_region_id < 0) {
      sbi_printf("[SM] intolerable error - failed to initialize OS memory");
      sbi_hart_hang();
    }

    if (platform_init_global_once() != SBI_ERR_SM_ENCLAVE_SUCCESS) {
      sbi_printf("[SM] platform global init fatal error");
      sbi_hart_hang();
    }
    // Copy the keypair from the root of trust
    // this will get key from the bootloader in bootrom
    // copy keys from sanctum TEE
    // although current implememtation is just for test
    // the workflow could be learnt. 
    sm_copy_key();

    // Init the enclave metadata
    // Just specify some data as invalid
    // not assigning the real region/enclave
    // but the max number of enclave is defined as 16
    enclave_init_metadata();

    sm_init_done = 1;
    mb();
  }

  /* wait until cold-boot hart finishes */
  while (!sm_init_done)
  {
    mb();
  }

  

  /* below are executed by all harts */
  // looks like this init will clean all pmp-regs
  pmp_init();
  
  // will reset the sm and os pmp region 
  // based on the log, we have below layout:
  // ----------------------------------------------
  // SM:
  // region_id: 0
  // perm: ---
  // mode: NAPOT
  // address range: 0x80000000 - 0x80200000 [2MB]
  // pmpaddr: 0x2003ffff
  // pmpcfg: 0x18
  // ----------------------------------------------
  // OS:
  // region_id: 7
  // perm: rwx
  // mode: NAPOT
  // address range: 0x0 - 0xffffffffffffffff [ALL]
  // pmpaddr: 0xffffffffffffffff
  // pmpcfg: 0x1f00000000000000

  pmp_set_keystone(sm_region_id, PMP_NO_PERM);
  pmp_set_keystone(os_region_id, PMP_ALL_PERM);

  // steps for creating a pmp region
  // 1. acquire a region id, eg. smm_init()
  // 2. pmp init, reset all pmpaddr and pmpcfg. only for the sm_init()
  // 3. set pmp with corresponding addr, region and permission


  /* Fire platform specific global init */
  if (platform_init_global() != SBI_ERR_SM_ENCLAVE_SUCCESS) {
    sbi_printf("[SM] platform global init fatal error");
    sbi_hart_hang();
  }

  sbi_printf("[SM] Keystone security monitor has been initialized!\n");

  sm_print_hash();


  // let's try some pmp test here
  sbi_printf("[SM] Performing some PMP tests here.\n");
  pmp_test();
  sbi_printf("[SM] PMP Tests Done\n");

  
  return;
  // for debug
  // sm_print_cert();
}


void pmp_test(void)
{
  sbi_printf("PMP Test Starts.\n");
  // test-case 1: define my own 2 regions r-1 (high prv) and r-2 (low prv)
  // specify the adjacent location
  // print the pmp information
  // r-1 access r-2 memory location
  // r-2 access r-1 memory location
  /* TEST CASE 1 HERE*/
  


  /*TEST CASE 1 ENDS HERE*/

  
  // test-case 2: define some regions as below:
  //             [*****]r2
  //      [-----]r1
  //[===========================================] OS
  //[====][-----][*****][=======================] net memory
  // test overflow case cross each boundary
  // especially,
  // os overflow to r1
  // r1 overflow to r2
  // r2 overflow to os
  // test the range between each region
  /*TEST CASE 2 HERE*/



  /*TEST CASE 2 ENDS HERE*/

  sbi_printf("PMP Test Ends.\n");

}