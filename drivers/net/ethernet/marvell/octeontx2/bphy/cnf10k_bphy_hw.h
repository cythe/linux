/* SPDX-License-Identifier: GPL-2.0 */
/* Marvell CNF10K BPHY Netdev Driver
 *
 * Copyright (C) 2021 Marvell.
 */

#ifndef _CNF10K_BPHY_HW_H_
#define _CNF10K_BPHY_HW_H_

#include <linux/types.h>

/* PSM register offsets */
#define PSM_QUEUE_CMD_LO(a)			(0x0 + (a) * 0x10)
#define PSM_QUEUE_CMD_HI(a)			(0x8 + (a) * 0x10)
#define PSM_QUEUE_CFG(a)			(0x1000 + (a) * 0x10)
#define PSM_QUEUE_PTR(a)			(0x2000 + (a) * 0x10)
#define PSM_QUEUE_SPACE(a)			(0x3000 + (a) * 0x10)
#define PSM_QUEUE_TIMEOUT_CFG(a)		(0x4000 + (a) * 0x10)
#define PSM_QUEUE_INFO(a)			(0x5000 + (a) * 0x10)
#define PSM_QUEUE_ENA_W1S(a)			(0x10000 + (a) * 0x8)
#define PSM_QUEUE_ENA_W1C(a)			(0x10100 + (a) * 0x8)
#define PSM_QUEUE_FULL_STS(a)			(0x10200 + (a) * 0x8)
#define PSM_QUEUE_BUSY_STS(a)			(0x10300 + (a) * 0x8)

/* BPHY PSM GPINT register offsets */
#define PSM_INT_GP_SUM_W1C(a)			(0x10E0000 + (a) * 0x100)
#define PSM_INT_GP_SUM_W1S(a)			(0x10E0040 + (a) * 0x100)
#define PSM_INT_GP_ENA_W1C(a)			(0x10E0080 + (a) * 0x100)
#define PSM_INT_GP_ENA_W1S(a)			(0x10E00C0 + (a) * 0x100)

/* RFOE MHAB register offsets */
#define RFOEX_RX_CTL(a)			(0x0818ULL | \
					 ((unsigned long)(a) << 24))
#define RFOEX_RX_VLANX_CFG(a, b)	(0x0870ULL | \
					 ((unsigned long)(a) << 24) | \
					 ((b) << 3))
#define RFOEX_RX_INDIRECT_INDEX_OFFSET(a)	(0x13F8ULL | \
						 ((unsigned long)(a) << 24))
#define RFOEX_RX_IND_FTX_CFG(a, b)	(0x1400ULL | \
					 (((unsigned long)(a) << 24)) + \
					 ((b) << 3))
#define RFOEX_RX_IND_MBT_CFG(a)		(0x1420ULL | \
					 ((unsigned long)(a) << 24))
#define RFOEX_RX_IND_MBT_CFG2(a)	(0x1428ULL | \
					 ((unsigned long)(a) << 24))
#define RFOEX_RX_IND_MBT_ADDR(a)	(0x1430ULL | \
					 ((unsigned long)(a) << 24))
#define RFOEX_RX_IND_MBT_SEG_STATE(a)	(0x1438ULL | \
					 ((unsigned long)(a) << 24))
#define RFOEX_RX_IND_VLANX_FWD(a, b)	(0x14D0ULL | \
					 ((unsigned long)(a) << 24) | \
					 ((b) << 3))
#define RFOEX_RX_IND_JDT_CFG0(a)	(0x1440ULL | \
					 ((unsigned long)(a) << 24))
#define RFOEX_RX_IND_JDT_CFG1(a)	(0x1448ULL | \
					 ((unsigned long)(a) << 24))
#define RFOEX_RX_IND_JDT_CFG2(a)	(0x1490ULL | \
					 ((unsigned long)(a) << 24))
#define RFOEX_RX_IND_JDT_PTR(a)		(0x1450ULL | \
					 ((unsigned long)(a) << 24))
#define RFOEX_RX_IND_JDT_STATE(a)	(0x1478ULL | \
					 ((unsigned long)(a) << 24))
#define RFOEX_RX_IND_ECPRI_FT_CFG(a)	(0x14C0ULL | \
					 ((unsigned long)(a) << 24))
#define RFOEX_TX_PTP_TSTMP_W0(a, b)	(0x7A0ULL | \
					 (((unsigned long)(a) << 24)) | \
					 ((b) << 3))
#define RFOEX_TX_PTP_TSTMP_W1(a, b)	(0x7C0ULL | \
					 (((unsigned long)(a) << 24)) | \
					 ((b) << 3))
#define RFOEX_TX_PKT_STAT(a, b)		(0x720ULL | \
					 (((unsigned long)(a) << 24)) | \
					 ((b) << 3))
#define RFOEX_TX_OCTS_STAT(a, b)	(0x740ULL | \
					 (((unsigned long)(a) << 24)) | \
					 ((b) << 3))
#define RFOEX_RX_VLAN_DROP_STAT(a, b)	(0x8A0ULL | \
					 (((unsigned long)(a) << 24)) | \
					 ((b) << 3))
#define RFOEX_RX_RPM_PKT_STAT(a, b)	(0x15C0ULL | \
					 (((unsigned long)(a) << 24)) | \
					 ((b) << 3))
#define RFOEX_RX_RPM_OCTS_STAT(a, b)	(0x15E0ULL | \
					 (((unsigned long)(a) << 24)) | \
					 ((b) << 3))

/* BCN register offsets and definitions */
#define BCN_CAPTURE_CFG				0x400
#define BCN_CAPTURE_N1_N2			0x410
#define BCN_CAPTURE_PTP				0x430

/* BCN_CAPTURE_CFG register definitions */
#define CAPT_EN					BIT(0)
#define CAPT_TRIG_SW				(3UL << 8)

/* CPRI register offsets */
#define CPRIX_RXD_GMII_UL_CBUF_CFG1(a)		(0x1000ULL | \
						 ((unsigned long)(a) << 24))
#define CPRIX_RXD_GMII_UL_CBUF_CFG2(a)		(0x1008ULL | \
						 ((unsigned long)(a) << 24))
#define CPRIX_RXD_GMII_UL_RD_DOORBELL(a)	(0x1010ULL | \
						 ((unsigned long)(a) << 24))
#define CPRIX_RXD_GMII_UL_SW_RD_PTR(a)		(0x1018ULL | \
						 ((unsigned long)(a) << 24))
#define CPRIX_RXD_GMII_UL_NXT_WR_PTR(a)		(0x1020ULL | \
						 ((unsigned long)(a) << 24))
#define CPRIX_RXD_GMII_UL_PKT_COUNT(a)		(0x1028ULL | \
						 ((unsigned long)(a) << 24))
#define CPRIX_TXD_GMII_DL_CBUF_CFG1(a)		(0x1100ULL | \
						 ((unsigned long)(a) << 24))
#define CPRIX_TXD_GMII_DL_CBUF_CFG2(a)		(0x1108ULL | \
						 ((unsigned long)(a) << 24))
#define CPRIX_TXD_GMII_DL_WR_DOORBELL(a)	(0x1110ULL | \
						 ((unsigned long)(a) << 24))
#define CPRIX_TXD_GMII_DL_SW_WR_PTR(a)		(0x1118ULL | \
						 ((unsigned long)(a) << 24))
#define CPRIX_TXD_GMII_DL_NXT_RD_PTR(a)		(0x1120ULL | \
						 ((unsigned long)(a) << 24))
#define CPRIX_ETH_UL_INT(a)			(0x280ULL | \
						 ((unsigned long)(a) << 24))
#define CPRIX_ETH_UL_INT_ENA_W1S(a)		(0x288ULL | \
						 ((unsigned long)(a) << 24))
#define CPRIX_ETH_UL_INT_ENA_W1C(a)		(0x290ULL | \
						 ((unsigned long)(a) << 24))
#define CPRIX_ETH_UL_INT_W1S(a)			(0x298ULL | \
						 ((unsigned long)(a) << 24))
#define CPRIX_ETH_BAD_CRC_CNT(a, b)		(0x400ULL | \
						 ((unsigned long)(a) << 24) | \
						 ((unsigned long)(b) << 11))
#define CPRIX_ETH_UL_ERR_CNT(a, b)		(0x408ULL | \
						 ((unsigned long)(a) << 24) | \
						 ((unsigned long)(b) << 11))
#define CPRIX_ETH_UL_OSIZE_CNT(a, b)		(0x410ULL | \
						 ((unsigned long)(a) << 24) | \
						 ((unsigned long)(b) << 11))
#define CPRIX_ETH_UL_USIZE_CNT(a, b)		(0x418ULL | \
						 ((unsigned long)(a) << 24) | \
						 ((unsigned long)(b) << 11))
#define CPRIX_ETH_UL_FIFO_ORUN_CNT(a, b)	(0x420ULL | \
						 ((unsigned long)(a) << 24) | \
						 ((unsigned long)(b) << 11))
#define CPRIX_ETH_UL_GPKTS_CNT(a, b)		(0x428ULL | \
						 ((unsigned long)(a) << 24) | \
						 ((unsigned long)(b) << 11))
#define CPRIX_ETH_UL_BOCT_CNT(a, b)		(0x430ULL | \
						 ((unsigned long)(a) << 24) | \
						 ((unsigned long)(b) << 11))
#define CPRIX_ETH_UL_GOCT_CNT(a, b)		(0x438ULL | \
						 ((unsigned long)(a) << 24) | \
						 ((unsigned long)(b) << 11))
#define CPRIX_ETH_UL_MALFORMED_CNT(a, b)	(0x440ULL | \
						 ((unsigned long)(a) << 24) | \
						 ((unsigned long)(b) << 11))
#define CPRIX_ETH_DL_GOCTETS_CNT(a, b)		(0x450ULL | \
						 ((unsigned long)(a) << 24) | \
						 ((unsigned long)(b) << 11))
#define CPRIX_ETH_DL_GPKTS_CNT(a, b)		(0x458ULL | \
						 ((unsigned long)(a) << 24) | \
						 ((unsigned long)(b) << 11))

/* MHAB Structures */
struct mhbw_jd_dma_cfg_word_0_s {
	u64 dma_mode		: 3;
	u64 reserved1		: 1;
	u64 dswap		: 3;
	u64 cmd_type		: 2;
	u64 reserved2		: 7;
	u64 chunk_size		: 16;
	u64 block_size		: 16;
	u64 thread_id		: 6;
	u64 reserved3		: 2;
	u64 group_id		: 4;
	u64 reserved4		: 4;
};

struct mhbw_jd_dma_cfg_word_1_s {
	u64 start_addr		: 53;
	u64 reserved1		: 11;
};

struct rfoex_abx_slotx_configuration {
	u64 pkt_mode			: 2;
	u64 da_sel			: 3;
	u64 sa_sel			: 3;
	u64 etype_sel			: 3;
	u64 flowid			: 8;
	u64 subtype			: 8;
	u64 reserved1			: 2;
	u64 sample_mode			: 1;
	u64 sample_width		: 5;
	u64 sample_width_option		: 1;
	u64 sample_width_sat_bypass	: 1;
	u64 orderinfotype		: 1;
	u64 orderinfooffset		: 5;
	u64 antenna			: 8;
	u64 symbol			: 8;
	u64 sos				: 1;
	u64 eos				: 1;
	u64 orderinfo_insert		: 1;
	u64 custom_timestamp_insert	: 1;
	u64 rfoe_mode			: 1;
};

struct rfoex_abx_slotx_configuration1 {
	u64 rbmap_bytes			: 8;
	u64 reserved1			: 16;
	u64 hdr_len			: 8;
	u64 presentation_time_offset	: 29;
	u64 reserved2			: 1;
	u64 sof_mode			: 2;
};

struct rfoex_abx_slotx_configuration2 {
	u64 vlan_sel		: 3;
	u64 vlan_num		: 2;
	u64 ptp_mode		: 1;
	u64 ecpri_id_insert	: 1;
	u64 ecpri_seq_id_insert	: 1;
	u64 ecpri_rev		: 8;
	u64 ecpri_msgtype	: 8;
	u64 ecpri_id		: 16;
	u64 ecpri_seq_id	: 16;
	u64 cc_mac_sec_en	: 1;
	u64 ptp_ring_id		: 2;
	u64 reserved1		: 5;
};

struct rfoex_abx_slotx_configuration3 {
	u64 pkt_len		: 16;
	u64 lmacid		: 2;
	u64 tx_err		: 1;
	u64 reserved		: 45;
};

struct mhab_job_desc_cfg {
	struct rfoex_abx_slotx_configuration cfg;
	struct rfoex_abx_slotx_configuration1 cfg1;
	struct rfoex_abx_slotx_configuration2 cfg2;
	struct rfoex_abx_slotx_configuration3 cfg3;
} __packed;

/* PSM Enumerations */
enum psm_opcode_e {
	PSM_OP_NOP	= 0x0,
	PSM_OP_ADDJOB	= 0x1,
	PSM_OP_CONTJOB	= 0x2,
	PSM_OP_DJCNT	= 0x10,
	PSM_OP_GPINT	= 0x11,
	PSM_OP_WAIT	= 0x12,
	PSM_OP_ADDWORK	= 0x13,
	PSM_OP_FREE	= 0x14,
	PSM_OP_WRSTS	= 0x15,
	PSM_OP_WRMSG	= 0x16,
	PSM_OP_ADDNOTIF	= 0x17,
	PSM_OP_QRST	= 0x20,
	PSM_OP_QBLK	= 0x21,
	PSM_OP_QRUN	= 0x22,
	PSM_OP_BCAST	= 0x3E,
	PSM_OP_RSP	= 0x3F,
};

/* PSM Structures */
struct psm_cmd_addjob_s {
	/* W0 */
	u64 opcode	: 6;
	u64 rsrc_set	: 2;
	u64 qid		: 8;
	u64 waitcond	: 8;
	u64 jobtag	: 16;
	u64 reserved1	: 8;
	u64 mabq	: 1;
	u64 reserved2	: 3;
	u64 tmem	: 1;
	u64 reserved3	: 3;
	u64 jobtype	: 8;
	/* W1 */
	u64 jobptr	: 53;
	u64 reserved4	: 8;
	u64 gm_id	: 3;
};

/* RFOE Enumerations */
enum rfoe_ecpri_hdr_err_type_e {
	NONE		= 0x0,
	CONCATENATION	= 0x1,
	ILLEGAL_VERSION	= 0x2,
	ILLEGAL_RSVD	= 0x3,
	PC_ID		= 0x4,
};

enum rfoe_ecpri_pcid_flowid_mode_e {
	HASH		= 0x0,
	BASE		= 0x1,
	LMAC_TRUNCATE	= 0x2,
	SHIFT		= 0x3,
};

enum rfoe_order_info_type_e {
	SEQNUM		= 0x0,
	TIMESTAMP	= 0x1,
};

enum rfoe_rx_dir_ctl_pkt_type_e {
	ROE		= 0x0,
	CHI		= 0x1,
	ALT		= 0x2,
	ECPRI		= 0x4,
	GENERIC		= 0x8,
};

enum rfoe_rx_pswt_e {
	RSVD5		= 0x0,
	ROE_BCN_TYPE	= 0x1,
	RSVD6		= 0x2,
	ECPRI_BCN_TYPE	= 0x3,
};

enum rfoe_rx_pkt_err_e {
	RE_NONE		= 0x0,
	RE_PARTIAL	= 0x1,
	RE_JABBER	= 0x2,
	RE_FCS		= 0x7,
	RE_FCS_RCV	= 0x8,
	RE_TERMINATE	= 0x9,
	RE_RX_CTL	= 0xB,
	RE_SKIP		= 0xC,
};

enum rfoe_rx_pkt_logger_idx_e {
	RX_PKT		= 0x0,
	TX_PKT		= 0x1,
};

/* RFOE Structures */
struct ecpri_hdr_s {
	u64 seq_id		: 16;
	u64 pc_id		: 16;
	u64 pyld_size		: 16;
	u64 msg_type		: 8;
	u64 concatenation	: 1;
	u64 reserved		: 3;
	u64 version		: 4;
};

struct rfoe_ab_cfg_w3_s {
	u64 pkt_len		: 16;
	u64 lmac_id		: 2;
	u64 tx_err		: 1;
	u64 reserved		: 45;
};

struct rfoe_psw_s {
	/* W0 */
	u64 jd_ptr		: 53;
	u64 jd_ptr_tmem		: 1;
	u64 jd_ptr_type		: 1;
	u64 reserved1		: 1;
	u64 gm_id		: 3;
	u64 reserved2		: 3;
	u64 pswt		: 2;
	/* W1 */
	u64 ethertype		: 16;
	u64 eindex		: 5;
	u64 reserved3		: 3;
	u64 pkt_len		: 16;
	u64 mcs_err_sts		: 8;
	u64 mac_err_sts		: 6;
	u64 reserved4		: 2;
	u64 pkt_type		: 4;
	u64 reserved5		: 4;
	/* W2 */
	u64 proto_sts_word;
	/* W3 */
	u64 rfoe_tstamp;
	/* W4 */
	u64 ptp_timestamp;
	/* W5 */
	u64 reserved6;
	/* W6 */
	u64 reserved7		: 24;
	u64 dec_error		: 8;
	u64 dec_num_sections	: 8;
	u64 dec_num_syminc	: 8;
	u64 reserved8		: 16;
	/* W7 */
	u64 reserved9;
};

struct rfoe_psw_w0_s {
	u64 jd_ptr		: 53;
	u64 jd_ptr_tmem		: 1;
	u64 jd_ptr_type		: 1;
	u64 reserved1		: 1;
	u64 gm_id		: 3;
	u64 reserved2		: 3;
	u64 pswt		: 2;
};

struct rfoe_psw_w1_s {
	u64 ethertype		: 16;
	u64 eindex		: 5;
	u64 reserved3		: 3;
	u64 pkt_len		: 16;
	u64 mcs_err_sts		: 8;
	u64 mac_err_sts		: 6;
	u64 reserved4		: 2;
	u64 pkt_type		: 4;
	u64 reserved5		: 4;
};

struct rfoe_psw_w2_ecpri_s {
	u64 msg_type		: 8;
	u64 pc_id		: 16;
	u64 seq_id		: 16;
	u64 flow_id		: 10;
	u64 lmac_id		: 2;
	u64 rfoe_id		: 4;
	u64 sa_table_index	: 7;
	u64 reserved		: 1;
};

struct rfoe_psw_w2_roe_s {
	u64 subtype		: 8;
	u64 fd_symbol		: 8;
	u64 fd_antid		: 8;
	u64 reserved1		: 16;
	u64 flowid		: 8;
	u64 reserved2		: 2;
	u64 lmac_id		: 2;
	u64 rfoe_id		: 4;
	u64 sa_table_index	: 7;
	u64 reserved3		: 1;
};

struct rfoe_psw_w3_bcn_s {
	u64 n2			: 24;
	u64 n1			: 40;
};

struct rfoe_psw_w4_s {
	u64 ptp_timestamp;
};

struct rfoe_rx_pkt_log_s {
	u64 timestamp;
	u64 psw_w2;
	u64 psw_w1;
	u64 psw_w0;
};

struct rfoe_timestamp_s {
	u32 time_tick		: 16;
	u32 sf			: 4;
	u32 bfn			: 12;
};

struct rfoe_tx_pkt_log_s {
	u64 timestamp;
	u64 lmac_id		: 2;
	u64 rfoe_id		: 4;
	u64 jobid		: 16;
	u64 drop		: 1;
	u64 tx_err		: 1;
	u64 reserved		: 40;
};

struct rfoe_tx_ptp_tstmp_s {
	u64 ptp_timestamp;
	u64 reserved1		: 2;
	u64 rfoe_id		: 4;
	u64 jobid		: 16;
	u64 drop		: 1;
	u64 tx_err		: 1;
	u64 reserved2		: 39;
	u64 valid		: 1;
};

struct rfoe_rx_ind_vlanx_fwd {
	u64 fwd			: 64;
};

#endif	/* _CNF10K_BPHY_HW_H_ */
