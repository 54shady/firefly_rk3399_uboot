/*
 * (C) Copyright 2017 Rockchip Electronics Co., Ltd
 *
 * SPDX-License-Identifier:     GPL-2.0+
 */

#include <common.h>
#include <command.h>
#include "test-rockchip.h"

typedef struct board_module {
	char *name;
	int (*test)(int argc, char * const argv[]);
} board_module_t;

static board_module_t g_board_modules[] = {
	{ .name = "timer",	.test = board_timer_test },
	{ .name = "key",	.test = board_key_test },
	{ .name = "emmc",	.test = board_emmc_test },
	{ .name = "regulator",	.test = board_regulator_test },
	{ .name = "rknand",	.test = board_rknand_test },
#if defined(CONFIG_GMAC_ROCKCHIP)
	{ .name = "eth",        .test = board_eth_test },
#endif
#if defined(CONFIG_RK_IR)
	{ .name = "ir",		.test = board_ir_test },
#endif
	{ .name = "brom_dnl",	.test = board_brom_dnl_test}
};

static int do_rockchip_test(cmd_tbl_t *cmdtp, int flag,
			    int argc, char * const argv[])
{
	ulong ms_start = 0, ms = 0, sec = 0;
	board_module_t *module = NULL;
	char *module_name = NULL;
	int index = 0, err = 0;
	bool found = false;

	if (argc >= 2) {
		module_name = argv[1];
	} else {
		printf("cmd format: test_rockchip [module_name] [args...]\n");
		return 0;
	}

	if (!module_name)
		return 0;


	for (index = 0; index < ARRAY_SIZE(g_board_modules); index++) {
		module = &g_board_modules[index];
		if (module && !strcmp(module->name, module_name)) {
			found = true;

			printf("***********************************************************\n");
			printf("Rockchip Board Module [%s] Test start.\n", module_name);
			printf("***********************************************************\n");

			ms_start = get_timer(0);

			err = module->test(argc, argv);

			ms = get_timer(ms_start);
			if (ms >= 1000) {
				sec = ms / 1000;
				ms = ms % 1000;
			}

			printf("-----------------------------------------------------------\n");
			printf("Rockchip Board Module [%s] Test end <%s>.. Total: %lu.%lus\n",
			       module->name, err ? "FAILED" : "PASS", sec, ms);
			printf("-----------------------------------------------------------\n\n\n");
		}
	}

	if (!found)
		printf("Rockchip Board Module [%s] is not support !\n",
		       module_name);

	return 0;
}

U_BOOT_CMD(
	rktest, 10, 0, do_rockchip_test,
	"Rockchip Board Module Test",
	""
);
