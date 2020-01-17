/*
 * Compile with ts-fpga.a, which was built with ecl Common Lisp via:
 *    (asdf:operate 'asdf:monolithic-lib-op :ts-fpga)
 *
 * gcc -DTESTBENCH -o test frac-clk-gen.c ts-fpga.a -lecl
 * strip test
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <ecl/ecl.h>

static int lisp_booted = 0;
static cl_object frac_clk_gen_sym;

static void lisp_init(void) {
  char *x = NULL;
  if (lisp_booted == 0) {
    extern void init_lib_RATMATH(cl_object);
    //extern void init_lib_TS_FPGA__ALL_SYSTEMS(cl_object);
    cl_boot(0, &x);
    ecl_init_module(NULL, init_lib_RATMATH);
    //ecl_init_module(NULL, init_lib_TS_FPGA__ALL_SYSTEMS);
    atexit(cl_shutdown);
    frac_clk_gen_sym = ecl_make_symbol("FRAC-CLK-GEN", "RATMATH");
    //frac_clk_gen_sym = ecl_make_symbol("FRAC-CLK-GEN", "TS-FPGA");
    assert (t_symbol == ecl_t_of(frac_clk_gen_sym));
    lisp_booted = 1;
  }
}

uint32_t frac_clk_gen(uint64_t freq) {
  cl_object ret;
  lisp_init();

  ret = cl_funcall(2, frac_clk_gen_sym, ecl_make_fixnum(freq));
  assert (t_fixnum == ecl_t_of(ret));
  return (fix(ret));
}

uint32_t frac_clk_genf(double freq) {
  cl_object ret;
  lisp_init();

  ret = cl_funcall(2, frac_clk_gen_sym, ecl_make_double_float(freq));
  assert (t_fixnum == ecl_t_of(ret));
  return (fix(ret));
}

#ifdef TESTBENCH
int main(int argc, char **argv) {
	printf("Register for 1.8432Mhz baud clk: 0x%x\n", frac_clk_gen(1843200*16));
	printf("Register for 1.8432Mhz baud clk: 0x%x (using floats)\n", frac_clk_genf(1.8432e6*16.0));
}
#endif
