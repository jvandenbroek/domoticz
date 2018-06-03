local evenItemIdentifier = require('eventItemIdentifier')

local function Hardware(domoticz, data)

	local self = {
		['name'] = data.name,
		['hardwareName'] = data.hardwareName,
		['type'] = data.variableType,
		['id'] = data.id,
		['password'] = data.password,
		["serialPort"] = data.serialPort,
		["mode4"] = data.mode4,
		["mode3"] = data.mode3,
		["username"] = data.username,
		["address"] = data.address,
		["port"] = data.port,
		["mode6"] = data.mode6,
		["mode5"] = data.mode5,
		["mode2"] = data.mode2,
		["extra"] = data.extra,
		["mode1"] = data.mode1,
	}

	evenItemIdentifier.setType(self, 'isHardware', domoticz.BASETYPE_HARDWARE, data.name)

	return self
end

return Variable
