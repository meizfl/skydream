/* SPDX-License-Identifier: GPL-3.0-only
 * Independent implementation of a subset of the classic Papyrus extension API.
 * No SKSE source or binary code is linked into this file. */
#include "skse_compat.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <limits.h>

enum { VER, MINOR, BETA, RELEASE, PLUGIN, SHL, SHR, AND, OR, XOR, NOT, LN,
       LENGTH, NTH, LETTER, DIGIT, PUNCT, PRINT, ORD, CHAR, FIND, SUBSTR,
       ALPHA, RED, GREEN, BLUE, HUE, SATURATION, VALUE,
       SET_ALPHA, SET_RED, SET_GREEN, SET_BLUE, SET_HUE, SET_SAT, SET_VALUE };
#define F(c,n,r,k,a,b,d,e,op) {c,n,r,k,{a,b,d,e},op}
static const SkseNative natives[] = {
    /* Report our incomplete compatibility version, never pretend to be 1.7.3. */
    F("SKSE","GetVersion",3,0,0,0,0,0,VER),
    F("SKSE","GetVersionMinor",3,0,0,0,0,0,MINOR),
    F("SKSE","GetVersionBeta",3,0,0,0,0,0,BETA),
    F("SKSE","GetVersionRelease",3,0,0,0,0,0,RELEASE),
    F("SKSE","GetPluginVersion",3,1,2,0,0,0,PLUGIN),
    F("Math","LeftShift",3,2,3,3,0,0,SHL),
    F("Math","RightShift",3,2,3,3,0,0,SHR),
    F("Math","LogicalAnd",3,2,3,3,0,0,AND),
    F("Math","LogicalOr",3,2,3,3,0,0,OR),
    F("Math","LogicalXor",3,2,3,3,0,0,XOR),
    F("Math","LogicalNot",3,1,3,0,0,0,NOT),
    F("Math","Log",4,1,4,0,0,0,LN),
    F("StringUtil","GetLength",3,1,2,0,0,0,LENGTH),
    F("StringUtil","GetNthChar",2,2,2,3,0,0,NTH),
    F("StringUtil","IsLetter",5,1,2,0,0,0,LETTER),
    F("StringUtil","IsDigit",5,1,2,0,0,0,DIGIT),
    F("StringUtil","IsPunctuation",5,1,2,0,0,0,PUNCT),
    F("StringUtil","IsPrintable",5,1,2,0,0,0,PRINT),
    F("StringUtil","AsOrd",3,1,2,0,0,0,ORD),
    F("StringUtil","AsChar",2,1,3,0,0,0,CHAR),
    F("StringUtil","Find",3,3,2,2,3,0,FIND),
    F("StringUtil","Substring",2,3,2,3,3,0,SUBSTR),
    F("ColorComponent","GetAlpha",3,1,3,0,0,0,ALPHA),
    F("ColorComponent","GetRed",3,1,3,0,0,0,RED),
    F("ColorComponent","GetGreen",3,1,3,0,0,0,GREEN),
    F("ColorComponent","GetBlue",3,1,3,0,0,0,BLUE),
    F("ColorComponent","GetHue",4,1,3,0,0,0,HUE),
    F("ColorComponent","GetSaturation",4,1,3,0,0,0,SATURATION),
    F("ColorComponent","GetValue",4,1,3,0,0,0,VALUE),
    F("ColorComponent","SetAlpha",3,2,3,3,0,0,SET_ALPHA),
    F("ColorComponent","SetRed",3,2,3,3,0,0,SET_RED),
    F("ColorComponent","SetGreen",3,2,3,3,0,0,SET_GREEN),
    F("ColorComponent","SetBlue",3,2,3,3,0,0,SET_BLUE),
    F("ColorComponent","SetHue",3,2,3,4,0,0,SET_HUE),
    F("ColorComponent","SetSaturation",3,2,3,4,0,0,SET_SAT),
    F("ColorComponent","SetValue",3,2,3,4,0,0,SET_VALUE),

    F("Form","GetType",3,0,0,0,0,0,S_METHOD|1300),
    F("UI","IsMenuOpen",5,1,2,0,0,0,S_UI_OPEN),
    F("UI","GetBool",5,2,2,2,0,0,S_UI_GBOOL),
    F("UI","SetBool",0,3,2,2,5,0,S_UI_SBOOL),
    F("UI","InvokeBool",0,3,2,2,5,0,S_UI_IBOOL),
    F("UI","InvokeBoolA",0,3,2,2,15,0,S_UI_ABOOL),
    F("UI","GetInt",3,2,2,2,0,0,S_UI_GINT),
    F("UI","SetInt",0,3,2,2,3,0,S_UI_SINT),
    F("UI","InvokeInt",0,3,2,2,3,0,S_UI_IINT),
    F("UI","InvokeIntA",0,3,2,2,13,0,S_UI_AINT),
    F("UI","GetFloat",4,2,2,2,0,0,S_UI_GFLOAT),
    F("UI","SetFloat",0,3,2,2,4,0,S_UI_SFLOAT),
    F("UI","InvokeFloat",0,3,2,2,4,0,S_UI_IFLOAT),
    F("UI","InvokeFloatA",0,3,2,2,14,0,S_UI_AFLOAT),
    F("UI","GetString",2,2,2,2,0,0,S_UI_GSTRING),
    F("UI","SetString",0,3,2,2,2,0,S_UI_SSTRING),
    F("UI","InvokeString",0,3,2,2,2,0,S_UI_ISTRING),
    F("UI","InvokeStringA",0,3,2,2,12,0,S_UI_ASTRING),
    F("UI","IsTextInputEnabled",5,0,0,0,0,0,S_UI_TEXT),
    F("Form","RegisterForMenu",0,1,2,0,0,0,S_METHOD|S_REG_MENU),
    F("Form","UnregisterForMenu",0,1,2,0,0,0,S_METHOD|S_UNREG_MENU),
    F("Form","UnregisterForAllMenus",0,0,0,0,0,0,S_METHOD|S_UNREG_MENUS),
    F("Form","RegisterForKey",0,1,3,0,0,0,S_METHOD|S_REG_KEY),
    F("Form","UnregisterForKey",0,1,3,0,0,0,S_METHOD|S_UNREG_KEY),
    F("Form","UnregisterForAllKeys",0,0,0,0,0,0,S_METHOD|S_UNREG_KEYS),
    F("Form","RegisterForModEvent",0,2,2,2,0,0,S_METHOD|S_REG_MOD),
    F("Form","UnregisterForModEvent",0,1,2,0,0,0,S_METHOD|S_UNREG_MOD),
    F("Form","UnregisterForAllModEvents",0,0,0,0,0,0,S_METHOD|S_UNREG_MODS),
    F("Form","SendModEvent",0,3,2,2,4,0,S_METHOD|S_SEND_MOD),
    F("Alias","RegisterForMenu",0,1,2,0,0,0,S_METHOD|S_REG_MENU),
    F("Alias","UnregisterForMenu",0,1,2,0,0,0,S_METHOD|S_UNREG_MENU),
    F("Alias","UnregisterForAllMenus",0,0,0,0,0,0,S_METHOD|S_UNREG_MENUS),
    F("Alias","RegisterForKey",0,1,3,0,0,0,S_METHOD|S_REG_KEY),
    F("Alias","UnregisterForKey",0,1,3,0,0,0,S_METHOD|S_UNREG_KEY),
    F("Alias","UnregisterForAllKeys",0,0,0,0,0,0,S_METHOD|S_UNREG_KEYS),
    F("Alias","RegisterForModEvent",0,2,2,2,0,0,S_METHOD|S_REG_MOD),
    F("Alias","UnregisterForModEvent",0,1,2,0,0,0,S_METHOD|S_UNREG_MOD),
    F("Alias","UnregisterForAllModEvents",0,0,0,0,0,0,S_METHOD|S_UNREG_MODS),
    F("Alias","SendModEvent",0,3,2,2,4,0,S_METHOD|S_SEND_MOD),
    F("ActiveMagicEffect","RegisterForMenu",0,1,2,0,0,0,S_METHOD|S_REG_MENU),
    F("ActiveMagicEffect","UnregisterForMenu",0,1,2,0,0,0,S_METHOD|S_UNREG_MENU),
    F("ActiveMagicEffect","UnregisterForAllMenus",0,0,0,0,0,0,S_METHOD|S_UNREG_MENUS),
    F("ActiveMagicEffect","RegisterForKey",0,1,3,0,0,0,S_METHOD|S_REG_KEY),
    F("ActiveMagicEffect","UnregisterForKey",0,1,3,0,0,0,S_METHOD|S_UNREG_KEY),
    F("ActiveMagicEffect","UnregisterForAllKeys",0,0,0,0,0,0,S_METHOD|S_UNREG_KEYS),
    F("ActiveMagicEffect","RegisterForModEvent",0,2,2,2,0,0,S_METHOD|S_REG_MOD),
    F("ActiveMagicEffect","UnregisterForModEvent",0,1,2,0,0,0,S_METHOD|S_UNREG_MOD),
    F("ActiveMagicEffect","UnregisterForAllModEvents",0,0,0,0,0,0,S_METHOD|S_UNREG_MODS),
    F("ActiveMagicEffect","SendModEvent",0,3,2,2,4,0,S_METHOD|S_SEND_MOD),
    F("Input","GetMappedControl",2,2,3,3,0,0,S_INPUT_CONTROL),
    F("Game","UsingGamepad",5,0,0,0,0,0,S_GAME_PAD),
    F("Utility","GetINIFloat",4,1,2,0,0,0,S_INI_FLOAT),
    F("Utility","GetINIInt",3,1,2,0,0,0,S_INI_INT),
    F("Utility","SetINIFloat",0,2,2,4,0,0,S_SET_INI_FLOAT),
};
#undef F
const SkseNative *skse_native_table(unsigned *count) {
    *count = sizeof natives / sizeof natives[0];
    return natives;
}
static uint32_t bits(float f) { uint32_t u; memcpy(&u,&f,4); return u; }
static float real(uint32_t u) { float f; memcpy(&f,&u,4); return f; }
static char *slice(const char *s, size_t start, size_t n) {
    char *p = malloc(n+1);
    if (p) { memcpy(p,s+start,n); p[n]=0; }
    return p;
}
static unsigned upper(unsigned x) { return x >= 'a' && x <= 'z' ? x - 32 : x; }
static void hsv(uint32_t color, float *h, float *s, float *v) {
    float r=((color>>16)&255)/255.f, g=((color>>8)&255)/255.f, b=(color&255)/255.f;
    float hi=fmaxf(r,fmaxf(g,b)), lo=fminf(r,fminf(g,b)), d=hi-lo;
    *v=hi; *s=hi ? d/hi : 0; *h=0;
    if (d) {
        *h=60.f*(hi==r ? (g-b)/d : hi==g ? (b-r)/d+2 : (r-g)/d+4);
        if (*h<0) *h+=360.f;
    }
}
static uint32_t from_hsv(uint32_t alpha, float h, float s, float v) {
    if (!isfinite(h)) h=0;
    if (!isfinite(s)) s=0;
    if (!isfinite(v)) v=0;
    h=fmodf(h,360.f); if(h<0) h+=360.f;
    s=fminf(1,fmaxf(0,s)); v=fminf(1,fmaxf(0,v));
    float chroma=v*s, x=chroma*(1-fabsf(fmodf(h/60.f,2)-1)), m=v-chroma;
    float r=0,g=0,b=0;
    switch ((int)(h/60.f)) {
    case 0:r=chroma;g=x;break; case 1:r=x;g=chroma;break;
    case 2:g=chroma;b=x;break; case 3:g=x;b=chroma;break;
    case 4:r=x;b=chroma;break; default:r=chroma;b=x;break;
    }
    return (alpha&0xff000000u) | ((uint32_t)((r+m)*255.f)<<16)
        | ((uint32_t)((g+m)*255.f)<<8) | (uint32_t)((b+m)*255.f);
}
int skse_native_eval(const SkseNative *fn, const uint32_t a[4],
                     const char *strings[4], uint32_t *out, char **text) {
    const char *s=strings[0] ? strings[0] : "";
    const char *needle=strings[1] ? strings[1] : "";
    size_t n=strlen(s), k=0;
    unsigned c=(unsigned char)s[0];
    float h=0,sat=0,v=0;
    *text=NULL; *out=0;
    switch(fn->operation) {
    case VER:case BETA:case RELEASE:break;
    case MINOR:*out=1;break;
    case PLUGIN:*out=UINT32_MAX;break;
    case SHL:*out=a[1]<32 ? a[0]<<a[1] : 0;break;
    case SHR:*out=a[1]<32 ? a[0]>>a[1] : 0;break;
    case AND:*out=a[0]&a[1];break; case OR:*out=a[0]|a[1];break;
    case XOR:*out=a[0]^a[1];break; case NOT:*out=~a[0];break;
    case LN:*out=bits(logf(real(a[0])));break;
    case LENGTH:*out=(uint32_t)n;break;
    case NTH:*text=slice(a[1]<n ? s+a[1] : "",0,a[1]<n ? 1 : 0);break;
    case LETTER:*out=(c>='A'&&c<='Z')||(c>='a'&&c<='z');break;
    case DIGIT:*out=c>='0'&&c<='9';break;
    case PUNCT:*out=c>=33&&c<=126&&!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9'));break;
    case PRINT:*out=c>=32&&c<=126;break;
    case ORD:*out=(uint32_t)(int32_t)(int8_t)c;break;
    case CHAR:{ char tmp[2]={(char)(a[0]<=255 ? a[0] : 0),0}; *text=slice(tmp,0,strlen(tmp));break; }
    case FIND:
        *out=UINT32_MAX; k=strlen(needle);
        if(a[2]<n && k<=n) for(size_t i=a[2];i<=n-k;i++) {
            size_t j=0; while(j<k && upper((unsigned char)s[i+j])==upper((unsigned char)needle[j])) j++;
            if(j==k) { *out=(uint32_t)i;break; }
        }
        break;
    case SUBSTR:
        k=a[1]<n ? n-a[1] : 0;
        if(a[2] && a[2]<k) k=a[2];
        *text=slice(k ? s+a[1] : "",0,k);break;
    case ALPHA:*out=a[0]>>24;break;case RED:*out=(a[0]>>16)&255;break;
    case GREEN:*out=(a[0]>>8)&255;break;case BLUE:*out=a[0]&255;break;
    case HUE:case SATURATION:case VALUE:
        hsv(a[0],&h,&sat,&v); *out=bits(fn->operation==HUE ? h : fn->operation==SATURATION ? sat : v);break;
    case SET_ALPHA:*out=(a[0]&0x00ffffffu)|(a[1]<<24);break;
    case SET_RED:*out=(a[0]&0xff00ffffu)|(a[1]<<16);break;
    case SET_GREEN:*out=(a[0]&0xffff00ffu)|(a[1]<<8);break;
    case SET_BLUE:*out=(a[0]&0xffffff00u)|a[1];break;
    case SET_HUE:case SET_SAT:case SET_VALUE:
        hsv(a[0],&h,&sat,&v);
        if(fn->operation==SET_HUE) h=real(a[1]);
        if(fn->operation==SET_SAT) sat=real(a[1]);
        if(fn->operation==SET_VALUE) v=real(a[1]);
        *out=from_hsv(a[0],h,sat,v);break;
    default:return 0;
    }
    return fn->result!=2 || *text!=NULL;
}
