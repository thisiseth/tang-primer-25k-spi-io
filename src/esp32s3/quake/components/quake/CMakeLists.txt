set(WINQUAKE_SRCS
	winquake/chase.c
	winquake/cmd.c
	winquake/common.c
	winquake/console.c
	winquake/crc.c
	winquake/cvar.c
	winquake/draw.c
	winquake/host.c
	winquake/host_cmd.c
	winquake/keys.c
	winquake/mathlib.c
	winquake/menu.c
	winquake/model.c
	winquake/nonintel.c
	winquake/screen.c
	winquake/sbar.c
	winquake/zone.c
	winquake/view.c
	winquake/wad.c
	winquake/world.c
	winquake/cl_demo.c
	winquake/cl_input.c
	winquake/cl_main.c
	winquake/cl_parse.c
	winquake/cl_tent.c
	winquake/d_edge.c
	winquake/d_fill.c
	winquake/d_init.c
	winquake/d_modech.c
	winquake/d_part.c
	winquake/d_polyse.c
	winquake/d_scan.c
	winquake/d_sky.c
	winquake/d_sprite.c
	winquake/d_surf.c
	winquake/d_vars.c
	winquake/d_zpoint.c
	winquake/net_loop.c
	winquake/net_main.c
	winquake/net_vcr.c
	winquake/pr_cmds.c
	winquake/pr_edict.c
	winquake/pr_exec.c
	winquake/r_aclip.c
	winquake/r_alias.c
	winquake/r_bsp.c
	winquake/r_light.c
	winquake/r_draw.c
	winquake/r_efrag.c
	winquake/r_edge.c
	winquake/r_misc.c
	winquake/r_main.c
	winquake/r_sky.c
	winquake/r_sprite.c
	winquake/r_surf.c
	winquake/r_part.c
	winquake/r_vars.c
	winquake/sv_main.c
	winquake/sv_phys.c
	winquake/sv_move.c
	winquake/sv_user.c

	winquake/cd_null.c
	winquake/net_none.c
	winquake/snd_mem.c)

set(ESP32QUAKE_SRCS
	esp32quake/in_esp32.c
	esp32quake/sys_esp32.c
	esp32quake/vid_esp32.c
	esp32quake/snd_esp32.c)

idf_component_register(SRCS ${WINQUAKE_SRCS} ${ESP32QUAKE_SRCS} quake_main.c fatfs_proxy.c
                    INCLUDE_DIRS "."
					PRIV_INCLUDE_DIRS "winquake" "esp32quake"
                    REQUIRES fpga_driver freertos esp_timer
                    LDFRAGMENTS quake.lf)

target_compile_definitions(${COMPONENT_LIB} PRIVATE ESP32_QUAKE=1)

target_compile_options(${COMPONENT_LIB} PRIVATE 
					   "-Wno-format-overflow" 
					   "-Wno-dangling-else" 
					   "-Wno-missing-field-initializers" 
					   "-Wno-pointer-sign"
					   "-Wno-trigraphs"
					   "-Wno-dangling-pointer"
					   "-fcommon"
					   "-Werror=incompatible-pointer-types")