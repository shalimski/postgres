/*-------------------------------------------------------------------------
 *
 * fmgr.c--
 *	  Interface routines for the table-driven function manager.
 *
 * Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  $Header: /cvsroot/pgsql/src/backend/utils/fmgr/fmgr.c,v 1.21 1999/02/03 21:17:35 momjian Exp $
 *
 *-------------------------------------------------------------------------
 */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "postgres.h"

/* these 2 files are generated by Gen_fmgrtab.sh; contain the declarations */
#include "fmgr.h"
#include "utils/fmgrtab.h"

#include "nodes/pg_list.h"
#include "catalog/pg_proc.h"
#include "catalog/pg_language.h"
#include "utils/syscache.h"
#include "nodes/params.h"

#include "utils/elog.h"

#include "nodes/parsenodes.h"
#include "commands/trigger.h"


static char *
fmgr_pl(char *arg0,...)
{
	va_list		pvar;
	FmgrValues	values;
	bool		isNull = false;
	int			i;

	memset(&values, 0, sizeof(values));

	if (fmgr_pl_finfo->fn_nargs > 0)
	{
		values.data[0] = arg0;
		if (fmgr_pl_finfo->fn_nargs > 1)
		{
			va_start(pvar, arg0);
			for (i = 1; i < fmgr_pl_finfo->fn_nargs; i++)
				values.data[i] = va_arg(pvar, char *);
			va_end(pvar);
		}
	}

	/* Call the PL handler */
	CurrentTriggerData = NULL;
	return (*(fmgr_pl_finfo->fn_plhandler)) (fmgr_pl_finfo,
											 &values,
											 &isNull);
}


char *
fmgr_c(FmgrInfo *finfo,
	   FmgrValues *values,
	   bool *isNull)
{
	char	   *returnValue = (char *) NULL;
	int			n_arguments = finfo->fn_nargs;
	func_ptr	user_fn = fmgr_faddr(finfo);


	if (user_fn == (func_ptr) NULL)
	{

		/*
		 * a NULL func_ptr denotet untrusted function (in postgres 4.2).
		 * Untrusted functions have very limited use and is clumsy. We
		 * just get rid of it.
		 */
		elog(ERROR, "internal error: untrusted function not supported.");
	}

	/*
	 * If finfo contains a PL handler for this function, call that
	 * instead.
	 */
	if (finfo->fn_plhandler != NULL)
		return (*(finfo->fn_plhandler)) (finfo, values, isNull);

	switch (n_arguments)
	{
		case 0:
			returnValue = (*user_fn) ();
			break;
		case 1:
			/* NullValue() uses isNull to check if args[0] is NULL */
			returnValue = (*user_fn) (values->data[0], isNull);
			break;
		case 2:
			returnValue = (*user_fn) (values->data[0], values->data[1]);
			break;
		case 3:
			returnValue = (*user_fn) (values->data[0], values->data[1],
									  values->data[2]);
			break;
		case 4:
			returnValue = (*user_fn) (values->data[0], values->data[1],
									  values->data[2], values->data[3]);
			break;
		case 5:
			returnValue = (*user_fn) (values->data[0], values->data[1],
									  values->data[2], values->data[3],
									  values->data[4]);
			break;
		case 6:
			returnValue = (*user_fn) (values->data[0], values->data[1],
									  values->data[2], values->data[3],
									  values->data[4], values->data[5]);
			break;
		case 7:
			returnValue = (*user_fn) (values->data[0], values->data[1],
									  values->data[2], values->data[3],
									  values->data[4], values->data[5],
									  values->data[6]);
			break;
		case 8:
			returnValue = (*user_fn) (values->data[0], values->data[1],
									  values->data[2], values->data[3],
									  values->data[4], values->data[5],
									  values->data[6], values->data[7]);
			break;
		case 9:

			/*
			 * XXX Note that functions with >8 arguments can only be
			 * called from inside the system, not from the user level,
			 * since the catalogs only store 8 argument types for user
			 * type-checking!
			 */
			returnValue = (*user_fn) (values->data[0], values->data[1],
									  values->data[2], values->data[3],
									  values->data[4], values->data[5],
									  values->data[6], values->data[7],
									  values->data[8]);
			break;
		default:
			elog(ERROR, "fmgr_c: function %d: too many arguments (%d > %d)",
				 finfo->fn_oid, n_arguments, MAXFMGRARGS);
			break;
	}
	return returnValue;
}

void
fmgr_info(Oid procedureId, FmgrInfo *finfo)
{
	FmgrCall   *fcp;
	HeapTuple	procedureTuple;
	FormData_pg_proc *procedureStruct;
	HeapTuple	languageTuple;
	Form_pg_language languageStruct;
	Oid			language;

	finfo->fn_addr = NULL;
	finfo->fn_plhandler = NULL;
	finfo->fn_oid = procedureId;

	if (!(fcp = fmgr_isbuiltin(procedureId)))
	{
		procedureTuple = SearchSysCacheTuple(PROOID,
										   ObjectIdGetDatum(procedureId),
											 0, 0, 0);
		if (!HeapTupleIsValid(procedureTuple))
		{
			elog(ERROR, "fmgr_info: function %d: cache lookup failed\n",
				 procedureId);
		}
		procedureStruct = (FormData_pg_proc *) GETSTRUCT(procedureTuple);
		if (!procedureStruct->proistrusted)
		{
			finfo->fn_addr = (func_ptr) NULL;
			finfo->fn_nargs = procedureStruct->pronargs;
			return;
		}
		language = procedureStruct->prolang;
		switch (language)
		{
			case INTERNALlanguageId:
				finfo->fn_addr = fmgr_lookupByName(procedureStruct->proname.data);
				if (!finfo->fn_addr)
					elog(ERROR, "fmgr_info: function %s: not in internal table",
						 procedureStruct->proname.data);
				finfo->fn_nargs = procedureStruct->pronargs;
				break;
			case ClanguageId:
				finfo->fn_addr = fmgr_dynamic(procedureId, &(finfo->fn_nargs));
				break;
			case SQLlanguageId:
				finfo->fn_addr = (func_ptr) NULL;
				finfo->fn_nargs = procedureStruct->pronargs;
				break;
			default:

				/*
				 * Might be a created procedural language Lookup the
				 * syscache for the language and check the lanispl flag If
				 * this is the case, we return a NULL function pointer and
				 * the number of arguments from the procedure.
				 */
				languageTuple = SearchSysCacheTuple(LANOID,
							  ObjectIdGetDatum(procedureStruct->prolang),
													0, 0, 0);
				if (!HeapTupleIsValid(languageTuple))
				{
					elog(ERROR, "fmgr_info: %s %ld",
						 "Cache lookup for language %d failed",
						 ObjectIdGetDatum(procedureStruct->prolang));
				}
				languageStruct = (Form_pg_language)
					GETSTRUCT(languageTuple);
				if (languageStruct->lanispl)
				{
					FmgrInfo	plfinfo;

					fmgr_info(((Form_pg_language) GETSTRUCT(languageTuple))->lanplcallfoid, &plfinfo);
					finfo->fn_addr = (func_ptr) fmgr_pl;
					finfo->fn_plhandler = plfinfo.fn_addr;
					finfo->fn_nargs = procedureStruct->pronargs;
				}
				else
				{
					elog(ERROR, "fmgr_info: function %d: unknown language %d",
						 procedureId, language);
				}
				break;
		}
	}
	else
	{
		finfo->fn_addr = fcp->func;
		finfo->fn_nargs = fcp->nargs;
	}
}

/*
 *		fmgr			- return the value of a function call
 *
 *		If the function is a system routine, it's compiled in, so call
 *		it directly.
 *
 *		Otherwise pass it to the the appropriate 'language' function caller.
 *
 *		Returns the return value of the invoked function if succesful,
 *		0 if unsuccessful.
 */
char *
fmgr(Oid procedureId,...)
{
	va_list		pvar;
	int			i;
	int			pronargs;
	FmgrValues	values;
	FmgrInfo	finfo;
	bool		isNull = false;

	va_start(pvar, procedureId);

	fmgr_info(procedureId, &finfo);
	pronargs = finfo.fn_nargs;

	if (pronargs > MAXFMGRARGS)
	{
		elog(ERROR, "fmgr: function %d: too many arguments (%d > %d)",
			 procedureId, pronargs, MAXFMGRARGS);
	}
	for (i = 0; i < pronargs; ++i)
		values.data[i] = va_arg(pvar, char *);
	va_end(pvar);

	/* XXX see WAY_COOL_ORTHOGONAL_FUNCTIONS */
	return fmgr_c(&finfo, &values, &isNull);
}

/*
 * This is just a version of fmgr() in which the hacker can prepend a C
 * function pointer.  This routine is not normally called; generally,
 * if you have all of this information you're likely to just jump through
 * the pointer, but it's available for use with macros in fmgr.h if you
 * want this routine to do sanity-checking for you.
 *
 * funcinfo, n_arguments, args...
 */
#ifdef NOT_USED
char *
fmgr_ptr(FmgrInfo *finfo,...)
{
	va_list		pvar;
	int			i;
	int			n_arguments;
	FmgrInfo	local_finfo;
	FmgrValues	values;
	bool		isNull = false;

	local_finfo->fn_addr = finfo->fn_addr;
	local_finfo->fn_plhandler = finfo->fn_plhandler;
	local_finfo->fn_oid = finfo->fn_oid;

	va_start(pvar, finfo);
	n_arguments = va_arg(pvar, int);
	local_finfo->fn_nargs = n_arguments;
	if (n_arguments > MAXFMGRARGS)
	{
		elog(ERROR, "fmgr_ptr: function %d: too many arguments (%d > %d)",
			 func_id, n_arguments, MAXFMGRARGS);
	}
	for (i = 0; i < n_arguments; ++i)
		values.data[i] = va_arg(pvar, char *);
	va_end(pvar);

	/* XXX see WAY_COOL_ORTHOGONAL_FUNCTIONS */
	return fmgr_c(&local_finfo, &values, &isNull);
}

#endif

/*
 * This routine is not well thought out.  When I get around to adding a
 * function pointer field to FuncIndexInfo, it will be replace by calls
 * to fmgr_c().
 */
char *
fmgr_array_args(Oid procedureId, int nargs, char *args[], bool *isNull)
{
	FmgrInfo	finfo;

	fmgr_info(procedureId, &finfo);
	finfo.fn_nargs = nargs;

	/* XXX see WAY_COOL_ORTHOGONAL_FUNCTIONS */
	return (fmgr_c(&finfo,
				(FmgrValues *) args,
				isNull));
}
