/*
 * Copyright (C) 1994-2020 Altair Engineering, Inc.
 * For more information, contact Altair at www.altair.com.
 *
 * This file is part of both the OpenPBS software ("OpenPBS")
 * and the PBS Professional ("PBS Pro") software.
 *
 * Open Source License Information:
 *
 * OpenPBS is free software. You can redistribute it and/or modify it under
 * the terms of the GNU Affero General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * OpenPBS is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Affero General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Commercial License Information:
 *
 * PBS Pro is commercially licensed software that shares a common core with
 * the OpenPBS software.  For a copy of the commercial license terms and
 * conditions, go to: (http://www.pbspro.com/agreement.html) or contact the
 * Altair Legal Department.
 *
 * Altair's dual-license business model allows companies, individuals, and
 * organizations to create proprietary derivative works of OpenPBS and
 * distribute them - whether embedded or bundled with other software -
 * under a commercial license agreement.
 *
 * Use of Altair's trademarks, including but not limited to "PBS™",
 * "OpenPBS®", "PBS Professional®", and "PBS Pro™" and Altair's logos is
 * subject to Altair's trademark licensing policies.
 */



/**
 * @file    db_postgres_que.c
 *
 * @brief
 *      Implementation of the queue data access functions for postgres
 */

#include <pbs_config.h>   /* the master config generated by configure */
#include "pbs_db.h"
#include "db_postgres.h"

/**
 * @brief
 *	Prepare all the queue related sqls. Typically called after connect
 *	and before any other sql exeuction
 *
 * @param[in]	conn - Database connection handle
 *
 * @return      Error code
 * @retval	-1 - Failure
 * @retval	 0 - Success
 *
 */
int
pg_db_prepare_que_sqls(pbs_db_conn_t *conn)
{
	snprintf(conn->conn_sql, MAX_SQL_LENGTH, "insert into pbs.queue("
		"qu_name, "
		"qu_type, "
		"qu_ctime, "
		"qu_mtime, "
		"attributes "
		") "
		"values "
		"($1, $2,  localtimestamp, localtimestamp, hstore($3::text[]))");

	if (pg_prepare_stmt(conn, STMT_INSERT_QUE, conn->conn_sql, 3) != 0)
		return -1;

	/* rewrite all attributes for FULL update */
	snprintf(conn->conn_sql, MAX_SQL_LENGTH, "update pbs.queue set "
			"qu_type = $2, "
			"qu_mtime = localtimestamp, "
			"attributes = hstore($3::text[])"
			" where qu_name = $1");
	if (pg_prepare_stmt(conn, STMT_UPDATE_QUE_FULL, conn->conn_sql, 3) != 0)
		return -1;


	snprintf(conn->conn_sql, MAX_SQL_LENGTH, "update pbs.queue set "
		"qu_mtime = localtimestamp,"
		"attributes = attributes - hstore($2::text[]) "
		"where qu_name = $1");
	if (pg_prepare_stmt(conn, STMT_REMOVE_QUEATTRS, conn->conn_sql, 2) != 0)
		return -1;

	snprintf(conn->conn_sql, MAX_SQL_LENGTH, "select qu_name, "
			"qu_type, "
			"extract(epoch from qu_ctime)::bigint as qu_ctime, "
			"extract(epoch from qu_mtime)::bigint as qu_mtime, "
			"hstore_to_array(attributes) as attributes "
			"from pbs.queue "
			"where qu_name = $1");
	if (pg_prepare_stmt(conn, STMT_SELECT_QUE, conn->conn_sql, 1) != 0)
		return -1;

	snprintf(conn->conn_sql, MAX_SQL_LENGTH, "select "
			"qu_name, "
			"qu_type, "
			"extract(epoch from qu_ctime)::bigint as qu_ctime, "
			"extract(epoch from qu_mtime)::bigint as qu_mtime, "
			"hstore_to_array(attributes) as attributes "
			"from pbs.queue order by qu_ctime");
	if (pg_prepare_stmt(conn, STMT_FIND_QUES_ORDBY_CREATTM, conn->conn_sql, 0) != 0)
		return -1;

	snprintf(conn->conn_sql, MAX_SQL_LENGTH, "delete from pbs.queue where qu_name = $1");
	if (pg_prepare_stmt(conn, STMT_DELETE_QUE, conn->conn_sql, 1) != 0)
		return -1;

	return 0;
}

/**
 * @brief
 *	Load queue data from the row into the queue object
 *
 * @param[in]	res - Resultset from a earlier query
 * @param[in]	pq  - Queue object to load data into
 * @param[in]	row - The current row to load within the resultset
 *
 * @return      Error code
 * @retval	-1 - On Error
 * @retval	 0 - On Success
 * @retval	>1 - Number of attributes
 */
static int
load_que(PGresult *res, pbs_db_que_info_t *pq, int row)
{
	char *raw_array;
	static int qu_name_fnum, qu_type_fnum, qu_ctime_fnum, qu_mtime_fnum, attributes_fnum;
	static int fnums_inited = 0;

	if (fnums_inited == 0) {
		qu_name_fnum = PQfnumber(res, "qu_name");
		qu_type_fnum = PQfnumber(res, "qu_type");
		qu_ctime_fnum = PQfnumber(res, "qu_ctime");
		qu_mtime_fnum = PQfnumber(res, "qu_mtime");
		attributes_fnum = PQfnumber(res, "attributes");
		fnums_inited = 1;
	}

	GET_PARAM_STR(res, row, pq->qu_name, qu_name_fnum);
	GET_PARAM_INTEGER(res, row, pq->qu_type, qu_type_fnum);
	GET_PARAM_BIGINT(res, row, pq->qu_ctime, qu_ctime_fnum);
	GET_PARAM_BIGINT(res, row, pq->qu_mtime, qu_mtime_fnum);
	GET_PARAM_BIN(res, row, raw_array, attributes_fnum);

	/* convert attributes from postgres raw array format */
	return (convert_array_to_db_attr_list(raw_array, &pq->attr_list));
}

/**
 * @brief
 *	Insert queue data into the database
 *
 * @param[in]	conn - Connection handle
 * @param[in]	obj  - Information of queue to be inserted
 *
 * @return      Error code
 * @retval	-1 - Failure
 * @retval	 0 - Success
 *
 */
int
pg_db_save_que(pbs_db_conn_t *conn, pbs_db_obj_info_t *obj, int savetype)
{
	pbs_db_que_info_t *pq = obj->pbs_db_un.pbs_db_que;
	char *stmt;
	int params;
	char *raw_array = NULL;

	SET_PARAM_STR(conn, pq->qu_name, 0);
	SET_PARAM_INTEGER(conn, pq->qu_type, 1);

	if (savetype == PBS_UPDATE_DB_QUICK) {
		params = 2;
	} else {
		int len = 0;
		/* convert attributes to postgres raw array format */
		if ((len = convert_db_attr_list_to_array(&raw_array, &pq->attr_list)) <= 0)
			return -1;

		SET_PARAM_BIN(conn, raw_array, len, 2);
		params = 3;
	}

	if (savetype == PBS_UPDATE_DB_FULL)
		stmt = STMT_UPDATE_QUE_FULL;
	else
		stmt = STMT_INSERT_QUE;

	if (pg_db_cmd(conn, stmt, params) != 0) {
		free(raw_array);
		return -1;
	}

	free(raw_array);

	return 0;
}

/**
 * @brief
 *	Load queue data from the database
 *
 * @param[in]	conn - Connection handle
 * @param[in]	obj  - Load queue information into this object
 *
 * @return      Error code
 * @retval	-1 - Failure
 * @retval	 0 - Success
 * @retval	 1 -  Success but no rows loaded
 *
 */
int
pg_db_load_que(pbs_db_conn_t *conn, pbs_db_obj_info_t *obj)
{
	PGresult *res;
	int rc;
	pbs_db_que_info_t *pq = obj->pbs_db_un.pbs_db_que;

	SET_PARAM_STR(conn, pq->qu_name, 0);

	if ((rc = pg_db_query(conn, STMT_SELECT_QUE, 1, &res)) != 0)
		return rc;

	rc = load_que(res, pq, 0);

	PQclear(res);
	return rc;
}

/**
 * @brief
 *	Find queues
 *
 * @param[in]	conn - Connection handle
 * @param[in]	st   - The cursor state variable updated by this query
 * @param[in]	obj  - Information of queue to be found
 * @param[in]	opts - Any other options (like flags, timestamp)
 *
 * @return      Error code
 * @retval	-1 - Failure
 * @retval	 0 - Success
 * @retval	 1 - Success, but no rows found
 *
 */
int
pg_db_find_que(pbs_db_conn_t *conn, void *st, pbs_db_obj_info_t *obj, pbs_db_query_options_t *opts)
{
	PGresult *res;
	int rc;
	pg_query_state_t *state = (pg_query_state_t *) st;

	if (!state)
		return -1;

	strcpy(conn->conn_sql, STMT_FIND_QUES_ORDBY_CREATTM);
	if ((rc = pg_db_query(conn, conn->conn_sql, 0, &res)) != 0)
		return rc;

	state->row = 0;
	state->res = res;
	state->count = PQntuples(res);

	return 0;
}

/**
 * @brief
 *	Get the next queue from the cursor
 *
 * @param[in]	conn - Connection handle
 * @param[in]	st   - The cursor state
 * @param[in]	obj  - queue information is loaded into this object
 *
 * @return      Error code
 *		(Even though this returns only 0 now, keeping it as int
 *			to support future change to return a failure)
 * @retval	 0 - Success
 *
 */
int
pg_db_next_que(pbs_db_conn_t* conn, void *st, pbs_db_obj_info_t* obj)
{
	pg_query_state_t *state = (pg_query_state_t *) st;

	return (load_que(state->res, obj->pbs_db_un.pbs_db_que, state->row));
}

/**
 * @brief
 *	Delete the queue from the database
 *
 * @param[in]	conn - Connection handle
 * @param[in]	obj  - queue information
 *
 * @return      Error code
 * @retval	-1 - Failure
 * @retval	 0 - Success
 * @retval	 1 - Success but no rows deleted
 *
 */
int
pg_db_delete_que(pbs_db_conn_t *conn, pbs_db_obj_info_t *obj)
{
	pbs_db_que_info_t *pq = obj->pbs_db_un.pbs_db_que;
	SET_PARAM_STR(conn, pq->qu_name, 0);
	return (pg_db_cmd(conn, STMT_DELETE_QUE, 1));
}


/**
 * @brief
 *	Deletes attributes of a queue
 *
 * @param[in]	conn - Connection handle
 * @param[in]	obj  - queue information
 * @param[in]	obj_id  - queue id
 * @param[in]	attr_list - List of attributes
 *
 * @return      Error code
 * @retval	 0 - Success
 * @retval	-1 - On Failure
 *
 */
int
pg_db_del_attr_que(pbs_db_conn_t *conn, pbs_db_obj_info_t *obj, void *obj_id, pbs_db_attr_list_t *attr_list)
{
	char *raw_array = NULL;
	int len = 0;

	if ((len = convert_db_attr_list_to_array(&raw_array, attr_list)) <= 0)
		return -1;
	SET_PARAM_STR(conn, obj_id, 0);

	SET_PARAM_BIN(conn, raw_array, len, 1);

	if (pg_db_cmd(conn, STMT_REMOVE_QUEATTRS, 2) != 0)
		return -1;

	free(raw_array);

	return 0;
}

/**
 * @brief
 *	Frees allocate memory of an Object
 *
 * @param[in]	obj - pbs_db_obj_info_t containing the DB object
 *
 * @return None
 *
 */
void
pg_db_reset_que(pbs_db_obj_info_t *obj)
{
	free_db_attr_list(&(obj->pbs_db_un.pbs_db_que->attr_list));
}
