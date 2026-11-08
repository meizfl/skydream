/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include "rt.h"
#include "skse_compat.h"
#ifdef SKYDREAM_SKSE
int skse_skyui_enabled(void);
uint32_t skse_member(CPU *,uint32_t,uint32_t,int,const uint32_t *);
uint32_t skse_virtual(CPU *,uint32_t,unsigned,int,const uint32_t *);
void skse_gfx_install(CPU *,uint32_t);
void skse_import_translations(CPU *);
uint32_t skse_ini_setting(CPU *,const char *);
void skse_gfx_clean(CPU *,uint32_t);
void skse_ui_pump(void);
void skse_input_install(CPU *);
void skse_gfx_input_event(CPU *,unsigned,const char *,int);
void skse_send_mod_event(CPU *,const char *,const char *,float,uint32_t);
void skse_gfx_read(CPU *,uint32_t,const char *,uint32_t);
void skse_gfx_number(CPU *,uint32_t,const char *,double);
uint32_t skse_menu_view(const char *);
int skse_system_run(CPU *,const SkseNative *,uint32_t,const uint32_t *,uint32_t,uint32_t);
#endif
