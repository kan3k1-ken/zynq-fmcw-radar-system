# Definitional proc to organize widgets for parameters.
proc init_gui { IPINST } {
  ipgui::add_param $IPINST -name "Component_Name"
  ipgui::add_param $IPINST -name "BB_VERSION"
  ipgui::add_param $IPINST -name "SUBSYSTEM_NUMS" -widget comboBox

}

proc update_PARAM_VALUE.BB_VERSION { PARAM_VALUE.BB_VERSION } {
	# Procedure called to update BB_VERSION when any of the dependent parameters in the arguments change
}

proc validate_PARAM_VALUE.BB_VERSION { PARAM_VALUE.BB_VERSION } {
	# Procedure called to validate BB_VERSION
	return true
}

proc update_PARAM_VALUE.SUBSYSTEM_NUMS { PARAM_VALUE.SUBSYSTEM_NUMS } {
	# Procedure called to update SUBSYSTEM_NUMS when any of the dependent parameters in the arguments change
}

proc validate_PARAM_VALUE.SUBSYSTEM_NUMS { PARAM_VALUE.SUBSYSTEM_NUMS } {
	# Procedure called to validate SUBSYSTEM_NUMS
	return true
}


proc update_MODELPARAM_VALUE.BB_VERSION { MODELPARAM_VALUE.BB_VERSION PARAM_VALUE.BB_VERSION } {
	# Procedure called to set VHDL generic/Verilog parameter value(s) based on TCL parameter value
	set_property value [get_property value ${PARAM_VALUE.BB_VERSION}] ${MODELPARAM_VALUE.BB_VERSION}
}

proc update_MODELPARAM_VALUE.SUBSYSTEM_NUMS { MODELPARAM_VALUE.SUBSYSTEM_NUMS PARAM_VALUE.SUBSYSTEM_NUMS } {
	# Procedure called to set VHDL generic/Verilog parameter value(s) based on TCL parameter value
	set_property value [get_property value ${PARAM_VALUE.SUBSYSTEM_NUMS}] ${MODELPARAM_VALUE.SUBSYSTEM_NUMS}
}

