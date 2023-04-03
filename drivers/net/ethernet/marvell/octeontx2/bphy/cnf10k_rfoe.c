// SPDX-License-Identifier: GPL-2.0
/* Marvell CNF10K BPHY RFOE Netdev Driver
 *
 * Copyright (C) 2021 Marvell.
 */

#include "cnf10k_rfoe.h"
#include "cnf10k_bphy_hw.h"

/* global driver ctx */
struct cnf10k_rfoe_drv_ctx cnf10k_rfoe_drv_ctx[CNF10K_RFOE_MAX_INTF];

void cnf10k_bphy_intr_handler(struct otx2_bphy_cdev_priv *cdev_priv, u32 status)
{
	struct cnf10k_rfoe_drv_ctx *cnf10k_drv_ctx;
	struct cnf10k_rfoe_ndev_priv *priv;
	struct net_device *netdev;
	int rfoe_num, i;
	u32 intr_mask;

	/* rx intr processing */
	for (rfoe_num = 0; rfoe_num < cdev_priv->num_rfoe_mhab; rfoe_num++) {
		intr_mask = CNF10K_RFOE_RX_INTR_MASK(rfoe_num);
		if (status & intr_mask)
			cnf10k_rfoe_rx_napi_schedule(rfoe_num, status);
	}

	/* tx intr processing */
	for (i = 0; i < CNF10K_RFOE_MAX_INTF; i++) {
		cnf10k_drv_ctx = &cnf10k_rfoe_drv_ctx[i];
		if (cnf10k_drv_ctx->valid) {
			netdev = cnf10k_drv_ctx->netdev;
			priv = netdev_priv(netdev);
			intr_mask = CNF10K_RFOE_TX_PTP_INTR_MASK(priv->rfoe_num,
								 priv->lmac_id,
						cdev_priv->num_rfoe_lmac);
			if ((status & intr_mask) && priv->ptp_tx_skb)
				schedule_work(&priv->ptp_tx_work);
		}
	}
}

void cnf10k_rfoe_disable_intf(int rfoe_num)
{
	struct cnf10k_rfoe_drv_ctx *drv_ctx;
	struct cnf10k_rfoe_ndev_priv *priv;
	struct net_device *netdev;
	int idx;

	for (idx = 0; idx < CNF10K_RFOE_MAX_INTF; idx++) {
		drv_ctx = &cnf10k_rfoe_drv_ctx[idx];
		if (drv_ctx->rfoe_num == rfoe_num && drv_ctx->valid) {
			netdev = drv_ctx->netdev;
			priv = netdev_priv(netdev);
			priv->if_type = IF_TYPE_NONE;
		}
	}
}

void cnf10k_bphy_rfoe_cleanup(void)
{
	struct cnf10k_rfoe_drv_ctx *drv_ctx = NULL;
	struct cnf10k_rfoe_ndev_priv *priv;
	struct cnf10k_rx_ft_cfg *ft_cfg;
	struct net_device *netdev;
	int i, idx;

	for (i = 0; i < CNF10K_RFOE_MAX_INTF; i++) {
		drv_ctx = &cnf10k_rfoe_drv_ctx[i];
		if (drv_ctx->valid) {
			netdev = drv_ctx->netdev;
			priv = netdev_priv(netdev);
			cnf10k_rfoe_ptp_destroy(priv);
			unregister_netdev(netdev);
			for (idx = 0; idx < PACKET_TYPE_MAX; idx++) {
				if (!(priv->pkt_type_mask & (1U << idx)))
					continue;
				ft_cfg = &priv->rx_ft_cfg[idx];
				netif_napi_del(&ft_cfg->napi);
			}
			--(priv->rfoe_common->refcnt);
			if (priv->rfoe_common->refcnt == 0)
				kfree(priv->rfoe_common);
			free_netdev(netdev);
			drv_ctx->valid = 0;
		}
	}
}

/* submit pending ptp tx requests */
static void cnf10k_rfoe_ptp_submit_work(struct work_struct *work)
{
	struct cnf10k_rfoe_ndev_priv *priv = container_of(work,
						struct cnf10k_rfoe_ndev_priv,
						ptp_queue_work);
	struct mhbw_jd_dma_cfg_word_0_s *jd_dma_cfg_word_0;
	struct mhbw_jd_dma_cfg_word_1_s *jd_dma_cfg_word_1;
	struct mhab_job_desc_cfg *jd_cfg_ptr;
	struct rfoe_tx_ptp_tstmp_s *tx_tstmp;
	struct psm_cmd_addjob_s *psm_cmd_lo;
	struct tx_job_queue_cfg *job_cfg;
	struct tx_job_entry *job_entry;
	struct ptp_tstamp_skb *ts_skb;
	u16 psm_queue_id, queue_space;
	struct sk_buff *skb = NULL;
	struct list_head *head;
	u64 jd_cfg_ptr_iova;
	unsigned long flags;
	u64 regval;

	job_cfg = &priv->tx_ptp_job_cfg;

	spin_lock_irqsave(&job_cfg->lock, flags);

	/* check pending ptp requests */
	if (list_empty(&priv->ptp_skb_list.list)) {
		netif_dbg(priv, tx_queued, priv->netdev,
			  "no pending ptp tx requests\n");
		spin_unlock_irqrestore(&job_cfg->lock, flags);
		return;
	}

	/* check psm queue space available */
	psm_queue_id = job_cfg->psm_queue_id;
	regval = readq(priv->psm_reg_base + PSM_QUEUE_SPACE(psm_queue_id));
	queue_space = regval & 0x7FFF;
	if (queue_space < 1) {
		netif_dbg(priv, tx_queued, priv->netdev,
			  "ptp tx psm queue %d full\n",
			  psm_queue_id);
		/* reschedule to check later */
		spin_unlock_irqrestore(&job_cfg->lock, flags);
		schedule_work(&priv->ptp_queue_work);
		return;
	}

	if (test_and_set_bit_lock(PTP_TX_IN_PROGRESS, &priv->state)) {
		netif_dbg(priv, tx_queued, priv->netdev, "ptp tx ongoing\n");
		spin_unlock_irqrestore(&job_cfg->lock, flags);
		return;
	}

	head = &priv->ptp_skb_list.list;
	ts_skb = list_entry(head->next, struct ptp_tstamp_skb, list);
	skb = ts_skb->skb;
	list_del(&ts_skb->list);
	kfree(ts_skb);
	priv->ptp_skb_list.count--;

	netif_dbg(priv, tx_queued, priv->netdev,
		  "submitting ptp tx skb %pS\n", skb);

	priv->last_tx_ptp_jiffies = jiffies;

	tx_tstmp = (struct rfoe_tx_ptp_tstmp_s *)
			((u8 *)priv->ptp_ring_cfg.ptp_ring_base +
			(128 * priv->ptp_ring_cfg.ptp_ring_idx));
	tx_tstmp->valid = 0;

	/* get the tx job entry */
	job_entry = (struct tx_job_entry *)
				&job_cfg->job_entries[job_cfg->q_idx];

	netif_dbg(priv, tx_queued, priv->netdev,
		  "rfoe=%d lmac=%d psm_queue=%d tx_job_entry %d job_cmd_lo=0x%llx job_cmd_high=0x%llx jd_iova_addr=0x%llx\n",
		  priv->rfoe_num, priv->lmac_id, psm_queue_id, job_cfg->q_idx,
		  job_entry->job_cmd_lo, job_entry->job_cmd_hi,
		  job_entry->jd_iova_addr);

	priv->ptp_tx_skb = skb;
	psm_cmd_lo = (struct psm_cmd_addjob_s *)&job_entry->job_cmd_lo;
	priv->ptp_job_tag = psm_cmd_lo->jobtag;

	/* update length and block size in jd dma cfg word */
	jd_cfg_ptr_iova = *(u64 *)((u8 *)job_entry->jd_ptr + 8);
	jd_cfg_ptr = otx2_iova_to_virt(priv->iommu_domain, jd_cfg_ptr_iova);
	jd_cfg_ptr->cfg3.pkt_len = skb->len;
	jd_dma_cfg_word_0 = (struct mhbw_jd_dma_cfg_word_0_s *)
				job_entry->rd_dma_ptr;
	jd_dma_cfg_word_0->block_size = (((skb->len + 15) >> 4) * 4);

	/* copy packet data to rd_dma_ptr start addr */
	jd_dma_cfg_word_1 = (struct mhbw_jd_dma_cfg_word_1_s *)
				((u8 *)job_entry->rd_dma_ptr + 8);
	memcpy(otx2_iova_to_virt(priv->iommu_domain,
				 jd_dma_cfg_word_1->start_addr),
	       skb->data, skb->len);

	/* make sure that all memory writes are completed */
	dma_wmb();

	/* submit PSM job */
	writeq(job_entry->job_cmd_lo,
	       priv->psm_reg_base + PSM_QUEUE_CMD_LO(psm_queue_id));
	writeq(job_entry->job_cmd_hi,
	       priv->psm_reg_base + PSM_QUEUE_CMD_HI(psm_queue_id));

	/* increment queue index */
	job_cfg->q_idx++;
	if (job_cfg->q_idx == job_cfg->num_entries)
		job_cfg->q_idx = 0;

	spin_unlock_irqrestore(&job_cfg->lock, flags);
}

#define OTX2_RFOE_PTP_TSTMP_POLL_CNT	100

/* ptp interrupt processing bottom half */
static void cnf10k_rfoe_ptp_tx_work(struct work_struct *work)
{
	struct cnf10k_rfoe_ndev_priv *priv = container_of(work,
						 struct cnf10k_rfoe_ndev_priv,
						 ptp_tx_work);
	struct rfoe_tx_ptp_tstmp_s *tx_tstmp;
	struct skb_shared_hwtstamps ts;
	u64 timestamp;
	u16 jobid;
	int cnt;

	if (!priv->ptp_tx_skb) {
		netif_err(priv, tx_done, priv->netdev,
			  "ptp tx skb not found, something wrong!\n");
		goto submit_next_req;
	}

	tx_tstmp = (struct rfoe_tx_ptp_tstmp_s *)
			((u8 *)priv->ptp_ring_cfg.ptp_ring_base +
			(128 * priv->ptp_ring_cfg.ptp_ring_idx));

	/* poll for timestamp valid bit to go high */
	for (cnt = 0; cnt < OTX2_RFOE_PTP_TSTMP_POLL_CNT; cnt++) {
		/* check valid bit */
		if (tx_tstmp->valid)
			break;
		usleep_range(5, 10);
	}

	if (cnt >= OTX2_RFOE_PTP_TSTMP_POLL_CNT) {
		netif_err(priv, tx_err, priv->netdev,
			  "ptp tx timestamp polling timeout, skb=%pS\n",
			  priv->ptp_tx_skb);
		priv->stats.tx_hwtstamp_failures++;
		goto submit_next_req;
	}

	if (tx_tstmp->drop || tx_tstmp->tx_err) {
		netif_err(priv, tx_done, priv->netdev,
			  "ptp tx timstamp error\n");
		goto submit_next_req;
	}

	/* match job id */
	jobid = tx_tstmp->jobid;
	if (jobid != priv->ptp_job_tag) {
		netif_err(priv, tx_done, priv->netdev,
			  "ptp job id doesn't match, job_id=0x%x skb->job_tag=0x%x\n",
			  jobid, priv->ptp_job_tag);
		goto submit_next_req;
	}

	/* update timestamp value in skb */
	timestamp = tx_tstmp->ptp_timestamp;

	memset(&ts, 0, sizeof(ts));
	ts.hwtstamp = ns_to_ktime(timestamp);
	skb_tstamp_tx(priv->ptp_tx_skb, &ts);

submit_next_req:
	priv->ptp_ring_cfg.ptp_ring_idx++;
	if (priv->ptp_ring_cfg.ptp_ring_idx >= priv->ptp_ring_cfg.ptp_ring_size)
		priv->ptp_ring_cfg.ptp_ring_idx = 0;
	if (priv->ptp_tx_skb)
		dev_kfree_skb_any(priv->ptp_tx_skb);
	priv->ptp_tx_skb = NULL;
	clear_bit_unlock(PTP_TX_IN_PROGRESS, &priv->state);
	schedule_work(&priv->ptp_queue_work);
}

/* psm queue timer callback to check queue space */
static void cnf10k_rfoe_tx_timer_cb(struct timer_list *t)
{
	struct cnf10k_rfoe_ndev_priv *priv =
			container_of(t, struct cnf10k_rfoe_ndev_priv, tx_timer);
	u16 psm_queue_id, queue_space;
	int reschedule = 0;
	u64 regval;

	/* check psm queue space for both ptp and oth packets */
	if (netif_queue_stopped(priv->netdev)) {
		psm_queue_id = priv->tx_ptp_job_cfg.psm_queue_id;
		// check queue space
		regval = readq(priv->psm_reg_base +
						PSM_QUEUE_SPACE(psm_queue_id));
		queue_space = regval & 0x7FFF;
		if (queue_space > 1) {
			netif_wake_queue(priv->netdev);
			reschedule = 0;
		} else {
			reschedule = 1;
		}

		psm_queue_id = priv->rfoe_common->tx_oth_job_cfg.psm_queue_id;
		// check queue space
		regval = readq(priv->psm_reg_base +
						PSM_QUEUE_SPACE(psm_queue_id));
		queue_space = regval & 0x7FFF;
		if (queue_space > 1) {
			netif_wake_queue(priv->netdev);
			reschedule = 0;
		} else {
			reschedule = 1;
		}
	}

	if (reschedule)
		mod_timer(&priv->tx_timer, jiffies + msecs_to_jiffies(100));
}

static void cnf10k_rfoe_process_rx_pkt(struct cnf10k_rfoe_ndev_priv *priv,
				       struct cnf10k_rx_ft_cfg *ft_cfg,
				       int mbt_buf_idx)
{
	struct otx2_bphy_cdev_priv *cdev_priv = priv->cdev_priv;
	struct mhbw_jd_dma_cfg_word_0_s *jd_dma_cfg_word_0;
	u64 tstamp = 0, mbt_state, jdt_iova_addr;
	struct rfoe_psw_w2_ecpri_s *ecpri_psw_w2;
	struct rfoe_psw_w2_roe_s *rfoe_psw_w2;
	struct cnf10k_rfoe_ndev_priv *priv2;
	struct cnf10k_rfoe_drv_ctx *drv_ctx;
	int found = 0, idx, len, pkt_type;
	struct rfoe_psw_s *psw = NULL;
	struct net_device *netdev;
	u8 *buf_ptr, *jdt_ptr;
	struct sk_buff *skb;
	u8 lmac_id;

	/* read mbt state */
	spin_lock(&cdev_priv->mbt_lock);
	writeq(mbt_buf_idx, (priv->rfoe_reg_base +
			 RFOEX_RX_INDIRECT_INDEX_OFFSET(priv->rfoe_num)));
	mbt_state = readq(priv->rfoe_reg_base +
			  RFOEX_RX_IND_MBT_SEG_STATE(priv->rfoe_num));
	spin_unlock(&cdev_priv->mbt_lock);

	if ((mbt_state >> 16 & 0xf) != 0) {
		pr_err("rx pkt error: mbt_buf_idx=%d, err=%d\n",
		       mbt_buf_idx, (u8)(mbt_state >> 16 & 0xf));
		return;
	}
	if (mbt_state >> 20 & 0x1) {
		pr_err("rx dma error: mbt_buf_idx=%d\n", mbt_buf_idx);
		return;
	}

	buf_ptr = (u8 *)ft_cfg->mbt_virt_addr +
				(ft_cfg->buf_size * mbt_buf_idx);

	pkt_type = ft_cfg->pkt_type;

	psw = (struct rfoe_psw_s *)buf_ptr;
	if (psw->mac_err_sts || psw->mcs_err_sts) {
		net_warn_ratelimited("%s: psw mac_err_sts = 0x%x, mcs_err_sts=0x%x\n",
				     priv->netdev->name,
				     psw->mac_err_sts,
				     psw->mcs_err_sts);
		return;
	}

	if (pkt_type != PACKET_TYPE_ECPRI) {
		/* check that the psw type is correct: */
		if (unlikely(psw->pkt_type == ECPRI)) {
			net_warn_ratelimited("%s: pswt is eCPRI for pkt_type = %d\n",
					     priv->netdev->name, pkt_type);
			return;
		}
		jdt_iova_addr = (u64)psw->jd_ptr;
		rfoe_psw_w2 = (struct rfoe_psw_w2_roe_s *)&psw->proto_sts_word;
		lmac_id = rfoe_psw_w2->lmac_id;
		if (priv->rx_hw_tstamp_en)
			tstamp = psw->ptp_timestamp;
	} else {
		/* check that the psw type is correct: */
		if (unlikely(psw->pkt_type != ECPRI)) {
			net_warn_ratelimited("%s: pswt is not eCPRI for pkt_type = %d\n",
					     priv->netdev->name, pkt_type);
			return;
		}
		jdt_iova_addr = (u64)psw->jd_ptr;
		ecpri_psw_w2 = (struct rfoe_psw_w2_ecpri_s *)
					&psw->proto_sts_word;
		lmac_id = ecpri_psw_w2->lmac_id;
		if (priv->rx_hw_tstamp_en)
			tstamp = psw->ptp_timestamp;
	}

	netif_dbg(priv, rx_status, priv->netdev,
		  "Rx: rfoe=%d lmac=%d mbt_buf_idx=%d\n",
		  priv->rfoe_num, lmac_id, mbt_buf_idx);

	/* read jd ptr from psw */
	jdt_ptr = otx2_iova_to_virt(priv->iommu_domain, jdt_iova_addr);
	jd_dma_cfg_word_0 = (struct mhbw_jd_dma_cfg_word_0_s *)
			((u8 *)jdt_ptr + ft_cfg->jd_rd_offset);
	len = (jd_dma_cfg_word_0->block_size) << 2;
	netif_dbg(priv, rx_status, priv->netdev, "jd rd_dma len = %d\n", len);

	if (unlikely(netif_msg_pktdata(priv))) {
		netdev_printk(KERN_DEBUG, priv->netdev, "RX MBUF DATA:");
		print_hex_dump(KERN_DEBUG, "", DUMP_PREFIX_OFFSET, 16, 4,
			       buf_ptr, len, true);
	}

	buf_ptr += (ft_cfg->pkt_offset * 16);
	len -= (ft_cfg->pkt_offset * 16);

	for (idx = 0; idx < CNF10K_RFOE_MAX_INTF; idx++) {
		drv_ctx = &cnf10k_rfoe_drv_ctx[idx];
		if (drv_ctx->valid && drv_ctx->rfoe_num == priv->rfoe_num &&
		    drv_ctx->lmac_id == lmac_id) {
			found = 1;
			break;
		}
	}
	if (found) {
		netdev = cnf10k_rfoe_drv_ctx[idx].netdev;
		priv2 = netdev_priv(netdev);
	} else {
		pr_err("netdev not found, something went wrong!\n");
		return;
	}

	/* drop the packet if interface is down */
	if (unlikely(!netif_carrier_ok(netdev))) {
		netif_err(priv2, rx_err, netdev,
			  "%s {rfoe%d lmac%d} link down, drop pkt\n",
			  netdev->name, priv2->rfoe_num,
			  priv2->lmac_id);
		/* update stats */
		if (pkt_type == PACKET_TYPE_PTP) {
			priv2->stats.ptp_rx_dropped++;
			priv2->last_rx_ptp_dropped_jiffies = jiffies;
		} else if (pkt_type == PACKET_TYPE_ECPRI) {
			priv2->stats.ecpri_rx_dropped++;
			priv2->last_rx_dropped_jiffies = jiffies;
		} else {
			priv2->stats.rx_dropped++;
			priv2->last_rx_dropped_jiffies = jiffies;
		}
		return;
	}

	skb = netdev_alloc_skb_ip_align(netdev, len);
	if (!skb) {
		netif_err(priv2, rx_err, netdev, "Rx: alloc skb failed\n");
		return;
	}

	memcpy(skb->data, buf_ptr, len);
	skb_put(skb, len);
	skb->protocol = eth_type_trans(skb, netdev);

	if (priv2->rx_hw_tstamp_en)
		skb_hwtstamps(skb)->hwtstamp = ns_to_ktime(tstamp);

	netif_receive_skb(skb);

	/* update stats */
	if (pkt_type == PACKET_TYPE_PTP) {
		priv2->stats.ptp_rx_packets++;
		priv2->last_rx_ptp_jiffies = jiffies;
	} else if (pkt_type == PACKET_TYPE_ECPRI) {
		priv2->stats.ecpri_rx_packets++;
		priv2->last_rx_jiffies = jiffies;
	} else {
		priv2->stats.rx_packets++;
		priv2->last_rx_jiffies = jiffies;
	}
	priv2->stats.rx_bytes += skb->len;
}

static int cnf10k_rfoe_process_rx_flow(struct cnf10k_rfoe_ndev_priv *priv,
				       int pkt_type, int budget)
{
	struct otx2_bphy_cdev_priv *cdev_priv = priv->cdev_priv;
	int count = 0, processed_pkts = 0;
	struct cnf10k_rx_ft_cfg *ft_cfg;
	u64 mbt_cfg;
	u16 nxt_buf;
	int *mbt_last_idx = &priv->rfoe_common->rx_mbt_last_idx[pkt_type];
	u16 *prv_nxt_buf = &priv->rfoe_common->nxt_buf[pkt_type];

	ft_cfg = &priv->rx_ft_cfg[pkt_type];

	spin_lock(&cdev_priv->mbt_lock);
	/* read mbt nxt_buf */
	writeq(ft_cfg->mbt_idx,
	       priv->rfoe_reg_base +
	       RFOEX_RX_INDIRECT_INDEX_OFFSET(priv->rfoe_num));
	mbt_cfg = readq(priv->rfoe_reg_base +
			RFOEX_RX_IND_MBT_CFG(priv->rfoe_num));
	spin_unlock(&cdev_priv->mbt_lock);

	nxt_buf = (mbt_cfg >> 32) & 0xffff;

	/* no mbt entries to process */
	if (nxt_buf == *prv_nxt_buf) {
		netif_dbg(priv, rx_status, priv->netdev,
			  "no rx packets to process, rfoe=%d pkt_type=%d mbt_idx=%d nxt_buf=%d mbt_buf_sw_head=%d\n",
			  priv->rfoe_num, pkt_type, ft_cfg->mbt_idx, nxt_buf,
			  *mbt_last_idx);
		return 0;
	}

	*prv_nxt_buf = nxt_buf;

	/* get count of pkts to process, check ring wrap condition */
	if (*mbt_last_idx > nxt_buf) {
		count = ft_cfg->num_bufs - *mbt_last_idx;
		count += nxt_buf;
	} else {
		count = nxt_buf - *mbt_last_idx;
	}

	netif_dbg(priv, rx_status, priv->netdev,
		  "rfoe=%d pkt_type=%d mbt_idx=%d nxt_buf=%d mbt_buf_sw_head=%d count=%d\n",
		  priv->rfoe_num, pkt_type, ft_cfg->mbt_idx, nxt_buf,
		  *mbt_last_idx, count);

	while (likely((processed_pkts < budget) && (processed_pkts < count))) {
		cnf10k_rfoe_process_rx_pkt(priv, ft_cfg, *mbt_last_idx);

		(*mbt_last_idx)++;
		if (*mbt_last_idx == ft_cfg->num_bufs)
			*mbt_last_idx = 0;

		processed_pkts++;
	}

	return processed_pkts;
}

/* napi poll routine */
static int cnf10k_rfoe_napi_poll(struct napi_struct *napi, int budget)
{
	struct cnf10k_rfoe_ndev_priv *priv;
	struct otx2_bphy_cdev_priv *cdev_priv;
	int workdone = 0, pkt_type;
	struct cnf10k_rx_ft_cfg *ft_cfg;
	u64 intr_en, regval;

	ft_cfg = container_of(napi, struct cnf10k_rx_ft_cfg, napi);
	priv = ft_cfg->priv;
	cdev_priv = priv->cdev_priv;
	pkt_type = ft_cfg->pkt_type;

	/* pkt processing loop */
	workdone += cnf10k_rfoe_process_rx_flow(priv, pkt_type, budget);

	if (workdone < budget) {
		napi_complete_done(napi, workdone);

		/* Re enable the Rx interrupts */
		intr_en = PKT_TYPE_TO_INTR(pkt_type) <<
				CNF10K_RFOE_RX_INTR_SHIFT(priv->rfoe_num);
		spin_lock(&cdev_priv->lock);
		regval = readq(bphy_reg_base + PSM_INT_GP_ENA_W1S(1));
		regval |= intr_en;
		writeq(regval, bphy_reg_base + PSM_INT_GP_ENA_W1S(1));
		spin_unlock(&cdev_priv->lock);
	}

	return workdone;
}

/* Rx GPINT napi schedule api */
void cnf10k_rfoe_rx_napi_schedule(int rfoe_num, u32 status)
{
	enum bphy_netdev_packet_type pkt_type;
	struct cnf10k_rfoe_drv_ctx *drv_ctx;
	struct cnf10k_rfoe_ndev_priv *priv;
	struct cnf10k_rx_ft_cfg *ft_cfg;
	int intf, bit_idx;
	u32 intr_sts;
	u64 regval;

	for (intf = 0; intf < CNF10K_RFOE_MAX_INTF; intf++) {
		drv_ctx = &cnf10k_rfoe_drv_ctx[intf];
		/* ignore lmac, one interrupt/pkt_type/rfoe */
		if (!(drv_ctx->valid && drv_ctx->rfoe_num == rfoe_num))
			continue;
		/* check if i/f down, napi disabled */
		priv = netdev_priv(drv_ctx->netdev);
		if (test_bit(RFOE_INTF_DOWN, &priv->state))
			continue;
		/* check rx pkt type */
		intr_sts = ((status >> CNF10K_RFOE_RX_INTR_SHIFT(rfoe_num)) &
			    RFOE_RX_INTR_EN);
		for (bit_idx = 0; bit_idx < PACKET_TYPE_MAX; bit_idx++) {
			if (!(intr_sts & BIT(bit_idx)))
				continue;
			pkt_type = INTR_TO_PKT_TYPE(bit_idx);
			if (unlikely(!(priv->pkt_type_mask & (1U << pkt_type))))
				continue;
			/* clear intr enable bit, re-enable in napi handler */
			regval = PKT_TYPE_TO_INTR(pkt_type) <<
				 CNF10K_RFOE_RX_INTR_SHIFT(rfoe_num);
			writeq(regval, bphy_reg_base + PSM_INT_GP_ENA_W1C(1));
			/* schedule napi */
			ft_cfg = &drv_ctx->ft_cfg[pkt_type];
			napi_schedule(&ft_cfg->napi);
		}
		/* napi scheduled per pkt_type, return */
		return;
	}
}

static void cnf10k_rfoe_get_stats64(struct net_device *netdev,
				    struct rtnl_link_stats64 *stats)
{
	struct cnf10k_rfoe_ndev_priv *priv = netdev_priv(netdev);
	struct otx2_rfoe_stats *dev_stats = &priv->stats;

	stats->rx_bytes = dev_stats->rx_bytes;
	stats->rx_packets = dev_stats->rx_packets +
			    dev_stats->ptp_rx_packets +
			    dev_stats->ecpri_rx_packets;
	stats->rx_dropped = dev_stats->rx_dropped +
			    dev_stats->ptp_rx_dropped +
			    dev_stats->ecpri_rx_dropped;

	stats->tx_bytes = dev_stats->tx_bytes;
	stats->tx_packets = dev_stats->tx_packets +
			    dev_stats->ptp_tx_packets +
			    dev_stats->ecpri_tx_packets;
	stats->tx_dropped = dev_stats->tx_dropped +
			    dev_stats->ptp_tx_dropped +
			    dev_stats->ecpri_tx_dropped;
}

static int cnf10k_rfoe_config_hwtstamp(struct net_device *netdev,
				       struct ifreq *ifr)
{
	struct cnf10k_rfoe_ndev_priv *priv = netdev_priv(netdev);
	struct hwtstamp_config config;

	if (copy_from_user(&config, ifr->ifr_data, sizeof(config)))
		return -EFAULT;

	/* reserved for future extensions */
	if (config.flags)
		return -EINVAL;

	/* ptp hw timestamp is always enabled, mark the sw flags
	 * so that tx ptp requests are submitted to ptp psm queue
	 * and rx timestamp is copied to skb
	 */

	switch (config.tx_type) {
	case HWTSTAMP_TX_OFF:
		priv->tx_hw_tstamp_en = 0;
		break;
	case HWTSTAMP_TX_ON:
		priv->tx_hw_tstamp_en = 1;
		break;
	default:
		return -ERANGE;
	}

	switch (config.rx_filter) {
	case HWTSTAMP_FILTER_NONE:
		priv->rx_hw_tstamp_en = 0;
		break;
	case HWTSTAMP_FILTER_ALL:
	case HWTSTAMP_FILTER_SOME:
	case HWTSTAMP_FILTER_PTP_V1_L4_EVENT:
	case HWTSTAMP_FILTER_PTP_V1_L4_SYNC:
	case HWTSTAMP_FILTER_PTP_V1_L4_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_L4_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L4_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_L2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L2_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_DELAY_REQ:
		priv->rx_hw_tstamp_en = 1;
		break;
	default:
		return -ERANGE;
	}

	if (copy_to_user(ifr->ifr_data, &config, sizeof(config)))
		return -EFAULT;

	return 0;
}

/* netdev ioctl */
static int cnf10k_rfoe_ioctl(struct net_device *netdev, struct ifreq *req,
			     int cmd)
{
	switch (cmd) {
	case SIOCSHWTSTAMP:
		return cnf10k_rfoe_config_hwtstamp(netdev, req);
	default:
		return -EOPNOTSUPP;
	}
}

/* netdev xmit */
static netdev_tx_t cnf10k_rfoe_eth_start_xmit(struct sk_buff *skb,
					      struct net_device *netdev)
{
	struct cnf10k_rfoe_ndev_priv *priv = netdev_priv(netdev);
	struct mhbw_jd_dma_cfg_word_0_s *jd_dma_cfg_word_0;
	struct mhbw_jd_dma_cfg_word_1_s *jd_dma_cfg_word_1;
	struct mhab_job_desc_cfg *jd_cfg_ptr;
	struct rfoe_tx_ptp_tstmp_s *tx_tstmp;
	struct psm_cmd_addjob_s *psm_cmd_lo;
	struct tx_job_queue_cfg *job_cfg;
	struct tx_job_entry *job_entry;
	struct ptp_tstamp_skb *ts_skb;
	int psm_queue_id, queue_space;
	u64 jd_cfg_ptr_iova, regval;
	unsigned long flags;
	struct ethhdr *eth;
	int pkt_type = 0;

	if (unlikely(skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP)) {
		if (!priv->tx_hw_tstamp_en) {
			netif_dbg(priv, tx_queued, priv->netdev,
				  "skb HW timestamp requested but not enabled, this packet will not be timestamped\n");
			job_cfg = &priv->rfoe_common->tx_oth_job_cfg;
			pkt_type = PACKET_TYPE_OTHER;
		} else {
			job_cfg = &priv->tx_ptp_job_cfg;
			pkt_type = PACKET_TYPE_PTP;
		}
	} else {
		job_cfg = &priv->rfoe_common->tx_oth_job_cfg;
		eth = (struct ethhdr *)skb->data;
		if (htons(eth->h_proto) == ETH_P_ECPRI)
			pkt_type = PACKET_TYPE_ECPRI;
		else
			pkt_type = PACKET_TYPE_OTHER;
	}

	spin_lock_irqsave(&job_cfg->lock, flags);

	if (unlikely(priv->if_type != IF_TYPE_ETHERNET)) {
		netif_err(priv, tx_queued, netdev,
			  "%s {rfoe%d lmac%d} invalid intf mode, drop pkt\n",
			  netdev->name, priv->rfoe_num, priv->lmac_id);
		/* update stats */
		priv->stats.tx_dropped++;
		priv->last_tx_dropped_jiffies = jiffies;
		goto exit;
	}

	if (unlikely(!netif_carrier_ok(netdev))) {
		netif_err(priv, tx_err, netdev,
			  "%s {rfoe%d lmac%d} link down, drop pkt\n",
			  netdev->name, priv->rfoe_num,
			  priv->lmac_id);
		/* update stats */
		if (pkt_type == PACKET_TYPE_ECPRI) {
			priv->stats.ecpri_tx_dropped++;
			priv->last_tx_dropped_jiffies = jiffies;
		} else if (pkt_type == PACKET_TYPE_PTP) {
			priv->stats.ptp_tx_dropped++;
			priv->last_tx_ptp_dropped_jiffies = jiffies;
		} else {
			priv->stats.tx_dropped++;
			priv->last_tx_dropped_jiffies = jiffies;
		}

		goto exit;
	}

	if (unlikely(!(priv->pkt_type_mask & (1U << pkt_type)))) {
		netif_err(priv, tx_queued, netdev,
			  "%s {rfoe%d lmac%d} pkt not supported, drop pkt\n",
			  netdev->name, priv->rfoe_num,
			  priv->lmac_id);
		/* update stats */
		if (pkt_type == PACKET_TYPE_ECPRI) {
			priv->stats.ecpri_tx_dropped++;
			priv->last_tx_dropped_jiffies = jiffies;
		} else if (pkt_type == PACKET_TYPE_PTP) {
			priv->stats.ptp_tx_dropped++;
			priv->last_tx_ptp_dropped_jiffies = jiffies;
		} else {
			priv->stats.tx_dropped++;
			priv->last_tx_dropped_jiffies = jiffies;
		}

		goto exit;
	}

	/* get psm queue number */
	psm_queue_id = job_cfg->psm_queue_id;
	netif_dbg(priv, tx_queued, priv->netdev,
		  "psm: queue(%d): cfg=0x%llx ptr=0x%llx space=0x%llx\n",
		  psm_queue_id,
		  readq(priv->psm_reg_base + PSM_QUEUE_CFG(psm_queue_id)),
		  readq(priv->psm_reg_base + PSM_QUEUE_PTR(psm_queue_id)),
		  readq(priv->psm_reg_base + PSM_QUEUE_SPACE(psm_queue_id)));

	/* check psm queue space available */
	regval = readq(priv->psm_reg_base + PSM_QUEUE_SPACE(psm_queue_id));
	queue_space = regval & 0x7FFF;
	if (queue_space < 1 && pkt_type != PACKET_TYPE_PTP) {
		netif_err(priv, tx_err, netdev,
			  "no space in psm queue %d, dropping pkt\n",
			   psm_queue_id);
		netif_stop_queue(netdev);
		dev_kfree_skb_any(skb);
		/* update stats */
		if (pkt_type == PACKET_TYPE_ECPRI)
			priv->stats.ecpri_tx_dropped++;
		else
			priv->stats.tx_dropped++;

		priv->last_tx_dropped_jiffies = jiffies;

		mod_timer(&priv->tx_timer, jiffies + msecs_to_jiffies(100));
		spin_unlock_irqrestore(&job_cfg->lock, flags);
		return NETDEV_TX_OK;
	}

	/* get the tx job entry */
	job_entry = (struct tx_job_entry *)
				&job_cfg->job_entries[job_cfg->q_idx];

	netif_dbg(priv, tx_queued, priv->netdev,
		  "rfoe=%d lmac=%d psm_queue=%d tx_job_entry %d job_cmd_lo=0x%llx job_cmd_high=0x%llx jd_iova_addr=0x%llx\n",
		  priv->rfoe_num, priv->lmac_id, psm_queue_id, job_cfg->q_idx,
		  job_entry->job_cmd_lo, job_entry->job_cmd_hi,
		  job_entry->jd_iova_addr);

	/* hw timestamp */
	if (unlikely(skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP) &&
	    priv->tx_hw_tstamp_en) {
		if (list_empty(&priv->ptp_skb_list.list) &&
		    !test_and_set_bit_lock(PTP_TX_IN_PROGRESS, &priv->state)) {
			skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;
			priv->ptp_tx_skb = skb;
			psm_cmd_lo = (struct psm_cmd_addjob_s *)
						&job_entry->job_cmd_lo;
			priv->ptp_job_tag = psm_cmd_lo->jobtag;

			tx_tstmp = (struct rfoe_tx_ptp_tstmp_s *)
				   ((u8 *)priv->ptp_ring_cfg.ptp_ring_base +
				    (128 * priv->ptp_ring_cfg.ptp_ring_idx));
			tx_tstmp->valid = 0;
		} else {
			/* check ptp queue count */
			if (priv->ptp_skb_list.count >= max_ptp_req) {
				netif_err(priv, tx_err, netdev,
					  "ptp list full, dropping pkt\n");
				priv->stats.ptp_tx_dropped++;
				priv->last_tx_ptp_dropped_jiffies = jiffies;
				goto exit;
			}
			/* allocate and add ptp req to queue */
			ts_skb = kmalloc(sizeof(*ts_skb), GFP_ATOMIC);
			if (!ts_skb) {
				priv->stats.ptp_tx_dropped++;
				priv->last_tx_ptp_dropped_jiffies = jiffies;
				goto exit;
			}
			ts_skb->skb = skb;
			list_add_tail(&ts_skb->list, &priv->ptp_skb_list.list);
			priv->ptp_skb_list.count++;
			skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;
			priv->stats.ptp_tx_packets++;
			priv->stats.tx_bytes += skb->len;
			/* sw timestamp */
			skb_tx_timestamp(skb);
			goto exit;	/* submit the packet later */
		}
	}

	/* sw timestamp */
	skb_tx_timestamp(skb);

	if (unlikely(netif_msg_pktdata(priv))) {
		netdev_printk(KERN_DEBUG, priv->netdev, "Tx: skb %pS len=%d\n",
			      skb, skb->len);
		print_hex_dump(KERN_DEBUG, "", DUMP_PREFIX_OFFSET, 16, 4,
			       skb->data, skb->len, true);
	}

	/* update length and block size in jd dma cfg word */
	jd_cfg_ptr_iova = *(u64 *)((u8 *)job_entry->jd_ptr + 8);
	jd_cfg_ptr = otx2_iova_to_virt(priv->iommu_domain, jd_cfg_ptr_iova);
	jd_cfg_ptr->cfg3.pkt_len = skb->len;
	jd_dma_cfg_word_0 = (struct mhbw_jd_dma_cfg_word_0_s *)
						job_entry->rd_dma_ptr;
	jd_dma_cfg_word_0->block_size = (((skb->len + 15) >> 4) * 4);

	/* update rfoe_mode and lmac id for non-ptp (shared) psm job entry */
	if (pkt_type != PACKET_TYPE_PTP) {
		jd_cfg_ptr->cfg3.lmacid = priv->lmac_id & 0x3;
		if (pkt_type == PACKET_TYPE_ECPRI)
			jd_cfg_ptr->cfg.rfoe_mode = 1;
		else
			jd_cfg_ptr->cfg.rfoe_mode = 0;
	}

	/* copy packet data to rd_dma_ptr start addr */
	jd_dma_cfg_word_1 = (struct mhbw_jd_dma_cfg_word_1_s *)
					((u8 *)job_entry->rd_dma_ptr + 8);
	memcpy(otx2_iova_to_virt(priv->iommu_domain,
				 jd_dma_cfg_word_1->start_addr),
	       skb->data, skb->len);

	/* make sure that all memory writes are completed */
	dma_wmb();

	/* submit PSM job */
	writeq(job_entry->job_cmd_lo,
	       priv->psm_reg_base + PSM_QUEUE_CMD_LO(psm_queue_id));
	writeq(job_entry->job_cmd_hi,
	       priv->psm_reg_base + PSM_QUEUE_CMD_HI(psm_queue_id));

	/* update stats */
	if (pkt_type == PACKET_TYPE_ECPRI) {
		priv->stats.ecpri_tx_packets++;
		priv->last_tx_jiffies = jiffies;
	} else if (pkt_type == PACKET_TYPE_PTP) {
		priv->stats.ptp_tx_packets++;
		priv->last_tx_ptp_jiffies = jiffies;
	} else {
		priv->stats.tx_packets++;
		priv->last_tx_jiffies = jiffies;
	}
	priv->stats.tx_bytes += skb->len;

	/* increment queue index */
	job_cfg->q_idx++;
	if (job_cfg->q_idx == job_cfg->num_entries)
		job_cfg->q_idx = 0;
exit:
	if (!(skb_shinfo(skb)->tx_flags & SKBTX_IN_PROGRESS))
		dev_kfree_skb_any(skb);

	spin_unlock_irqrestore(&job_cfg->lock, flags);

	return NETDEV_TX_OK;
}

/* netdev open */
static int cnf10k_rfoe_eth_open(struct net_device *netdev)
{
	struct cnf10k_rfoe_ndev_priv *priv = netdev_priv(netdev);
	int idx;

	for (idx = 0; idx < PACKET_TYPE_MAX; idx++) {
		if (!(priv->pkt_type_mask & (1U << idx)))
			continue;
		napi_enable(&priv->rx_ft_cfg[idx].napi);
	}

	priv->ptp_tx_skb = NULL;

	netif_carrier_on(netdev);
	netif_start_queue(netdev);

	clear_bit(RFOE_INTF_DOWN, &priv->state);
	priv->link_state = 1;

	return 0;
}

/* netdev close */
static int cnf10k_rfoe_eth_stop(struct net_device *netdev)
{
	struct cnf10k_rfoe_ndev_priv *priv = netdev_priv(netdev);
	struct ptp_tstamp_skb *ts_skb, *ts_skb2;
	int idx;

	set_bit(RFOE_INTF_DOWN, &priv->state);

	netif_stop_queue(netdev);
	netif_carrier_off(netdev);
	priv->link_state = 0;

	for (idx = 0; idx < PACKET_TYPE_MAX; idx++) {
		if (!(priv->pkt_type_mask & (1U << idx)))
			continue;
		napi_disable(&priv->rx_ft_cfg[idx].napi);
	}

	del_timer_sync(&priv->tx_timer);

	/* cancel any pending ptp work item in progress */
	cancel_work_sync(&priv->ptp_tx_work);
	if (priv->ptp_tx_skb) {
		dev_kfree_skb_any(priv->ptp_tx_skb);
		priv->ptp_tx_skb = NULL;
		clear_bit_unlock(PTP_TX_IN_PROGRESS, &priv->state);
	}

	/* clear ptp skb list */
	cancel_work_sync(&priv->ptp_queue_work);
	list_for_each_entry_safe(ts_skb, ts_skb2,
				 &priv->ptp_skb_list.list, list) {
		list_del(&ts_skb->list);
		kfree(ts_skb);
	}
	priv->ptp_skb_list.count = 0;

	return 0;
}

static int cnf10k_rfoe_init(struct net_device *netdev)
{
	struct cnf10k_rfoe_ndev_priv *priv = netdev_priv(netdev);

	/* Enable VLAN TPID match */
	writeq(0x18100, (priv->rfoe_reg_base +
			 RFOEX_RX_VLANX_CFG(priv->rfoe_num, 0)));
	netdev->features |= NETIF_F_HW_VLAN_CTAG_FILTER;

	return 0;
}

static int cnf10k_rfoe_vlan_rx_configure(struct net_device *netdev, u16 vid,
					 bool forward)
{
	struct cnf10k_rfoe_ndev_priv *priv = netdev_priv(netdev);
	struct otx2_bphy_cdev_priv *cdev_priv = priv->cdev_priv;
	struct rfoe_rx_ind_vlanx_fwd fwd;
	unsigned long flags;
	u64 mask, index;

	if (vid >= VLAN_N_VID) {
		netdev_err(netdev, "Invalid VLAN ID %d\n", vid);
		return -EINVAL;
	}

	mask = (0x1ll << (vid & 0x3F));
	index = (vid >> 6) & 0x3F;

	spin_lock_irqsave(&cdev_priv->mbt_lock, flags);

	if (forward && priv->rfoe_common->rx_vlan_fwd_refcnt[vid]++)
		goto out;

	if (!forward && --priv->rfoe_common->rx_vlan_fwd_refcnt[vid])
		goto out;

	/* read current fwd mask */
	writeq(index, (priv->rfoe_reg_base +
		       RFOEX_RX_INDIRECT_INDEX_OFFSET(priv->rfoe_num)));
	fwd.fwd = readq(priv->rfoe_reg_base +
			RFOEX_RX_IND_VLANX_FWD(priv->rfoe_num, 0));

	if (forward)
		fwd.fwd |= mask;
	else
		fwd.fwd &= ~mask;

	/* write the new fwd mask */
	writeq(index, (priv->rfoe_reg_base +
		       RFOEX_RX_INDIRECT_INDEX_OFFSET(priv->rfoe_num)));
	writeq(fwd.fwd, (priv->rfoe_reg_base +
			 RFOEX_RX_IND_VLANX_FWD(priv->rfoe_num, 0)));

out:
	spin_unlock_irqrestore(&cdev_priv->mbt_lock, flags);

	return 0;
}

static int cnf10k_rfoe_vlan_rx_add(struct net_device *netdev, __be16 proto,
				   u16 vid)
{
	return cnf10k_rfoe_vlan_rx_configure(netdev, vid, true);
}

static int cnf10k_rfoe_vlan_rx_kill(struct net_device *netdev, __be16 proto,
				    u16 vid)
{
	return cnf10k_rfoe_vlan_rx_configure(netdev, vid, false);
}

static const struct net_device_ops cnf10k_rfoe_netdev_ops = {
	.ndo_init		= cnf10k_rfoe_init,
	.ndo_open		= cnf10k_rfoe_eth_open,
	.ndo_stop		= cnf10k_rfoe_eth_stop,
	.ndo_start_xmit		= cnf10k_rfoe_eth_start_xmit,
	.ndo_do_ioctl		= cnf10k_rfoe_ioctl,
	.ndo_set_mac_address	= eth_mac_addr,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_get_stats64	= cnf10k_rfoe_get_stats64,
	.ndo_vlan_rx_add_vid	= cnf10k_rfoe_vlan_rx_add,
	.ndo_vlan_rx_kill_vid	= cnf10k_rfoe_vlan_rx_kill,
};

static void cnf10k_rfoe_dump_rx_ft_cfg(struct cnf10k_rfoe_ndev_priv *priv)
{
	struct cnf10k_rx_ft_cfg *ft_cfg;
	int idx;

	for (idx = 0; idx < PACKET_TYPE_MAX; idx++) {
		if (!(priv->pkt_type_mask & (1U << idx)))
			continue;
		ft_cfg = &priv->rx_ft_cfg[idx];
		pr_debug("rfoe=%d lmac=%d pkttype=%d flowid=%d mbt: idx=%d size=%d nbufs=%d iova=0x%llx jdt: idx=%d size=%d num_jd=%d iova=0x%llx\n",
			 priv->rfoe_num, priv->lmac_id, ft_cfg->pkt_type,
			 ft_cfg->flow_id, ft_cfg->mbt_idx, ft_cfg->buf_size,
			 ft_cfg->num_bufs, ft_cfg->mbt_iova_addr,
			 ft_cfg->jdt_idx, ft_cfg->jd_size, ft_cfg->num_jd,
			 ft_cfg->jdt_iova_addr);
	}
}

static void cnf10k_rfoe_fill_rx_ft_cfg(struct cnf10k_rfoe_ndev_priv *priv,
				       struct cnf10k_bphy_ndev_comm_if *if_cfg)
{
	struct otx2_bphy_cdev_priv *cdev_priv = priv->cdev_priv;
	struct cnf10k_bphy_ndev_rbuf_info *rbuf_info;
	struct cnf10k_rx_ft_cfg *ft_cfg;
	u64 jdt_cfg0, iova;
	int idx;

	/* RX flow table configuration */
	for (idx = 0; idx < PACKET_TYPE_MAX; idx++) {
		if (!(priv->pkt_type_mask & (1U << idx)))
			continue;
		ft_cfg = &priv->rx_ft_cfg[idx];
		rbuf_info = &if_cfg->rbuf_info[idx];
		ft_cfg->pkt_type = rbuf_info->pkt_type;
		ft_cfg->gp_int_num = rbuf_info->gp_int_num;
		ft_cfg->flow_id = rbuf_info->flow_id;
		ft_cfg->mbt_idx = rbuf_info->mbt_index;
		ft_cfg->buf_size = rbuf_info->buf_size * 16;
		ft_cfg->num_bufs = rbuf_info->num_bufs;
		ft_cfg->mbt_iova_addr = rbuf_info->mbt_iova_addr;
		iova = ft_cfg->mbt_iova_addr;
		ft_cfg->mbt_virt_addr = otx2_iova_to_virt(priv->iommu_domain,
							  iova);
		ft_cfg->jdt_idx = rbuf_info->jdt_index;
		ft_cfg->jd_size = rbuf_info->jd_size * 8;
		ft_cfg->num_jd = rbuf_info->num_jd;
		ft_cfg->jdt_iova_addr = rbuf_info->jdt_iova_addr;
		iova = ft_cfg->jdt_iova_addr;
		ft_cfg->jdt_virt_addr = otx2_iova_to_virt(priv->iommu_domain,
							  iova);
		spin_lock(&cdev_priv->mbt_lock);
		writeq(ft_cfg->jdt_idx,
		       (priv->rfoe_reg_base +
			RFOEX_RX_INDIRECT_INDEX_OFFSET(priv->rfoe_num)));
		jdt_cfg0 = readq(priv->rfoe_reg_base +
				 RFOEX_RX_IND_JDT_CFG0(priv->rfoe_num));
		spin_unlock(&cdev_priv->mbt_lock);
		ft_cfg->jd_rd_offset = ((jdt_cfg0 >> 27) & 0x3f) * 8;
		ft_cfg->pkt_offset = (u8)((jdt_cfg0 >> 52) & 0x1f);
		ft_cfg->priv = priv;
		netif_napi_add(priv->netdev, &ft_cfg->napi,
			       cnf10k_rfoe_napi_poll);
	}
}

static void cnf10k_rfoe_fill_tx_job_entries(struct cnf10k_rfoe_ndev_priv *priv,
					    struct tx_job_queue_cfg *job_cfg,
				struct cnf10k_bphy_ndev_tx_psm_cmd_info *tx_job,
					    int num_entries)
{
	struct tx_job_entry *job_entry;
	u64 jd_cfg_iova, iova;
	int i;

	for (i = 0; i < num_entries; i++) {
		job_entry = &job_cfg->job_entries[i];
		job_entry->job_cmd_lo = tx_job->low_cmd;
		job_entry->job_cmd_hi = tx_job->high_cmd;
		job_entry->jd_iova_addr = tx_job->jd_iova_addr;
		iova = job_entry->jd_iova_addr;
		job_entry->jd_ptr = otx2_iova_to_virt(priv->iommu_domain, iova);
		jd_cfg_iova = *(u64 *)((u8 *)job_entry->jd_ptr + 8);
		job_entry->jd_cfg_ptr = otx2_iova_to_virt(priv->iommu_domain,
							  jd_cfg_iova);
		job_entry->rd_dma_iova_addr = tx_job->rd_dma_iova_addr;
		iova = job_entry->rd_dma_iova_addr;
		job_entry->rd_dma_ptr = otx2_iova_to_virt(priv->iommu_domain,
							  iova);
		pr_debug("job_cmd_lo=0x%llx job_cmd_hi=0x%llx jd_iova_addr=0x%llx rd_dma_iova_addr=%llx\n",
			 tx_job->low_cmd, tx_job->high_cmd,
			 tx_job->jd_iova_addr, tx_job->rd_dma_iova_addr);
		tx_job++;
	}
	/* get psm queue id */
	job_entry = &job_cfg->job_entries[0];
	job_cfg->psm_queue_id = (job_entry->job_cmd_lo >> 8) & 0xff;
	job_cfg->q_idx = 0;
	job_cfg->num_entries = num_entries;
	spin_lock_init(&job_cfg->lock);
}

int cnf10k_rfoe_parse_and_init_intf(struct otx2_bphy_cdev_priv *cdev,
				    struct cnf10k_rfoe_ndev_comm_intf_cfg *cfg)
{
	int i, intf_idx = 0, num_entries, lmac, idx, ret;
	struct cnf10k_bphy_ndev_tx_psm_cmd_info *tx_info;
	struct cnf10k_bphy_ndev_tx_ptp_ring_info *info;
	struct cnf10k_rfoe_drv_ctx *drv_ctx = NULL;
	struct cnf10k_rfoe_ndev_priv *priv, *priv2;
	struct cnf10k_bphy_ndev_rfoe_if *rfoe_cfg;
	struct cnf10k_bphy_ndev_comm_if *if_cfg;
	struct tx_ptp_ring_cfg *ptp_ring_cfg;
	struct tx_job_queue_cfg *tx_cfg;
	struct cnf10k_rx_ft_cfg *ft_cfg;
	struct net_device *netdev;
	u8 pkt_type_mask;

	cdev->hw_version = cfg->hw_params.chip_ver;
	dev_dbg(cdev->dev, "hw_version = 0x%x\n", cfg->hw_params.chip_ver);

	if (CHIP_CNF10KB(cdev->hw_version)) {
		cdev->num_rfoe_mhab = 7;
		cdev->num_rfoe_lmac = 2;
		cdev->tot_rfoe_intf = 14;
	} else if (CHIP_CNF10KA(cdev->hw_version)) {
		cdev->num_rfoe_mhab = 2;
		cdev->num_rfoe_lmac = 4;
		cdev->tot_rfoe_intf = 8;
	} else {
		dev_err(cdev->dev, "unsupported chip version\n");
		return -EINVAL;
	}

	for (i = 0; i < BPHY_MAX_RFOE_MHAB; i++) {
		priv2 = NULL;
		rfoe_cfg = &cfg->rfoe_if_cfg[i];
		pkt_type_mask = rfoe_cfg->pkt_type_mask;
		for (lmac = 0; lmac < MAX_LMAC_PER_RFOE; lmac++) {
			if_cfg = &rfoe_cfg->if_cfg[lmac];
			/* check if lmac is valid */
			if (!if_cfg->lmac_info.is_valid) {
				dev_dbg(cdev->dev,
					"rfoe%d lmac%d invalid, skipping\n",
					i, lmac);
				continue;
			}
			if (lmac >= cdev->num_rfoe_lmac) {
				dev_dbg(cdev->dev,
					"rfoe%d, lmac%d not supported, skipping\n",
					i, lmac);
				continue;
			}
			netdev = alloc_etherdev(sizeof(*priv));
			if (!netdev) {
				dev_err(cdev->dev,
					"error allocating net device\n");
				ret = -ENOMEM;
				goto err_exit;
			}
			priv = netdev_priv(netdev);
			memset(priv, 0, sizeof(*priv));
			if (!priv2) {
				priv->rfoe_common =
					kzalloc(sizeof(struct rfoe_common_cfg),
						GFP_KERNEL);
				if (!priv->rfoe_common) {
					dev_err(cdev->dev, "kzalloc failed\n");
					free_netdev(netdev);
					ret = -ENOMEM;
					goto err_exit;
				}
				priv->rfoe_common->refcnt = 1;
			}
			spin_lock_init(&priv->lock);
			priv->netdev = netdev;
			priv->cdev_priv = cdev;
			priv->msg_enable = netif_msg_init(-1, 0);
			spin_lock_init(&priv->stats.lock);
			priv->rfoe_num = if_cfg->lmac_info.rfoe_num;
			priv->lmac_id = if_cfg->lmac_info.lane_num;
			priv->if_type = IF_TYPE_ETHERNET;
			memcpy(priv->mac_addr, if_cfg->lmac_info.eth_addr,
			       ETH_ALEN);
			if (is_valid_ether_addr(priv->mac_addr))
				eth_hw_addr_set(netdev,
						priv->mac_addr);
			else
				eth_hw_addr_random(netdev);
			priv->pdev = pci_get_device(OTX2_BPHY_PCI_VENDOR_ID,
						    OTX2_BPHY_PCI_DEVICE_ID,
						    NULL);
			priv->iommu_domain =
				iommu_get_domain_for_dev(&priv->pdev->dev);
			priv->bphy_reg_base = bphy_reg_base;
			priv->psm_reg_base = psm_reg_base;
			priv->rfoe_reg_base = rfoe_reg_base;
			priv->bcn_reg_base = bcn_reg_base;
			priv->ptp_reg_base = ptp_reg_base;

			/* Initialise PTP TX work queue */
			INIT_WORK(&priv->ptp_tx_work, cnf10k_rfoe_ptp_tx_work);
			INIT_WORK(&priv->ptp_queue_work,
				  cnf10k_rfoe_ptp_submit_work);

			/* Initialise PTP skb list */
			INIT_LIST_HEAD(&priv->ptp_skb_list.list);
			priv->ptp_skb_list.count = 0;
			timer_setup(&priv->tx_timer,
				    cnf10k_rfoe_tx_timer_cb, 0);

			priv->pkt_type_mask = pkt_type_mask;
			cnf10k_rfoe_fill_rx_ft_cfg(priv, if_cfg);
			cnf10k_rfoe_dump_rx_ft_cfg(priv);

			/* TX PTP job configuration */
			if (priv->pkt_type_mask & (1U << PACKET_TYPE_PTP)) {
				tx_cfg = &priv->tx_ptp_job_cfg;
				tx_info = &if_cfg->ptp_pkt_info[0];
				num_entries = MAX_PTP_MSG_PER_LMAC;
				cnf10k_rfoe_fill_tx_job_entries(priv, tx_cfg,
								tx_info,
								num_entries);
				/* fill ptp ring info */
				ptp_ring_cfg = &priv->ptp_ring_cfg;
				info = &if_cfg->ptp_ts_ring_info[0];
				ptp_ring_cfg->ptp_ring_base =
					otx2_iova_to_virt(priv->iommu_domain,
							  info->ring_iova_addr);
				ptp_ring_cfg->ptp_ring_id = info->ring_idx;
				ptp_ring_cfg->ptp_ring_size = info->ring_size;
				ptp_ring_cfg->ptp_ring_idx = 0;
			}

			/* TX ECPRI/OTH(PTP) job configuration */
			if (!priv2 &&
			    ((priv->pkt_type_mask &
			      (1U << PACKET_TYPE_OTHER)) ||
			     (priv->pkt_type_mask &
			      (1U << PACKET_TYPE_ECPRI)))) {
				num_entries = cdev->num_rfoe_lmac *
						MAX_OTH_MSG_PER_LMAC;
				tx_cfg = &priv->rfoe_common->tx_oth_job_cfg;
				tx_info = &rfoe_cfg->oth_pkt_info[0];
				cnf10k_rfoe_fill_tx_job_entries(priv, tx_cfg,
								tx_info,
								num_entries);
			} else {
				/* share rfoe_common data */
				priv->rfoe_common = priv2->rfoe_common;
				++(priv->rfoe_common->refcnt);
			}

			/* keep last (rfoe + lmac) priv structure */
			if (!priv2)
				priv2 = priv;

			intf_idx = (i * cdev->num_rfoe_lmac) + lmac;
			snprintf(netdev->name, sizeof(netdev->name),
				 "rfoe%d", intf_idx);
			netdev->netdev_ops = &cnf10k_rfoe_netdev_ops;
			cnf10k_rfoe_set_ethtool_ops(netdev);
			cnf10k_rfoe_ptp_init(priv);
			netdev->watchdog_timeo = (15 * HZ);
			netdev->mtu = 1500U;
			netdev->min_mtu = ETH_MIN_MTU;
			netdev->max_mtu = 1500U;
			ret = register_netdev(netdev);
			if (ret < 0) {
				dev_err(cdev->dev,
					"failed to register net device %s\n",
					netdev->name);
				free_netdev(netdev);
				ret = -ENODEV;
				goto err_exit;
			}
			dev_dbg(cdev->dev, "net device %s registered\n",
				netdev->name);

			netif_carrier_off(netdev);
			netif_stop_queue(netdev);
			set_bit(RFOE_INTF_DOWN, &priv->state);
			priv->link_state = 0;

			/* initialize global ctx */
			drv_ctx = &cnf10k_rfoe_drv_ctx[intf_idx];
			drv_ctx->rfoe_num = priv->rfoe_num;
			drv_ctx->lmac_id = priv->lmac_id;
			drv_ctx->valid = 1;
			drv_ctx->netdev = netdev;
			drv_ctx->ft_cfg = &priv->rx_ft_cfg[0];
		}
	}

	return 0;

err_exit:
	for (i = 0; i < CNF10K_RFOE_MAX_INTF; i++) {
		drv_ctx = &cnf10k_rfoe_drv_ctx[i];
		if (drv_ctx->valid) {
			netdev = drv_ctx->netdev;
			priv = netdev_priv(netdev);
			cnf10k_rfoe_ptp_destroy(priv);
			unregister_netdev(netdev);
			for (idx = 0; idx < PACKET_TYPE_MAX; idx++) {
				if (!(priv->pkt_type_mask & (1U << idx)))
					continue;
				ft_cfg = &priv->rx_ft_cfg[idx];
				netif_napi_del(&ft_cfg->napi);
			}
			--(priv->rfoe_common->refcnt);
			if (priv->rfoe_common->refcnt == 0)
				kfree(priv->rfoe_common);
			free_netdev(netdev);
			drv_ctx->valid = 0;
		}
	}

	return ret;
}
