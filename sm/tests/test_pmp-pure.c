//#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include "../src/pmp.c"

#define PMP_SUCCESS SBI_ERR_SM_PMP_SUCCESS

#define rep_assert_int_equal (a,b) \
  sbi_printf("test value:%d ;true value:%d\n",a,b)



static void test_search_rightmost_unset()
{
  // static int search_rightmost_unset(uint32 bitmap, int max, uint32_t mask)

  // test with mask = 0x1
  rep_assert_int_equal(search_rightmost_unset(0x0, 8, 0x1), 0);
  rep_assert_int_equal(search_rightmost_unset(0x2, 8, 0x1), 0);
  rep_assert_int_equal(search_rightmost_unset(0x1, 8, 0x1), 1);
  rep_assert_int_equal(search_rightmost_unset(0xffef, 8, 0x1), 4);
  rep_assert_int_equal(search_rightmost_unset(0xffff, 8, 0x1), -1);

  rep_assert_int_equal(search_rightmost_unset(0xff, 8, 0x1), -1);
  rep_assert_int_equal(search_rightmost_unset(0xff, 16, 0x1), 8);
  rep_assert_int_equal(search_rightmost_unset(0xffff, 16, 0x1), -1);
  rep_assert_int_equal(search_rightmost_unset(0xffff, 31, 0x1), 16);
  rep_assert_int_equal(search_rightmost_unset(0xffffffff, 31, 0x1), -1);

  // test with mask = 0x3
  rep_assert_int_equal(search_rightmost_unset(0x0, 8, 0x3), 0);
  rep_assert_int_equal(search_rightmost_unset(0x2, 8, 0x3), 2);
  rep_assert_int_equal(search_rightmost_unset(0x1, 8, 0x3), 1);
  rep_assert_int_equal(search_rightmost_unset(0xfcef, 31, 0x3), 8);
  rep_assert_int_equal(search_rightmost_unset(0xffef, 8, 0x3), -1);
  rep_assert_int_equal(search_rightmost_unset(0xffff, 8, 0x3), -1);

  // test with invalid arguments
  expect_assert_failure(search_rightmost_unset(0x0, 36, 0x1));
  expect_assert_failure(search_rightmost_unset(0x0, 16, 0x5));
}

static void test_get_free_region_idx()
{
  region_def_bitmap = 0x20;
  rep_assert_int_equal(get_free_region_idx(), 0);
  region_def_bitmap = 0x3f;
  rep_assert_int_equal(get_free_region_idx(), 6);

  // tear down
  region_def_bitmap = 0x0;
}

static void test_get_free_reg_idx()
{
  reg_bitmap = 0x20;
  rep_assert_int_equal(get_free_reg_idx(), 0);
  reg_bitmap = 0x3f;
  rep_assert_int_equal(get_free_reg_idx(), 6);

  // tear down
  reg_bitmap = 0x0;
}

static void test_get_conseq_free_reg_idx()
{
  reg_bitmap = 0x1;
  rep_assert_int_equal(get_conseq_free_reg_idx(), 0x1);
  reg_bitmap = 0x2e;
  rep_assert_int_equal(get_conseq_free_reg_idx(), 6);
  reg_bitmap = 0xffffffff;
  rep_assert_int_equal(get_conseq_free_reg_idx(), -1);

  // tear down
  reg_bitmap = 0x0;
}

static void test_pmp_region_init_and_free()
{
  rep_assert_int_equal(region_def_bitmap, 0x0);
  region_id rid;
  // region from 0x8000 - 0xc000
  pmp_region_init_atomic(0x8000, 0x4000, PMP_PRI_TOP, &rid, false);

  rep_assert_int_equal(region_def_bitmap, 0x1);
  rep_assert_int_equal(reg_bitmap, 0x1);
  rep_assert_int_equal(rid, 0);
  rep_assert_int_equal(regions[0].size, 0x4000);
  rep_assert_int_equal(regions[0].addrmode, PMP_A_NAPOT);
  rep_assert_int_equal(regions[0].addr, 0x8000);
  rep_assert_int_equal(regions[0].allow_overlap, false);
  rep_assert_int_equal(regions[0].reg_idx, 0);

  pmp_region_free_atomic(rid);

  rep_assert_int_equal(region_def_bitmap, 0x0);
  rep_assert_int_equal(reg_bitmap, 0x0);
  struct pmp_region zero = {0};
  
  assert_memory_equal(&regions[0], &zero, sizeof(struct pmp_region));
}

static void test_pmp_region_init_not_page_granularity()
{
  region_id rid;
  rep_assert_int_equal(region_def_bitmap, 0x0);
  rep_assert_int_equal(reg_bitmap, 0x0);
  struct pmp_region zero = {0};
  assert_memory_equal(&regions[0], &zero, sizeof(struct pmp_region));

  assert_int_not_equal(
      pmp_region_init_atomic(0xdeadbeef,
                             0x4000,
                             PMP_PRI_TOP,
                             &rid,
                             false),
      PMP_SUCCESS);

  assert_int_not_equal(
      pmp_region_init_atomic(0xdeadb000,
                             0x4444,
                             PMP_PRI_TOP,
                             &rid,
                             false),
      PMP_SUCCESS);

  rep_assert_int_equal(region_def_bitmap, 0x0);
  rep_assert_int_equal(reg_bitmap, 0x0);
  assert_memory_equal(&regions[0], &zero, sizeof(struct pmp_region));
}

static void test_pmp_region_init_tor_pri_top()
{
  rep_assert_int_equal(region_def_bitmap, 0x0);
  region_id rid;

  // TOP TOR must start from 0, so this should fail
  assert_int_not_equal(
      pmp_region_init_atomic(0xdeadb000,
                             0x4000,
                             PMP_PRI_TOP,
                             &rid,
                             false),
      PMP_SUCCESS);

  rep_assert_int_equal(region_def_bitmap, 0x0);
  rep_assert_int_equal(reg_bitmap, 0x0);
  struct pmp_region zero = {0};
  assert_memory_equal(&regions[0], &zero, sizeof(struct pmp_region));

  // This should succeed
  rep_assert_int_equal(
      pmp_region_init_atomic(0x0,
                             0xdead000,
                             PMP_PRI_TOP,
                             &rid,
                             false),
      PMP_SUCCESS);

  rep_assert_int_equal(region_def_bitmap, 0x1);
  rep_assert_int_equal(reg_bitmap, 0x1);
  rep_assert_int_equal(rid, 0);
  rep_assert_int_equal(regions[0].size, 0xdead000);
  rep_assert_int_equal(regions[0].addrmode, PMP_A_TOR);
  rep_assert_int_equal(regions[0].addr, 0x0);
  rep_assert_int_equal(regions[0].allow_overlap, false);
  rep_assert_int_equal(regions[0].reg_idx, 0);

  pmp_region_free_atomic(rid);
}

static void test_region_helpers()
{
  int rid = 4;
  region_init(rid,
              0xdeadbeef,
              0xfeed,
              PMP_A_TOR,
              true,
              5);

  rep_assert_int_equal(region_register_idx(rid), 6);
  assert_true(region_allows_overlap(rid));
  rep_assert_int_equal(region_get_addr(rid), 0xdeadbeef);
  rep_assert_int_equal(region_get_size(rid), 0xfeed);
  assert_false(region_is_napot(rid));
  assert_true(region_is_tor(rid));
  assert_true(region_needs_two_entries(rid));
  assert_false(region_is_napot_all(rid));
  rep_assert_int_equal(region_pmpaddr_val(rid), 0x37ABAF77);

  region_clear_all(rid);

  struct pmp_region zero = {0};
  assert_memory_equal(&regions[rid], &zero, sizeof(struct pmp_region));

  rid = 7;
  region_init(rid,
              0x8000,
              0x4000,
              PMP_A_NAPOT,
              false,
              0);

  rep_assert_int_equal(region_register_idx(rid), 0);
  assert_false(region_allows_overlap(rid));
  rep_assert_int_equal(region_get_addr(rid), 0x8000);
  rep_assert_int_equal(region_get_size(rid), 0x4000);
  assert_true(region_is_napot(rid));
  assert_false(region_is_tor(rid));
  assert_false(region_needs_two_entries(rid));
  assert_false(region_is_napot_all(rid));
  rep_assert_int_equal(region_pmpaddr_val(rid), 0x27ff);

  region_clear_all(rid);

  assert_memory_equal(&regions[rid], &zero, sizeof(struct pmp_region));
}

int main()
{
  const struct CMUnitTest tests[] = {
    cmocka_unit_test(test_search_rightmost_unset),
    cmocka_unit_test(test_get_free_region_idx),
    cmocka_unit_test(test_get_free_reg_idx),
    cmocka_unit_test(test_get_conseq_free_reg_idx),
    cmocka_unit_test(test_pmp_region_init_and_free),
    cmocka_unit_test(test_pmp_region_init_not_page_granularity),
    cmocka_unit_test(test_pmp_region_init_tor_pri_top),
    cmocka_unit_test(test_region_helpers),
  };

  return cmocka_run_group_tests(tests, NULL, NULL);
}
