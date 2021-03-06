/*
 * Copyright (C) 2008-2015 Ole André Vadla Ravnås <ole.andre.ravnas@tillitech.com>
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "gumsymbolutil.h"

#include "gum-init.h"
#include "gumdbghelp.h"

#include <psapi.h>

/* HACK: don't have access to this enum */
#define GUM_SymTagFunction       5
#define GUM_SymTagPublicSymbol  10

typedef struct _GumSymbolInfo GumSymbolInfo;

#ifdef _MSC_VER
#pragma pack(push)
#pragma pack(1)
#else
#error Fix this for other compilers
#endif

struct _GumSymbolInfo
{
  SYMBOL_INFO sym_info;
  gchar sym_name_buf[GUM_MAX_SYMBOL_NAME + 1];
};

#ifdef _MSC_VER
#pragma pack(pop)
#endif

static gpointer do_init (gpointer data);
static void do_deinit (void);

static BOOL CALLBACK enum_functions_callback (SYMBOL_INFO * sym_info,
    gulong symbol_size, gpointer user_context);
static gboolean is_function (SYMBOL_INFO * sym_info);

static GumDbgHelpImpl *
gum_symbol_util_try_get_dbghelp (void)
{
  static GOnce init_once = G_ONCE_INIT;

  g_once (&init_once, do_init, NULL);

  return init_once.retval;
}

static gpointer
do_init (gpointer data)
{
  GumDbgHelpImpl * dbghelp;

  (void) data;

  dbghelp = gum_dbghelp_impl_obtain ();
  if (dbghelp == NULL)
    return NULL;

  dbghelp->SymInitialize (GetCurrentProcess (), NULL, TRUE);

  _gum_register_destructor (do_deinit);

  return dbghelp;
}

static void
do_deinit (void)
{
  GumDbgHelpImpl * dbghelp;

  dbghelp = gum_symbol_util_try_get_dbghelp ();
  g_assert (dbghelp != NULL);

  dbghelp->SymCleanup (GetCurrentProcess ());

  gum_dbghelp_impl_release (dbghelp);
}

gboolean
gum_symbol_details_from_address (gpointer address,
                                 GumDebugSymbolDetails * details)
{
  GumDbgHelpImpl * dbghelp;
  GumSymbolInfo si = { 0, };
  IMAGEHLP_LINE64 li = { 0, };
  DWORD displacement_dw;
  DWORD64 displacement_qw;
  BOOL has_sym_info, has_file_info;

  dbghelp = gum_symbol_util_try_get_dbghelp ();
  if (dbghelp == NULL)
    return FALSE;

  memset (details, 0, sizeof (GumDebugSymbolDetails));
  details->address = GUM_ADDRESS (address);

  si.sym_info.SizeOfStruct = sizeof (SYMBOL_INFO);
  si.sym_info.MaxNameLen = sizeof (si.sym_name_buf);

  li.SizeOfStruct = sizeof (li);

  dbghelp->Lock ();

  has_sym_info = dbghelp->SymFromAddr (GetCurrentProcess (),
      (DWORD64) address, &displacement_qw, &si.sym_info);
  if (has_sym_info)
  {
    HMODULE mod = GSIZE_TO_POINTER (si.sym_info.ModBase);

    GetModuleBaseNameA (GetCurrentProcess (), mod, details->module_name,
        sizeof (details->module_name) - 1);
    strcpy_s (details->symbol_name, sizeof (details->symbol_name),
        si.sym_info.Name);
  }

  has_file_info = dbghelp->SymGetLineFromAddr64 (GetCurrentProcess (),
      (DWORD64) address, &displacement_dw, &li);
  if (has_file_info)
  {
    strcpy_s (details->file_name, sizeof (details->file_name), li.FileName);
    details->line_number = li.LineNumber;
  }

  dbghelp->Unlock ();

  return (has_sym_info || has_file_info);
}

gchar *
gum_symbol_name_from_address (gpointer address)
{
  GumDebugSymbolDetails details;

  if (gum_symbol_details_from_address (address, &details))
    return g_strdup (details.symbol_name);
  else
    return NULL;
}

gpointer
gum_find_function (const gchar * name)
{
  gpointer result = NULL;
  GArray * matches;

  matches = gum_find_functions_matching (name);
  if (matches->len >= 1)
    result = g_array_index (matches, gpointer, 0);
  g_array_free (matches, TRUE);

  return result;
}

GArray *
gum_find_functions_named (const gchar * name)
{
  return gum_find_functions_matching (name);
}

GArray *
gum_find_functions_matching (const gchar * str)
{
  GArray * matches;
  GumDbgHelpImpl * dbghelp;
  gchar * match_formatted_str;
  HANDLE cur_process_handle;
  guint64 any_module_base;

  matches = g_array_new (FALSE, FALSE, sizeof (gpointer));

  dbghelp = gum_symbol_util_try_get_dbghelp ();
  if (dbghelp == NULL)
    return matches;

  match_formatted_str = g_strdup_printf ("*!%s", str);

  cur_process_handle = GetCurrentProcess ();
  any_module_base = 0;

  dbghelp->Lock ();
  dbghelp->SymEnumSymbols (cur_process_handle, any_module_base,
      match_formatted_str, enum_functions_callback, matches);
  dbghelp->Unlock ();

  g_free (match_formatted_str);

  return matches;
}

static BOOL CALLBACK
enum_functions_callback (SYMBOL_INFO * sym_info,
                         gulong symbol_size,
                         gpointer user_context)
{
  GArray * result = user_context;

  (void) symbol_size;

  if (is_function (sym_info))
  {
    gpointer address = GSIZE_TO_POINTER (sym_info->Address);
    g_array_append_val (result, address);
  }

  return TRUE;
}

static gboolean
is_function (SYMBOL_INFO * sym_info)
{
  gboolean result;

  switch (sym_info->Tag)
  {
    case GUM_SymTagFunction:
    case GUM_SymTagPublicSymbol:
      result = TRUE;
      break;
    default:
      result = FALSE;
      break;
  }

  return result;
}
