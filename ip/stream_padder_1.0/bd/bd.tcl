proc init { cellpath otherInfo } {

	set cell_handle [get_bd_cells $cellpath]
	set all_busif [get_bd_intf_pins $cellpath/*]

	foreach busif $all_busif {
		if { [string equal -nocase [get_property CONFIG.PROTOCOL $busif] "AXI4"] != 1 } {
			continue
		}
	}
}


proc pre_propagate {cellpath otherInfo } {
}


proc propagate {cellpath otherInfo } {
}