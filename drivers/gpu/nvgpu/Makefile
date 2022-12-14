GCOV_PROFILE := y

ccflags-y += -Idrivers/gpu/nvgpu/include
ccflags-y += -Idrivers/video/tegra/host
ccflags-y += -Idrivers/devfreq
ccflags-y += -Idrivers/gpu/nvgpu/include
ccflags-y += -Idrivers/gpu/nvgpu
ccflags-y += -Idrivers/gpu/nvgpu/sync
ccflags-y += -Wno-multichar
ccflags-y += -Werror
ccflags-y += -Wno-error=cpp
ccflags-y += -Wno-error=sign-compare
ccflags-y += -Wno-error=uninitialized
ccflags-y += -Wno-error=unused-variable
ifeq ($(VERSION),4)
ccflags-y += -Wextra -Wno-unused-parameter -Wno-missing-field-initializers
endif

obj-$(CONFIG_GK20A) := nvgpu.o

nvgpu-y := \
	common/linux/kmem.o \
	common/linux/timers.o \
	common/mm/nvgpu_allocator.o \
	common/mm/bitmap_allocator.o \
	common/mm/buddy_allocator.o \
	common/mm/page_allocator.o \
	common/mm/lockless_allocator.o \
	common/nvgpu_common.o \
	common/semaphore.o \
	common/vbios/bios.o \
	gk20a/gk20a.o \
	gk20a/sched_gk20a.o \
	gk20a/as_gk20a.o \
	gk20a/ctrl_gk20a.o \
	gk20a/ce2_gk20a.o \
	gk20a/fifo_gk20a.o \
	gk20a/channel_gk20a.o \
	gk20a/channel_sync_gk20a.o \
	gk20a/debug_gk20a.o \
	gk20a/dbg_gpu_gk20a.o \
	gk20a/regops_gk20a.o \
	gk20a/gr_gk20a.o \
	gk20a/kind_gk20a.o \
	gk20a/mm_gk20a.o \
	gk20a/pmu_gk20a.o \
	gk20a/priv_ring_gk20a.o \
	gk20a/fence_gk20a.o \
	gk20a/therm_gk20a.o \
	gk20a/gr_ctx_gk20a_sim.o \
	gk20a/gr_ctx_gk20a.o \
	gk20a/gk20a_gating_reglist.o \
	gk20a/gk20a_sysfs.o \
	gk20a/ltc_gk20a.o \
	gk20a/fb_gk20a.o \
	gk20a/hal.o \
	gk20a/hal_gk20a.o \
	gk20a/cde_gk20a.o \
	gk20a/platform_gk20a_generic.o \
	gk20a/tsg_gk20a.o \
	gk20a/ctxsw_trace_gk20a.o \
	gk20a/fecs_trace_gk20a.o \
	gk20a/mc_gk20a.o \
	gm20b/hal_gm20b.o \
	gm20b/ltc_gm20b.o \
	gm20b/ce2_gm20b.o \
	gm20b/gr_gm20b.o \
	gm20b/gr_gm20b.o \
	gm20b/fb_gm20b.o \
	gm20b/fifo_gm20b.o \
	gm20b/gr_ctx_gm20b.o \
	gm20b/gm20b_gating_reglist.o \
	gm20b/acr_gm20b.o \
	gm20b/pmu_gm20b.o \
	gm20b/mm_gm20b.o \
	gm20b/regops_gm20b.o \
	gm20b/mc_gm20b.o \
	gm20b/debug_gm20b.o \
	gm20b/cde_gm20b.o \
	gm20b/therm_gm20b.o \
	gm206/bios_gm206.o \
	boardobj/boardobj.o \
	boardobj/boardobjgrp.o \
	boardobj/boardobjgrpmask.o \
	boardobj/boardobjgrp_e255.o \
	boardobj/boardobjgrp_e32.o

nvgpu-y += gk20a/platform_gk20a_tegra.o
nvgpu-y += sync/sync.o
nvgpu-y += gk20a/sync_gk20a.o
nvgpu-y += pci.o

nvgpu-$(CONFIG_TEGRA_CLK_FRAMEWORK) += gk20a/clk_gk20a.o
ifneq (,$filter y,$(CONFIG_TEGRA_CLK_FRAMEWORK),$(CONFIG_COMMON_CLK))
nvgpu-y += \
	clk/clk_common.o \
	gm20b/clk_gm20b.o
endif

nvgpu-$(CONFIG_GK20A_DEVFREQ) += \
	gk20a/gk20a_scale.o

nvgpu-$(CONFIG_GK20A_CYCLE_STATS) += \
	gk20a/css_gr_gk20a.o

nvgpu-y += \
	gp10b/gr_gp10b.o  \
	gp10b/gr_ctx_gp10b.o  \
	gp10b/ce_gp10b.o \
	gp10b/mc_gp10b.o  \
	gp10b/fifo_gp10b.o  \
	gp10b/ltc_gp10b.o \
	gp10b/mm_gp10b.o \
	gp10b/fb_gp10b.o \
	gp10b/pmu_gp10b.o \
	gp10b/hal_gp10b.o \
	gp10b/rpfb_gp10b.o \
	gp10b/gp10b_gating_reglist.o \
	gp10b/regops_gp10b.o \
	gp10b/cde_gp10b.o \
	gp10b/therm_gp10b.o \
	gp10b/fecs_trace_gp10b.o \
	gp10b/gp10b_sysfs.o \
	gp10b/gp10b.o \
	gp106/hal_gp106.o \
	gp106/mm_gp106.o \
	gp106/pmu_gp106.o \
	gp106/gr_gp106.o \
	gp106/gr_ctx_gp106.o \
	gp106/acr_gp106.o \
	gp106/sec2_gp106.o \
	gp106/fifo_gp106.o \
	gp106/ltc_gp106.o \
	gp106/fb_gp106.o \
	gp106/regops_gp106.o \
	clk/clk_mclk.o \
	pstate/pstate.o \
	clk/clk_vin.o \
	clk/clk_fll.o \
	clk/clk_domain.o \
	clk/clk_prog.o \
	clk/clk_vf_point.o \
	clk/clk_arb.o \
	clk/clk_freq_controller.o \
	perf/vfe_var.o \
	perf/vfe_equ.o \
	perf/perf.o \
	clk/clk.o \
	gp106/clk_gp106.o \
	gp106/clk_arb_gp106.o \
	gp106/gp106_gating_reglist.o \
	gp106/xve_gp106.o \
	gp106/therm_gp106.o \
	gp106/xve_gp106.o \
	pmgr/pwrdev.o \
	pmgr/pmgr.o \
	pmgr/pmgrpmu.o \
	pmgr/pwrmonitor.o \
	pmgr/pwrpolicy.o \
	volt/volt_rail.o \
	volt/volt_dev.o \
	volt/volt_policy.o \
	volt/volt_pmu.o \
	therm/thrm.o \
	therm/thrmdev.o \
	therm/thrmchannel.o \
	therm/thrmpmu.o \
	lpwr/rppg.o \
	lpwr/lpwr.o

nvgpu-y += gp10b/platform_gp10b_tegra.o
