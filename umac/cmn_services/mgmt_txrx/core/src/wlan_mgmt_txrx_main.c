/*
 * Copyright (c) 2016 The Linux Foundation. All rights reserved.
 *
 * Permission to use, copy, modify, and/or distribute this software for
 * any purpose with or without fee is hereby granted, provided that the
 * above copyright notice and this permission notice appear in all
 * copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
 * WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
 * AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 * PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/**
 *  DOC:    wlan_mgmt_txrx_main.c
 *  This file contains mgmt txrx private API definitions for
 *  mgmt txrx component.
 */

#include "wlan_mgmt_txrx_main_i.h"
#include "qdf_nbuf.h"

QDF_STATUS wlan_mgmt_txrx_desc_pool_init(
			struct mgmt_txrx_priv_context *mgmt_txrx_ctx,
			uint32_t pool_size)
{
	uint32_t i;

	if (!pool_size) {
		mgmt_txrx_err("Invalid pool size %u given", pool_size);
		qdf_assert_always(pool_size);
		return QDF_STATUS_E_INVAL;
	}

	mgmt_txrx_info("mgmt_txrx ctx: %p psoc: %p, initialize mgmt desc pool of size %d",
				mgmt_txrx_ctx, mgmt_txrx_ctx->psoc, pool_size);
	mgmt_txrx_ctx->mgmt_desc_pool.pool = qdf_mem_malloc(pool_size *
					sizeof(struct mgmt_txrx_desc_elem_t));

	if (!mgmt_txrx_ctx->mgmt_desc_pool.pool) {
		mgmt_txrx_err("Failed to allocate desc pool");
		return QDF_STATUS_E_NOMEM;
	}
	qdf_list_create(&mgmt_txrx_ctx->mgmt_desc_pool.free_list, pool_size);

	for (i = 0; i < (pool_size - 1); i++) {
		mgmt_txrx_ctx->mgmt_desc_pool.pool[i].desc_id = i;
		mgmt_txrx_ctx->mgmt_desc_pool.pool[i].in_use = false;
		qdf_list_insert_front(&mgmt_txrx_ctx->mgmt_desc_pool.free_list,
				&mgmt_txrx_ctx->mgmt_desc_pool.pool[i].entry);
	}

	qdf_spinlock_create(
		&mgmt_txrx_ctx->mgmt_desc_pool.desc_pool_lock);

	return QDF_STATUS_SUCCESS;
}

void wlan_mgmt_txrx_desc_pool_deinit(
			struct mgmt_txrx_priv_context *mgmt_txrx_ctx)
{
	uint8_t i;
	uint32_t pool_size;
	QDF_STATUS status;

	qdf_spin_lock_bh(&mgmt_txrx_ctx->mgmt_desc_pool.desc_pool_lock);
	if (!mgmt_txrx_ctx->mgmt_desc_pool.pool) {
		qdf_spin_unlock_bh(
			&mgmt_txrx_ctx->mgmt_desc_pool.desc_pool_lock);
		mgmt_txrx_err("Empty mgmt descriptor pool");
		qdf_assert_always(0);
		return;
	}

	pool_size = mgmt_txrx_ctx->mgmt_desc_pool.free_list.max_size;
	for (i = 0; i < (pool_size - 1); i++) {
		status = qdf_list_remove_node(
				&mgmt_txrx_ctx->mgmt_desc_pool.free_list,
				&mgmt_txrx_ctx->mgmt_desc_pool.pool[i].entry);
		if (status != QDF_STATUS_SUCCESS)
			mgmt_txrx_err("Failed to get mgmt descriptor from freelist, desc id: %d with status %d",
					i, status);
	}

	qdf_list_destroy(&mgmt_txrx_ctx->mgmt_desc_pool.free_list);
	qdf_mem_free(mgmt_txrx_ctx->mgmt_desc_pool.pool);
	mgmt_txrx_ctx->mgmt_desc_pool.pool = NULL;

	qdf_spin_unlock_bh(
		&mgmt_txrx_ctx->mgmt_desc_pool.desc_pool_lock);
	qdf_spinlock_destroy(
		&mgmt_txrx_ctx->mgmt_desc_pool.desc_pool_lock);
}

struct mgmt_txrx_desc_elem_t *wlan_mgmt_txrx_desc_get(
			struct mgmt_txrx_priv_context *mgmt_txrx_ctx)
{
	QDF_STATUS status;
	qdf_list_node_t *desc_node;
	struct mgmt_txrx_desc_elem_t *mgmt_txrx_desc;

	qdf_spin_lock_bh(&mgmt_txrx_ctx->mgmt_desc_pool.desc_pool_lock);
	if (qdf_list_peek_front(&mgmt_txrx_ctx->mgmt_desc_pool.free_list,
			    &desc_node)
			!= QDF_STATUS_SUCCESS) {
		qdf_spin_unlock_bh(
			&mgmt_txrx_ctx->mgmt_desc_pool.desc_pool_lock);
		mgmt_txrx_err("Descriptor freelist empty for mgmt_txrx_ctx %p",
				mgmt_txrx_ctx);
		return NULL;
	}

	status = qdf_list_remove_node(&mgmt_txrx_ctx->mgmt_desc_pool.free_list,
			     desc_node);
	if (status != QDF_STATUS_SUCCESS) {
		qdf_spin_unlock_bh(
			&mgmt_txrx_ctx->mgmt_desc_pool.desc_pool_lock);
		mgmt_txrx_err("Failed to get descriptor from list: status %d",
					status);
		qdf_assert_always(0);
		return NULL;
	}

	mgmt_txrx_desc = qdf_container_of(desc_node,
					  struct mgmt_txrx_desc_elem_t,
					  entry);
	mgmt_txrx_desc->in_use = true;
	qdf_spin_unlock_bh(&mgmt_txrx_ctx->mgmt_desc_pool.desc_pool_lock);

	mgmt_txrx_info("retrieved mgmt desc: %p with desc id: %d",
			mgmt_txrx_desc, mgmt_txrx_desc->desc_id);
	return mgmt_txrx_desc;
}

void wlan_mgmt_txrx_desc_put(struct mgmt_txrx_priv_context *mgmt_txrx_ctx,
			uint32_t desc_id)
{
	struct mgmt_txrx_desc_elem_t *desc;

	desc = &mgmt_txrx_ctx->mgmt_desc_pool.pool[desc_id];
	qdf_spin_lock_bh(&mgmt_txrx_ctx->mgmt_desc_pool.desc_pool_lock);
	desc->in_use = false;
	qdf_list_insert_front(&mgmt_txrx_ctx->mgmt_desc_pool.free_list,
			      &desc->entry);
	qdf_spin_unlock_bh(&mgmt_txrx_ctx->mgmt_desc_pool.desc_pool_lock);

	mgmt_txrx_info("put mgmt desc: %p with desc id: %d into freelist",
			desc, desc->desc_id);
}
