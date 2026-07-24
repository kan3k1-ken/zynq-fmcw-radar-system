#BK-B220 development board IO constraint file
#
##----------------------系统时钟---------------------------
#set_property -dict {PACKAGE_PIN U18 IOSTANDARD LVCMOS33} [get_ports sys_clk];  #sys_clk
#
##-----------------------PL_KEY----------------------------
#
#set_property -dict {PACKAGE_PIN R18 IOSTANDARD LVCMOS33} [get_ports PL_KEY[0]];  #PL_KEY[0]
#set_property -dict {PACKAGE_PIN T17 IOSTANDARD LVCMOS33} [get_ports PL_KEY[1]];  #PL_KEY[1]
#set_property -dict {PACKAGE_PIN R14 IOSTANDARD LVCMOS33} [get_ports PL_KEY[2]];  #PL_KEY[2]
#set_property -dict {PACKAGE_PIN P14 IOSTANDARD LVCMOS33} [get_ports PL_KEY3]];  #PL_KEY[3]
#                                                                                 #
##-----------------------PL_LED----------------------------                       #
#set_property -dict {PACKAGE_PIN W13 IOSTANDARD LVCMOS33} [get_ports PL_LED[0]];  #PL_LED[0]
#set_property -dict {PACKAGE_PIN V12 IOSTANDARD LVCMOS33} [get_ports PL_LED[1]];  #PL_LED[1]    
#set_property -dict {PACKAGE_PIN W19 IOSTANDARD LVCMOS33} [get_ports PL_LED[2]];  #PL_LED[2]
#set_property -dict {PACKAGE_PIN W18 IOSTANDARD LVCMOS33} [get_ports PL_LED[3]];  #PL_LED[3]
#                                                                                 #
##-----------------------PL_UART---------------------------                       #
set_property -dict {PACKAGE_PIN F16 IOSTANDARD LVCMOS33} [get_ports Rx];   #uart_rxd
set_property -dict {PACKAGE_PIN F17 IOSTANDARD LVCMOS33} [get_ports Tx];   #uart_txd
#
##----------------------HDMI_TX---------------------------
#
#set_property -dict {PACKAGE_PIN K19  IOSTANDARD TMDS_33 } [get_ports {tmds_data_p[2]}];       #tmds_data_p[2]
#set_property -dict {PACKAGE_PIN J19  IOSTANDARD TMDS_33 } [get_ports {tmds_data_n[2]}];       #tmds_data_n[2]
#set_property -dict {PACKAGE_PIN J18  IOSTANDARD TMDS_33 } [get_ports {tmds_data_p[1]}];       #tmds_data_p[1]
#set_property -dict {PACKAGE_PIN H18  IOSTANDARD TMDS_33 } [get_ports {tmds_data_n[1]}];       #tmds_data_n[1]
#set_property -dict {PACKAGE_PIN G17  IOSTANDARD TMDS_33 } [get_ports {tmds_data_p[0]}];       #tmds_data_p[0]
#set_property -dict {PACKAGE_PIN G18  IOSTANDARD TMDS_33 } [get_ports {tmds_data_n[0]}];       #tmds_data_n[0]
#set_property -dict {PACKAGE_PIN E18  IOSTANDARD TMDS_33 } [get_ports tmds_clk_p];             #tmds_clk_p
#set_property -dict {PACKAGE_PIN E19  IOSTANDARD TMDS_33 } [get_ports tmds_clk_n];             #tmds_clk_n
#                                                                                              #
#set_property -dict {PACKAGE_PIN E17  IOSTANDARD LVCMOS33} [get_ports tmds_cec];               #tmds_cec
#set_property -dict {PACKAGE_PIN D18  IOSTANDARD LVCMOS33} [get_ports tmds_scl];               #tmds_scl
#set_property -dict {PACKAGE_PIN K14  IOSTANDARD LVCMOS33} [get_ports tmds_sda];               #tmds_sda
#set_property -dict {PACKAGE_PIN F20  IOSTANDARD LVCMOS33} [get_ports tmds_hpd];               #tmds_hpd

#LOGO    PMOD1 													   	 PMOD2											  PMOD3													     PMOD4
#LOGO    3V3  GND B13_L13_N B13_L14_N  B13_L16_N  B13_L18_N          3V3  GND  B34_L1_N B34_L2_N B34_L9_N B34_L8_N    3V3  GND  B34_L10_N  B34_L7_N   B34_L16_N  B34_L19_N       3V3  GND  B35_L7_N  B35_L9_N B35_L21_N  B35_L11_N
#LOGO    3V3  GND B13_L13_P B13_L14_P  B13_L16_P  B13_L18_P          3V3  GND  B34_L1_P B34_L2_P B34_L9_P B34_L8_P    3V3  GND  B34_L10_P  B34_L7_P   B34_L16_P  B34_L19_P       3V3  GND  B35_L7_P  B35_L9_P B35_L21_P  B35_L11_P     

#                                                                                             #
##----------------------PMOD1---------------------------									  #
#                                                                                             #
#set_property -dict {PACKAGE_PIN Y11 IOSTANDARD LVCMOS33}  [get_ports ] ;             		  #B13_L18_N
#set_property -dict {PACKAGE_PIN W11 IOSTANDARD LVCMOS33}  [get_ports ] ;             		  #B13_L18_P
#set_property -dict {PACKAGE_PIN W9 IOSTANDARD LVCMOS33}   [get_ports ] ;             		  #B13_L16_N
#set_property -dict {PACKAGE_PIN W10 IOSTANDARD LVCMOS33}  [get_ports ] ;             		  #B13_L16_P
#set_property -dict {PACKAGE_PIN Y8 IOSTANDARD LVCMOS33}   [get_ports ] ;           		  #B13_L14_N
#set_property -dict {PACKAGE_PIN Y9 IOSTANDARD LVCMOS33}   [get_ports ] ;           		  #B13_L14_p
#set_property -dict {PACKAGE_PIN Y6 IOSTANDARD LVCMOS33}   [get_ports ] ;           		  #B13_L13_N
#set_property -dict {PACKAGE_PIN Y7 IOSTANDARD LVCMOS33}   [get_ports ] ;            		  #B13_L13_P
#                                                                                             #
##----------------------PMOD2---------------------------                                      #
#                                                                                             #
#set_property -dict {PACKAGE_PIN Y14 IOSTANDARD LVCMOS33} [get_ports ] ;                	  #B34_L8_N
#set_property -dict {PACKAGE_PIN W14 IOSTANDARD LVCMOS33} [get_ports ] ;                      #B34_L8_P
#set_property -dict {PACKAGE_PIN U17 IOSTANDARD LVCMOS33} [get_ports ] ;                      #B34_L9_N
#set_property -dict {PACKAGE_PIN T16 IOSTANDARD LVCMOS33} [get_ports ] ;                      #B34_L9_P
#set_property -dict {PACKAGE_PIN U12 IOSTANDARD LVCMOS33} [get_ports ] ;                      #B34_L2_N
#set_property -dict {PACKAGE_PIN T12 IOSTANDARD LVCMOS33} [get_ports ] ;                      #B34_L2_P
#set_property -dict {PACKAGE_PIN T10 IOSTANDARD LVCMOS33} [get_ports ] ;                      #B34_L1_N
#set_property -dict {PACKAGE_PIN T11 IOSTANDARD LVCMOS33} [get_ports ] ;                      #B34_L1_P
#                                                                                             #
##----------------------PMOD3---------------------------                                      #
#                                                                                             #
#set_property -dict {PACKAGE_PIN R17 IOSTANDARD LVCMOS33} [get_ports ] ;                      #B34_L19_N
#set_property -dict {PACKAGE_PIN R16 IOSTANDARD LVCMOS33} [get_ports ] ;                      #B34_L19_P
#set_property -dict {PACKAGE_PIN W20 IOSTANDARD LVCMOS33} [get_ports ] ;                      #B34_L16_N
#set_property -dict {PACKAGE_PIN V20 IOSTANDARD LVCMOS33} [get_ports ] ;                      #B34_L16_P
#set_property -dict {PACKAGE_PIN Y17 IOSTANDARD LVCMOS33} [get_ports ] ;                      #B34_L7_N
#set_property -dict {PACKAGE_PIN Y16 IOSTANDARD LVCMOS33} [get_ports ] ;                      #B34_L7_P
#set_property -dict {PACKAGE_PIN W15 IOSTANDARD LVCMOS33} [get_ports ] ;                      #B34_L10_N
#set_property -dict {PACKAGE_PIN V15 IOSTANDARD LVCMOS33} [get_ports ] ;                      #B34_L10_P
#                                                                                             #
##----------------------PMOD4---------------------------                                      #
#                                                                                             #
#set_property -dict {PACKAGE_PIN L17 IOSTANDARD LVCMOS33} [get_ports ] ;                 	  #B35_L11_N
#set_property -dict {PACKAGE_PIN L16 IOSTANDARD LVCMOS33} [get_ports ] ;                 	  #B35_L11_P
#set_property -dict {PACKAGE_PIN N16 IOSTANDARD LVCMOS33} [get_ports ] ;                 	  #B35_L21_N
#set_property -dict {PACKAGE_PIN N15 IOSTANDARD LVCMOS33} [get_ports ] ;                	  #B35_L21_P
#set_property -dict {PACKAGE_PIN L20 IOSTANDARD LVCMOS33} [get_ports ] ;                  	  #B35_L9_N
#set_property -dict {PACKAGE_PIN L19 IOSTANDARD LVCMOS33} [get_ports ] ;                  	  #B35_L9_P
#set_property -dict {PACKAGE_PIN M20 IOSTANDARD LVCMOS33} [get_ports ] ;               		  #B35_L7_N
#set_property -dict {PACKAGE_PIN M19 IOSTANDARD LVCMOS33} [get_ports ] ;                 	  #B35_L7_P
#                                                            
#                                                                                             #
##----------------------LCD接口---------------------------                                    #
#                                                                                             #
#set_property -dict {PACKAGE_PIN J20 IOSTANDARD LVCMOS33} [get_ports B0];                     #B0
#set_property -dict {PACKAGE_PIN P19 IOSTANDARD LVCMOS33} [get_ports B1];                     #B1
#set_property -dict {PACKAGE_PIN N18 IOSTANDARD LVCMOS33} [get_ports B2];                     #B2
#set_property -dict {PACKAGE_PIN K17 IOSTANDARD LVCMOS33} [get_ports B3];                     #B3
#set_property -dict {PACKAGE_PIN P20 IOSTANDARD LVCMOS33} [get_ports B4];                     #B4
#                                                                                             #
#set_property -dict {PACKAGE_PIN T14 IOSTANDARD LVCMOS33} [get_ports G0];                     #G0
#set_property -dict {PACKAGE_PIN U15 IOSTANDARD LVCMOS33} [get_ports G1];                     #G1
#set_property -dict {PACKAGE_PIN U14 IOSTANDARD LVCMOS33} [get_ports G2];                     #G2
#set_property -dict {PACKAGE_PIN K18 IOSTANDARD LVCMOS33} [get_ports G3];                     #G3
#set_property -dict {PACKAGE_PIN H20 IOSTANDARD LVCMOS33} [get_ports G4];                     #G4
#set_property -dict {PACKAGE_PIN M17 IOSTANDARD LVCMOS33} [get_ports G5];                     #G5
#                                                                                             #  
#set_property -dict {PACKAGE_PIN Y19 IOSTANDARD LVCMOS33} [get_ports R0];                     #R0
#set_property -dict {PACKAGE_PIN U19 IOSTANDARD LVCMOS33} [get_ports R1];                     #R1
#set_property -dict {PACKAGE_PIN M18 IOSTANDARD LVCMOS33} [get_ports R2];                     #R2
#set_property -dict {PACKAGE_PIN V13 IOSTANDARD LVCMOS33} [get_ports R3];                     #R3
#set_property -dict {PACKAGE_PIN T15 IOSTANDARD LVCMOS33} [get_ports R4];                     #R4
#                                                                                             #
#                                                                                             #
#set_property -dict {PACKAGE_PIN T20 IOSTANDARD LVCMOS33} [get_ports lcd_hs]  ;               #lcd_hs
#set_property -dict {PACKAGE_PIN M15 IOSTANDARD LVCMOS33} [get_ports lcd_vs]  ;               #lcd_vs
#set_property -dict {PACKAGE_PIN U20 IOSTANDARD LVCMOS33} [get_ports lcd_de]  ;               #lcd_de
#set_property -dict {PACKAGE_PIN V17 IOSTANDARD LVCMOS33} [get_ports lcd_bl]  ;               #lcd_bl
#set_property -dict {PACKAGE_PIN M14 IOSTANDARD LVCMOS33} [get_ports lcd_clk] ;               #lcd_clk
#set_property -dict {PACKAGE_PIN V18 IOSTANDARD LVCMOS33} [get_ports lcd_rst] ;               #lcd_rst
#                                                                                             #
#                                                                                             #
##lcd_scl:                                                                                    #
#set_property -dict {PACKAGE_PIN P15 IOSTANDARD LVCMOS33} [get_ports touch_scl] ;             #touch_scl
##lcd_sda:                                                                                    #         
#set_property -dict {PACKAGE_PIN P16 IOSTANDARD LVCMOS33} [get_ports touch_sda];              #touch_sda
##CT_RST                                                                                      #         
#set_property -dict {PACKAGE_PIN P18 IOSTANDARD LVCMOS33} [get_ports touch_rst];              #touch_rst
##CT_INT                                                                                      #         
#set_property -dict {PACKAGE_PIN N17 IOSTANDARD LVCMOS33} [get_ports touch_int];              #touch_int

#-------------------------音频接口----------------------------
##Audio 
																							  #
#set_property -dict { PACKAGE_PIN A20   IOSTANDARD LVCMOS33 } [get_ports { adr0 }];           #adr0 
#set_property -dict { PACKAGE_PIN B19   IOSTANDARD LVCMOS33 } [get_ports { adr1 }];           #adr1 
																							  #
#set_property -dict { PACKAGE_PIN V16    IOSTANDARD LVCMOS33 } [get_ports { au_mclk_r }];     #au_mclk_r
#set_property -dict { PACKAGE_PIN H17    IOSTANDARD LVCMOS33 } [get_ports { au_sda_r  }];     #au_sda_r 
#set_property -dict { PACKAGE_PIN H16    IOSTANDARD LVCMOS33 } [get_ports { au_scl_r  }];     #au_scl_r 
#set_property -dict { PACKAGE_PIN D20   IOSTANDARD LVCMOS33 } [get_ports  { au_dout_r }];     #au_dout_r
#set_property -dict { PACKAGE_PIN D19   IOSTANDARD LVCMOS33 } [get_ports  { au_din_r  }];     #au_din_r 
#set_property -dict { PACKAGE_PIN B20   IOSTANDARD LVCMOS33 } [get_ports  { au_wclk_r }];     #au_wclk_r
#set_property -dict { PACKAGE_PIN C20   IOSTANDARD LVCMOS33 } [get_ports  { au_bclk_r }];     #au_bclk_r

