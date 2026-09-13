/*
 *      DirectX File private interfaces (D3DXOF.DLL)
 *
 * Copyright 2004, 2008 Christian Costa
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 *
 * --------------------------------------------------------------------
 * Adapted from Wine's dlls/d3dxof/d3dxof_private.h: IDirectXFileImpl,
 * the COM template-registry object, is reduced to XFileTemplates, a
 * plain struct holding only the two fields xfile_parse.c reads through
 * pdxf (xtemplates[]/nb_xtemplates) - see VERSION for the rest of what
 * changed and why.
 */
#ifndef __XFILE_PRIVATE_INCLUDED__
#define __XFILE_PRIVATE_INCLUDED__

#include "xfile_compat.h"

#define MAX_NAME_LEN 40
#define MAX_ARRAY_DIM 4
#define MAX_MEMBERS 50
#define MAX_CHILDREN 200
#define MAX_TEMPLATES 200
#define MAX_OBJECTS 500
#define MAX_SUBOBJECTS 2000
#define MAX_STRINGS_BUFFER 10000

typedef struct
{
    DWORD type;
    LONG idx_template;
    char name[MAX_NAME_LEN];
    ULONG nb_dims;
    BOOL dim_fixed[MAX_ARRAY_DIM];
    ULONG dim_value[MAX_ARRAY_DIM];
} member;

typedef struct
{
    char name[MAX_NAME_LEN];
    GUID class_id;
    BOOL open;
    BOOL binary;
    ULONG nb_children;
    char children[MAX_CHILDREN][MAX_NAME_LEN];
    ULONG nb_members;
    member members[MAX_MEMBERS];
} xtemplate;

typedef struct
{
    char *name;
    ULONG start;
    ULONG size;
} xobject_member;

struct _xobject
{
    BOOL binary;
    struct _xobject *ptarget;
    char name[MAX_NAME_LEN];
    GUID class_id;
    GUID type;
    LPBYTE pdata;
    ULONG pos_data;
    DWORD size;
    ULONG nb_members;
    xobject_member members[MAX_MEMBERS];
    ULONG nb_children;
    ULONG nb_subobjects;
    struct _xobject *children[MAX_CHILDREN];
    struct _xobject *root;
};

typedef struct _xobject xobject;

/* stands in for Wine's IDirectXFileImpl: xfile_parse.c only ever reads
   buf->pdxf->xtemplates[...] and buf->pdxf->nb_xtemplates */
typedef struct
{
    ULONG nb_xtemplates;
    xtemplate xtemplates[MAX_TEMPLATES];
} XFileTemplates;

typedef struct
{
    LPBYTE buffer;
    DWORD rem_bytes;
    WORD current_token;
    BOOL token_present;
    BOOL txt;
    DWORD list_nb_elements;
    BOOL list_type_float;
    BOOL list_separator;
    ULONG cur_pos_data;
    LPBYTE cur_pstrings;
    BYTE value[512];
    xobject **pxo_globals;
    ULONG nb_pxo_globals;
    xobject *pxo_tab;
    XFileTemplates *pdxf;
    xobject *pxo;
    xtemplate *pxt[MAX_SUBOBJECTS];
    ULONG level;
    LPBYTE pdata;
    ULONG capacity;
    LPBYTE pstrings;
} parse_buffer;

#define XFILE_TOKEN_NONE 0

XFILE_HRESULT xfile_parse_header(parse_buffer *buf, BYTE **decomp_buffer_ptr);
BOOL xfile_parse_object(parse_buffer *buf);
BOOL xfile_parse_templates(parse_buffer *buf, BOOL templates_only);
/* peeks (does not consume) the next token; XFILE_TOKEN_NONE means the
   buffer is exhausted - the condition for "no more top-level objects" */
WORD xfile_check_token(parse_buffer *buf);

int xfile_mszip_decompress(int inlen, int outlen, char *inbuffer, char *outbuffer);

/* xfile_parse.c's parse_template_members_list resolves a member whose
   type names another template by searching xtemplates[1..nb_xtemplates)
   - index 0 is permanently reserved for the case-sensitive legacy type
   "indexColor" (see d3dxof.c's IDirectXFileImpl_Create). Call this once
   on a freshly-zeroed XFileTemplates before registering any real
   templates, or every "array SomeTemplate member[n]" fails to resolve. */
static inline void xfile_reserve_index_color(XFileTemplates *t)
{
    t->nb_xtemplates = 1;
    strcpy(t->xtemplates[0].name, "indexColor");
    t->xtemplates[0].nb_members = 2;
    t->xtemplates[0].members[0].type = 41; /* TOKEN_DWORD */
    t->xtemplates[0].members[0].nb_dims = 0;
    t->xtemplates[0].members[1].type = 42; /* TOKEN_FLOAT */
    t->xtemplates[0].members[1].nb_dims = 1;
    t->xtemplates[0].members[1].dim_fixed[0] = TRUE;
    t->xtemplates[0].members[1].dim_value[0] = 4;
}

#endif /* __XFILE_PRIVATE_INCLUDED__ */
