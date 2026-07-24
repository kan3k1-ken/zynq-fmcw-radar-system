proc init_gui { IPINST } {
  ipgui::add_param $IPINST -name "Component_Name"
  set Page_0 [ipgui::add_page $IPINST -name "Page 0"]
  ipgui::add_param $IPINST -name "C_S_AXIS_TDATA_WIDTH" -parent ${Page_0} -widget comboBox
  ipgui::add_param $IPINST -name "C_M_AXIS_TDATA_WIDTH" -parent ${Page_0} -widget comboBox
  ipgui::add_param $IPINST -name "DMA_BYTES" -parent ${Page_0}
  ipgui::add_param $IPINST -name "TOTAL_BYTES" -parent ${Page_0}
}

proc update_PARAM_VALUE.C_S_AXIS_TDATA_WIDTH { PARAM_VALUE.C_S_AXIS_TDATA_WIDTH } {
}

proc validate_PARAM_VALUE.C_S_AXIS_TDATA_WIDTH { PARAM_VALUE.C_S_AXIS_TDATA_WIDTH } {
	return true
}

proc update_PARAM_VALUE.C_M_AXIS_TDATA_WIDTH { PARAM_VALUE.C_M_AXIS_TDATA_WIDTH } {
}

proc validate_PARAM_VALUE.C_M_AXIS_TDATA_WIDTH { PARAM_VALUE.C_M_AXIS_TDATA_WIDTH } {
	return true
}

proc update_PARAM_VALUE.DMA_BYTES { PARAM_VALUE.DMA_BYTES } {
}

proc validate_PARAM_VALUE.DMA_BYTES { PARAM_VALUE.DMA_BYTES } {
	return true
}

proc update_PARAM_VALUE.TOTAL_BYTES { PARAM_VALUE.TOTAL_BYTES } {
}

proc validate_PARAM_VALUE.TOTAL_BYTES { PARAM_VALUE.TOTAL_BYTES } {
	return true
}

proc update_MODELPARAM_VALUE.C_S_AXIS_TDATA_WIDTH { MODELPARAM_VALUE.C_S_AXIS_TDATA_WIDTH PARAM_VALUE.C_S_AXIS_TDATA_WIDTH } {
	set_property value [get_property value ${PARAM_VALUE.C_S_AXIS_TDATA_WIDTH}] ${MODELPARAM_VALUE.C_S_AXIS_TDATA_WIDTH}
}

proc update_MODELPARAM_VALUE.C_M_AXIS_TDATA_WIDTH { MODELPARAM_VALUE.C_M_AXIS_TDATA_WIDTH PARAM_VALUE.C_M_AXIS_TDATA_WIDTH } {
	set_property value [get_property value ${PARAM_VALUE.C_M_AXIS_TDATA_WIDTH}] ${MODELPARAM_VALUE.C_M_AXIS_TDATA_WIDTH}
}

proc update_MODELPARAM_VALUE.DMA_BYTES { MODELPARAM_VALUE.DMA_BYTES PARAM_VALUE.DMA_BYTES } {
	set_property value [get_property value ${PARAM_VALUE.DMA_BYTES}] ${MODELPARAM_VALUE.DMA_BYTES}
}

proc update_MODELPARAM_VALUE.TOTAL_BYTES { MODELPARAM_VALUE.TOTAL_BYTES PARAM_VALUE.TOTAL_BYTES } {
	set_property value [get_property value ${PARAM_VALUE.TOTAL_BYTES}] ${MODELPARAM_VALUE.TOTAL_BYTES}
}