-- @brief bvc_srt Protocol dissector plugin
-- @author bilibili-quzhenyu
-- @date 2019.07.25

-- create a new dissector
local NAME = "bvc_srt"
local bvc_srt = Proto(NAME, "BVC_SRT Protocol")

-- create a preference of a Protocol
bvc_srt.prefs["srt_udp_port"] = Pref.uint("SRT UDP Port", 1935, "SRT UDP Port")

-- create fields of bvc_srt
-- Base.HEX, Base.DEC, Base.OCT, Base.UNIT_STRING, Base.NONE
local fields = bvc_srt.fields
-- 通用字段
local pack_type_select = {
	[0] = "Data Packet",
	[1] = "Control Packet"
}
fields.pack_type_tree = ProtoField.uint32(NAME .. ".pack_type_tree", "Packet Type", base.HEX)
fields.pack_type = ProtoField.uint16("bvc_srt.pack_type", "Packet Type", base.HEX, pack_type_select, 0x8000)
fields.reserve = ProtoField.uint16("bvc_srt.reserve", "Reserve", base.DEC)
fields.additional_info = ProtoField.uint32("bvc_srt.additional_info", "Additional Information", base.DEC)
fields.time_stamp = ProtoField.uint32("bvc_srt.time_stamp", "Time Stamp", base.DEC)
fields.dst_sock = ProtoField.uint32("bvc_srt.dst_sock", "Destination Socket ID", base.DEC)
fields.none = ProtoField.none("bvc_srt.none", "none", base.NONE)

-- Data packet字段
fields.data_flag_info_tree = ProtoField.uint8("bvc_srt.data_flag_info_tree", "Data Flag Info", base.HEX)
local FF_state_select = {
	[0] = "[Middle packet]",
	[1] = "[Last packet]",
	[2] = "[First packet]",
	[3] = "[Single packet]"
}
fields.FF_state = ProtoField.uint8("bvc_srt.FF_state", "FF state", base.HEX, FF_state_select, 0xC0)
local O_state_select = {
	[0] = "[Data delivered unordered]",
	[1] = "[Data delivered in order]"
}
fields.O_state = ProtoField.uint8("bvc_srt.O_state", "O state", base.HEX, O_state_select, 0x20)
local KK_state_select = {
	[0] = "[Not encrypted]",
	[1] = "[Data encrypted with even key]",
	[2] = "[Data encrypted with odd key]"
}
fields.KK_state = ProtoField.uint8("bvc_srt.KK_state", "KK state", base.HEX, KK_state_select, 0x18)
local R_state_select = {
	[0] = "[Not Set]",
	[1] = "[Retransmit Packet]"
}
fields.R_state = ProtoField.uint8("bvc_srt.R_state", "R state", base.HEX, R_state_select, 0x04)
fields.seq_num = ProtoField.uint32("bvc_srt.seq_num", "Sequence Number", base.DEC)
fields.msg_num = ProtoField.uint32("bvc_srt.msg_num", "Message Number", base.DEC)--, nil, 0x3FFFFFF)

-- control packet字段
local msg_type_select = {
	[0] = "[HANDSHAKE]",
	[1] = "[KEEPALIVE]",
	[2] = "[ACK]",
	[3] = "[NAK(Loss Report)]",
	[4] = "[Congestion Warning]",
	[5] = "[Shutdown]",
	[6] = "[ACKACK]",
	[7] = "[Drop Request]",
	[8] = "[Peer Error]",
	[0x7FFF] = "[Message Extension Type]"
}
fields.msg_type = ProtoField.uint16("bvc_srt.msg_type", "Message Type", base.HEX, msg_type_select, 0x7FFF)
fields.msg_ext_type = ProtoField.uint16("bvc_srt.msg_ext_type", "Message Extented Type", base.DEC)

-- Handshake packet字段
fields.UDT_version = ProtoField.uint32("bvc_srt.UDT_version", "UDT Version", base.DEC)
fields.sock_type = ProtoField.uint32("bvc_srt.sock_type", "Socket Type", base.DEC)
fields.ency_fld = ProtoField.uint16("bvc_srt.ency_fld", "Encryption Field", base.DEC)
fields.ext_fld = ProtoField.uint16("bvc_srt.ext_fld", "Extension Field", base.HEX)
fields.isn = ProtoField.uint32("bvc_srt.isn", "Initial packet sequence number", base.DEC)
fields.mss = ProtoField.uint32("bvc_srt.mss", "Max Packet Size", base.DEC)
fields.fc = ProtoField.uint32("bvc_srt.fc", "Maximum Flow Window Size", base.DEC)
fields.conn_type = ProtoField.uint32("bvc_srt.conn_type", "Connection Type", base.DEC)
fields.sock_id = ProtoField.uint32("bvc_srt.sock_id", "Socket ID", base.DEC)
fields.syn_cookie = ProtoField.uint32("bvc_srt.syn_cookie", "SYN cookie", base.DEC)
fields.peer_ipaddr = ProtoField.none("bvc_srt.peer_ipaddr", "Peer IP address", base.NONE)
fields.peer_ipaddr_4 = ProtoField.ipv4("bvc_srt.peer_ipaddr", "Peer IP address")
fields.peer_ipaddr_6 = ProtoField.ipv6("bvc_srt.peer_ipaddr", "Peer IP address")
local ext_type_select = {
	[-1] = "SRT_CMD_NONE",
	[0] = "none",
	[1] = "SRT_CMD_HSREQ",
	[2] = "SRT_CMD_HSRSP",
	[3] = "SRT_CMD_KMREQ",
	[4] = "SRT_CMD_KMRSP",
	[5] = "SRT_CMD_SID",
	[6] = "SRT_CMD_SMOOTHER"
}
fields.ext_type_msg_tree = ProtoField.none("bvc_srt.ext_type", "Extension Type Message", base.NONE)
fields.ext_type = ProtoField.uint16("bvc_srt.ext_type", "Extension Type", base.HEX, ext_type_select, 0xF)
fields.ext_size = ProtoField.uint16("bvc_srt.ext_size", "Extension Size", base.DEC)

-- Handshake packet, ext type == SRT_CMD_HSREQ 或 SRT_CMD_HSRSP字段
fields.srt_version = ProtoField.uint32("bvc_srt.srt_version", "SRT Version", base.HEX)
fields.srt_flags = ProtoField.uint32("bvc_srt.srt_flags", "SRT Flags", base.HEX)
fields.tsbpb_resv = ProtoField.uint16("bvc_srt.tsbpb_resv", "TsbPb Receive", base.DEC)
fields.tsbpb_delay = ProtoField.uint16("bvc_srt.tsbpb_delay", "TsbPb Delay", base.DEC)
fields.rcv_tsbpb_delay = ProtoField.uint16("bvc_srt.rcv_tsbpb_delay", "Receive TsbPd Delay", base.DEC)
fields.snd_tsbpb_delay = ProtoField.uint16("bvc_srt.snd_tsbpb_delay", "Send TsbPd Delay", base.DEC)

-- V和PT状态位
local V_state_select = {
	[1] = "Initial version"
}
fields.V_state = ProtoField.uint8("bvc_srt.V_state", "V", base.HEX, V_state_select, 0x70)
local PT_state_select = {
	[0] = "Reserved",
	[1] = "MSmsg",
	[2] = "KMmsg",
	[7] = "Reserved to discriminate MPEG-TS packet(0x47=sync byte)"
}
fields.PT_state = ProtoField.uint8("bvc_srt.PT_state", "PT", base.HEX, state_table, 0xF)
fields.sign = ProtoField.uint16("bvc_srt.sign", "Signature", base.HEX)
local resv_select = {
	[0] = "Reserved for flag extension or other usage"
}
fields.resv = ProtoField.uint8("bvc_srt.resv", "Resv", base.DEC, state_table, 0xFC)
fields.ext_KK_state = ProtoField.uint8("bvc_srt.ext_KK_state", "KK_state", base.HEX, KK_state_select, 0x3)
fields.KEKI = ProtoField.uint32("bvc_srt.KEKI", "KEKI", base.DEC)
fields.cipher = ProtoField.uint8("bvc_srt.cipher", "Cipher", base.DEC)
fields.auth = ProtoField.uint8("bvc_srt.auth", "auth", base.DEC)
fields.SE = ProtoField.uint8("bvc_srt.SE", "SE", base.DEC)
fields.resv1 = ProtoField.uint8("bvc_srt.resv1", "resv1", base.DEC)
fields.resv2 = ProtoField.uint16("bvc_srt.resv2", "resv2", base.DEC)
fields.slen = ProtoField.uint8("bvc_srt.slen", "Salt length(bytes)/4", base.DEC)
fields.klen = ProtoField.uint8("bvc_srt.klen", "SEK length(bytes)/4", base.DEC)
fields.salt = ProtoField.uint32("bvc_srt.salt", "Salt key", base.DEC)
fields.wrap = ProtoField.none("bvc_srt.wrap", "Wrap key(s)", base.NONE)

-- Handshake packet, ext_type == SRT_CMD_SMOOTHER字段
fields.smoother_block = ProtoField.none("bvc_srt.smoother_block", "Smoother Block", base.NONE)

-- Handshake packet, ext_type == SRT_CMD_SID字段
fields.sid_block = ProtoField.none("bvc_srt.sid_block", "SID Block", base.NONE)

-- SRT flags
local flag_state_select = {
	[0] = "Not set",
	[1] = "Set"
}
fields.srt_opt_tsbpdsnd = ProtoField.uint32("bvc_srt.srt_opt_tsbpdsnd", "SRT_OPT_TSBPDSND", base.HEX, flag_state_select, 0x1)
fields.srt_opt_tsbpdrcv = ProtoField.uint32("bvc_srt.srt_opt_tsbpdrcv", "SRT_OPT_TSBPDRCV", base.HEX, flag_state_select, 0x2)
fields.srt_opt_haicrypt = ProtoField.uint32("bvc_srt.srt_opt_haicrypt", "SRT_OPT_HAICRYPT", base.HEX, flag_state_select, 0x4)
fields.srt_opt_tlpktdrop = ProtoField.uint32("bvc_srt.srt_opt_tlpktdrop", "SRT_OPT_TLPKTDROP", base.HEX, flag_state_select, 0x8)
fields.srt_opt_nakreport = ProtoField.uint32("bvc_srt.srt_opt_nakreport", "SRT_OPT_NAKREPORT", base.HEX, flag_state_select, 0x10)
fields.srt_opt_rexmitflg = ProtoField.uint32("bvc_srt.srt_opt_rexmitflg", "SRT_OPT_REXMITFLG", base.HEX, flag_state_select, 0x20)
fields.srt_opt_stream = ProtoField.uint32("bvc_srt.srt_opt_stream", "SRT_OPT_STREAM", base.HEX, flag_state_select, 0x40)

-- ACK字段
fields.last_ack_pack = ProtoField.uint32("bvc_srt.last_ack_pack", "Last ACK Packet Sequence Number", base.DEC)
fields.rtt = ProtoField.uint32("bvc_srt.rtt", "Round Trip Time", base.DEC)
fields.rtt_variance = ProtoField.uint32("bvc_srt.rtt_variance", "Round Trip Time Variance", base.DEC)
fields.buf_size = ProtoField.uint32("bvc_srt.buf_size", "Available Buffer Size", base.DEC)
fields.pack_rcv_rate = ProtoField.uint32("bvc_srt.pack_rcv_rate", "Packet Receiving Rate", base.DEC)
fields.est_link_capacity = ProtoField.uint32("bvc_srt.est_link_capacity", "Estimated Link Capacity", base.DEC)
fields.rcv_rate = ProtoField.uint32("bvc_srt.rcv_rate", "Receiving Rate", base.DEC)

-- ACKACK字段
fields.ack_seq_num = ProtoField.uint32("bvc_srt.ack_seq_num", "ACK sequence number", base.DEC)
fields.ctl_info = ProtoField.uint32("bvc_srt.ctl_info", "Control Information", base.DEC)

-- KMRSP字段
local srt_km_state_select = {
	[0] = "[SRT_KM_UNSECURED]",
	[1] = "[SRT_KM_SECURING]",
	[2] = "[SRT_KM_SECURED]",
	[3] = "[SRT_KM_NOSECRET]",
	[4] = "[SRT_KM_BADSECRET]"
}
fields.km_err = ProtoField.uint32("bvc_srt.km_err", "Key Message Error", base.HEX, srt_km_state_select, 0xF)

-- NAK Control Packet字段
fields.lost_list_tree = ProtoField.none("bvc_srt.lost_list_tree", "Lost Packet List", base.NONE)
fields.lost_pack_seq = ProtoField.uint32("bvc_srt.lost_pack_seq", "Lost Packet Sequence Number", base.DEC)
fields.lost_pack_range_tree = ProtoField.none("bvc_srt.lost_pack_range_tree", "Lost Packet Range", base.NONE)
fields.lost_start = ProtoField.uint32("bvc_srt.lost_start", "Lost Starting Sequence", base.DEC)
fields.lost_up_to = ProtoField.uint32("bvc_srt.lost_up_to", "Lost Up To(including)", base.DEC)

-- dissect packet
function bvc_srt.dissector (tvb, pinfo, tree)
	-- 包是在UDP基础上处理的，所以直接处理UDP后的数据即可
	local subtree = tree:add(bvc_srt, tvb())
	local offset = 0
	
	-- 修改经过1935端口的协议名
	pinfo.cols.protocol = bvc_srt.name
	
	-- 取出包第一位，如果为0->Data Packet 1->Control Packet
	local typebit = bit.rshift(tvb(offset, 1):uint(), 7)
	pack_type_tree = subtree:add(fields.pack_type_tree, tvb(offset, 4))
	
	if typebit == 1 then
		-- 1 -> Control Packet
		pack_type_tree:add(fields.pack_type, tvb(offset, 2))
		
		local msg_type = tvb(offset, 2):uint()
		if msg_type ~= 0xFFFF then
			-- 如果type字段不为0x7FFF，则说明为普通消息包，处理type字段
			msg_type = bit.band(msg_type, 0x7FFF)
			
			function parse_three_param()
				-- 忽略Additional Info（该包类型此字段未定义）
				subtree:add(fields.additional_info, tvb(offset, 4)):append_text(" [undefined]")
				offset = offset + 4
				
				-- 处理Time Stamp
				subtree:add(fields.time_stamp, tvb(offset, 4)):append_text(" μs")
				offset = offset + 4
				
				-- 处理Destination Socket
				subtree:add(fields.dst_sock, tvb(offset, 4))
				offset = offset + 4
			end
			
			local switch = {
				[0] = function()
					pinfo.cols.info:append(" [HANDSHAKE]")
					pack_type_tree:append_text(" [HANDSHAKE]")
					pack_type_tree:add(fields.msg_type, tvb(offset, 2))
					pack_type_tree:add(fields.reserve, tvb(offset + 2, 2)):append_text(" [Undefined]")
					offset = offset + 4
					
					-- 处理Additional Info、Time Stamp、Destination Socket
					parse_three_param()

					-- 处理UDT version字段
					local UDT_version = tvb(offset, 4):uint()
					subtree:add(fields.UDT_version, tvb(offset, 4))
					offset = offset + 4
					
					if UDT_version == 4 then
						-- UDT version为4，包同版本5不同
						-- 处理sock type
						local sock_type = tvb(offset, 4):uint()
						if sock_type == 1 then
							subtree:add(fields.sock_type, tvb(offset, 4)):append_text(" [SOCK_STREAM]")
						elseif sock_type == 2 then
							subtree:add(fields.sock_type, tvb(offset, 4)):append_text(" [SOCK_DRAGAM]")
						end
						offset = offset + 4
					elseif UDT_version == 5 then
						-- 处理Encryption Field
						local encr_fld = tvb(offset, 2):int()
						if encr_fld == 0 then
							subtree:add(fields.ency_fld, tvb(offset, 2)):append_text(" (PBKEYLEN not advertised)")
						elseif encr_fld == 2 then
							subtree:add(fields.ency_fld, tvb(offset, 2)):append_text(" (AES-128)")
						elseif encr_fld == 3 then
							subtree:add(fields.ency_fld, tvb(offset, 2)):append_text(" (AES-192)")
						else
							subtree:add(fields.ency_fld, tvb(offset, 2)):append_text(" (AES-256)")
						end
						offset = offset + 2
						
						-- 处理Extension Field
						local ext_fld = tvb(offset, 2):int()
						if ext_fld == 0x4A17 then
							subtree:add(fields.ext_fld, tvb(offset, 2)):append_text(" [SrtHSRequest::SRT_MAGIC_CODE]")
						elseif ext_fld == 0 then
							subtree:add(fields.ext_fld, tvb(offset, 2))
						else
							-- Extension Field是HS_HEX_prefix
							-- 定义代码位于handshake.h中
							if ext_fld == 1 then
								subtree:add(fields.ext_fld, tvb(offset, 2)):append_text(" [CHandShake::HS_EXT_HSREQ]")
							elseif ext_fld == 2 then
								subtree:add(fields.ext_fld, tvb(offset, 2)):append_text(" [CHandShake::HS_EXT_KMREQ]")
							elseif ext_fld == 4 then
								subtree:add(fields.ext_fld, tvb(offset, 2)):append_text(" [CHandShake::HS_EXT_CONFIG]")
							else
								subtree:add(fields.ext_fld, tvb(offset, 2)):append_text(" [Bad Flag]")
							end
						end
						offset = offset + 2
					end
					
					-- 处理Initial packet sequence number
					subtree:add(fields.isn, tvb(offset, 4))
					offset = offset + 4
					
					-- 处理Maximum Packet Size
					subtree:add(fields.mss, tvb(offset, 4))
					offset = offset + 4
					
					-- 处理Maximum Flow Window Size
					subtree:add(fields.fc, tvb(offset, 4))
					offset = offset + 4
					
					-- 处理Connection Type
					local conn_type = tvb(offset, 4):int()
					local conn_type_tree = subtree:add(fields.conn_type, tvb(offset, 4))
					if conn_type == 0 then
						conn_type_tree:append_text(" [WAVEAHAND] (Rendezvous Mode)")
						pinfo.cols.info:append(" [WAVEAHAND] (Rendezvous Mode)")
					elseif conn_type == 1 then
						conn_type_tree:append_text(" [INDUCTION]")
					elseif conn_type == -1 then
						conn_type_tree:append_text(" [CONCLUSION]")
					elseif conn_type == -2 then
						conn_type_tree:append_text(" [AGREEMENT] (Rendezvous Mode)")
						pinfo.cols.info:append(" [AGREEMENT] (Rendezvous Mode)")
					end
					offset = offset + 4
					
					-- 处理Socket ID
					subtree:add(fields.sock_id, tvb(offset, 4))
					offset = offset + 4
					
					-- 处理SYN cookie
					local syn_cookie = tvb(offset, 4):int()
					subtree:add(fields.syn_cookie, tvb(offset, 4))
					if syn_cookie == 0 then
						conn_type_tree:append_text(" (Caller to Listener)")
						pinfo.cols.info:append(" (Caller to Listener)")
					else
						if conn_type == 1 then
							-- reports cookie from listener
							conn_type_tree:append_text(" (Listener to Caller)")
							pinfo.cols.info:append(" (Listener to Caller)")
						end
					end
					offset = offset + 4
					
					-- 处理Peer IP address
					-- 注意是网络字节序
					local the_last_96_bits = 0
					the_last_96_bits = the_last_96_bits + math.floor(tvb(offset + 4, 4):int() * (2 ^ 16))
					the_last_96_bits = the_last_96_bits + math.floor(tvb(offset + 8, 4):int() * (2 ^ 8))
					the_last_96_bits = the_last_96_bits + tvb(offset + 12, 4):int()
					if the_last_96_bits == 0 then
						subtree:add_le(fields.peer_ipaddr_4, tvb(offset, 4))
						subtree:add(fields.peer_ipaddr, tvb(offset + 4, 12)):append_text(" [Undefined]")
					else
						subtree:add_le(fields.peer_ipaddr, tvb(offset, 16))
					end
					
					offset = offset + 16
					
					-- UDT version为4，包到此结束
					if UDT_version == 4 or offset == tvb:len() then
						return
					end
					
					function process_ext_type()
						-- 处理Ext Type，根据type进行处理
						local ext_type = tvb(offset, 2):int()
						if ext_type == 1 or ext_type == 2 then
							local ext_type_msg_tree = subtree:add(fields.ext_type_msg_tree, tvb(offset, 16))
							if ext_type == 1 then
								ext_type_msg_tree:append_text(" [SRT_CMD_HSREQ]")
								ext_type_msg_tree:add(fields.ext_type, tvb(offset, 2))
								conn_type_tree:append_text(" (Caller to Listener)")
								pinfo.cols.info:append(" (Caller to Listener)")
							else
								ext_type_msg_tree:append_text(" [SRT_CMD_HSRSP]")
								ext_type_msg_tree:add(fields.ext_type, tvb(offset, 2))
								conn_type_tree:append_text(" (Listener to Caller)")
								pinfo.cols.info:append(" (Listener to Caller)")
							end
							offset = offset + 2
							
							-- 处理Ext Size
							ext_type_msg_tree:add(fields.ext_size, tvb(offset, 2))
							offset = offset + 2
							
							-- 处理SRT Version
							ext_type_msg_tree:add(fields.srt_version, tvb(offset, 4))
							offset = offset + 4
							
							-- 处理SRT Flags
							local SRT_flags_tree = ext_type_msg_tree:add(fields.srt_flags, tvb(offset, 4))
							SRT_flags_tree:add(fields.srt_opt_tsbpdsnd, tvb(offset, 4))
							SRT_flags_tree:add(fields.srt_opt_tsbpdrcv, tvb(offset, 4))
							SRT_flags_tree:add(fields.srt_opt_haicrypt, tvb(offset, 4))
							SRT_flags_tree:add(fields.srt_opt_tlpktdrop, tvb(offset, 4))
							SRT_flags_tree:add(fields.srt_opt_nakreport, tvb(offset, 4))
							SRT_flags_tree:add(fields.srt_opt_rexmitflg, tvb(offset, 4))
							SRT_flags_tree:add(fields.srt_opt_stream, tvb(offset, 4))
							offset = offset + 4
							
							-- 处理Rcv TsbPd Delay和Snd TsbPd Delay
							ext_type_msg_tree:add(fields.rcv_tsbpb_delay, tvb(offset, 2))
							offset = offset + 2
							ext_type_msg_tree:add(fields.rcv_tsbpb_delay, tvb(offset, 2))
							offset = offset + 2
						elseif ext_type == 3 or ext_type == 4 then
							local ext_type_msg_tree = subtree:add(fields.ext_type_msg_tree, tvb(offset, 16))
							if ext_type == 3 then
								ext_type_msg_tree:append_text(" [SRT_CMD_KMREQ]")
								ext_type_msg_tree:add(fields.ext_type, tvb(offset, 2))
								conn_type_tree:append_text(" (Listener to Caller)")
							else
								ext_type_msg_tree:append_text(" [SRT_CMD_KMRSP]")
								ext_type_msg_tree:add(fields.ext_type, tvb(offset, 2))
							end
							offset = offset + 2
							
							-- 处理Ext Size
							local km_len = tvb(offset, 2):uint()
							ext_type_msg_tree:add(fields.ext_size, tvb(offset, 2)):append_text(" (byte/4)")
							offset = offset + 2
							
							-- 处理SRT_CMD_KMREQ信息
							-- V和PT状态位
							ext_type_msg_tree:add(fields.V_state, tvb(offset, 1))
							ext_type_msg_tree:add(fields.PT_state, tvb(offset, 1))
							offset = offset + 1
							
							-- 处理sign
							ext_type_msg_tree:add(fields.sign, tvb(offset, 2)):append_text(" (/'HAI/' PnP Vendor ID in big endian order)")
							offset = offset + 2
							
							-- 处理resv
							ext_type_msg_tree:add(fields.resv, tvb(offset, 1))
							
							-- 处理KK标志位
							local KK = tvb(offset, 1):uint()
							ext_type_msg_tree:add(fields.ext_KK_state, tvb(offset, 1))
							offset = offset + 1
							
							-- 处理KEKI
							if tvb(offset, 4):uint() == 0 then
								ext_type_msg_tree:add(fields.KEKI, tvb(offset, 4)):append_text(" (Default stream associated key(stream/system default))")
							else
								ext_type_msg_tree:add(fields.KEKI, tvb(offset, 4)):append_text(" (Reserved for manually indexed keys)")
							end
							offset = offset + 4
							
							-- 处理Cipher
							local cipher_node = ext_type_msg_tree:add(fields.cipher, tvb(offset, 1))
							local cipher = tvb(offset, 1):uint()
							if cipher == 0 then
							elseif cipher == 1 then
								cipher_node:append_text(" (AES-ECB(potentially for VF 2.0 compatible message))")
							elseif cipher == 2 then
								cipher_node:append_text(" (AES-CTR[FP800-38A])")
							else
								cipher_node:append_text(" (AES-CCM or AES-GCM)")
							end
							offset = offset + 1
							
							-- 处理Auth
							if tvb(offset, 1):uint() == 0 then
								ext_type_msg_tree:add(fields.auth, tvb(offset, 1)):append_text(" (None or KEKI indexed crypto context)")
							else
								ext_type_msg_tree:add(fields.auth, tvb(offset, 1))
							end
							offset = offset + 1
							
							-- 处理SE
							local SE_node = ext_type_msg_tree:add(fields.SE, tvb(offset, 1))
							local SE = tvb(offset, 1):uint()
							if SE == 0 then
								SE_node:append_text( " (Unspecified or KEKI indexed crypto context)")
							elseif SE == 1 then
								SE_node:append_text( " (MPEG-TS/UDP)")
							elseif SE == 2 then
								SE_node:append_text( " (MPEG-TS/SRT)")
							end
							offset = offset + 1
							
							-- 处理resv1
							ext_type_msg_tree:add(fields.resv1, tvb(offset, 1))
							offset = offset + 1
							
							-- 处理resv2
							ext_type_msg_tree:add(fields.resv2, tvb(offset, 2))
							offset = offset + 2
							
							-- 处理slen
							ext_type_msg_tree:add(fields.slen, tvb(offset, 1))
							offset = offset + 1
							
							-- 处理klen
							local klen = tvb(offset, 1):uint()
							ext_type_msg_tree:add(fields.klen, tvb(offset, 1))
							offset = offset + 1
							
							-- 处理salt
							ext_type_msg_tree:add(fields.salt, tvb(offset, slen * 8))
							offset = offset + slen * 4 * 8
							
							-- 处理wrap
							local wrap_len = klen * 4 * KK + klen * 4 / 2 + 8
							ext_type_msg_tree:add(fields.wrap, tvb(offset, wrap_len))
							offset = offset + wrap_len
						elseif ext_type == 5 then
							local ext_type_msg_tree = subtree:add(fields.ext_type_msg_tree, tvb(offset, 16)):append_text(" [SRT_CMD_SID]")
							ext_type_msg_tree:add(fields.ext_type, tvb(offset, 2))
							offset = offset + 2
							
							-- 处理Ext Size
							ext_type_msg_tree:add(fields.ext_size, tvb(offset, 2)):append_text(" (byte/4)")
							offset = offset + 2
							
							-- SID BLOCK
							ext_type_msg_tree:add(fields.sid_block, tvb(offset, 128))
							offset = offset + 128
						elseif ext_type == 6 then
							local ext_type_msg_tree = subtree:add(fields.ext_type_msg_tree, tvb(offset, 16)):append_text(" [SRT_CMD_SMOOTHER]")
							ext_type_msg_tree:add(fields.ext_type, tvb(offset, 2))
							offset = offset + 2
							
							-- 处理Ext Size
							ext_type_msg_tree:add(fields.ext_size, tvb(offset, 2)):append_text(" (byte/4)")
							offset = offset + 2
							
							-- SMOOTHER BLOCK
							ext_type_msg_tree:add(fields.smoother_block, tvb(offset, 128))
							offset = offset + 128
						elseif ext_type == -1 then
							local ext_type_msg_tree = subtree:add(fields.ext_type_msg_tree, tvb(offset, 16)):append_text(" [SRT_CMD_NONE]")
							ext_type_msg_tree:add(fields.ext_type, tvb(offset, 2))
							offset = offset + 2
							
							-- none
							if offset == tvb:len() then
								return
							end
							ext_type_msg_tree:add(fields.none, tvb(offset, tvb:len() - offset))
							offset = tvb:len()
						end
						if offset == tvb:len() then
							return
						else
							process_ext_type()
						end
					end
					
					process_ext_type()
				end,
				[1] = function()
					pinfo.cols.info:append(" [KEEPALIVE]")
					pack_type_tree:append_text(" [KEEPALIVE]")
					pack_type_tree:add(fields.msg_type, tvb(offset, 2)):append_text(" [KEEPALIVE]")
					pack_type_tree:add(fields.reserve, tvb(offset + 2, 2)):append_text(" [Undefined]")
					offset = offset + 4
					
					-- 处理Additional Info、Time Stamp、Destination Socket
					parse_three_param()
					
					-- 后面为none
				end,
				[2] = function()
					pinfo.cols.info:append(" [ACK]")
					pack_type_tree:append_text(" [ACK]")
					pack_type_tree:add(fields.msg_type, tvb(offset, 2))
					pack_type_tree:add(fields.reserve, tvb(offset + 2, 2)):append_text(" [Undefined]")
					offset = offset + 4
					
					-- 处理ACK Sequence Number该包类型此字段未定义）
					subtree:add(fields.ack_seq_num, tvb(offset, 4))
					offset = offset + 4
					
					-- 处理Time Stamp
					subtree:add(fields.time_stamp, tvb(offset, 4)):append_text(" μs")
					offset = offset + 4
					
					-- 处理Destination Socket
					subtree:add(fields.dst_sock, tvb(offset, 4))
					offset = offset + 4
					
					-- 处理Last Ack Packet Sequence
					local last_ack_pack = tvb(offset, 4):uint()
					pinfo.cols.info:append(" (Last ACK Seq:" .. last_ack_pack .. ")")
					subtree:add(fields.last_ack_pack, tvb(offset, 4))
					offset = offset + 4
					
					-- 处理RTT
					subtree:add(fields.rtt, tvb(offset, 4)):append_text(" μs")
					offset = offset + 4
					
					-- 处理RTT variance
					subtree:add(fields.rtt_variance, tvb(offset, 4))
					offset = offset + 4
					
					-- 处理Available Buffer Size(pkts)
					subtree:add(fields.buf_size, tvb(offset, 4)):append_text(" pkts")
					offset = offset + 4
					
					-- 处理Packets Receiving Rate(Pkts/sec)
					subtree:add(fields.pack_rcv_rate, tvb(offset, 4)):append_text(" pkts/sec")
					offset = offset + 4
					
					-- 处理Estmated Link Capacity
					subtree:add(fields.est_link_capacity, tvb(offset, 4))
					offset = offset + 4
					
					-- 处理Receiving Rate(bps)
					subtree:add(fields.rcv_rate, tvb(offset, 4)):append_text(" bps")
					offset = offset + 4
				end,
				[3] = function()
					pinfo.cols.info:append(" [NAK(loss Report)]")
					pack_type_tree:append_text(" [NAK(loss Report)]")
					pack_type_tree:add(fields.msg_type, tvb(offset, 2))
					pack_type_tree:add(fields.reserve, tvb(offset + 2, 2)):append_text(" [Undefined]")
					offset = offset + 4
					
					-- 处理Additional Info、Time Stamp、Destination Socket
					parse_three_param()
					
					-- 处理lost packet seq
					-- lua不支持循环内改变循环变量，要用闭包的方式
					-- https://blog.csdn.net/Ai102iA/article/details/75371239
					local start = offset
					local ending = tvb:len()
					local lost_list_tree = subtree:add(fields.lost_list_tree, tvb(offset, ending - offset))
					for start in function()
							local first_bit = bit.rshift(tvb(start, 1):uint(), 7)
							if first_bit == 1 then
								local lost_pack_range_tree = lost_list_tree:add(fields.lost_pack_range_tree, tvb(start, 8))
								local lost_start = bit.band(tvb(start, 4):uint(), 0x7FFFFFFF)
								lost_pack_range_tree:append_text(" (" .. lost_start .. " -> " .. tvb(start + 4, 4):uint() .. ")")
								lost_pack_range_tree:add(fields.lost_start, tvb(start, 4), lost_start)
								start = start + 4
								lost_pack_range_tree:add(fields.lost_up_to, tvb(start, 4))
								start = start + 4
							else
								lost_list_tree:add(fields.lost_pack_seq, tvb(start, 4))
								start = start + 4
							end
							return start
						end
						do
							if start == ending then
								break
							end
					end
				end,
				[4] = function()
					pinfo.cols.info:append(" [Congestion Warning]")
					pack_type_tree:append_text(" [Congestion Warning]")
					pack_type_tree:add(fields.msg_type, tvb(offset, 2))
					pack_type_tree:add(fields.reserve, tvb(offset + 2, 2)):append_text(" [Undefined]")
					offset = offset + 4
				end,
				[5] = function()
					pinfo.cols.info:append(" [Shutdown]")
					pack_type_tree:append_text(" [Shutdown]")
					pack_type_tree:add(fields.msg_type, tvb(offset, 2))
					pack_type_tree:add(fields.reserve, tvb(offset + 2, 2)):append_text(" [Undefined]")
					offset = offset + 4
					
					-- 处理Additional Info、Time Stamp、Destination Socket
					parse_three_param()
				end,
				[6] = function()
					pinfo.cols.info:append(" [ACKACK]")
					pack_type_tree:append_text(" [ACKACK]")
					pack_type_tree:add(fields.msg_type, tvb(offset, 2))
					pack_type_tree:add(fields.reserve, tvb(offset + 2, 2)):append_text(" [Undefined]")
					offset = offset + 4
					
					-- 处理ACK sequence number
					subtree:add(fields.ack_seq_num, tvb(offset, 4))
					offset = offset + 4
					
					-- 处理Time Stamp
					subtree:add(fields.time_stamp, tvb(offset, 4)):append_text(" μs")
					offset = offset + 4
					
					-- 处理Destination Socket
					subtree:add(fields.dst_sock, tvb(offset, 4))
					offset = offset + 4
					
					-- 处理Control Information
					subtree:add(fields.ctl_info, tvb(offset, 4))
					offset = offset + 4
				end,
				[7] = function()
					pinfo.cols.info:append(" [Drop Request]")
					pack_type_tree:append_text(" [Drop Request]")
					pack_type_tree:add(fields.msg_type, tvb(offset, 2)):append_text(" [Drop Request]")
					pack_type_tree:add(fields.reserve, tvb(offset + 2, 2)):append_text(" [Undefined]")
					offset = offset + 4
				end,
				[8] = function()
					pinfo.cols.info:append(" [Peer Error]")
					pack_type_tree:append_text(" [Peer Error]")
					pack_type_tree:add(fields.msg_type, tvb(offset, 2)):append_text(" [Peer Error]")
					pack_type_tree:add(fields.reserve, tvb(offset + 2, 2)):append_text(" [Undefined]")
					offset = offset + 4
				end
			}
			-- 处理对应的msg_type，通过switch实现
			local case = switch[msg_type]
			if case then
				case()
			else
				-- default case
				subtree:add(fields.msg_type, tvb(offset, 2)):append_text(" [Unknown Message Type]")
				offset = offset + 4
			end
		else
			-- 如果type字段为0x7FFF，则说明为拓展类型，处理Reserve字段
			offset = offset + 2
			local msg_ext_type = tvb(offset, 2):uint()
			if msg_ext_type == 0 then
				pinfo.cols.info:append(" [Message Extension]")
				
				pack_type_tree:add(fields.msg_ext_type, tvb(offset, 2)):append_text(" [Message Extension]")
				offset = offset + 2
				
				-- 处理Additional Info、Time Stamp、Destination Socket
				parse_three_param()
				
				-- 控制信息：用户定义
			elseif msg_ext_type == 1 or ext_type == 2 then
				if msg_ext_type == 1 then
					pack_type_tree:add(fields.msg_ext_type, tvb(offset, 2)):append_text(" [SRT Handshake Request]")
					pinfo.cols.info:append(" [SRT Handshake Request]")
				elseif msg_ext_type == 2 then
					pack_type_tree:add(fields.msg_ext_type, tvb(offset, 2)):append_text(" [SRT Handshake Response]")
					pinfo.cols.info:append(" [SRT Handshake Response]")
				end
				offset = offset + 2
				
				-- 忽略additional info（此包类型未定义该字段）
				subtree:add(fields.additional_info, tvb(offset, 4)):append_text(" [undefined]")
				offset = offset + 4
				
				-- 处理Time Stamp
				subtree:add(fields.time_stamp, tvb(offset, 4)):append_text("μs")
				offset = offset + 4
				
				-- 处理Destination Socket
				subtree:add(fields.dst_sock, tvb(offset, 4))
				offset = offset + 4
				
				-- 处理SRT Version字段
				subtree:add(fields.srt_version, tvb(offset, 4))
				offset = ofssset + 4
				
				-- 处理SRT Flags
				local SRT_flags_tree = subtree:add(fields.srt_flags, tvb(offset, 4))
				SRT_flags_tree:add(fields.srt_opt_tsbpdsnd, tvb(offset, 4))
				SRT_flags_tree:add(fields.srt_opt_tsbpdrcv, tvb(offset, 4))
				SRT_flags_tree:add(fields.srt_opt_haicrypt, tvb(offset, 4))
				SRT_flags_tree:add(fields.srt_opt_tlpktdrop, tvb(offset, 4))
				SRT_flags_tree:add(fields.srt_opt_nakreport, tvb(offset, 4))
				SRT_flags_tree:add(fields.srt_opt_rexmitflg, tvb(offset, 4))
				SRT_flags_tree:add(fields.srt_opt_stream, tvb(offset, 4))
				offset = offset + 4
				
				-- 处理TsbPd Resv
				subtree:add(fields.tsbpb_resv, tvb(offset, 2))
				offset = offset + 2
				
				-- 处理TsbPb Delay
				subtree:add(fields.tsbpb_delay, tvb(offset, 2))
				offset = offset + 2

				-- 处理Reserved字段
				subtree:add(fields.reserve, tvb(offset, 4))
				offset = offset + 4
			elseif msg_ext_type == 3 or msg_ext_type == 4 then
				if msg_ext_type == 3 then
					pack_type_tree:add(fields.msg_ext_type, tvb(offset, 2)):append_text(" [Encryption Keying Material Request]")
					pinfo.cols.info:append(" [Encryption Keying Material Request]")
				elseif msg_ext_type == 4 then
					pack_type_tree:add(fields.msg_ext_type, tvb(offset, 2)):append_text(" [Encryption Keying Material Response]")
					pinfo.cols.info:append(" [Encryption Keying Material Response]")
				end
				offset = offset + 2
				
				-- 忽略additional info（此包类型未定义该字段）
				subtree:add(fields.additional_info, tvb(offset, 4)):append_text(" [undefined]")
				offset = offset + 4
				
				-- 处理Time Stamp
				subtree:add(fields.time_stamp, tvb(offset, 4)):append_text("μs")
				offset = offset + 4
				
				-- 处理Destination Socket
				subtree:add(fields.dst_sock, tvb(offset, 4))
				offset = offset + 4
				
				-- 处理KmErr
				if msg_ext_type == 4 then
					subtree:add(fields.km_err, tvb(offset, 4))
					offset = offset + 4
					return
				end
				
				-- 加密信息未处理
			end
		end
	else
		-- 0 -> Data Packet
		pack_type_tree:add(fields.pack_type, tvb(offset, 2))
		pack_type_tree:append_text(" (Data Packet)")
		local seq_num = tvb(offset, 4):uint()
		pinfo.cols.info:append(" (Data Packet)(Seq Num:" .. seq_num .. ")")
		
		-- Data Packet，则前4字节为包序号
		subtree:add(fields.seq_num, tvb(offset, 4))
		offset = offset + 4
		
		data_flag_info_tree = subtree:add(fields.data_flag_info_tree, tvb(offset, 1))
		-- 处理FF标志位
		local FF_state = bit.rshift(bit.band(tvb(offset, 1):uint(), 0xC0), 6)
		if FF_state == 0 then
			data_flag_info_tree:append_text(" [Middle packet]")
		elseif FF_state == 1 then
			data_flag_info_tree:append_text(" [Last packet]")
		elseif FF_state == 2 then
			data_flag_info_tree:append_text(" [First packet]")
		else
			data_flag_info_tree:append_text(" [Single packet]")
		end
		data_flag_info_tree:add(fields.FF_state, tvb(offset, 1))
		
		-- 处理O标志位
		local O_state = bit.rshift(bit.band(tvb(offset, 1):uint(), 0x20), 5)
		if O_state == 0 then
			data_flag_info_tree:append_text(" [Data delivered unordered]")
		else
			data_flag_info_tree:append_text(" [Data delivered in order]")
		end
		data_flag_info_tree:add(fields.O_state, tvb(offset, 1))
		
		-- 处理KK标志位
		local KK_state = bit.rshift(bit.band(tvb(offset, 1):uint(), 0x18), 3)
		if KK_state == 1 then
			data_flag_info_tree:append_text(" [Encrypted with even key]")
		elseif KK_state == 2 then
			data_flag_info_tree:append_text(" [Encrypted with odd key]")
		end
		data_flag_info_tree:add(fields.KK_state, tvb(offset, 1))
		
		-- 处理R标志位
		local R_state = bit.rshift(bit.band(tvb(offset, 1):uint(), 0x04), 2)
		if R_state == 1 then
			data_flag_info_tree:append_text(" [Retransmit packet]")
			pinfo.cols.info:append(" [Retransmit packet]")
		end
		data_flag_info_tree:add(fields.R_state, tvb(offset, 1))
		
		-- 处理message number
		local msg_num = tvb(offset, 4):uint()
		msg_num = bit.band(tvb(offset, 4):uint(), 0x03FFFFFF)
		-- subtree:add(fields.msg_num, bit.band(tvb(offset, 4):uint(), 0x03FFFFFF))
		subtree:add(fields.msg_num, tvb(offset, 4), msg_num)
		offset = offset + 4
		
		-- 处理time stamp
		subtree:add(fields.time_stamp, tvb(offset, 4)):append_text(" μs")
		offset = offset + 4
		
		-- 处理destination socket
		subtree:add(fields.dst_sock, tvb(offset, 4))
		offset = offset + 4
	end
end

-- add the protocol into udp table
local port = 1935

local function enable_dissector()
    DissectorTable.get("udp.port"):add(port, bvc_srt)
end
-- call it now - enabled by default
enable_dissector()

local function disable_dissector()
    DissectorTable.get("udp.port"):remove(port, bvc_srt)
end

-- prefs changed will listen at new port
function bvc_srt.prefs_changed()
	if port ~= bvc_srt.prefs.srt_udp_port then
		if port ~= 0 then
			disable_dissector()
		end
	
		port = bvc_srt.prefs.srt_udp_port
	
		if port ~= 0 then
			enable_dissector()
		end
	end
end